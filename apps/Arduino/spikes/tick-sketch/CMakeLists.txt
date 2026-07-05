# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza_tick_sketch LANGUAGES C)

add_llext_target(sketch
	OUTPUT  ${PROJECT_BINARY_DIR}/sketch.llext
	SOURCES src/tick.c
)
target_sources(app PRIVATE src/empty.c)
