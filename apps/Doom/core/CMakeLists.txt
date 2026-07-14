# SPDX-License-Identifier: Apache-2.0
#
# PiZZa -- sdl2-doom built as a loadable libretro content core (doom.llext).
#
# Same game + shim composition as the app build (apps/Doom), minus
# everything the frontend owns (present backend, shell, USB, baked IWAD,
# Zephyr main) and plus the sdl2shim-over-libretro glue, which supplies
# the retro_* ABI, the present seam, input synthesis and the audio pump.
# The WAD is NOT baked: this is a need_fullpath content core that reads
# the launcher-picked file off the filesystem (doom_core_fs.c). First
# light builds no OPL music (see prj.conf). Build against the SAME board
# as the frontend, then `west build -t doom_core` produces doom.llext.
#
# sdl2-doom is GPL-2.0; this binary is GPL-2.0 by linking it. The shim +
# port glue are Apache-2.0.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza_doom_core LANGUAGES C ASM)

set(DOOM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ── Doom game tree (untouched checkout) ─────────────────────────────
if(NOT DEFINED DOOM_SRC_DIR)
  set(DOOM_SRC_DIR "$ENV{HOME}/github/sdl2-doom/src")
endif()
if(NOT EXISTS "${DOOM_SRC_DIR}/d_main.c")
  message(FATAL_ERROR
    "sdl2-doom sources not found at ${DOOM_SRC_DIR}. Clone "
    "https://github.com/AlexOberhofer/sdl2-doom and keep it at "
    "~/github/sdl2-doom or pass -DDOOM_SRC_DIR=<path>.")
endif()

# The fork's build list minus i_cdmus.c (SDL 1.2 CD API) and i_video.c
# (fed in rewritten below). OPL off -> the original i_sound.c stays.
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

# i_video.c rewritten to native 320x200 (SDL_RESX/Y *3 -> plain; the HVS
# upscales). Keeps the checkout pristine -- the whole delta is this sed.
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

# ── OPL music (CONFIG_DOOM_MUSIC_OPL) ───────────────────────────────
# Mirror the app build (apps/Doom/CMakeLists.txt). The fork hardcodes
# music_sdl_module (stubbed silent in shim_mixer.c); swap it to
# music_opl_module via a pristine-checkout sed (same discipline as the
# i_video rewrite), and pull Chocolate Doom's OPL synth + opl_glue.c into
# the llext. The synth folds into the shared mixer as a MIX_CHANNEL_POST
# effect -- the seam SFX already proves. The MUS->MIDI temp round-trip is
# RAM-backed in doom_core_fs.c (the core's fs is the real SD card).
set(DOOM_OPL_SRC "")
set(DOOM_OPL_GLUE "")
set(DOOM_OPL_INC "")
if(CONFIG_DOOM_MUSIC_OPL)
  list(REMOVE_ITEM DOOM_GAME_SRC ${DOOM_SRC_DIR}/i_sound.c)
  set(DOOM_ISOUND_GEN ${CMAKE_BINARY_DIR}/gen/i_sound_opl.c)
  add_custom_command(
    OUTPUT ${DOOM_ISOUND_GEN}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
    COMMAND ${CMAKE_COMMAND} -E copy ${DOOM_SRC_DIR}/i_sound.c ${DOOM_ISOUND_GEN}
    COMMAND sed -i.bak
            -e "s|music_module = &music_sdl_module|music_module = \\&music_opl_module|"
            ${DOOM_ISOUND_GEN}
    DEPENDS ${DOOM_SRC_DIR}/i_sound.c
    COMMENT "Selecting OPL music module (music_sdl -> music_opl)"
    VERBATIM
  )
  list(APPEND DOOM_GAME_SRC ${DOOM_ISOUND_GEN})

  if(NOT DEFINED CHOCOLATE_SRC_DIR)
    set(CHOCOLATE_SRC_DIR "$ENV{HOME}/github/chocolate-doom")
  endif()
  if(NOT EXISTS "${CHOCOLATE_SRC_DIR}/opl/opl3.c")
    message(FATAL_ERROR
      "OPL music needs chocolate-doom at ${CHOCOLATE_SRC_DIR} "
      "(opl/ + src/i_oplmusic.c). Pass -DCHOCOLATE_SRC_DIR=<path>.")
  endif()
  set(DOOM_OPL_SRC
    ${CHOCOLATE_SRC_DIR}/opl/opl.c
    ${CHOCOLATE_SRC_DIR}/opl/opl3.c
    ${CHOCOLATE_SRC_DIR}/opl/opl_queue.c
    ${CHOCOLATE_SRC_DIR}/opl/opl_sdl.c
    ${CHOCOLATE_SRC_DIR}/src/midifile.c
    ${CHOCOLATE_SRC_DIR}/src/i_oplmusic.c
  )
  set(DOOM_OPL_GLUE ${DOOM_DIR}/src/opl_glue.c)
  set(DOOM_OPL_INC ${CHOCOLATE_SRC_DIR}/opl ${CHOCOLATE_SRC_DIR}/src)
endif()

# ── shim ────────────────────────────────────────────────────────────
include(${DOOM_DIR}/../lib/sdl2shim/sdl2shim.cmake)

add_llext_target(doom_core
	OUTPUT  ${PROJECT_BINARY_DIR}/doom.llext
	SOURCES
	  ${DOOM_GAME_SRC}
	  ${DOOM_DIR}/src/shim_video.c
	  ${DOOM_DIR}/src/shim_mixer.c
	  src/doom_core_fs.c
	  src/doom_retro_desc.c
	  ${SDL2SHIM_CORE_SOURCES}
	  ${SDL2SHIM_LIBRETRO_SOURCES}
	  ${DOOM_OPL_SRC}
	  ${DOOM_OPL_GLUE}
)

llext_include_directories(doom_core
	${SDL2SHIM_INCLUDE_DIRS}
	${SDL2SHIM_DIR}/include/SDL2
	${DOOM_DIR}/src
	${DOOM_SRC_DIR}/../include
	${DOOM_SRC_DIR}
	${DOOM_OPL_INC}
)

llext_compile_options(doom_core
	-D__PICOLIBC_ERRNO_FUNCTION=s2s_core_errno_ptr
	-Wno-unused-variable
	-Wno-unused-but-set-variable
	-Wno-unused-parameter
	-Wno-unused-function
	-Wno-missing-braces
	-Wno-missing-field-initializers
	-Wno-missing-prototypes
	-Wno-double-promotion
	-Wno-format-truncation
)

# Game TUs assume glibc (strcasecmp/alloca via string.h/stdlib.h);
# picolibc keeps them in <strings.h>/<alloca.h>. FEATURE_SOUND gates the
# SFX module table (set from the makefile in the upstream build, not
# config.h). Same set as the app build.
set_source_files_properties(${DOOM_GAME_SRC} PROPERTIES COMPILE_OPTIONS
  "-Wno-maybe-uninitialized;-Wno-implicit-fallthrough;-Wno-format;-Wno-int-conversion;-include;strings.h;-include;alloca.h;-D_DEFAULT_SOURCE;-DFEATURE_SOUND"
)

# i_main.c defines main(argc,argv) without including <SDL2/SDL.h>, so the
# header's main->SDL_main redirect can't reach it; force it so the glue's
# SDL_main entry is this TU's main.
set_source_files_properties(${DOOM_SRC_DIR}/i_main.c PROPERTIES
  COMPILE_DEFINITIONS "main=SDL_main")

# Chocolate OPL TUs need the glibc-ism relief the game TUs get. opl_glue.c
# is ours (Zephyr-native) and rides the core-wide llext options instead.
if(CONFIG_DOOM_MUSIC_OPL)
  set(OPL_FLAGS "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function;-Wno-missing-braces;-Wno-implicit-fallthrough;-Wno-format;-Wno-maybe-uninitialized;-Wno-int-conversion;-include;strings.h;-D_DEFAULT_SOURCE")
  set_source_files_properties(${DOOM_OPL_SRC} PROPERTIES COMPILE_OPTIONS "${OPL_FLAGS}")
endif()

target_sources(app PRIVATE src/empty.c)
