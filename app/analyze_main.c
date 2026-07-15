#include "dkc2/cpu.h"
#include "dkc2/decode.h"
#include "dkc2/rom.h"
#include "dkc2/symbols.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct analysis_point {
    uint32_t address;
    dkc2_decode_state state;
    uint16_t return_words[8];
    uint8_t return_depth;
    bool return_stack_known;
} analysis_point;

typedef struct call_site {
    uint32_t source;
    uint32_t target;
} call_site;

typedef enum graph_edge_kind {
    GRAPH_EDGE_NEXT,
    GRAPH_EDGE_BRANCH_TAKEN,
    GRAPH_EDGE_JUMP,
    GRAPH_EDGE_CALL,
    GRAPH_EDGE_STACK_RETURN
} graph_edge_kind;

typedef struct graph_endpoint {
    uint32_t address;
    dkc2_decode_state state;
} graph_endpoint;

typedef struct graph_edge {
    graph_endpoint source;
    graph_endpoint target;
    graph_edge_kind kind;
} graph_edge;

typedef enum analysis_entry_kind {
    ANALYSIS_ENTRY_RESET,
    ANALYSIS_ENTRY_NMI,
    ANALYSIS_ENTRY_IRQ
} analysis_entry_kind;

static size_t fetch_bytes(const dkc2_rom_image *image,
                          uint32_t address,
                          uint8_t bytes[4]) {
    uint32_t bank = address & UINT32_C(0xFF0000);
    uint16_t pc = (uint16_t)address;
    size_t count;

    for (count = 0; count < 4; ++count) {
        uint32_t current = bank | (uint16_t)(pc + (uint16_t)count);
        if (!dkc2_rom_image_read8(image, current, &bytes[count])) {
            break;
        }
    }
    return count;
}

static bool point_equal(const analysis_point *left, const analysis_point *right) {
    return left->address == right->address &&
           dkc2_decode_state_equal(&left->state, &right->state) &&
           left->return_depth == right->return_depth &&
           left->return_stack_known == right->return_stack_known &&
           (!left->return_stack_known ||
            memcmp(left->return_words,
                   right->return_words,
                   (size_t)left->return_depth * sizeof(left->return_words[0])) == 0);
}

static bool already_visited(const analysis_point *visited,
                            size_t visited_count,
                            const analysis_point *point) {
    size_t i;
    for (i = 0; i < visited_count; ++i) {
        if (point_equal(&visited[i], point)) {
            return true;
        }
    }
    return false;
}

static bool record_call(call_site *calls,
                        size_t *call_count,
                        size_t capacity,
                        uint32_t source,
                        uint32_t target) {
    size_t i;
    for (i = 0; i < *call_count; ++i) {
        if (calls[i].source == source && calls[i].target == target) {
            return true;
        }
    }
    if (*call_count >= capacity) {
        return false;
    }
    calls[*call_count].source = source;
    calls[*call_count].target = target;
    ++*call_count;
    return true;
}

static bool record_edge(graph_edge *edges,
                        size_t *edge_count,
                        size_t capacity,
                        const analysis_point *source,
                        const analysis_point *target,
                        graph_edge_kind kind) {
    graph_edge *edge;

    if (*edge_count >= capacity) {
        return false;
    }
    edge = &edges[*edge_count];
    edge->source.address = source->address;
    edge->source.state = source->state;
    edge->target.address = target->address;
    edge->target.state = target->state;
    edge->kind = kind;
    ++*edge_count;
    return true;
}

static void format_address(uint32_t address, char text[8]) {
    (void)snprintf(text,
                   8,
                   "%02X:%04X",
                   (unsigned)((address >> 16) & UINT32_C(0xFF)),
                   (unsigned)(address & UINT32_C(0xFFFF)));
}

static char graph_bit(dkc2_known_bit bit) {
    return bit == DKC2_BIT_ZERO ? '0' : bit == DKC2_BIT_ONE ? '1' : 'U';
}

