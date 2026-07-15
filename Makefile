CC ?= cc
CPPFLAGS ?= -Iinclude -Ithird_party/lakesnes_apu
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=

BUILD_DIR := build
CORE_SOURCES := src/apu.c src/bus.c src/cpu.c src/decode.c src/execute.c src/hash.c src/hirom.c \
	src/rom.c src/snes_io.c src/symbols.c third_party/lakesnes_apu/apu.c \
	third_party/lakesnes_apu/dsp.c third_party/lakesnes_apu/spc.c \
	third_party/lakesnes_apu/statehandler.c
CORE_OBJECTS := $(CORE_SOURCES:%.c=$(BUILD_DIR)/%.o)
VERIFY := $(BUILD_DIR)/dkc2_verify
ANALYZE := $(BUILD_DIR)/dkc2_analyze
BOOT := $(BUILD_DIR)/dkc2_boot
TESTS := $(BUILD_DIR)/test_apu $(BUILD_DIR)/test_bus $(BUILD_DIR)/test_cpu $(BUILD_DIR)/test_decode \
	$(BUILD_DIR)/test_execute $(BUILD_DIR)/test_hash \
	$(BUILD_DIR)/test_hirom $(BUILD_DIR)/test_rom $(BUILD_DIR)/test_snes_io \
	$(BUILD_DIR)/test_symbols $(BUILD_DIR)/test_timing

.PHONY: all clean test verify-rom

all: $(VERIFY) $(ANALYZE) $(BOOT)

$(VERIFY): $(BUILD_DIR)/app/verify_main.o $(CORE_OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

$(ANALYZE): $(BUILD_DIR)/app/analyze_main.o $(CORE_OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BOOT): $(BUILD_DIR)/app/boot_main.o $(CORE_OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/test_%: $(BUILD_DIR)/tests/test_%.o $(CORE_OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TESTS)
	@for test_program in $(TESTS); do \
		$$test_program || exit $$?; \
	done

verify-rom: $(VERIFY)
	@test -n "$(ROM)" || (echo 'Usage: make verify-rom ROM="/path/to/game.smc"' >&2; exit 64)
	@$(VERIFY) "$(ROM)"

.PHONY: analyze-rom boot-rom

analyze-rom: $(ANALYZE)
	@test -n "$(ROM)" || (echo 'Usage: make analyze-rom ROM="/path/to/game.smc"' >&2; exit 64)
	@$(ANALYZE) "$(ROM)" 128

boot-rom: $(BOOT)
	@test -n "$(ROM)" || (echo 'Usage: make boot-rom ROM="/path/to/game.smc"' >&2; exit 64)
	@$(BOOT) "$(ROM)"

clean:
	rm -rf $(BUILD_DIR)
