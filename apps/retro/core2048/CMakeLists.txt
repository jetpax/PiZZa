# SPDX-License-Identifier: Apache-2.0
#
# PiZZa retro -- 2048 core built as a loadable llext.
#
# Mirrors the proven apps/Arduino/sketch pattern: a dummy firmware app
# satisfies Zephyr's build; the product is 2048.llext (relocatable
# multi-TU ELF), loaded at runtime by apps/retro's llext core binder.
# Build against the SAME board as the frontend image (relocations are
# arch-specific).

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza_core2048 LANGUAGES C)

if(NOT DEFINED ENV{LIBRETRO_2048_SRC_DIR})
  message(FATAL_ERROR
    "core2048: set LIBRETRO_2048_SRC_DIR to a libretro-2048 checkout")
endif()
set(CORE_DIR $ENV{LIBRETRO_2048_SRC_DIR})

add_llext_target(core2048
	OUTPUT  ${PROJECT_BINARY_DIR}/2048.llext
	SOURCES
	  ${CORE_DIR}/libretro.c
	  ${CORE_DIR}/game_noncairo.c
	  ${CORE_DIR}/game_shared.c
)

llext_include_directories(core2048
	${CORE_DIR}
	${CORE_DIR}/libretro-common/include
)

llext_compile_options(core2048
	-Wno-unused-variable
	-Wno-unused-parameter
	-Wno-missing-field-initializers
	-Wno-double-promotion
	-Wno-missing-prototypes
	-Wno-unused-but-set-variable
)

target_sources(app PRIVATE src/empty.c)