static void write_graph_id(FILE *file, const graph_endpoint *endpoint) {
    (void)fprintf(file,
                  "n_%06X_E%cM%cX%cC%c",
                  (unsigned)(endpoint->address & UINT32_C(0xFFFFFF)),
                  graph_bit(endpoint->state.e),
                  graph_bit(endpoint->state.m),
                  graph_bit(endpoint->state.x),
                  graph_bit(endpoint->state.c));
}

static void write_dot_escaped(FILE *file, const char *text) {
    const unsigned char *character = (const unsigned char *)text;
    while (*character != 0) {
        if (*character == '\\' || *character == '"') {
            (void)fputc('\\', file);
            (void)fputc(*character, file);
        } else if (*character == '\n' || *character == '\r') {
            (void)fputs("\\n", file);
        } else if (*character >= 0x20) {
            (void)fputc(*character, file);
        }
        ++character;
    }
}

static void write_graph_node(FILE *file,
                             const graph_endpoint *endpoint,
                             const dkc2_symbol_table *symbols) {
    char address[8];
    const char *label = dkc2_symbols_lookup(symbols, endpoint->address);

    format_address(endpoint->address, address);
    (void)fputs("  ", file);
    write_graph_id(file, endpoint);
    (void)fputs(" [label=\"", file);
    if (label != NULL) {
        write_dot_escaped(file, label);
        (void)fputs("\\n", file);
    }
    write_dot_escaped(file, address);
    (void)fprintf(file,
                  "\\nE%c M%c X%c C%c\"];\n",
                  dkc2_known_bit_character(endpoint->state.e),
                  dkc2_known_bit_character(endpoint->state.m),
                  dkc2_known_bit_character(endpoint->state.x),
                  dkc2_known_bit_character(endpoint->state.c));
}

static const char *graph_edge_name(graph_edge_kind kind) {
    switch (kind) {
        case GRAPH_EDGE_NEXT:
            return "next";
        case GRAPH_EDGE_BRANCH_TAKEN:
            return "taken";
        case GRAPH_EDGE_JUMP:
            return "jump";
        case GRAPH_EDGE_CALL:
            return "call";
        case GRAPH_EDGE_STACK_RETURN:
            return "stack return";
    }
    return "unknown";
}

static bool write_dot_graph(const char *path,
                            const analysis_point *visited,
                            size_t visited_count,
                            const graph_edge *edges,
                            size_t edge_count,
                            const dkc2_symbol_table *symbols,
                            char *error,
                            size_t error_size) {
    FILE *file = fopen(path, "w");
    size_t i;

    if (file == NULL) {
        (void)snprintf(error,
                       error_size,
                       "cannot create graph: %s",
                       strerror(errno));
        return false;
    }

    (void)fputs("digraph dkc2_entry {\n"
                "  graph [rankdir=TB];\n"
                "  node [shape=box, fontname=\"monospace\"];\n"
                "  edge [fontname=\"sans-serif\"];\n",
                file);
    for (i = 0; i < visited_count; ++i) {
        graph_endpoint endpoint;
        endpoint.address = visited[i].address;
        endpoint.state = visited[i].state;
        write_graph_node(file, &endpoint, symbols);
    }
    for (i = 0; i < edge_count; ++i) {
        /* Declaring targets also makes analysis-frontier nodes readable. */
        write_graph_node(file, &edges[i].target, symbols);
        (void)fputs("  ", file);
        write_graph_id(file, &edges[i].source);
        (void)fputs(" -> ", file);
        write_graph_id(file, &edges[i].target);
        (void)fprintf(file,
                      " [label=\"%s\"%s];\n",
                      graph_edge_name(edges[i].kind),
                      edges[i].kind == GRAPH_EDGE_CALL
                          ? ", style=dashed, color=\"#3567a8\""
                          : "");
    }
    (void)fputs("}\n", file);

    if (ferror(file) || fclose(file) != 0) {
        (void)snprintf(error, error_size, "cannot finish writing graph");
        return false;
    }
    return true;
}

