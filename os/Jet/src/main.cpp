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
#include <zephyr/drivers/display/bcm2835_fb.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

extern "C" {
#include <rpi_fw.h>
}

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
 *  VC clock probe -- ARM and CORE rates, current vs max
 * ----------------------------------------------------------------- */

/* VideoCore clock IDs for RPI_FW_TAG_GET_*_CLOCK_RATE. Same numbering
 * as Linux drivers/firmware/raspberrypi.c.
 */
#define VC_CLOCK_ID_CORE 4U
#define VC_CLOCK_ID_ARM  3U

static void jet_report_clock(const struct device *fw, const char *name,
                             uint32_t clock_id)
{
	uint32_t cur[2] = {clock_id, 0U};
	uint32_t max[2] = {clock_id, 0U};

	int e1 = rpi_fw_transfer(fw, RPI_FW_TAG_GET_CLOCK_RATE, cur, sizeof(cur));
	int e2 = rpi_fw_transfer(fw, RPI_FW_TAG_GET_MAX_CLOCK_RATE, max, sizeof(max));

	if (e1 != 0 || e2 != 0) {
		printk("[jet] clock %s: query failed (%d/%d)\r\n", name, e1, e2);
		return;
	}
	printk("[jet] clock %s: cur=%u Hz max=%u Hz\r\n", name, cur[1], max[1]);
}

/* Pin a clock to a specific rate via SET_CLOCK_RATE. The third u32 is
 * `skip_setting_turbo`: 0 lets VC raise turbo (i.e. lift the power-cap
 * and bump core voltage if needed) so a rate above the idle floor
 * sticks instead of being throttled back. Returns the achieved rate VC
 * actually committed to -- if voltage / thermal headroom is short, VC
 * may grant less than asked.
 */
static int jet_set_clock_rate(const struct device *fw, uint32_t clock_id,
                              uint32_t rate_hz)
{
	uint32_t req[3] = {clock_id, rate_hz, 0U /* skip_setting_turbo */};
	int err = rpi_fw_transfer(fw, RPI_FW_TAG_SET_CLOCK_RATE, req, sizeof(req));

	if (err != 0) {
		return err;
	}
	/* Response is [clock_id, achieved_rate]. */
	return (int)req[1];
}

static void jet_probe_clocks(void)
{
	const struct device *fw = DEVICE_DT_GET_ONE(raspberrypi_bcm283x_firmware);

	if (!device_is_ready(fw)) {
		printk("[jet] VC firmware not ready -- skipping clock probe\r\n");
		return;
	}
	jet_report_clock(fw, "ARM",  VC_CLOCK_ID_ARM);
	jet_report_clock(fw, "CORE", VC_CLOCK_ID_CORE);

	/* Bump ARM to its rated 1.0 GHz. VC idles the A53 to arm_freq_min
	 * (~600 MHz) under nominal load and our render loop's k_yield-once
	 * pattern apparently doesn't trip its busy heuristic. A direct
	 * SET_CLOCK_RATE with skip_setting_turbo=0 forces the bump and
	 * lifts the turbo gate so the rate persists. If VC grants less
	 * than asked (under-voltage / thermal), the re-report will show
	 * the actual rate.
	 */
	int achieved = jet_set_clock_rate(fw, VC_CLOCK_ID_ARM, 1000000000U);

	if (achieved < 0) {
		printk("[jet] ARM SET_CLOCK_RATE failed: %d\r\n", achieved);
	} else {
		printk("[jet] ARM SET_CLOCK_RATE -> %d Hz\r\n", achieved);
		jet_report_clock(fw, "ARM (post-set)", VC_CLOCK_ID_ARM);
	}
}

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

/* Ball Y-spin rate in degrees per second. Time-based (not per-frame)
 * so the visible spin speed stays constant across fps swings -- the
 * old rotate(0, 3, 0) per frame at 240 fps was 720 dps.
 */
