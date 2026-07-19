cmake_minimum_required(VERSION 3.20)

project(dkc2_port
    VERSION 0.8.0
    DESCRIPTION "Clean-room tooling and runtime foundation for a DKC2 native port"
    LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(DKC2_ROM "" CACHE FILEPATH "Private path to a headerless DKC2 USA v1.0 ROM")
option(DKC2_BUILD_SNESRECOMP
    "Build the experimental snesrecomp headless and desktop runners" OFF)
option(DKC2_BUILD_SNESRECOMP_HEADLESS
    "Deprecated alias for DKC2_BUILD_SNESRECOMP" OFF)
if(DKC2_BUILD_SNESRECOMP_HEADLESS)
    set(DKC2_BUILD_SNESRECOMP ON)
endif()

add_library(dkc2_lakesnes_apu STATIC
    third_party/lakesnes_apu/apu.c
    third_party/lakesnes_apu/dsp.c
    third_party/lakesnes_apu/spc.c
    third_party/lakesnes_apu/statehandler.c)

target_include_directories(dkc2_lakesnes_apu
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lakesnes_apu)

add_library(dkc2_core STATIC
    src/apu.c
    src/bus.c
    src/cpu.c
    src/decode.c
    src/execute.c
    src/hash.c
    src/hirom.c
    src/ppu_render.c
    src/rom.c
    src/snes_io.c
    src/symbols.c)

target_include_directories(dkc2_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lakesnes_apu)

target_link_libraries(dkc2_core PRIVATE dkc2_lakesnes_apu)

function(dkc2_enable_project_warnings target_name)
    if(MSVC)
        # Visual Studio otherwise sends every source in a target through one
        # serial cl.exe invocation. Generated recompilation units are large,
        # so allow the compiler to process them concurrently.
        target_compile_options(${target_name} PRIVATE /W4 /MP)
        target_compile_definitions(${target_name} PRIVATE
            _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
endfunction()

dkc2_enable_project_warnings(dkc2_core)

if(MSVC)
    # Imported code retains upstream conversion behavior; keep its diagnostics
    # isolated so project warnings remain actionable without changing vendored
    # source or suppressing compiler errors.
    target_compile_options(dkc2_lakesnes_apu PRIVATE /W0)
endif()

add_executable(dkc2_verify app/verify_main.c)
target_link_libraries(dkc2_verify PRIVATE dkc2_core)
dkc2_enable_project_warnings(dkc2_verify)

add_executable(dkc2_analyze app/analyze_main.c)
target_link_libraries(dkc2_analyze PRIVATE dkc2_core)
dkc2_enable_project_warnings(dkc2_analyze)

add_executable(dkc2_boot app/boot_main.c)
target_link_libraries(dkc2_boot PRIVATE dkc2_core)
dkc2_enable_project_warnings(dkc2_boot)

if(WIN32 AND EXISTS
        "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp/tools/snesref/libretro.h")
    add_executable(dkc2_libretro_capture tools/libretro_capture.c)
    target_include_directories(dkc2_libretro_capture PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp/tools/snesref)
    target_link_libraries(dkc2_libretro_capture PRIVATE dkc2_core)
    dkc2_enable_project_warnings(dkc2_libretro_capture)
endif()

if(DKC2_BUILD_SNESRECOMP)
    if(NOT DEFINED SNESRECOMP_ROOT)
        set(SNESRECOMP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp)
    endif()
    set(DKC2_SNESRECOMP_GEN_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/snesrecomp)
    if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
        message(FATAL_ERROR
            "snesrecomp is not initialized; run git submodule update --init --recursive")
    endif()
    include(${SNESRECOMP_ROOT}/runner/runner.cmake)
    file(GLOB DKC2_SNESRECOMP_GEN_SOURCES CONFIGURE_DEPENDS
        "${DKC2_SNESRECOMP_GEN_DIR}/*.c")
    if(NOT DKC2_SNESRECOMP_GEN_SOURCES)
        message(FATAL_ERROR
            "No private generated sources found; run scripts/generate_snesrecomp.ps1 first")
    endif()
    # These project-owned hosts have no SDL dependency or event thread. Keep
    # the portable snesrecomp runtime while providing their small host adapters.
    list(REMOVE_ITEM SNESRECOMP_RUNNER_SOURCES
        "${SNESRECOMP_ROOT}/runner/src/keybinds.c"
        "${SNESRECOMP_ROOT}/runner/src/host_report.c")
    add_executable(dkc2_snesrecomp_headless
        ${SNESRECOMP_RUNNER_SOURCES}
        ${DKC2_SNESRECOMP_GEN_SOURCES}
        runner/dkc2_game.c
        runner/headless_host.c
        runner/verified_rom.c
        runner/headless_main.c)
    target_include_directories(dkc2_snesrecomp_headless PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(dkc2_snesrecomp_headless PRIVATE
        SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
        SNESRECOMP_REVERSE_DEBUG=0
        SYSTEM_VOLUME_MIXER_AVAILABLE=0)
    target_link_libraries(dkc2_snesrecomp_headless PRIVATE
        ${SNESRECOMP_RUNNER_LIBRARIES})
    dkc2_enable_project_warnings(dkc2_snesrecomp_headless)

    if(WIN32)
        add_executable(dkc2_snesrecomp_desktop WIN32
            ${SNESRECOMP_RUNNER_SOURCES}
            ${DKC2_SNESRECOMP_GEN_SOURCES}
            runner/dkc2_game.c
            runner/headless_host.c
            runner/verified_rom.c
            runner/desktop_input.c
            runner/desktop_present.c
            runner/desktop_rewind.c
            runner/desktop_main.c)
        target_include_directories(dkc2_snesrecomp_desktop PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/runner
            ${CMAKE_CURRENT_SOURCE_DIR}/recomp
            ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
        target_compile_definitions(dkc2_snesrecomp_desktop PRIVATE
            SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
            SNESRECOMP_REVERSE_DEBUG=0
            SYSTEM_VOLUME_MIXER_AVAILABLE=0)
        target_link_libraries(dkc2_snesrecomp_desktop PRIVATE
            ${SNESRECOMP_RUNNER_LIBRARIES}
            user32
            comdlg32
            gdi32
            shell32
            winmm
            xinput9_1_0)
        dkc2_enable_project_warnings(dkc2_snesrecomp_desktop)
    endif()

    if(BUILD_TESTING AND WIN32 AND DKC2_ROM)
        add_test(NAME supplied_rom_snesrecomp_smoke
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_snesrecomp_smoke.ps1"
                -Rom "${DKC2_ROM}"
                -Executable "$<TARGET_FILE:dkc2_snesrecomp_headless>"
                -Frames 600)
        set_tests_properties(supplied_rom_snesrecomp_smoke PROPERTIES
            LABELS "private;integration;native"
            TIMEOUT 120)

        add_test(NAME supplied_rom_snesrecomp_desktop_smoke
            COMMAND ${CMAKE_COMMAND} -E env
                DKC2_DESKTOP_TEST_FRAMES=180
                DKC2_DESKTOP_TEST_HIDDEN=1
                DKC2_DESKTOP_TEST_REWIND=1
                DKC2_DESKTOP_TEST_FASTFORWARD=1
                DKC2_DESKTOP_DISABLE_SRAM=1
                "$<TARGET_FILE:dkc2_snesrecomp_desktop>"
                "${DKC2_ROM}")
        set_tests_properties(supplied_rom_snesrecomp_desktop_smoke
            PROPERTIES
                LABELS "private;integration;native;desktop"
                PASS_REGULAR_EXPRESSION
                    "result=desktop_completed frames=180"
                TIMEOUT 30)

        # This active demo checkpoint is byte-identical between full AOT and
        # the interpreter oracle in WRAM and VRAM, and its captured frame has
        # been visually inspected. Keep the logic state in the gate as well as
        # the rendered output so a clean-looking but incorrect run cannot pass.
        add_test(NAME supplied_rom_snesrecomp_sprite_regression
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_snesrecomp_smoke.ps1"
                -Rom "${DKC2_ROM}"
                -Executable "$<TARGET_FILE:dkc2_snesrecomp_headless>"
                -Frames 3400
                -ExpectedFrameSha256
                    c2ba10df32cfe1e47dc5d2ef6e9deb3896f300e975bd4dbc226aee5b421b3c70
                -ExpectedWramSha256
                    ef1951a5b271459b0aec4f0fe25f2e0a39669319484cb0ce3836e9afbaf7d83c
                -ExpectedVramSha256
                    9aecf3dd226803fc6dd45f46a2aa2b2535c0371002a37708c5fcf2d3ae277ba9
                -ExpectedCgramSha256
                    7367e1127ea308fcfae78bdbb0dfe76962e79803bf618c9c9f9caac758c66cab
                -ExpectedOamSha256
                    d887880b305fcfe0f4c26d2e33ac7e7d93cb53c6d1b3ba56969fc154cbc67d50)
        set_tests_properties(supplied_rom_snesrecomp_sprite_regression
            PROPERTIES
                LABELS "private;integration;native;reference"
                TIMEOUT 120)

        add_test(NAME supplied_rom_snesrecomp_attract_two_cycles
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_snesrecomp_smoke.ps1"
                -Rom "${DKC2_ROM}"
                -Executable "$<TARGET_FILE:dkc2_snesrecomp_headless>"
                -Frames 12000
                -MinimumAttractCycles 2
                # Do not require state-event or audio waveform hashes here.
                # Both include host-frame pacing; the semantic attract order,
                # active audio, clipping, and discontinuity checks below are
                # stable correctness signals.
                )
        set_tests_properties(supplied_rom_snesrecomp_attract_two_cycles
            PROPERTIES
                LABELS "private;integration;native;attract"
                TIMEOUT 180)
    endif()
endif()

if(BUILD_TESTING)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME audio_pcm_compare
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compare_audio_pcm.py)
        set_tests_properties(audio_pcm_compare PROPERTIES
            LABELS "unit;audio")
        add_test(NAME state_trace_compare
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compare_state_traces.py)
        set_tests_properties(state_trace_compare PROPERTIES
            LABELS "unit;timing")
    endif()

    add_executable(test_desktop_input
        tests/test_desktop_input.c
        runner/desktop_input.c)
    target_include_directories(test_desktop_input PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc2_enable_project_warnings(test_desktop_input)
    add_test(NAME desktop_input COMMAND test_desktop_input)

    add_executable(test_desktop_rewind
        tests/test_desktop_rewind.c
        runner/desktop_rewind.c)
    target_include_directories(test_desktop_rewind PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc2_enable_project_warnings(test_desktop_rewind)
    add_test(NAME desktop_rewind COMMAND test_desktop_rewind)
    set_tests_properties(desktop_rewind PROPERTIES
        LABELS "unit;desktop;rewind")

    if(WIN32)
        add_executable(test_desktop_present
            tests/test_desktop_present.c
            runner/desktop_present.c)
        target_include_directories(test_desktop_present PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/runner)
        target_link_libraries(test_desktop_present PRIVATE gdi32 user32)
        dkc2_enable_project_warnings(test_desktop_present)
        add_test(NAME desktop_present COMMAND test_desktop_present)
        set_tests_properties(desktop_present PROPERTIES
            LABELS "unit;desktop;video")
    endif()

    # Keep the exact generic interpreter regressions exercised from the parent
    # repository while the upstream snesrecomp pull request is still pending.
    # A source-only checkout without an initialized submodule simply omits it.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp/runner/src/snes/interp816.c")
        add_executable(test_snesrecomp_interp816
            snesrecomp/tests/interp816/interp816_test.c
            snesrecomp/runner/src/snes/interp816.c)
        target_include_directories(test_snesrecomp_interp816 PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp/runner/src/snes)
        dkc2_enable_project_warnings(test_snesrecomp_interp816)
        add_test(NAME snesrecomp_interp816 COMMAND test_snesrecomp_interp816)
        set_tests_properties(snesrecomp_interp816 PROPERTIES
            LABELS "unit;native;upstream")

        add_executable(test_snesrecomp_interp_bridge
            snesrecomp/tests/interp816/bridge_test.c
            snesrecomp/runner/src/snes/interp816.c
            snesrecomp/runner/src/snes/interp_bridge.c)
        target_include_directories(test_snesrecomp_interp_bridge PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp/runner/src
            ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp/runner/src/snes)
        dkc2_enable_project_warnings(test_snesrecomp_interp_bridge)
        add_test(NAME snesrecomp_interp_bridge
            COMMAND test_snesrecomp_interp_bridge)
        set_tests_properties(snesrecomp_interp_bridge PROPERTIES
            LABELS "unit;native;upstream")
    endif()

    foreach(test_name IN ITEMS apu bus cpu decode execute hash hirom ppu_render rom snes_io symbols timing)
        add_executable(test_${test_name} tests/test_${test_name}.c)
        target_link_libraries(test_${test_name} PRIVATE dkc2_core)
        dkc2_enable_project_warnings(test_${test_name})
        if(test_name STREQUAL "symbols")
            target_compile_definitions(test_symbols PRIVATE
                TEST_SYMBOL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test_symbols.sym")
        endif()
        add_test(NAME ${test_name} COMMAND test_${test_name})
    endforeach()

    if(DKC2_ROM)
        add_test(NAME supplied_rom
                 COMMAND dkc2_verify "${DKC2_ROM}")
        set_tests_properties(supplied_rom PROPERTIES
            LABELS "private;integration")

        add_test(NAME supplied_rom_reset_analysis
                 COMMAND dkc2_analyze "${DKC2_ROM}" 64)
        set_tests_properties(supplied_rom_reset_analysis PROPERTIES
            LABELS "private;integration")

        add_test(NAME supplied_rom_nmi_analysis
                 COMMAND dkc2_analyze "${DKC2_ROM}" 32 --entry nmi)
        set_tests_properties(supplied_rom_nmi_analysis PROPERTIES
            LABELS "private;integration")

        add_test(NAME supplied_rom_irq_analysis
                 COMMAND dkc2_analyze "${DKC2_ROM}" 16 --entry irq)
        set_tests_properties(supplied_rom_irq_analysis PROPERTIES
            LABELS "private;integration")

        add_test(NAME supplied_rom_boot_probe
                 COMMAND dkc2_boot "${DKC2_ROM}" 200000)
        set_tests_properties(supplied_rom_boot_probe PROPERTIES
            LABELS "private;integration"
            PASS_REGULAR_EXPRESSION "VRAM clear:    confirmed")

        add_test(NAME supplied_rom_apu_probe
                 COMMAND dkc2_boot "${DKC2_ROM}" 5000000 --with-apu)
        set_tests_properties(supplied_rom_apu_probe PROPERTIES
            LABELS "private;integration"
            PASS_REGULAR_EXPRESSION
                "ARAM SHA-256:  49dd67b90ddb9ba3b7c75c3fcd02bf1bcebaf3ecabfa4392cb84a4e68b17784f")

        add_test(NAME supplied_rom_timing_probe
                 COMMAND dkc2_boot "${DKC2_ROM}" 20000000 --with-timing)
        set_tests_properties(supplied_rom_timing_probe PROPERTIES
            LABELS "private;integration"
            PASS_REGULAR_EXPRESSION
                "VRAM SHA-256:  fa7fa5b8d66b584757bbe01fa5e35906263791318c356e4080a91b2730274cdc")

        add_test(NAME supplied_rom_render_probe
                 COMMAND dkc2_boot "${DKC2_ROM}" 2000000 --with-render)
        set_tests_properties(supplied_rom_render_probe PROPERTIES
            LABELS "private;integration"
            PASS_REGULAR_EXPRESSION
                "Frame SHA-256: fd62d5bea3f0961e286bd4ae266ff1c09a30be9260da820003dc06b26d307b8d")

        add_test(NAME supplied_rom_mode7_render_probe
                 COMMAND dkc2_boot "${DKC2_ROM}" 1700000 --with-render)
        set_tests_properties(supplied_rom_mode7_render_probe PROPERTIES
            LABELS "private;integration"
            PASS_REGULAR_EXPRESSION
                "Frame SHA-256: ce5c1873327e39ba4d77c33e101ce9956ee86554c889855b8e3531b330923c2f")
    endif()
endif()
