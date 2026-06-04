/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * PiZZa Jet demo on rpi_zero_2w.
 *
 * Boots, brings up USB CDC (so the shell is available for inspection)
 * and the VideoCore HDMI framebuffer at RGB565, then renders a
 * spinning lit scene with Jet straight into a heap RGB565 backbuffer.
 * One full-frame display_write() per frame copies the backbuffer into
 * the VideoCore-scanout buffer.
 *
 * Why a backbuffer at all: VC's scanout buffer is mapped
 * K_MEM_CACHE_NONE so CPU writes land in DRAM without separate cache
 * maintenance, but Jet's rasteriser does many small scattered writes
 * per triangle -- uncached small writes are pathologically slow. The
 * backbuffer is cached normal memory; one big memcpy at end-of-frame
 * is bandwidth-bound but cache-friendly.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT)
#include <zephyr/usb/usbd.h>
/* sample_usbd.h is a sample-private header that lacks extern "C"
 * guards; force C linkage from C++ so the linker resolves to the .c
 * definitions pulled in via common.cmake.
 */
extern "C" {
#include <sample_usbd.h>
}
#endif

/* Include the Jet pieces we use directly rather than pulling in
 * <Jet.hpp> -- the umbrella header drags in ObjLoader.h which uses
 * strdup(), and Zephyr's picolibc headers don't expose strdup by
 * default. We don't need OBJ loading in this demo, so this avoids
 * the unsatisfied symbol entirely.
 */
#include <Camera.hpp>
#include <Light.hpp>
#include <Material.hpp>
#include <Object.hpp>
#include <Primitives.hpp>
#include <Scene.hpp>
#include <TrigLUT.hpp>

#include <cmath>

LOG_MODULE_REGISTER(pizza_jet, LOG_LEVEL_INF);

using namespace Renderer;

/* ----------------------------------------------------------------- *
 *  USB CDC bring-up (boards with the USB device stack)
 * ----------------------------------------------------------------- */
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT)
static int jet_usb_start(void)
{
	struct usbd_context *usbd = sample_usbd_init_device(NULL);

	if (usbd == NULL) {
		printk("[jet] sample_usbd_init_device failed\n");
		return -ENODEV;
	}
	if (!usbd_can_detect_vbus(usbd)) {
		int err = usbd_enable(usbd);

		if (err) {
			printk("[jet] usbd_enable failed: %d\n", err);
			return err;
		}
	}
	return 0;
}
#else
static inline int jet_usb_start(void) { return 0; }
#endif

/* ----------------------------------------------------------------- *
 *  Demo scene
 * ----------------------------------------------------------------- */

