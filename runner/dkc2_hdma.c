#include "dkc2_hdma.h"

#include <string.h>

typedef struct Dkc2HdmaChannelState {
  const uint8_t *table;
  const uint8_t *indirect;
  uint8_t repeat_count;
  uint8_t mode; /* bit 6 = indirect, low 3 bits = transfer mode */
  uint8_t b_address;
  uint8_t indirect_bank;
} Dkc2HdmaChannelState;

/* B-bus register offsets and lengths per transfer mode, matching the
 * runner's SimpleHdma_DoLine tables exactly. */
static const uint8_t kRegisterOffsets[8][4] = {
  {0, 0, 0, 0},
  {0, 1, 0, 1},
  {0, 0, 0, 0},
  {0, 0, 1, 1},
  {0, 1, 2, 3},
  {0, 1, 0, 1},
  {0, 0, 0, 0},
  {0, 0, 1, 1},
};
static const uint8_t kTransferLengths[8] = {1, 2, 2, 4, 4, 4, 2, 4};

/* Mirror only the PPU register semantics that decide widescreen policy:
 * BGnHOFS/BGnVOFS through the shared offset latch, BGnSC, TM, and TS. Every other
 * B-bus target (windows, color math, VRAM ports) is consumed without
 * changing the tracked state. */
static void Dkc2HdmaApplyWrite(Dkc2HdmaFrameState *state, uint8_t reg,
                               uint8_t value) {
  switch (reg) {
    case 0x0d:
    case 0x0f:
    case 0x11:
    case 0x13:
      state->h_scroll[(reg - 0x0d) / 2] = (uint16_t)(
          (((uint16_t)value << 8) | (state->scroll_prev & 0xf8u) |
           (state->scroll_prev2 & 0x07u)) & 0x03ffu);
      state->scroll_prev = value;
      state->scroll_prev2 = value;
      break;
    case 0x0e:
    case 0x10:
    case 0x12:
    case 0x14:
      state->v_scroll[(reg - 0x0e) / 2] = (uint16_t)(
          (((uint16_t)value << 8) | state->scroll_prev) & 0x03ffu);
      state->scroll_prev = value;
      break;
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0a:
      state->bg_sc[reg - 0x07] = value;
      break;
    case 0x2c:
      state->main_layers = value;
      break;
    case 0x2d:
      state->sub_layers = value;
      break;
    default:
      break;
  }
}

static void Dkc2HdmaStepChannel(Dkc2HdmaChannelState *channel,
                                const Dkc2HdmaMemory *memory,
                                Dkc2HdmaFrameState *state) {
  if (!channel->table)
    return;
  bool do_transfer = false;
  if ((channel->repeat_count & 0x7fu) == 0) {
    if (!memory->readable(memory->context, channel->table, 1)) {
      channel->table = NULL;
      return;
    }
    channel->repeat_count = *channel->table++;
    if (channel->repeat_count == 0) {
      channel->table = NULL;
      return;
    }
    if (channel->mode & 0x40u) {
      if (!memory->readable(memory->context, channel->table, 2)) {
        channel->table = NULL;
        return;
      }
      const uint32_t address =
          ((uint32_t)channel->indirect_bank << 16) |
          (uint32_t)channel->table[0] |
          ((uint32_t)channel->table[1] << 8);
      channel->indirect = memory->pointer(memory->context, address);
      channel->table += 2;
    }
    do_transfer = true;
  }
  if (do_transfer || (channel->repeat_count & 0x80u)) {
    const unsigned mode = channel->mode & 7u;
    for (unsigned j = 0; j < kTransferLengths[mode]; j++) {
      const uint8_t **source =
          (channel->mode & 0x40u) ? &channel->indirect : &channel->table;
      if (!*source || !memory->readable(memory->context, *source, 1)) {
        channel->table = NULL;
        break;
      }
      const uint8_t value = **source;
      (*source)++;
      Dkc2HdmaApplyWrite(
          state, (uint8_t)(channel->b_address + kRegisterOffsets[mode][j]),
          value);
    }
  }
  channel->repeat_count--;
}

static bool Dkc2HdmaBandMatches(const Dkc2HdmaBand *band,
                                const Dkc2HdmaFrameState *state) {
  return memcmp(band->h_scroll, state->h_scroll, sizeof band->h_scroll) == 0 &&
         memcmp(band->v_scroll, state->v_scroll, sizeof band->v_scroll) == 0 &&
         memcmp(band->bg_sc, state->bg_sc, sizeof band->bg_sc) == 0 &&
         band->main_layers == state->main_layers &&
         band->sub_layers == state->sub_layers;
}

void Dkc2HdmaScanBands(const Dkc2HdmaChannelConfig channels[8],
                       const Dkc2HdmaFrameState *start,
                       const Dkc2HdmaMemory *memory,
                       Dkc2HdmaBands *out) {
  if (!out)
    return;
  out->count = 0;
  memset(out->band_for_line, 0, sizeof out->band_for_line);
  if (!start || !memory || !memory->pointer || !memory->readable)
    return;

  Dkc2HdmaChannelState channel_state[8];
  memset(channel_state, 0, sizeof channel_state);
  for (int index = 0; index < 8; index++) {
    const Dkc2HdmaChannelConfig *config = channels ? &channels[index] : NULL;
    if (!config || !config->active)
      continue;
    channel_state[index].table =
        memory->pointer(memory->context, config->table_address);
    channel_state[index].mode =
        (uint8_t)((config->mode & 7u) | (config->indirect ? 0x40u : 0u));
    channel_state[index].b_address = config->b_address;
    channel_state[index].indirect_bank = config->indirect_bank;
  }

  Dkc2HdmaFrameState state = *start;
  /* The runner renders line y, then applies every active channel's entry
   * for that line; rendered line y therefore sees the writes made after
   * lines 0..y-1. Line 0 is never drawn. */
  for (int line = 0; line <= kDkc2HdmaLastLine; line++) {
    if (line >= kDkc2HdmaFirstLine) {
      if (out->count == 0 ||
          !Dkc2HdmaBandMatches(&out->band[out->count - 1], &state)) {
        if (out->count >= kDkc2HdmaMaxBands)
          break;
        Dkc2HdmaBand *band = &out->band[out->count++];
        band->first_line = (uint8_t)line;
        band->last_line = (uint8_t)line;
        memcpy(band->h_scroll, state.h_scroll, sizeof band->h_scroll);
        memcpy(band->v_scroll, state.v_scroll, sizeof band->v_scroll);
        band->main_layers = state.main_layers;
        band->sub_layers = state.sub_layers;
        memcpy(band->bg_sc, state.bg_sc, sizeof band->bg_sc);
      } else {
        out->band[out->count - 1].last_line = (uint8_t)line;
      }
      out->band_for_line[line] = (uint8_t)(out->count - 1);
    }
    for (int index = 0; index < 8; index++)
      Dkc2HdmaStepChannel(&channel_state[index], memory, &state);
  }
}

const Dkc2HdmaBand *Dkc2HdmaBandForLine(const Dkc2HdmaBands *bands,
                                        int line) {
  if (!bands || bands->count <= 0)
    return NULL;
  if (line < kDkc2HdmaFirstLine)
    line = kDkc2HdmaFirstLine;
  if (line > kDkc2HdmaLastLine)
    line = kDkc2HdmaLastLine;
  const int index = bands->band_for_line[line];
  return index < bands->count ? &bands->band[index] : NULL;
}
