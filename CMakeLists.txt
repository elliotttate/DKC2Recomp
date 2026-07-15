cmake_minimum_required(VERSION 3.20)

project(dkc2_port
    VERSION 0.6.0
    DESCRIPTION "Clean-room tooling and runtime foundation for a DKC2 native port"
    LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(DKC2_ROM "" CACHE FILEPATH "Private path to a headerless DKC2 USA v1.0 ROM")

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
        target_compile_options(${target_name} PRIVATE /W4)
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

if(BUILD_TESTING)
    foreach(test_name IN ITEMS apu bus cpu decode execute hash hirom rom snes_io symbols timing)
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
    endif()
endif()