namespace {

/* RGB565 packing helper. */
#define RGB565(r5, g6, b5) (uint16_t)(((r5) << 11) | ((g6) << 5) | (b5))

/* Boing-ball palette: light grey backdrop, magenta grid, classic
 * red + white checker, near-black shadow disc.
 */
constexpr uint16_t kBackcolor   = RGB565(24, 50, 24);  /* light warm grey  */
constexpr uint16_t kRed         = RGB565(31,  0,  0);  /* pure red         */
constexpr uint16_t kWhite       = RGB565(31, 63, 31);  /* pure white       */
constexpr uint16_t kMagenta     = RGB565(31,  0, 25);  /* Amiga magenta    */
constexpr uint16_t kShadow      = RGB565( 8, 12,  8);  /* dark grey disc   */

/* Build a per-face red/white checkered sphere. Materials are UNLIT
 * so the colours render exactly as authored -- this matches the
 * Amiga original, which used flat colour-cycled palette entries
 * rather than shaded lighting. lats × lons quads, 2 tris each.
 */
Object *createCheckeredSphere(int32_t radius, int lats, int lons,
                              Material *matA, Material *matB)
{
	using namespace Renderer;
	Object *s = new Object();

	/* Proportional integer math so the last lat actually hits 180°
	 * (south pole) and the last lon actually hits 360° (= 0° after
	 * mod ANGLE_MAX). Using a pre-computed integer step would
	 * truncate -- e.g. 360/16 = 22, leaving an 8° wedge at the
	 * seam -- and that's exactly the missing slice we saw.
	 *
	 * (lats+1) × (lons+1) vertex grid; the j=lons seam column
	 * deliberately duplicates the j=0 column so the face loop
	 * doesn't need wrap-around indexing.
	 */
	for (int i = 0; i <= lats; ++i) {
		const int32_t latDeg = (i * 180) / lats;
		const int32_t sinLat = lookupSinI(latDeg);
		const int32_t cosLat = lookupCosI(latDeg);
		for (int j = 0; j <= lons; ++j) {
			const int32_t lonDeg = ((j * ANGLE_MAX) / lons) % ANGLE_MAX;
			const int32_t sinLon = lookupSinI(lonDeg);
			const int32_t cosLon = lookupCosI(lonDeg);
			Vector3 p = {
				(int32_t)((int64_t)radius * sinLat * cosLon /
				          ((int64_t)FIXED_POINT_SCALE * FIXED_POINT_SCALE)),
				(int32_t)((int64_t)radius * cosLat / FIXED_POINT_SCALE),
				(int32_t)((int64_t)radius * sinLat * sinLon /
				          ((int64_t)FIXED_POINT_SCALE * FIXED_POINT_SCALE)),
			};
			Vector3 n = {
				p.x * FIXED_POINT_SCALE / radius,
				p.y * FIXED_POINT_SCALE / radius,
				p.z * FIXED_POINT_SCALE / radius,
			};
			s->addVertex({p, {0, 0}, n});
		}
	}

	const int rowStride = lons + 1;
	for (int i = 0; i < lats; ++i) {
		for (int j = 0; j < lons; ++j) {
			Material *m = ((i + j) & 1) ? matA : matB;
			const uint16_t v0 = (uint16_t)(i * rowStride + j);
			const uint16_t v1 = (uint16_t)(v0 + 1);
			const uint16_t v2 = (uint16_t)(v1 + rowStride);
			const uint16_t v3 = (uint16_t)(v0 + rowStride);
			s->addFace(v0, v1, v2, v3, m);
		}
	}
	s->calculateBoundingBox();
	return s;
}

/* Build a wireframe-style grid floor out of thin filled quads (one
 * per visible grid line). Real wireframe shading would draw triangle
 * diagonals too; explicit line-quads keep the look orthogonal like
 * the Amiga original.
 */
Object *createGridFloor(int32_t width, int32_t depth, int cellsX, int cellsZ,
                        int32_t thickness, Material *material)
{
	using namespace Renderer;
	Object *g = new Object();
	const int32_t hw = width / 2;
	const int32_t hd = depth / 2;
	const int32_t halfT = thickness / 2;
	const Vector3 up = {0, FIXED_POINT_SCALE, 0};

	/* Lines parallel to X (varying Z). */
	for (int i = 0; i <= cellsZ; ++i) {
		const int32_t z = -hd + (int32_t)((int64_t)i * depth / cellsZ);
		const uint16_t b = (uint16_t)g->vertices.size();
		g->addVertex({{-hw, 0, z - halfT}, {0, 0}, up});
		g->addVertex({{ hw, 0, z - halfT}, {0, 0}, up});
		g->addVertex({{ hw, 0, z + halfT}, {0, 0}, up});
		g->addVertex({{-hw, 0, z + halfT}, {0, 0}, up});
		g->addFace(b, (uint16_t)(b + 1), (uint16_t)(b + 2), (uint16_t)(b + 3), material);
	}
	/* Lines parallel to Z (varying X). */
	for (int i = 0; i <= cellsX; ++i) {
		const int32_t x = -hw + (int32_t)((int64_t)i * width / cellsX);
		const uint16_t b = (uint16_t)g->vertices.size();
		g->addVertex({{x - halfT, 0, -hd}, {0, 0}, up});
		g->addVertex({{x + halfT, 0, -hd}, {0, 0}, up});
		g->addVertex({{x + halfT, 0,  hd}, {0, 0}, up});
		g->addVertex({{x - halfT, 0,  hd}, {0, 0}, up});
		g->addFace(b, (uint16_t)(b + 1), (uint16_t)(b + 2), (uint16_t)(b + 3), material);
	}
	g->calculateBoundingBox();
	return g;
}

/* Flat horizontal N-gon disc -- the ball's shadow. */
Object *createDisc(int32_t radiusX, int32_t radiusZ, int segments, Material *material)
{
	using namespace Renderer;
	Object *d = new Object();
	const Vector3 up = {0, FIXED_POINT_SCALE, 0};

	d->addVertex({{0, 0, 0}, {0, 0}, up});                      /* centre */
	const int32_t step = ANGLE_MAX / segments;
	for (int i = 0; i < segments; ++i) {
		const int32_t a = (i * step) % ANGLE_MAX;
		const int32_t cx = lookupCosI(a);
		const int32_t sz = lookupSinI(a);
		d->addVertex({{(int32_t)((int64_t)radiusX * cx / FIXED_POINT_SCALE),
		               0,
		               (int32_t)((int64_t)radiusZ * sz / FIXED_POINT_SCALE)},
		              {0, 0}, up});
	}
	for (int i = 0; i < segments; ++i) {
		const uint16_t a = (uint16_t)(1 + i);
		const uint16_t b = (uint16_t)(1 + ((i + 1) % segments));
		d->addTriangle(0, a, b, material);
	}
	d->calculateBoundingBox();
	return d;
}

struct DemoScene {
	Scene *scene = nullptr;
	Camera camera{};
	/* Lighting is set up but the Boing materials are UNLIT, so the
	 * sun / ambient affect only the floor and shadow disc.
	 */
	DirectionalLight sun{Vector3{50, 40, 0}, Color{255, 240, 210}, 200};
	AmbientLight     ambient{Color{60, 70, 90}};