static void print_instruction(const dkc2_instruction *instruction,
                              const dkc2_decode_state *state,
                              const dkc2_symbol_table *symbols) {
    char address[8];
    char assembly[64];
    char byte_text[13] = "";
    const char *label = dkc2_symbols_lookup(symbols, instruction->address);
    const char *target_label = instruction->target_known
                                   ? dkc2_symbols_lookup(symbols,
                                                         instruction->target_address)
                                   : NULL;
    size_t i;
    size_t position = 0;

    format_address(instruction->address, address);
    if (label != NULL) {
        (void)printf("%s:\n", label);
    }
    if (!dkc2_format_instruction(instruction, assembly, sizeof(assembly))) {
        (void)snprintf(assembly, sizeof(assembly), "<format error>");
    }
    for (i = 0; i < instruction->length && position + 3 < sizeof(byte_text); ++i) {
        int written = snprintf(byte_text + position,
                               sizeof(byte_text) - position,
                               i == 0 ? "%02X" : " %02X",
                               (unsigned)instruction->bytes[i]);
        if (written < 0) {
            break;
        }
        position += (size_t)written;
    }

    (void)printf("%s  E%c M%c X%c C%c  %-11s  %s%s%s\n",
                 address,
                 dkc2_known_bit_character(state->e),
                 dkc2_known_bit_character(state->m),
                 dkc2_known_bit_character(state->x),
                 dkc2_known_bit_character(state->c),
                 byte_text,
                 assembly,
                 target_label == NULL ? "" : "  ; ",
                 target_label == NULL ? "" : target_label);
}

static bool parse_limit(const char *text, size_t *limit) {
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 || value > 100000UL) {
        return false;
    }
    *limit = (size_t)value;
    return true;
}

static void track_stack_effect(analysis_point *point,
                               const dkc2_instruction *instruction) {
    uint8_t opcode = instruction->bytes[0];

    if (opcode == UINT8_C(0x9A) || opcode == UINT8_C(0x1B)) { /* TXS, TCS */
        point->return_depth = 0;
        point->return_stack_known = true;
        return;
    }

    if (opcode == UINT8_C(0xF4)) { /* PEA */
        if (point->return_stack_known &&
            point->return_depth < sizeof(point->return_words) /
                                      sizeof(point->return_words[0])) {
            point->return_words[point->return_depth++] =
                (uint16_t)instruction->operand;
        } else {
            point->return_stack_known = false;
        }
        return;
    }

    if (opcode == UINT8_C(0x62)) { /* PER */
        if (point->return_stack_known &&
            point->return_depth < sizeof(point->return_words) /
                                      sizeof(point->return_words[0])) {
            int16_t displacement = (int16_t)(uint16_t)instruction->operand;
            point->return_words[point->return_depth++] =
                (uint16_t)(instruction->next_address + displacement);
        } else {
            point->return_stack_known = false;
        }
        return;
    }

    switch (opcode) {
        case 0x08: /* PHP */
        case 0x0B: /* PHD */
        case 0x28: /* PLP */
        case 0x2B: /* PLD */
        case 0x48: /* PHA */
        case 0x4B: /* PHK */
        case 0x5A: /* PHY */
        case 0x68: /* PLA */
        case 0x7A: /* PLY */
        case 0x8B: /* PHB */
        case 0xAB: /* PLB */
        case 0xD4: /* PEI */
        case 0xDA: /* PHX */
        case 0xFA: /* PLX */
            point->return_stack_known = false;
            break;
        default:
            break;
    }
}

static const char *entry_name(analysis_entry_kind entry) {
    switch (entry) {
        case ANALYSIS_ENTRY_RESET:
            return "reset";
        case ANALYSIS_ENTRY_NMI:
            return "native NMI";
        case ANALYSIS_ENTRY_IRQ:
            return "native IRQ";
    }
    return "unknown";
}

static uint16_t entry_vector(const dkc2_rom_image *image,
                             analysis_entry_kind entry) {
    switch (entry) {
        case ANALYSIS_ENTRY_RESET:
            return image->report.reset_vector;
        case ANALYSIS_ENTRY_NMI:
            return image->report.native_nmi_vector;
        case ANALYSIS_ENTRY_IRQ:
            return image->report.native_irq_vector;
    }
    return 0;
}

