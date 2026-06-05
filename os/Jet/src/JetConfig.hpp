// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Jet engine configuration for the PiZZa rpi_zero_2w demo. Derived
// from upstream Config.example.hpp. ESP-specific machinery
// (esp_attr.h / IRAM_ATTR) is gated by ESP_PLATFORM in FastMath.hpp,
// which is not defined on AArch64 -- nothing to override here.

#ifndef JETCONFIG_HPP_PIZZA_JET_DEMO
#define JETCONFIG_HPP_PIZZA_JET_DEMO

#define RENDER_TILE_BUFFER 0
#define TILE_WIDTH 32
#define TILE_HEIGHT 32

#define FAST_Z 1
#define LAZY_Z 0

#define SCREEN_DOOR_ALPHA 1
#define SKIP_ZERO_AREA_TRIANGLES 1
#define NOISE_ALPHA 0

// Painter's-algorithm sort is the right pick for a low-triangle-count
// scene with no overdraw budget to spare.
#define Z_BUFFERING 0
#define SORT_TRIANGLES 1
#define SORT_SCENE_OBJECTS 0
#define SORT_SCENE_REVERSE 0

#define DEPTH_ALPHA_BLEND 0

#define TEXTURE_MAPPING 0
#define PERSPECTIVE_CORRECT_TEXTURES 0
#define BILINEAR_FILTER 0

#define LIGHTING 1
#define Z_BRIGHTNESS 0

#define FLOAT_CAMERA_ANGLES 1
#define FLOAT_SIN_CACHE_SCALE 10
#define FLOAT_TAN_CACHE_SCALE 1

#define POSTFX_CRT 0
#define POSTFX_CELLSHADING 0
#define POSTFX_ANTIALIASING 0
#define POSTFX_BLOOM 0
#define POSTFX_MOTION_BLUR 0
#define POSTFX_CHROMATIC 0
#define POSTFX_PIXELATE 0

#define DEBUG_OVERDRAW 0

#define CRT_SCANLINE_INTENSITY 48
#define MOTION_BLUR_STRENGTH 50
#define CHROMATIC_OFFSET 2
#define PIXELATE_SIZE 4
#define CELLSHADING_CELL_BITS 4

#define zBrightFar  (1600 * 8)
#define zBrightNear (200  * 8)
#define zBrightScale 48

#define depthFogFar  (8192 * 8)
#define depthFogNear (6144 * 8)

// Full progressive frames -- HDMI scanout on the Pi is plenty.
// HALF_WIDTH_BUFFERS tried 2026-06-05: ~2.5x rast speedup but VC's
// HVS preserves buffer aspect ratio on scaling, so a 448x492 buffer
// pillarboxes to ~895x984 inside the 1824x984 monitor. Reverted to
// 912 wide for full-screen 16:9 output. To re-enable: flip this back
// to 1, set overlay render-width to a 32-aligned half (~448), and
// accept the pillarbox (or move to FIELD_BUFFERS=1 for half-area).
#define HALF_WIDTH_BUFFERS 0
#define FIELD_BUFFERS 0

#define CHECKERBOARD_MODE 0
#define CHECKERBOARD_RECONSTRUCTION 0

#define MAX_PICK_QUERIES 0

#endif /* JETCONFIG_HPP_PIZZA_JET_DEMO */