	Material red    {kRed};
	Material white  {kWhite};
	Material magenta{kMagenta};
	Material shadow {kShadow};

	Object *ball       = nullptr;
	Object *floor      = nullptr;
	Object *backwall   = nullptr;
	Object *shadow_obj = nullptr;
};

void build_scene(DemoScene &demo, uint16_t *fb, int w, int h)
{
	/* Boing materials are flat-coloured panels -- no shading, exactly
	 * matching the original colour-cycled palette look. The floor and
	 * shadow ride the same UNLIT path so their colours stay pure
	 * (and we don't have to fight Jet's per-face lighting headroom).
	 */
	demo.red.shadingMode     = ShadingMode::UNLIT;
	demo.white.shadingMode   = ShadingMode::UNLIT;
	demo.magenta.shadingMode = ShadingMode::UNLIT;
	demo.shadow.shadingMode  = ShadingMode::UNLIT;

	demo.scene = new Scene(fb, /* zBuffer = */ nullptr, w, h);
	demo.scene->setBackcolor(kBackcolor);
	demo.scene->setClearBuffer(true);

	/* Lower the camera height and aim slightly above the ball's
	 * resting altitude. Steep top-down angle made floor cells read
	 * as 3:1 rectangles; ~7° pitch keeps the grid visible without
	 * crushing the depth axis.
	 */
	demo.camera.setPosition(0, 120, -360);
	demo.camera.lookAt(Vector3{0, 60, 360});
	demo.camera.setFOV(60.0f, w);
	demo.camera.nearPlane = 32;
	demo.camera.farPlane = 8192;
	demo.scene->setCamera(&demo.camera);

	demo.scene->setDirectionalLight(&demo.sun);
	demo.scene->setAmbientLight(&demo.ambient);

	/* Order matters because Z_BUFFERING is off and SORT_TRIANGLES
	 * is on: paint the floor first, then the shadow disc just
	 * above it, then the ball on top.
	 */
	/* Floor: horizontal grid in XZ plane. Depth truncated so the far
	 * edge lands exactly on the back wall's z (1200); without this
	 * the floor's far cells get painted on top of the wall's lower
	 * row (Z_BUFFERING is off; painter's algorithm by triangle Z).
	 */
	demo.floor = createGridFloor(/*width*/2400, /*depth*/1600,
	                             /*cellsX*/12, /*cellsZ*/8,
	                             /*thickness*/3, &demo.magenta);
	demo.floor->setPosition(0, -180, 400);   /* far edge at z=1200 */
	demo.floor->cullingMode = CullingMode::NO_CULLING;
	demo.scene->addObject(demo.floor);

	/* Back wall: same grid helper, rotated 90° around X so it
	 * stands up. Its local "depth" axis becomes vertical, so we
	 * spec it shorter than the floor (the wall doesn't need to
	 * extend below floor level).
	 */
	demo.backwall = createGridFloor(/*width*/2400, /*depth*/1200,
	                                /*cellsX*/12, /*cellsZ*/6,
	                                /*thickness*/3, &demo.magenta);
	demo.backwall->setRotation(90, 0, 0);
	/* Wall plane stands at z=1200. Y centred so the wall's bottom
	 * meets the floor (y=-180): center = -180 + depth/2 = 420.
	 */
	demo.backwall->setPosition(0, 420, 1200);
	demo.backwall->cullingMode = CullingMode::NO_CULLING;
	demo.scene->addObject(demo.backwall);

	demo.shadow_obj = createDisc(/*rx*/180, /*rz*/55, /*segs*/32, &demo.shadow);
	demo.shadow_obj->setPosition(0, -178, 280);  /* 2 above floor */
	demo.shadow_obj->cullingMode = CullingMode::NO_CULLING;
	demo.scene->addObject(demo.shadow_obj);

	demo.ball = createCheckeredSphere(/*radius*/140, /*lats*/8, /*lons*/16,
	                                  &demo.red, &demo.white);
	demo.ball->setPosition(0, 0, 280);
	/* Z-tilt of 23° = the Amiga axial tilt. Spin around Y on top
	 * of that in the render loop.
	 */
	demo.ball->setRotation(0, 0, 23);
	demo.scene->addObject(demo.ball);
}

} /* namespace */