static int analyze_entry(const dkc2_rom_image *image,
                         analysis_entry_kind entry,
                         size_t limit,
                         bool follow_calls,
                         const dkc2_symbol_table *symbols,
                         const char *dot_path) {
    analysis_point *queue;
    analysis_point *visited;
    call_site *calls;
    graph_edge *edges;
    size_t capacity = limit * 4 + 16;
    size_t edge_capacity = limit * 2 + 16;
    size_t queue_head = 0;
    size_t queue_count = 0;
    size_t visited_count = 0;
    size_t call_count = 0;
    size_t edge_count = 0;
    size_t blocked_count = 0;
    size_t terminated_count = 0;
    analysis_point start;
    size_t i;

    queue = (analysis_point *)calloc(capacity, sizeof(*queue));
    visited = (analysis_point *)calloc(limit, sizeof(*visited));
    calls = (call_site *)calloc(limit, sizeof(*calls));
    edges = (graph_edge *)calloc(edge_capacity, sizeof(*edges));
    if (queue == NULL || visited == NULL || calls == NULL || edges == NULL) {
        free(edges);
        free(calls);
        free(visited);
        free(queue);
        (void)fprintf(stderr, "not enough memory for analysis\n");
        return 1;
    }

    start.address = entry_vector(image, entry); /* All vectors enter Bank 0. */
    if (entry == ANALYSIS_ENTRY_RESET) {
        dkc2_decode_state_reset(&start.state);
    } else {
        /* Native interrupt vectors imply E=0; the interrupted P flags vary. */
        start.state.e = DKC2_BIT_ZERO;
        start.state.m = DKC2_BIT_UNKNOWN;
        start.state.x = DKC2_BIT_UNKNOWN;
        start.state.c = DKC2_BIT_UNKNOWN;
    }
    start.return_stack_known = false;
    queue[queue_count++] = start;

    (void)printf("DKC2 %s control-flow analysis\n", entry_name(entry));
    (void)printf("Entry: $00:%04X  Limit: %zu decoded instructions\n",
                 (unsigned)entry_vector(image, entry),
                 limit);
    (void)printf(follow_calls
                     ? "Direct callees are explored independently; post-call width "
                       "state is assumed unchanged.\n\n"
                     : "Calls are recorded but not entered; post-call width state "
                       "is assumed unchanged.\n\n");
    if (symbols != NULL) {
        (void)printf("Loaded %zu external labels.\n\n", symbols->count);
    }

    while (queue_head < queue_count && visited_count < limit) {
        analysis_point point = queue[queue_head++];

        while (visited_count < limit) {
            uint8_t bytes[4] = {0};
            size_t available;
            dkc2_instruction instruction;
            dkc2_decode_status status;
            dkc2_decode_state next_state;
            analysis_point source_point;

            if (already_visited(visited, visited_count, &point)) {
                break;
            }
            visited[visited_count++] = point;

            available = fetch_bytes(image, point.address, bytes);
            status = dkc2_decode_instruction(bytes,
                                             available,
                                             point.address,
                                             &point.state,
                                             &instruction);
            if (status != DKC2_DECODE_OK) {
                char address[8];
                format_address(point.address, address);
                (void)printf("%s  E%c M%c X%c C%c  <blocked: %s>\n",
                             address,
                             dkc2_known_bit_character(point.state.e),
                             dkc2_known_bit_character(point.state.m),
                             dkc2_known_bit_character(point.state.x),
                             dkc2_known_bit_character(point.state.c),
                             dkc2_decode_status_name(status));
                ++blocked_count;
                break;
            }

            print_instruction(&instruction, &point.state, symbols);
            source_point = point;
            next_state = point.state;
            dkc2_decode_state_apply(&next_state,
                                    instruction.bytes[0],
                                    instruction.length > 1 ? instruction.bytes[1] : 0);
            point.state = next_state;
            track_stack_effect(&point, &instruction);

            switch (instruction.description->flow) {
                case DKC2_FLOW_CONDITIONAL_BRANCH:
                    {
                        analysis_point branch_point = point;
                        branch_point.address = instruction.target_address;
                        (void)record_edge(edges,
                                          &edge_count,
                                          edge_capacity,
                                          &source_point,
                                          &branch_point,
                                          GRAPH_EDGE_BRANCH_TAKEN);
                        branch_point.address = instruction.next_address;
                        (void)record_edge(edges,
                                          &edge_count,
                                          edge_capacity,
                                          &source_point,
                                          &branch_point,
                                          GRAPH_EDGE_NEXT);
                    }
                    if (queue_count < capacity) {
                        queue[queue_count] = point;
                        queue[queue_count].address = instruction.target_address;
                        ++queue_count;
                    }
                    point.address = instruction.next_address;
                    break;
                case DKC2_FLOW_BRANCH:
                case DKC2_FLOW_JUMP:
                    point.address = instruction.target_address;
                    (void)record_edge(edges,
                                      &edge_count,
                                      edge_capacity,
                                      &source_point,
                                      &point,
                                      instruction.description->flow == DKC2_FLOW_JUMP
                                          ? GRAPH_EDGE_JUMP
                                          : GRAPH_EDGE_BRANCH_TAKEN);
                    break;
                case DKC2_FLOW_CALL:
                    (void)record_call(calls,
                                      &call_count,
                                      limit,
                                      instruction.address,
                                      instruction.target_address);
                    {
                        analysis_point call_point = point;
                        call_point.address = instruction.target_address;
                        (void)record_edge(edges,
                                          &edge_count,
                                          edge_capacity,
                                          &source_point,
                                          &call_point,
                                          GRAPH_EDGE_CALL);
                        call_point.address = instruction.next_address;
                        (void)record_edge(edges,
                                          &edge_count,
                                          edge_capacity,
                                          &source_point,
                                          &call_point,
                                          GRAPH_EDGE_NEXT);
                    }
                    if (follow_calls && queue_count < capacity) {
                        queue[queue_count] = point;
                        queue[queue_count].address = instruction.target_address;
                        queue[queue_count].return_depth = 0;
                        queue[queue_count].return_stack_known = false;
                        ++queue_count;
                    }
                    point.address = instruction.next_address;
                    break;
                case DKC2_FLOW_NEXT:
                    point.address = instruction.next_address;
                    (void)record_edge(edges,
                                      &edge_count,
                                      edge_capacity,
                                      &source_point,
                                      &point,
                                      GRAPH_EDGE_NEXT);
                    break;
                case DKC2_FLOW_INDIRECT_CALL:
                    point.address = instruction.next_address;
                    (void)record_edge(edges,
                                      &edge_count,
                                      edge_capacity,
                                      &source_point,
                                      &point,
                                      GRAPH_EDGE_NEXT);
                    break;
                case DKC2_FLOW_INDIRECT_JUMP:
                case DKC2_FLOW_RETURN:
                    if (instruction.bytes[0] == UINT8_C(0x60) &&
                        point.return_stack_known && point.return_depth > 0) {
                        uint16_t return_word =
                            point.return_words[--point.return_depth];
                        point.address =
                            (instruction.address & UINT32_C(0xFF0000)) |
                            (uint16_t)(return_word + UINT16_C(1));
                        (void)record_edge(edges,
                                          &edge_count,
                                          edge_capacity,
                                          &source_point,
                                          &point,
                                          GRAPH_EDGE_STACK_RETURN);
                        break;
                    }
                    ++terminated_count;
                    goto path_finished;
                case DKC2_FLOW_TRAP:
                case DKC2_FLOW_STOP:
                    ++terminated_count;
                    goto path_finished;
            }
        }
path_finished:
        (void)putchar('\n');
    }

    (void)printf("Direct call sites (%zu):\n", call_count);
    for (i = 0; i < call_count; ++i) {
        char source[8];
        char target[8];
        const char *source_label = dkc2_symbols_lookup(symbols, calls[i].source);
        const char *target_label = dkc2_symbols_lookup(symbols, calls[i].target);
        format_address(calls[i].source, source);
        format_address(calls[i].target, target);
        (void)printf("  %s%s%s -> %s%s%s\n",
                     source,
                     source_label == NULL ? "" : " ",
                     source_label == NULL ? "" : source_label,
                     target,
                     target_label == NULL ? "" : " ",
                     target_label == NULL ? "" : target_label);
    }
    (void)printf("\nSummary: decoded=%zu queued_paths=%zu blocked=%zu terminated=%zu\n",
                 visited_count,
                 queue_count,
                 blocked_count,
                 terminated_count);

    if (dot_path != NULL) {
        char graph_error[256];
        if (!write_dot_graph(dot_path,
                             visited,
                             visited_count,
                             edges,
                             edge_count,
                             symbols,
                             graph_error,
                             sizeof(graph_error))) {
            (void)fprintf(stderr, "%s\n", graph_error);
            free(edges);
            free(calls);
            free(visited);
            free(queue);
            return 1;
        }
        (void)printf("Graph: %s (%zu edges)\n", dot_path, edge_count);
    }

    free(edges);
    free(calls);
    free(visited);
    free(queue);
    return blocked_count == 0 ? 0 : 2;
}

