# SPDX-License-Identifier: Apache-2.0
#
# PiZZa -- sokoban built as a loadable libretro core (sokoban.llext).
#
# Same game + shim composition as the app build, minus everything the
# frontend owns (present backend, shell, btinput, USB, Zephyr main):
# the sdl2shim-over-libretro glue supplies the retro_* ABI, present
# seam, input synthesis and audio pump; sok_retro_desc.c is the only
# core-specific policy. Build against the SAME board as the frontend,
# then `west build -t sokoban_core` produces sokoban.llext.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza_sokoban_core LANGUAGES C CXX)

set(SOK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SOK_GAME_DIR ${SOK_DIR}/game)

include(${SOK_DIR}/../lib/sdl2shim/sdl2shim.cmake)

# Asset pack, baked into the core's rodata exactly as in the app build.
set(SOK_PACK_BIN ${CMAKE_BINARY_DIR}/sokoban_assets.bin)
file(GLOB_RECURSE SOK_DATA_FILES ${SOK_GAME_DIR}/data/*)
add_custom_command(
  OUTPUT ${SOK_PACK_BIN}
  COMMAND ${Python3_EXECUTABLE} ${SOK_DIR}/tools/pack_assets.py
          ${SOK_GAME_DIR}/data ${SOK_PACK_BIN} --font "Font/ARCADE_N.TTF@8"
  DEPENDS ${SOK_DATA_FILES} ${SOK_DIR}/tools/pack_assets.py
  COMMENT "Packing sokoban assets (PNG -> PIMG, TMX -> S2TM, TTF -> S2SA)"
)
add_custom_target(sok_core_assets DEPENDS ${SOK_PACK_BIN})

set_source_files_properties(${SOK_DIR}/src/sok_pack.S PROPERTIES
  OBJECT_DEPENDS ${SOK_PACK_BIN}
  COMPILE_OPTIONS "-I${CMAKE_BINARY_DIR};-Wa,-I${CMAKE_BINARY_DIR}"
)

add_llext_target(sokoban_core
	OUTPUT  ${PROJECT_BINARY_DIR}/sokoban.llext
	SOURCES
	  ${SOK_GAME_DIR}/src/main.cpp
	  ${SOK_GAME_DIR}/src/App.cpp
	  ${SOK_GAME_DIR}/src/Game.cpp
	  ${SOK_GAME_DIR}/src/MapRender.cpp
	  ${SOK_GAME_DIR}/src/Renderer.cpp
	  ${SOK_GAME_DIR}/src/TileMap.cpp
	  ${SOK_GAME_DIR}/src/hp_assert.cpp
	  ${SOK_DIR}/src/shim_tmx.c
	  ${SOK_DIR}/src/sok_assets.c
	  ${SOK_DIR}/src/sok_pack.S
	  ${SDL2SHIM_CORE_SOURCES}
	  ${SDL2SHIM_SURFACE_SOURCES}
	  ${SDL2SHIM_RW_SOURCES}
	  ${SDL2SHIM_RENDER_SOURCES}
	  ${SDL2SHIM_TTF_ATLAS_SOURCES}
	  ${SDL2SHIM_IMAGE_PIMG_SOURCES}
	  ${SDL2SHIM_LIBRETRO_SOURCES}
	  ${SDL2SHIM_LIBRETRO_CXXRT}
	  src/sok_retro_desc.c
	  src/sok_core_stdio.c
)
add_dependencies(sokoban_core sok_core_assets)

llext_include_directories(sokoban_core
	${SDL2SHIM_INCLUDE_DIRS}
	${SDL2SHIM_DIR}/include/SDL2
	${SOK_DIR}/include
	${SOK_DIR}/src
)

# C++ TUs emit COMDAT groups (inline/template methods); ld -r keeps
# them as SHT_GROUP + AXG sections, which the llext loader rejects
# (-ENOTSUP). --force-group-allocation resolves the groups into plain
# sections at the partial link, exactly as a final link would.
llext_link_options(sokoban_core
	-Wl,--force-group-allocation
)

llext_compile_options(sokoban_core
	-D__PICOLIBC_ERRNO_FUNCTION=s2s_core_errno_ptr
	-Wno-unused-variable
	-Wno-unused-but-set-variable
	-Wno-unused-parameter
	-Wno-missing-field-initializers
	-Wno-format-truncation
	-Wno-double-promotion
	-Wno-missing-prototypes
)

target_sources(app PRIVATE src/empty.c)