constexpr float kBallSpinDPS = 90.0f;

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
	/* Light from upper-front-right (azimuth=300°, elevation=40°). At
	 * camera position the +Y world axis is "up", -Z is "toward camera".
	 * Azimuth 300° puts the light source on the camera side; elev 40°
	 * gives a noticeable diagonal gradient across the visible
	 * hemisphere of the ball.
	 */
	/* Sun: warm key from upper-front-right (azimuth=300°, elev=40°).
	 * Ambient: cool blue uniform fill. Jet supports only one
	 * DirectionalLight, so a real "fill from bottom-left" would need
	 * a Jet patch (Scene + Renderer.cpp + jetShadeBrightness to sum
	 * two Lambert contributions). For now the cool ambient at least
	 * lifts the shadow hemisphere from black to a dim sky-tinted
	 * dim-grey -- not directional, but the warm/cool contrast
	 * reads as "sky-fill" rather than "no light".
	 */
	DirectionalLight sun{Vector3{300, 40, 0}, Color{255, 245, 220}, 255};
	AmbientLight     ambient{Color{40, 55, 90}};

	/* Ball materials: PHONG with diffuse=255 (full lit term, was 180 =
	 * 30% attenuated) and specular=200 (widens the brightness ceiling
	 * above 255 so the forward-facing pixels can blow out toward white
	 * -- Jet's specular is a static N.z² brightening at the camera-
	 * facing silhouette, not a true Blinn-Phong moving highlight).
	 * Constructor args:
	 *   (color, diffuseMap, shader, emissive, alpha, diffuse, specular).
	 */
	Material red    {kRed,   nullptr, nullptr, false, 255, 255, 200};
	Material white  {kWhite, nullptr, nullptr, false, 255, 255, 200};
	Material magenta{kMagenta};
	Material shadow {kShadow};

	Object *ball       = nullptr;
	Object *floor      = nullptr;
	Object *backwall   = nullptr;
	Object *shadow_obj = nullptr;
};

