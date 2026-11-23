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
string(FIND "${_rtl}" "if (!fs.error && g_snes && g_snes->apu)" _file_clear)
string(FIND "${_rtl}" "if (!memory.error && g_snes && g_snes->apu)" _memory_clear)
if(_file_clear EQUAL -1 OR _memory_clear EQUAL -1)
    message(FATAL_ERROR "a snapshot loader does not clear the APU port queue")
endif()
message(STATUS "dkc2_runtime_apu_patch: ok")
