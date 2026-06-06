cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza_sketch LANGUAGES C)

# Produce sketch.llext via the standard Zephyr llext build.
add_llext_target(sketch
	OUTPUT  ${PROJECT_BINARY_DIR}/sketch.llext
	SOURCES src/sketch.c
)

# A dummy firmware app to satisfy Zephyr's app target -- we discard
# zephyr.bin and only ship sketch.llext to the SD card.
target_sources(app PRIVATE src/empty.c)