void build_scene(DemoScene &demo, uint16_t *fb, int w, int h)
{
	/* Ball goes PHONG so the per-pixel diffuse gradient is smooth across
	 * the curvature -- the checker pattern visibly rotates through the
	 * fixed world-space lit/unlit hemisphere as the ball spins.
	 * (Jet's "specular" is a static N.z² brightening on the camera-
	 * facing pole, NOT a moving Blinn-Phong highlight; for a real
	 * sweeping highlight we'd need to patch Renderer.cpp's
	 * jetShadeBrightness.)
	 */
	demo.red.shadingMode     = ShadingMode::PHONG;
	demo.white.shadingMode   = ShadingMode::PHONG;
	demo.magenta.shadingMode = ShadingMode::UNLIT;
	demo.shadow.shadingMode  = ShadingMode::UNLIT;

	demo.scene = new Scene(fb, /* zBuffer = */ nullptr, w, h);
	demo.scene->setBackcolor(kBackcolor);
	demo.scene->setClearBuffer(true);

	/* Camera pulled back to z=-440 so the ball at its bounce peak
	 * (top of ball at world y ~ 300) stays inside the frustum's
	 * vertical extent at FOV 70. FOV preserved so the perspective
	 * doesn't go wide-angle; only the distance changes.
	 */
	demo.camera.setPosition(0, 160, -440);
	demo.camera.lookAt(Vector3{0, 40, 280});
	demo.camera.setFOV(70.0f, w);
	demo.camera.nearPlane = 32;
	demo.camera.farPlane = 8192;
	demo.scene->setCamera(&demo.camera);

	/* Jet quirk: initializeTrigTables() is called inside Scene::Scene()
	 * (TrigLUT.cpp populates sin_table only at that point). Our
	 * DirectionalLight is a value member of DemoScene constructed in
	 * main() before any Scene exists, so its constructor's
	 * calculateLightDirection() runs against an all-zero sin_table and
	 * caches worldLightDir = (0, 0, 0). Re-trigger now that the tables
	 * are populated. Upstream Sample.cpp avoids this by constructing
	 * lights AFTER Scene; our struct-with-value-members order doesn't.
	 */
	demo.sun.updateDirection(demo.sun.direction);

	demo.scene->setDirectionalLight(&demo.sun);
	demo.scene->setAmbientLight(&demo.ambient);


	/* PHONG-lit checker sphere. Per-vertex outward normals interpolate
	 * smoothly across the curvature so the diffuse N.L gradient
	 * sweeps the checker as the ball spins.
	 */
	demo.ball = createCheckeredSphere(/*radius*/140, /*lats*/8, /*lons*/16,
	                                  &demo.red, &demo.white);
	demo.ball->setPosition(0, 0, 280);
	demo.ball->setRotation(0, 0, 23);  /* Amiga axial tilt */
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

	/* Single-shot at boot: ARM + CORE current vs max clock rates.
	 * VC idles the A53 down to its arm_freq_min (~600 MHz on Zero 2 W);
	 * if max > cur the chip can take a SET_CLOCK_RATE bump and Jet
	 * would scale linearly with frequency.
	 */
	jet_probe_clocks();

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

	/* 64-byte alignment: cache-line so DMA cache-maintenance never
	 * sweeps a neighbour, and >= 16-byte so the 128-bit-wide AXI
	 * reads the fb-blit DMA performs land on aligned addresses
	 * (misaligned 128-bit reads wedge the channel with an AXI error).
	 *
	 * Two buffers: one is being rendered into by the CPU while the
	 * other is being DMA-blitted to the VC scanout. Swapping which
	 * is which after each frame turns the prior render+blit serial
	 * sequence into render N+1 || blit N, dropping frame time to
	 * max(render, blit). Costs 2x the backbuffer footprint (~1.75
	 * MiB total at 912x492 RGB565).
	 */
	uint16_t *backbuf[2] = {
		static_cast<uint16_t *>(k_aligned_alloc(64, fb_bytes)),
		static_cast<uint16_t *>(k_aligned_alloc(64, fb_bytes)),
	};

	if (backbuf[0] == nullptr || backbuf[1] == nullptr) {
		printk("[jet] no heap for double backbuffer (need 2 x %zu bytes; "
		       "raise CONFIG_HEAP_MEM_POOL_SIZE)\r\n",
		       fb_bytes);
		return -ENOMEM;
	}

	DemoScene demo;

	/* Scene is bound to backbuf[0] for the first frame; we swap to
	 * backbuf[1] via setFramebuffer() at the start of frame 2 (and
	 * back again every frame after) so the rasteriser writes into
	 * whichever buffer the DMA is NOT currently reading from.
	 */
	build_scene(demo, backbuf[0], width, height);

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

	/* Per-window cycle accumulators for each measured phase. Reset
	 * every time the FPS line prints. We split scene->render() into
	 * its two upstream-exposed sub-phases (prepareFrame = cull/
	 * transform/sort, rasterizeBand = the per-triangle rasteriser
	 * pass), then time the blit kick-off separately. With double-
	 * buffered async DMA the blit time measured here is just the
	 * tail-wait + kick-off cost -- the bulk of the DMA wall-clock
	 * runs in parallel with the next frame's prepareFrame/rasterize.
	 */
	uint64_t cyc_prep = 0, cyc_rast = 0, cyc_blit = 0;

	const uint64_t cycles_per_sec = sys_clock_hw_cycles_per_sec();

	const int64_t t0_ms = k_uptime_get();

	/* cur = which backbuffer the CPU is currently rendering into;
	 * the OTHER one is being DMA'd to the VC framebuffer (or has
	 * just finished, depending on timing). After each frame's
	 * blit kick-off we flip cur so the next frame's prepareFrame/
	 * rasterizeBand writes into the freshly-freed buffer.
	 */
	unsigned int cur = 0;

	printk("[jet] entering render loop (double-buffered async DMA)\r\n");
	for (;;) {
		const float t = (k_uptime_get() - t0_ms) * 0.001f;

		/* Spin around Y on top of the static 23° Z tilt. Time-based
		 * (fps-independent) at kBallSpinDPS deg/sec.
		 */
		demo.ball->setRotation(0, (int32_t)(kBallSpinDPS * t), 23);

		/* Boing arc: |sin| half-sine. */
		const float bounce = std::fabs(std::sin(t * 2.3f)) * 200.0f;
		demo.ball->setPosition(0, (int32_t)(-40.0f + bounce), 280);

		/* Point the rasteriser at the buffer the previous frame's
		 * DMA is NOT touching. The async write of the prior frame
		 * is reading backbuf[cur ^ 1]; we write into backbuf[cur].
		 */
		demo.scene->setFramebuffer(backbuf[cur]);

		/* Phase split: prepareFrame() does the cull / transform /
		 * triangle-sort pass and rasterizeBand(0, h) walks the
		 * resulting sorted queue. Together they produce exactly the
		 * same framebuffer as scene->render() (which is itself just
		 * prepareFrame + rasterizeBand(0, screenHeight) + post-FX,
		 * none of which are enabled in our JetConfig). Splitting
		 * lets us see which half of the render budget is the
		 * rasteriser and which is everything before it.
		 */
		const uint32_t c0 = k_cycle_get_32();
		demo.scene->prepareFrame();
		const uint32_t c1 = k_cycle_get_32();
		demo.scene->rasterizeBand(0, height);
		const uint32_t c2 = k_cycle_get_32();
		/* Async kick: if the previous DMA hasn't completed yet,
		 * this blocks for the remaining tail. Otherwise it just
		 * configures + starts the new DMA and returns. Frame time
		 * becomes max(render, blit) instead of render + blit.
		 */
		int rc = bcm2835_fb_write_async(display, &desc, backbuf[cur]);
		const uint32_t c3 = k_cycle_get_32();

		if (rc < 0) {
			printk("[jet] write_async failed: %d (stopping)\r\n", rc);
			break;
		}

		cur ^= 1U;

		/* k_cycle_get_32 is a free-running counter; modular subtract
		 * yields the elapsed cycles regardless of wraparound.
		 */
		cyc_prep += (uint32_t)(c1 - c0);
		cyc_rast += (uint32_t)(c2 - c1);
		cyc_blit += (uint32_t)(c3 - c2);
		frames_in_window++;

		const int64_t now = k_uptime_get();
		const int64_t elapsed_ms = now - fps_window_start;

		if (elapsed_ms >= 1000) {
			const uint32_t fps_x10 =
				(uint32_t)((int64_t)frames_in_window * 10000 / elapsed_ms);
			/* Cycles -> microseconds via the kernel's hw-cycle
			 * frequency. Divide before accumulating per frame
			 * would lose precision; do it at the print site.
			 */
			const uint32_t us_prep =
				(uint32_t)((cyc_prep * 1000000U) /
				           (cycles_per_sec * frames_in_window));
			const uint32_t us_rast =
				(uint32_t)((cyc_rast * 1000000U) /
				           (cycles_per_sec * frames_in_window));
			const uint32_t us_blit =
				(uint32_t)((cyc_blit * 1000000U) /
				           (cycles_per_sec * frames_in_window));

			/* printk (synchronous, direct to console) rather than
			 * LOG_INF -- the deferred log thread is at low
			 * priority and a tight render loop on a priority-0
			 * main never gives it a slot.
			 */
			printk("[jet] %u.%u fps  prep=%u us  rast=%u us  "
			       "blit=%u us  (%d objs, %d tris, %d rast)\r\n",
			       fps_x10 / 10U, fps_x10 % 10U,
			       us_prep, us_rast, us_blit,
			       demo.scene->lastFrameDrawnObjects,
			       demo.scene->lastFrameDrawnTriangles,
			       demo.scene->lastFrameRasterizedTriangles);
			fps_window_start = now;
			frames_in_window = 0;
			cyc_prep = cyc_rast = cyc_blit = 0;
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
