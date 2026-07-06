# SPDX-License-Identifier: Apache-2.0
#
# PiZZa -- sdl2-doom via the shared minimal SDL2 shim (apps/lib/sdl2shim).
#
# Links the AlexOberhofer/sdl2-doom game sources (pure C, Chocolate-Doom
# lineage, UNTOUCHED at ~/github/sdl2-doom) against the shim. The one
# concession to "untouched" is a build-time rewrite of i_video.c's
# SDL_RESX/SDL_RESY from *3 to native 320x200: the fork bakes a 3x
# software upscale we do not want (the VC4 HVS scales instead). The
# checkout is not modified -- only a generated copy in the build tree.
#
# sdl2-doom is GPL-2.0; this binary is GPL-2.0 by virtue of linking it.
# The shim + port glue are Apache-2.0, same model as the SDLPoP port.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza-doom C ASM)

# sample_usbd helper for the USB CDC ACM shell (rpi_zero_2w), same as
# PizzaShell / wipeout / SDLPoP.
if(CONFIG_USB_DEVICE_STACK_NEXT)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

# ── Doom game tree (untouched) ──────────────────────────────────────
if(NOT DEFINED DOOM_SRC_DIR)
  set(DOOM_SRC_DIR "$ENV{HOME}/github/sdl2-doom/src")
endif()
if(NOT EXISTS "${DOOM_SRC_DIR}/d_main.c")
  message(FATAL_ERROR
    "sdl2-doom sources not found at ${DOOM_SRC_DIR}. Clone "
    "https://github.com/AlexOberhofer/sdl2-doom and keep it at "
    "~/github/sdl2-doom or pass -DDOOM_SRC_DIR=<path>.")
endif()

# The fork's own build list (its CMakeLists), minus i_cdmus.c (SDL 1.2
# CD API, absent in SDL2; grep confirms nothing else references
# I_CDMus*) and minus i_video.c, which is fed in rewritten below.
set(DOOM_GAME_SRC
  ${DOOM_SRC_DIR}/i_main.c ${DOOM_SRC_DIR}/dummy.c ${DOOM_SRC_DIR}/am_map.c
  ${DOOM_SRC_DIR}/doomdef.c ${DOOM_SRC_DIR}/doomstat.c ${DOOM_SRC_DIR}/dstrings.c
  ${DOOM_SRC_DIR}/d_event.c ${DOOM_SRC_DIR}/d_items.c ${DOOM_SRC_DIR}/d_iwad.c
  ${DOOM_SRC_DIR}/d_loop.c ${DOOM_SRC_DIR}/d_main.c ${DOOM_SRC_DIR}/d_mode.c
  ${DOOM_SRC_DIR}/d_net.c ${DOOM_SRC_DIR}/f_finale.c ${DOOM_SRC_DIR}/f_wipe.c
  ${DOOM_SRC_DIR}/g_game.c ${DOOM_SRC_DIR}/hu_lib.c ${DOOM_SRC_DIR}/hu_stuff.c
  ${DOOM_SRC_DIR}/info.c ${DOOM_SRC_DIR}/i_endoom.c ${DOOM_SRC_DIR}/i_joystick.c
  ${DOOM_SRC_DIR}/i_scale.c ${DOOM_SRC_DIR}/i_sound.c ${DOOM_SRC_DIR}/i_sdlmusic.c
  ${DOOM_SRC_DIR}/i_sdlsound.c ${DOOM_SRC_DIR}/i_system.c ${DOOM_SRC_DIR}/i_timer.c
  ${DOOM_SRC_DIR}/i_input.c ${DOOM_SRC_DIR}/mus2mid.c ${DOOM_SRC_DIR}/memio.c
  ${DOOM_SRC_DIR}/m_argv.c ${DOOM_SRC_DIR}/m_bbox.c ${DOOM_SRC_DIR}/m_cheat.c
  ${DOOM_SRC_DIR}/m_config.c ${DOOM_SRC_DIR}/m_controls.c ${DOOM_SRC_DIR}/m_fixed.c
  ${DOOM_SRC_DIR}/m_menu.c ${DOOM_SRC_DIR}/m_misc.c ${DOOM_SRC_DIR}/m_random.c
  ${DOOM_SRC_DIR}/p_ceilng.c ${DOOM_SRC_DIR}/p_doors.c ${DOOM_SRC_DIR}/p_enemy.c
  ${DOOM_SRC_DIR}/p_floor.c ${DOOM_SRC_DIR}/p_inter.c ${DOOM_SRC_DIR}/p_lights.c
  ${DOOM_SRC_DIR}/p_map.c ${DOOM_SRC_DIR}/p_maputl.c ${DOOM_SRC_DIR}/p_mobj.c
  ${DOOM_SRC_DIR}/p_plats.c ${DOOM_SRC_DIR}/p_pspr.c ${DOOM_SRC_DIR}/p_saveg.c
  ${DOOM_SRC_DIR}/p_setup.c ${DOOM_SRC_DIR}/p_sight.c ${DOOM_SRC_DIR}/p_spec.c
  ${DOOM_SRC_DIR}/p_switch.c ${DOOM_SRC_DIR}/p_telept.c ${DOOM_SRC_DIR}/p_tick.c
  ${DOOM_SRC_DIR}/p_user.c ${DOOM_SRC_DIR}/r_bsp.c ${DOOM_SRC_DIR}/r_data.c
  ${DOOM_SRC_DIR}/r_draw.c ${DOOM_SRC_DIR}/r_main.c ${DOOM_SRC_DIR}/r_plane.c
  ${DOOM_SRC_DIR}/r_segs.c ${DOOM_SRC_DIR}/r_sky.c ${DOOM_SRC_DIR}/r_things.c
  ${DOOM_SRC_DIR}/sha1.c ${DOOM_SRC_DIR}/sounds.c ${DOOM_SRC_DIR}/statdump.c
  ${DOOM_SRC_DIR}/st_lib.c ${DOOM_SRC_DIR}/st_stuff.c ${DOOM_SRC_DIR}/s_sound.c
  ${DOOM_SRC_DIR}/tables.c ${DOOM_SRC_DIR}/v_video.c ${DOOM_SRC_DIR}/wi_stuff.c
  ${DOOM_SRC_DIR}/w_checksum.c ${DOOM_SRC_DIR}/w_file.c ${DOOM_SRC_DIR}/w_main.c
  ${DOOM_SRC_DIR}/w_wad.c ${DOOM_SRC_DIR}/z_zone.c ${DOOM_SRC_DIR}/w_file_stdc.c
)

