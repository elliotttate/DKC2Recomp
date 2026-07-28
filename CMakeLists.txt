cmake_minimum_required(VERSION 3.20)

project(dkc2_port
    VERSION 0.0.1
    DESCRIPTION "Native PC recompilation of Donkey Kong Country 2"
    LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(DKC2_ROM "" CACHE FILEPATH "Private path to a headerless DKC2 USA v1.0 ROM")
set(DKC2_PIRATE_PANIC_INPUT "" CACHE FILEPATH
    "Private deterministic input recording for the Pirate Panic entrance-to-goal gate")
set(DKC2_DESKTOP_ICON "" CACHE FILEPATH
    "Optional private ICO embedded in the Windows desktop executable")
option(DKC2_BUILD_SNESRECOMP
    "Build the experimental snesrecomp headless and desktop runners" OFF)
option(DKC2_BUILD_SNESRECOMP_HEADLESS
    "Deprecated alias for DKC2_BUILD_SNESRECOMP" OFF)
option(DKC2_FETCH_SDL2
    "Fetch the pinned SDL2 source when no system package is available" ON)
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

function(dkc2_enable_release_optimization target_name)
    if(MSVC)
        # MSVC has no /O3 level; /O2 is its maximum supported speed preset.
        target_compile_options(${target_name} PRIVATE
            "$<$<CONFIG:Release>:/O2>")
    else()
        target_compile_options(${target_name} PRIVATE
            "$<$<CONFIG:Release>:-O3>")
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
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/snesrecomp" CACHE PATH
        "Directory containing private generated snesrecomp C sources")
    if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
        message(FATAL_ERROR
            "snesrecomp is not initialized; run git submodule update --init --recursive")
    endif()
    include(${SNESRECOMP_ROOT}/runner/runner.cmake)
    # SDL2 and recomp-ui are portable dependencies shared by the launcher and
    # the gameplay host on Windows, Linux, and macOS. Prefer a package config
    # (vcpkg/Homebrew commonly provide one), then accept a distribution module.
    find_package(SDL2 CONFIG QUIET)
    if(NOT TARGET SDL2::SDL2)
        if(NOT DKC2_FETCH_SDL2)
            message(FATAL_ERROR
                "SDL2 was not found. Install SDL2 or configure with DKC2_FETCH_SDL2=ON")
        endif()
        include(FetchContent)
        set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-2.30.9
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(SDL2)
    endif()
    include(${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/recomp_ui.cmake)
    file(GLOB DKC2_SNESRECOMP_GEN_SOURCES CONFIGURE_DEPENDS
        "${DKC2_SNESRECOMP_GEN_DIR}/*.c")
    if(NOT DKC2_SNESRECOMP_GEN_SOURCES)
        message(FATAL_ERROR
            "No private generated sources found; run scripts/generate_snesrecomp.py "
            "or scripts/generate_snesrecomp.ps1 first")
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
        runner/dkc2_video.c
        runner/headless_host.c
        runner/input_playback.c
        runner/verified_rom.c
        runner/headless_main.c)
    target_include_directories(dkc2_snesrecomp_headless PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(dkc2_snesrecomp_headless PRIVATE
        SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
        SNESRECOMP_REVERSE_DEBUG=0
        SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0)
    target_link_libraries(dkc2_snesrecomp_headless PRIVATE
        ${SNESRECOMP_RUNNER_LIBRARIES})
    dkc2_enable_project_warnings(dkc2_snesrecomp_headless)
    dkc2_enable_release_optimization(dkc2_snesrecomp_headless)

    if(WIN32)
        set(DKC2_DESKTOP_ICON_RESOURCE "")
        if(DKC2_DESKTOP_ICON)
            if(NOT EXISTS "${DKC2_DESKTOP_ICON}")
                message(FATAL_ERROR
                    "DKC2_DESKTOP_ICON does not exist: ${DKC2_DESKTOP_ICON}")
            endif()
            enable_language(RC)
            get_filename_component(DKC2_DESKTOP_ICON_ABSOLUTE
                "${DKC2_DESKTOP_ICON}" ABSOLUTE)
            file(TO_CMAKE_PATH "${DKC2_DESKTOP_ICON_ABSOLUTE}"
                DKC2_DESKTOP_ICON_RC)
            configure_file(
                runner/desktop_icon.rc.in
                "${CMAKE_CURRENT_BINARY_DIR}/dkc2_desktop_icon.rc"
                @ONLY)
            set(DKC2_DESKTOP_ICON_RESOURCE
                "${CMAKE_CURRENT_BINARY_DIR}/dkc2_desktop_icon.rc")
        endif()

        add_executable(dkc2_snesrecomp_desktop WIN32
            ${DKC2_DESKTOP_ICON_RESOURCE}
            ${SNESRECOMP_RUNNER_SOURCES}
            ${DKC2_SNESRECOMP_GEN_SOURCES}
            runner/dkc2_game.c
            runner/dkc2_video.c
            runner/diagnostics.c
            runner/headless_host.c
            runner/verified_rom.c
            runner/desktop_fps.c
            runner/desktop_filter.c
            runner/desktop_input.c
            runner/desktop_launcher.c
            runner/desktop_overlay.cpp
            runner/desktop_overlay_model.c
            runner/desktop_perf.c
            runner/desktop_present.c
            runner/desktop_present_gl.c
            runner/desktop_rewind.c
            runner/desktop_viewport.c
            runner/desktop_main.c
            ${SNESRECOMP_ROOT}/runner/src/host_report.c)
        target_include_directories(dkc2_snesrecomp_desktop PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/runner
            ${CMAKE_CURRENT_SOURCE_DIR}/recomp
            ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
        target_compile_definitions(dkc2_snesrecomp_desktop PRIVATE
            DKC2_RELEASE_VERSION="${PROJECT_VERSION}"
            SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
            SNESRECOMP_REVERSE_DEBUG=0
            SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
            SYSTEM_VOLUME_MIXER_AVAILABLE=0
            DKC2_REAL_HOST_REPORT=1)
        target_link_libraries(dkc2_snesrecomp_desktop PRIVATE
            ${SNESRECOMP_RUNNER_LIBRARIES}
            SDL2::SDL2
            user32
            comdlg32
            gdi32
            opengl32
            shell32
            winmm
            xinput9_1_0)
        set_target_properties(dkc2_snesrecomp_desktop PROPERTIES
            OUTPUT_NAME DKC2Recomp)
        # Shared Dear ImGui pre-boot launcher with the North American retail
        # cover used to identify the required ROM region.
        recomp_target_launcher_ui(dkc2_snesrecomp_desktop
            BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
        dkc2_enable_project_warnings(dkc2_snesrecomp_desktop)
        dkc2_enable_release_optimization(dkc2_snesrecomp_desktop)
    endif()

    # Cross-platform playable host. The established Win32 executable remains
    # available while this SDL2 host is validated on each native platform.
    add_executable(dkc2_snesrecomp_sdl
        ${SNESRECOMP_RUNNER_SOURCES}
        ${DKC2_SNESRECOMP_GEN_SOURCES}
        runner/dkc2_game.c
        runner/dkc2_video.c
        runner/diagnostics.c
        runner/headless_host.c
        runner/verified_rom.c
        runner/desktop_filter.c
        runner/desktop_fps.c
        runner/desktop_input.c
        runner/desktop_launcher.c
        runner/desktop_overlay.cpp
        runner/desktop_overlay_model.c
        runner/desktop_present_sdl.c
        runner/desktop_rewind.c
        runner/desktop_viewport.c
        runner/sdl_main.c
        ${SNESRECOMP_ROOT}/runner/src/host_report.c)
    target_include_directories(dkc2_snesrecomp_sdl PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(dkc2_snesrecomp_sdl PRIVATE
        DKC2_RELEASE_VERSION="${PROJECT_VERSION}"
        SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
        SNESRECOMP_REVERSE_DEBUG=0
        SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0
        SDL_MAIN_HANDLED
        DKC2_REAL_HOST_REPORT=1)
    target_link_libraries(dkc2_snesrecomp_sdl PRIVATE
        ${SNESRECOMP_RUNNER_LIBRARIES}
        SDL2::SDL2)
    if(UNIX AND NOT APPLE)
        target_link_libraries(dkc2_snesrecomp_sdl PRIVATE m)
    endif()
    if(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(dkc2_snesrecomp_sdl PRIVATE Threads::Threads)
    endif()
    if(WIN32)
        set_target_properties(dkc2_snesrecomp_sdl PROPERTIES
            OUTPUT_NAME DKC2RecompSDL)
    else()
        set_target_properties(dkc2_snesrecomp_sdl PROPERTIES
            OUTPUT_NAME DKC2Recomp)
    endif()
    recomp_target_launcher_ui(dkc2_snesrecomp_sdl
        BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
    dkc2_enable_project_warnings(dkc2_snesrecomp_sdl)
    dkc2_enable_release_optimization(dkc2_snesrecomp_sdl)

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
                DKC2_DESKTOP_TEST_OVERLAY=1
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

        add_test(NAME supplied_rom_desktop_crt_opengl
            COMMAND ${CMAKE_COMMAND} -E env
                DKC2_DESKTOP_TEST_FRAMES=60
                DKC2_DESKTOP_TEST_HIDDEN=1
                DKC2_DESKTOP_DISABLE_SRAM=1
                DKC2_DESKTOP_REQUIRE_GPU=1
                SNESRECOMP_NO_LAUNCHER=1
                DKC2_SCREEN=crt
                "$<TARGET_FILE:dkc2_snesrecomp_desktop>"
                "${DKC2_ROM}")
        set_tests_properties(supplied_rom_desktop_crt_opengl PROPERTIES
            LABELS "private;integration;desktop;video;gpu"
            TIMEOUT 60)

        add_test(NAME supplied_rom_desktop_crt_gdi
            COMMAND ${CMAKE_COMMAND} -E env
                DKC2_DESKTOP_TEST_FRAMES=60
                DKC2_DESKTOP_TEST_HIDDEN=1
                DKC2_DESKTOP_DISABLE_SRAM=1
                DKC2_DESKTOP_FORCE_GDI=1
                SNESRECOMP_NO_LAUNCHER=1
                DKC2_SCREEN=crt
                "$<TARGET_FILE:dkc2_snesrecomp_desktop>"
                "${DKC2_ROM}")
        set_tests_properties(supplied_rom_desktop_crt_gdi PROPERTIES
            LABELS "private;integration;desktop;video;fallback"
                TIMEOUT 60)

        add_test(NAME supplied_rom_sdl_desktop_smoke
            COMMAND ${CMAKE_COMMAND} -E env
                DKC2_DESKTOP_TEST_FRAMES=180
                DKC2_DESKTOP_TEST_HIDDEN=1
                DKC2_DESKTOP_TEST_OVERLAY=1
                DKC2_DESKTOP_TEST_REWIND=1
                DKC2_DESKTOP_TEST_FASTFORWARD=1
                DKC2_DESKTOP_DISABLE_SRAM=1
                SNESRECOMP_NO_LAUNCHER=1
                "$<TARGET_FILE:dkc2_snesrecomp_sdl>"
                "${DKC2_ROM}")
        set_tests_properties(supplied_rom_sdl_desktop_smoke PROPERTIES
            LABELS "private;integration;native;desktop;sdl;portable"
            PASS_REGULAR_EXPRESSION
                "result=desktop_completed frames=180.*host=sdl2"
            TIMEOUT 30)

        foreach(diagnostic_mode clean die seh)
            add_test(NAME supplied_rom_diagnostic_${diagnostic_mode}
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_diagnostic_bundle.ps1"
                    -Rom "${DKC2_ROM}"
                    -Executable "$<TARGET_FILE:dkc2_snesrecomp_sdl>"
                    -Mode ${diagnostic_mode})
            set_tests_properties(supplied_rom_diagnostic_${diagnostic_mode}
                PROPERTIES
                    LABELS "private;integration;desktop;diagnostics;crash"
                    TIMEOUT 45)
        endforeach()

        # This active demo checkpoint is byte-identical to the interpreter
        # oracle's frame-3400 state in the framebuffer, WRAM, VRAM, CGRAM, and
        # OAM. The frame indices differ by the one-time 91-frame host-pacing
        # shift introduced when long block moves became frame-interruptible.
        # Keep the complete logic state in the gate as well as the visually
        # inspected render so a clean-looking but incorrect run cannot pass.
        add_test(NAME supplied_rom_snesrecomp_sprite_regression
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_snesrecomp_smoke.ps1"
                -Rom "${DKC2_ROM}"
                -Executable "$<TARGET_FILE:dkc2_snesrecomp_headless>"
                -Frames 3309
                -ExpectedFrameSha256
                    52e2b6bf0985371e5f334f0f0b9d2bfdecebfe080608b27389d9577eb75240cc
                -ExpectedWramSha256
                    1c1ef6965e1cfd4c1b467287dbe83b019cac433ae9c632159e0408a86cb87752
                -ExpectedVramSha256
                    09c88c5be0986ae1c23c76921d416dd7a5a3014ced5263f04606ed662d824458
                -ExpectedCgramSha256
                    b4292587e6cfc9c9c2be69d97f9955167ee86c3583b6f86c12909375ebb8646d
                -ExpectedOamSha256
                    18944a45873b897d870182e96693f73b27582a6c76f5acdb55521837a254c0b6)
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

        if(DKC2_PIRATE_PANIC_INPUT)
            if(NOT EXISTS "${DKC2_PIRATE_PANIC_INPUT}")
                message(FATAL_ERROR
                    "DKC2_PIRATE_PANIC_INPUT does not exist: "
                    "${DKC2_PIRATE_PANIC_INPUT}")
            endif()
            add_test(NAME supplied_rom_pirate_panic_route
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_pirate_panic_route.ps1"
                    -Rom "${DKC2_ROM}"
                    -Executable "$<TARGET_FILE:dkc2_snesrecomp_headless>"
                    -Input "${DKC2_PIRATE_PANIC_INPUT}")
            set_tests_properties(supplied_rom_pirate_panic_route
                PROPERTIES
                    LABELS "private;integration;native;first-level"
                    TIMEOUT 300)
        endif()
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
        add_test(NAME generate_snesrecomp_script
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_generate_snesrecomp.py)
        set_tests_properties(generate_snesrecomp_script PROPERTIES
            LABELS "unit;tooling;portable")
        add_test(NAME dkc2_widescreen_overrides
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_apply_dkc2_widescreen_overrides.py)
        set_tests_properties(dkc2_widescreen_overrides PROPERTIES
            LABELS "unit;tooling;widescreen;portable")
        add_test(NAME dkc2_tcp_screenshot_tool
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_capture_tcp_screenshot.py)
        set_tests_properties(dkc2_tcp_screenshot_tool PROPERTIES
            LABELS "unit;tooling;widescreen;portable")
    endif()

    add_executable(test_desktop_input
        tests/test_desktop_input.c
        runner/desktop_input.c)
    target_include_directories(test_desktop_input PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/src)
    dkc2_enable_project_warnings(test_desktop_input)
    add_test(NAME desktop_input COMMAND test_desktop_input)

    add_executable(test_input_playback
        tests/test_input_playback.c
        runner/input_playback.c)
    target_include_directories(test_input_playback PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc2_enable_project_warnings(test_input_playback)
    add_test(NAME input_playback
        COMMAND test_input_playback
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/input_playback_valid.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/input_playback_invalid.txt)
    set_tests_properties(input_playback PROPERTIES
        LABELS "unit;native;input")

    add_executable(test_launcher_defaults
        tests/test_launcher_defaults.c
        recomp-ui/src/common/launcher_model.c
        recomp-ui/src/common/crc32.c
        recomp-ui/src/common/sha256.c
        recomp-ui/src/common/sha1.c
        recomp-ui/src/common/ips_patch.c
        recomp-ui/src/consoles/psx/memcard_format.c)
    target_include_directories(test_launcher_defaults PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/src
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/src/common)
    dkc2_enable_project_warnings(test_launcher_defaults)
    add_test(NAME launcher_defaults COMMAND test_launcher_defaults)
    set_tests_properties(launcher_defaults PROPERTIES
        LABELS "unit;desktop;launcher")

    add_executable(test_desktop_rewind
        tests/test_desktop_rewind.c
        runner/desktop_rewind.c)
    target_include_directories(test_desktop_rewind PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc2_enable_project_warnings(test_desktop_rewind)
    add_test(NAME desktop_rewind COMMAND test_desktop_rewind)
    set_tests_properties(desktop_rewind PROPERTIES
        LABELS "unit;desktop;rewind")

    add_executable(test_desktop_overlay_model
        tests/test_desktop_overlay_model.c
        runner/desktop_overlay_model.c)
    target_include_directories(test_desktop_overlay_model PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc2_enable_project_warnings(test_desktop_overlay_model)
    add_test(NAME desktop_overlay_model COMMAND test_desktop_overlay_model)
    set_tests_properties(desktop_overlay_model PROPERTIES
        LABELS "unit;desktop;overlay")

    add_executable(test_desktop_fps
        tests/test_desktop_fps.c
        runner/desktop_fps.c)
    target_include_directories(test_desktop_fps PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc2_enable_project_warnings(test_desktop_fps)
    add_test(NAME desktop_fps COMMAND test_desktop_fps)
    set_tests_properties(desktop_fps PROPERTIES
        LABELS "unit;desktop;video;performance")

    add_executable(test_desktop_perf
        tests/test_desktop_perf.c
        runner/desktop_perf.c)
    target_include_directories(test_desktop_perf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc2_enable_project_warnings(test_desktop_perf)
    add_test(NAME desktop_perf COMMAND test_desktop_perf)
    set_tests_properties(desktop_perf PROPERTIES
        LABELS "unit;desktop;performance")

    add_executable(test_desktop_viewport
        tests/test_desktop_viewport.c
        runner/desktop_viewport.c)
    target_include_directories(test_desktop_viewport PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc2_enable_project_warnings(test_desktop_viewport)
    add_test(NAME desktop_viewport COMMAND test_desktop_viewport)
    set_tests_properties(desktop_viewport PROPERTIES
        LABELS "unit;desktop;video;portable")

    add_executable(test_dkc2_video
        tests/test_dkc2_video.c
        runner/dkc2_video.c)
    target_include_directories(test_dkc2_video PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc2_enable_project_warnings(test_dkc2_video)
    add_test(NAME dkc2_video COMMAND test_dkc2_video)
    set_tests_properties(dkc2_video PROPERTIES
        LABELS "unit;desktop;video;widescreen")

    if(WIN32)
        add_test(NAME create_windows_version
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_create_windows_version.ps1"
                -Script "${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_windows_version.ps1")
        set_tests_properties(create_windows_version PROPERTIES
            LABELS "unit;packaging;windows")

        add_executable(test_desktop_present
            tests/test_desktop_present.c
            runner/desktop_filter.c
            runner/desktop_present.c
            runner/desktop_present_gl.c
            runner/desktop_viewport.c
            snesrecomp/runner/src/snes/color_lut.c)
        target_include_directories(test_desktop_present PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/runner
            ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp/runner/src)
        target_link_libraries(test_desktop_present PRIVATE
            gdi32 user32 opengl32)
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
