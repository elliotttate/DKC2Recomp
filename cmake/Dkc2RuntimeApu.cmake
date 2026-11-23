# Keep the shared submodule pristine. The DKC2 build compiles a build-local
# copy of common_rtl.c with the APU snapshot-load fix ported from DKC3Recomp
# PR 17 (0152975), using exact anchors that fail on drift.
#
# apu_saveload snapshots only the guest APU state; the host port queue and the
# guest-to-SPC mapping are not serialized, and neither snapshot loader cleared
# them. apu_runToGuestCycle also returns at once while apu->portTimeValid is
# false, so after a load the SPC advanced only on frames that wrote an APU
# port: on the owner's quick save 593 of 599 frames were ~466/533 samples
# silent, heard as garbled audio. The loaders now clear the stale queue, and
# RtlRunFrame anchors an invalid timeline at the frame start so the frame
# boundary sync always advances the SPC by one frame.
set(_dkc2_rtl_source "${SNESRECOMP_ROOT}/runner/src/common_rtl.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_dkc2_rtl_source}")
file(READ "${_dkc2_rtl_source}" _dkc2_rtl)
function(dkc2_rtl_patch before after)
    string(FIND "${_dkc2_rtl}" "${before}" match)
    if(match EQUAL -1)
        message(FATAL_ERROR "DKC2 APU runtime patch needs review: missing anchor ${before}")
    endif()
    string(LENGTH "${before}" anchor_length)
    math(EXPR after_anchor "${match} + ${anchor_length}")
    string(SUBSTRING "${_dkc2_rtl}" ${after_anchor} -1 remainder)
    string(FIND "${remainder}" "${before}" duplicate)
    if(NOT duplicate EQUAL -1)
        message(FATAL_ERROR "DKC2 APU runtime patch has an ambiguous anchor ${before}")
    endif()
    string(REPLACE "${before}" "${after}" _patched "${_dkc2_rtl}")
    set(_dkc2_rtl "${_patched}" PARENT_SCOPE)
endfunction()
dkc2_rtl_patch("g_apu_frame_time_valid = true;"
    "g_apu_frame_time_valid = true;
  /* DKC2: anchor an invalid APU timeline at this frame's start. */
  RtlApuLock();
  if (g_snes && g_snes->apu && !g_snes->apu->portTimeValid) {
    uint64_t frame_start_guest =
        (uint64_t)snes_frame_counter * RTL_APU_CYCLES_PER_FRAME;
    g_snes->apu->portGuestAnchor = frame_start_guest;
    g_snes->apu->portTargetAnchor = g_snes->apu->portClock;
    g_snes->apu->portLastGuest = frame_start_guest;
    g_snes->apu->portLastTarget = g_snes->apu->portClock;
    g_snes->apu->portTimeValid = true;
  }
  RtlApuUnlock();")
dkc2_rtl_patch("if (hdr[1] >= 5 && g_rtl_game_info && g_rtl_game_info->state_load_extra) {"
    "/* DKC2: drop the host port queue and mapping from before the load. */
  if (!fs.error && g_snes && g_snes->apu)
    apu_clearPortQueue(g_snes->apu);
  if (hdr[1] >= 5 && g_rtl_game_info && g_rtl_game_info->state_load_extra) {")
dkc2_rtl_patch("if (hdr[1] >= 5 && g_rtl_game_info && g_rtl_game_info->state_load_extra &&"
    "/* DKC2: drop the host port queue and mapping from before the load. */
  if (!memory.error && g_snes && g_snes->apu)
    apu_clearPortQueue(g_snes->apu);
  if (hdr[1] >= 5 && g_rtl_game_info && g_rtl_game_info->state_load_extra &&")
set(_dkc2_rtl_output "${CMAKE_CURRENT_BINARY_DIR}/dkc2_common_rtl.c")
file(WRITE "${_dkc2_rtl_output}.in" "${_dkc2_rtl}")
configure_file("${_dkc2_rtl_output}.in" "${_dkc2_rtl_output}" COPYONLY)
# runner.cmake gives the original per-source definitions (frame
# fingerprints); the copy must compile with the same ones.
get_source_file_property(_dkc2_rtl_definitions "${_dkc2_rtl_source}"
    COMPILE_DEFINITIONS)
if(NOT _dkc2_rtl_definitions)
    message(FATAL_ERROR "DKC2 APU runtime patch needs review: common_rtl.c "
        "has no per-source definitions to carry over")
endif()
set_source_files_properties("${_dkc2_rtl_output}" PROPERTIES
    INCLUDE_DIRECTORIES "${SNESRECOMP_ROOT}/runner/src"
    COMPILE_DEFINITIONS "${_dkc2_rtl_definitions}")
list(REMOVE_ITEM SNESRECOMP_RUNNER_SOURCES "${_dkc2_rtl_source}")
list(APPEND SNESRECOMP_RUNNER_SOURCES "${_dkc2_rtl_output}")
set(DKC2_RUNTIME_APU_SOURCE "${_dkc2_rtl_output}")
