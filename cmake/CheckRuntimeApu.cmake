# CTest check (cmake -DFILE=<dkc2_common_rtl.c> -P): the build-local runtime
# copy carries the APU snapshot-load fix from cmake/Dkc2RuntimeApu.cmake.
file(READ "${FILE}" _rtl)
# Match without the trailing ';', which CMake would split as a list.
string(REGEX MATCHALL "apu_clearPortQueue\\(g_snes->apu\\)" _clears "${_rtl}")
list(LENGTH _clears _clear_count)
if(NOT _clear_count EQUAL 3)
    message(FATAL_ERROR "expected 3 port-queue clears (upload and both "
        "snapshot loaders), found ${_clear_count}")
endif()
string(FIND "${_rtl}" "if (g_snes && g_snes->apu && !g_snes->apu->portTimeValid) {" _anchor)
if(_anchor EQUAL -1)
    message(FATAL_ERROR "frame-start APU timeline anchor is missing")
endif()
# Each snapshot loader's guard must be followed directly by its clear.
foreach(_loader fs memory)
    string(REGEX MATCH
        "if \\(!${_loader}\\.error && g_snes && g_snes->apu\\)[ \t\r\n]+apu_clearPortQueue\\(g_snes->apu\\)"
        _loader_clear "${_rtl}")
    if(NOT _loader_clear)
        message(FATAL_ERROR
            "the ${_loader} snapshot loader does not clear the APU port queue")
    endif()
endforeach()
message(STATUS "dkc2_runtime_apu_patch: ok")