/* ----------------------------------------------------------------- *
 *  main
 * ----------------------------------------------------------------- */

int main(void)
{
	printk("\r\n*** PiZZa Jet demo on rpi_zero_2w ***\r\n");

	/* Bring up USB CDC (shell endpoint) -- no-op without USB. */
	(void)jet_usb_start();

	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display)) {
		printk("[jet] display device not ready\r\n");
		return -ENODEV;
	}

	struct display_capabilities caps;

	display_get_capabilities(display, &caps);
	if (caps.current_pixel_format != PIXEL_FORMAT_RGB_565) {
		printk("[jet] display is %u bpp; Jet wants RGB565. Check the "
		       "overlay pixel-format property.\r\n",
		       caps.current_pixel_format);
		return -ENOTSUP;
	}

	const int width = caps.x_resolution;
	const int height = caps.y_resolution;
	const size_t fb_pixels = (size_t)width * (size_t)height;
	const size_t fb_bytes = fb_pixels * sizeof(uint16_t);

	printk("[jet] display %dx%d RGB565 (backbuffer %zu KiB)\r\n", width,
	       height, fb_bytes / 1024U);

	uint16_t *backbuf = static_cast<uint16_t *>(k_aligned_alloc(8, fb_bytes));

	if (backbuf == nullptr) {
		printk("[jet] no heap for backbuffer (need %zu bytes; "
		       "raise CONFIG_HEAP_MEM_POOL_SIZE)\r\n",
		       fb_bytes);
		return -ENOMEM;
	}

	DemoScene demo;

	build_scene(demo, backbuf, width, height);

	/* Display descriptor reused every frame -- the backbuffer's pitch
	 * equals its width.
	 */
	struct display_buffer_descriptor desc = {
		.buf_size = (uint32_t)fb_bytes,
		.width = (uint16_t)width,
		.height = (uint16_t)height,
		.pitch = (uint16_t)width,
		.frame_incomplete = false,
	};

	int64_t fps_window_start = k_uptime_get();
	uint32_t frames_in_window = 0;

	const int64_t t0_ms = k_uptime_get();

	printk("[jet] entering render loop\r\n");
	for (;;) {
		const float t = (k_uptime_get() - t0_ms) * 0.001f;

		/* Spin around Y on top of the static 23° Z tilt. */
		demo.ball->rotate(0, 3, 0);

		/* Boing arc: |sin| half-sine. Floor sits at y=-180 and
		 * ball radius is 140, so y=-40 plants the ball exactly
		 * on the grid. Peak rises ~180 above that.
		 */
		const float bounce = std::fabs(std::sin(t * 2.3f)) * 200.0f;
		demo.ball->setPosition(0, (int32_t)(-40.0f + bounce), 280);

		/* Squash the shadow horizontally as the ball rises. */
		const float h_factor = 1.0f - 0.55f * (bounce / 200.0f);
		demo.shadow_obj->scale = {(int32_t)(FIXED_POINT_SCALE * h_factor),
		                          FIXED_POINT_SCALE,
		                          (int32_t)(FIXED_POINT_SCALE * h_factor)};
		demo.shadow_obj->transformScale = true;

		demo.scene->render();

		int rc = display_write(display, 0, 0, &desc, backbuf);

		if (rc < 0) {
			printk("[jet] display_write failed: %d (stopping)\r\n", rc);
			break;
		}

		frames_in_window++;

		const int64_t now = k_uptime_get();
		const int64_t elapsed_ms = now - fps_window_start;

		if (elapsed_ms >= 1000) {
			const uint32_t fps_x10 =
				(uint32_t)((int64_t)frames_in_window * 10000 / elapsed_ms);

			/* printk (synchronous, direct to console) rather than
			 * LOG_INF -- the deferred log thread is at low
			 * priority and a tight render loop on a priority-0
			 * main never gives it a slot.
			 */
			printk("[jet] %u.%u fps  (%d objs, %d tris drawn, "
			       "%d rasterized)\r\n",
			       fps_x10 / 10U, fps_x10 % 10U,
			       demo.scene->lastFrameDrawnObjects,
			       demo.scene->lastFrameDrawnTriangles,
			       demo.scene->lastFrameRasterizedTriangles);
			fps_window_start = now;
			frames_in_window = 0;
		}

		/* Yield once per frame so lower-priority threads (log
		 * processing, USB CDC service work, system housekeeping)
		 * get a slot. The render thread re-runs immediately if
		 * nothing else is runnable.
		 */
		k_yield();
	}

	return 0;
}
