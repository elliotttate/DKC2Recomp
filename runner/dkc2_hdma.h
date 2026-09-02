#ifndef DKC2_HDMA_H
#define DKC2_HDMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Host-only dry run of the HDMA tables the cartridge has already built for
 * the frame about to be rendered. The shared runner applies those tables to
 * the PPU one scanline at a time while rendering; this pass walks the same
 * tables in advance and records, for every rendered scanline, the BG scroll
 * and screen-enable values that will be in effect. Consecutive scanlines with
 * identical values form a band. The widescreen adapter decides a presentation
 * policy per (layer, band) from that exact geometry instead of inferring band
 * edges from register deltas while drawing.
 *
 * Nothing here writes guest memory, PPU registers, or DMA channel state.
 */

enum {
  kDkc2HdmaFirstLine = 1,
  kDkc2HdmaLastLine = 224,
  kDkc2HdmaMaxBands = 224,
};

typedef struct Dkc2HdmaBand {
  uint8_t first_line; /* inclusive rendered scanline, 1..224 */
  uint8_t last_line;  /* inclusive */
  uint16_t h_scroll[4];
  uint16_t v_scroll[4];
  uint8_t main_layers;
  uint8_t sub_layers;
  uint8_t bg_sc[4]; /* BG1SC..BG4SC: tilemap base and size per layer */
} Dkc2HdmaBand;

typedef struct Dkc2HdmaBands {
  int count;
  Dkc2HdmaBand band[kDkc2HdmaMaxBands];
  uint8_t band_for_line[kDkc2HdmaLastLine + 1];
} Dkc2HdmaBands;

typedef struct Dkc2HdmaChannelConfig {
  bool active;
  bool indirect;
  uint8_t b_address;      /* B-bus register offset $00-$3F */
  uint8_t mode;           /* transfer mode 0-7 */
  uint8_t indirect_bank;
  uint32_t table_address; /* 24-bit A-bus address of the table */
} Dkc2HdmaChannelConfig;

typedef struct Dkc2HdmaFrameState {
  uint16_t h_scroll[4];
  uint16_t v_scroll[4];
  uint8_t main_layers;
  uint8_t sub_layers;
  uint8_t bg_sc[4];
  /* The PPU's shared BG offset write latch. */
  uint8_t scroll_prev;
  uint8_t scroll_prev2;
} Dkc2HdmaFrameState;

/* Guest-address resolution supplied by the host. `pointer` maps a 24-bit
 * address to host memory exactly like the runner's HDMA path; `readable`
 * reports whether `length` bytes starting at that host pointer stay inside
 * a backing store. Both may be exercised with synthetic memory in tests. */
typedef struct Dkc2HdmaMemory {
  const uint8_t *(*pointer)(void *context, uint32_t address);
  bool (*readable)(void *context, const uint8_t *pointer, size_t length);
  void *context;
} Dkc2HdmaMemory;

void Dkc2HdmaScanBands(const Dkc2HdmaChannelConfig channels[8],
                       const Dkc2HdmaFrameState *start,
                       const Dkc2HdmaMemory *memory,
                       Dkc2HdmaBands *out);

/* The band that covers a rendered scanline. Line 0 is not rendered and maps
 * to the first band. Returns NULL when no bands were scanned. */
const Dkc2HdmaBand *Dkc2HdmaBandForLine(const Dkc2HdmaBands *bands,
                                        int line);

#endif