# i_video.c rewritten to native 320x200 (SDL_RESX/Y *3 -> plain). Keeps
# the checkout pristine; the whole delta is this two-line sed.
set(DOOM_IVIDEO_GEN ${CMAKE_BINARY_DIR}/gen/i_video_320.c)
add_custom_command(
  OUTPUT ${DOOM_IVIDEO_GEN}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
  COMMAND ${CMAKE_COMMAND} -E copy ${DOOM_SRC_DIR}/i_video.c ${DOOM_IVIDEO_GEN}
  COMMAND sed -i.bak
          -e "s|#define SDL_RESX 320 \\* 3|#define SDL_RESX 320|"
          -e "s|#define SDL_RESY 200 \\* 3|#define SDL_RESY 200|"
          ${DOOM_IVIDEO_GEN}
  DEPENDS ${DOOM_SRC_DIR}/i_video.c
  COMMENT "Rewriting i_video.c to native 320x200 (HVS upscales)"
  VERBATIM
)
list(APPEND DOOM_GAME_SRC ${DOOM_IVIDEO_GEN})

# ── shim ────────────────────────────────────────────────────────────
include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl2shim/sdl2shim.cmake)

set(DOOM_SHIM_SRC
  src/shim_main.c
  src/shim_video.c
  src/shim_mixer.c
  src/doom_assets.c
  src/doom_pack.S
)

if(CONFIG_DOOM_PRESENT_DISPLAY)
  list(APPEND DOOM_SHIM_SRC src/doom_present_display.c)
elseif(CONFIG_DOOM_PRESENT_SEMIHOST)
  list(APPEND DOOM_SHIM_SRC src/doom_present_semihost.c)
else()
  list(APPEND DOOM_SHIM_SRC src/doom_present_none.c)
endif()