int main(int argc, char **argv) {
    dkc2_rom_image image;
    dkc2_symbol_table symbols;
    char error[256];
    size_t limit = 128;
    bool follow_calls = false;
    bool limit_seen = false;
    const char *symbol_path = NULL;
    const char *dot_path = NULL;
    analysis_entry_kind entry = ANALYSIS_ENTRY_RESET;
    bool entry_seen = false;
    int argument;
    int result;

    memset(&symbols, 0, sizeof(symbols));
    if (argc < 2) {
        (void)fprintf(stderr,
                      "Usage: %s <path-to-dkc2-rom> [instruction-limit] "
                      "[--follow-calls] [--symbols <wla-symbol-file>] "
                      "[--dot <graph-file>] [--entry reset|nmi|irq]\n",
                      argv[0]);
        return 64;
    }
    for (argument = 2; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--follow-calls") == 0) {
            follow_calls = true;
        } else if (strcmp(argv[argument], "--symbols") == 0) {
            if (symbol_path != NULL || argument + 1 >= argc) {
                (void)fprintf(stderr,
                              "--symbols requires exactly one WLA symbol-file path\n");
                return 64;
            }
            symbol_path = argv[++argument];
        } else if (strcmp(argv[argument], "--dot") == 0) {
            if (dot_path != NULL || argument + 1 >= argc) {
                (void)fprintf(stderr,
                              "--dot requires exactly one output-file path\n");
                return 64;
            }
            dot_path = argv[++argument];
        } else if (strcmp(argv[argument], "--entry") == 0) {
            const char *entry_text;
            if (entry_seen || argument + 1 >= argc) {
                (void)fprintf(stderr,
                              "--entry requires exactly one of reset, nmi, or irq\n");
                return 64;
            }
            entry_seen = true;
            entry_text = argv[++argument];
            if (strcmp(entry_text, "reset") == 0) {
                entry = ANALYSIS_ENTRY_RESET;
            } else if (strcmp(entry_text, "nmi") == 0) {
                entry = ANALYSIS_ENTRY_NMI;
            } else if (strcmp(entry_text, "irq") == 0) {
                entry = ANALYSIS_ENTRY_IRQ;
            } else {
                (void)fprintf(stderr,
                              "--entry requires exactly one of reset, nmi, or irq\n");
                return 64;
            }
        } else if (!limit_seen && parse_limit(argv[argument], &limit)) {
            limit_seen = true;
        } else {
            (void)fprintf(stderr,
                          "arguments must be an instruction limit (1-100000), "
                          "--follow-calls, --symbols <path>, --dot <path>, "
                          "and/or --entry reset|nmi|irq\n");
            return 64;
        }
    }

    if (!dkc2_rom_image_load(argv[1], &image, error, sizeof(error))) {
        (void)fprintf(stderr, "cannot load ROM for analysis: %s\n", error);
        return 1;
    }

    if (symbol_path != NULL &&
        !dkc2_symbols_load_wla(symbol_path, &symbols, error, sizeof(error))) {
        (void)fprintf(stderr, "cannot load symbols: %s\n", error);
        dkc2_rom_image_free(&image);
        return 1;
    }

    result = analyze_entry(&image,
                           entry,
                           limit,
                           follow_calls,
                           symbol_path == NULL ? NULL : &symbols,
                           dot_path);
    dkc2_symbols_free(&symbols);
    dkc2_rom_image_free(&image);
    return result;
}