# Audio backend (lib-owned choice): qemu uses "none" (Mix_OpenAudio
# fails, Doom silent); rpi_zero_2w uses the HDMI/MAI backend the mixer
# feeds through s2s_audio_open.
if(CONFIG_SDL2SHIM_AUDIO_HDMI)
  list(APPEND DOOM_SHIM_SRC ${SDL2SHIM_AUDIO_HDMI_SOURCES})
else()
  list(APPEND DOOM_SHIM_SRC ${SDL2SHIM_AUDIO_NONE})
endif()

# USB device bring-up + the `doom` shell = interactive input on hardware.
if(CONFIG_USB_DEVICE_STACK_NEXT)
  list(APPEND DOOM_SHIM_SRC ${SDL2SHIM_USB})
endif()
if(CONFIG_SHELL)
  list(APPEND DOOM_SHIM_SRC src/doom_shell.c)
endif()

if(CONFIG_DOOM_SCRIPTED_INPUT)
  # Doom brings its own timeline; the lib's shim_scripted has SDLPoP's.
  list(APPEND DOOM_SHIM_SRC src/doom_scripted.c)
endif()

# ── IWAD bake (§5): raw .incbin into rodata ─────────────────────────
if(NOT DEFINED DOOM_WAD)
  # Prefer a complete shareware/retail IWAD; fall back to the minimal
  # test IWAD (boots + inits, but lacks title/music/map lumps).
  if(EXISTS "$ENV{HOME}/github/doom1.wad")
    set(DOOM_WAD "$ENV{HOME}/github/doom1.wad")
  else()
    set(DOOM_WAD "$ENV{HOME}/github/chocolate-doom/quickcheck/iwads/miniwad.wad")
  endif()
endif()
if(NOT EXISTS "${DOOM_WAD}")
  message(FATAL_ERROR "IWAD not found: ${DOOM_WAD}. Pass -DDOOM_WAD=<path/to/doom1.wad>.")
endif()
set(DOOM_WAD_BIN ${CMAKE_BINARY_DIR}/doom_iwad.wad)
add_custom_command(
  OUTPUT ${DOOM_WAD_BIN}
  COMMAND ${CMAKE_COMMAND} -E copy ${DOOM_WAD} ${DOOM_WAD_BIN}
  DEPENDS ${DOOM_WAD}
  COMMENT "Baking IWAD ${DOOM_WAD} into rodata"
)
add_custom_target(doom_wad_bake DEPENDS ${DOOM_WAD_BIN})
add_dependencies(app doom_wad_bake)
set_source_files_properties(src/doom_pack.S PROPERTIES
  OBJECT_DEPENDS ${DOOM_WAD_BIN}
  COMPILE_OPTIONS "-I${CMAKE_BINARY_DIR};-Wa,-I${CMAKE_BINARY_DIR}"
)

# ── target ──────────────────────────────────────────────────────────
target_sources(app PRIVATE
  ${DOOM_GAME_SRC}
  ${DOOM_SHIM_SRC}
  ${SDL2SHIM_CORE_SOURCES}
)

# include/ order: shim SDL2 headers first so <SDL2/SDL.h> and
# <SDL2/SDL_mixer.h> resolve to the shim, then the game headers.
target_include_directories(app PRIVATE
  ${SDL2SHIM_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DOOM_SRC_DIR}/../include
  ${DOOM_SRC_DIR}
)

# Game TUs assume glibc (strcasecmp/alloca via string.h/stdlib.h);
# picolibc keeps them in <strings.h>/<alloca.h>. Silence the pedantic
# warnings the upstream build silences too.
set_source_files_properties(${DOOM_GAME_SRC} PROPERTIES COMPILE_OPTIONS
  "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-missing-braces;-Wno-unused-function;-Wno-maybe-uninitialized;-Wno-implicit-fallthrough;-Wno-format;-Wno-int-conversion;-include;strings.h;-include;alloca.h;-D_DEFAULT_SOURCE"
)

# i_main.c defines main(argc,argv) but does not include <SDL2/SDL.h>, so
# the header's main->SDL_main redirect can't reach it; force it here so
# shim_main.c's main(void) is the Zephyr entry.
set_source_files_properties(${DOOM_SRC_DIR}/i_main.c PROPERTIES
  COMPILE_DEFINITIONS "main=SDL_main")
