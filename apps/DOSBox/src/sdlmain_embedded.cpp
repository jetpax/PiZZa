/* PiZZa embedded platform layer for DOSBox-X — replaces src/gui/sdlmain.cpp.
 *
 * P2b: real startup (config sections + the upstream init cascade minus
 * dropped subsystems), real GFX_* dispatching to the kept
 * output_surface.cpp, an event pump into the mapper, and the
 * DOSBOX_RunMachine try/catch loop. Contract per notes/P2_SCOPE.md §3;
 * the cascade is transcribed from upstream sdlmain.cpp main()
 * (~9638-9990 at 56bf51e21).
 */

/* keep our main() as the Zephyr entry (shim SDL.h remaps main otherwise) */
#define SDL_MAIN_HANDLED

#include "config.h"
#include "dosbox.h"
#include "logging.h"
#include "video.h"
#include "sdlmain.h"
#include "vga.h"
#include "glidedef.h"
#include "control.h"
#include "setup.h"
#include "programs.h"
#include "mapper.h"
#include "menu.h"
#include "mem.h"
#include "mouse.h"
#include "output/output_surface.h"

#include <vector>

#include <map>
#include <string>
#include <iostream>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

extern "C" void dbx_window_resize(int w, int h);
extern "C" void dbx_bump_arm_clock(void);
#ifdef CONFIG_DOSBOX_SCRIPTED_INPUT
extern "C" void dbx_scripted_at_prompt(void);
extern "C" void dbx_scripted_pump(void);
#endif

/* ── the sdl global every output/gui TU pokes ───────────────────── */
SDL_Block sdl;

/* ── color contract (vga.h externs) ─────────────────────────────── */
uint32_t GFX_Rmask, GFX_Gmask, GFX_Bmask, GFX_Amask;
unsigned char GFX_Rshift, GFX_Gshift, GFX_Bshift, GFX_Ashift;
unsigned char GFX_bpp;

/* ── window management ──────────────────────────────────────────── */
SDL_Window *GFX_SetSDLWindowMode(uint16_t width, uint16_t height, SCREEN_TYPES screenType)
{
	(void)screenType;
	if (sdl.window == NULL) {
		sdl.window = SDL_CreateWindow("DOSBox-X", 0, 0, width, height,
					      SDL_WINDOW_SHOWN);
	}
	dbx_window_resize(width, height);
	return sdl.window;
}

void GFX_DrawSDLMenu(DOSBoxMenu &menu, DOSBoxMenu::displaylist &dl)
{
	(void)menu; (void)dl;
}

/* ── GFX contract, surface path only ────────────────────────────── */
static void gfx_update_color_contract(void)
{
	SDL_Surface *s = sdl.surface;

	if (s == NULL || s->format == NULL) {
		return;
	}
	GFX_Rmask = s->format->Rmask; GFX_Rshift = s->format->Rshift;
	GFX_Gmask = s->format->Gmask; GFX_Gshift = s->format->Gshift;
	GFX_Bmask = s->format->Bmask; GFX_Bshift = s->format->Bshift;
	GFX_Amask = s->format->Amask; GFX_Ashift = s->format->Ashift;
	GFX_bpp = s->format->BitsPerPixel;
}

void GFX_Start(void)
{
	sdl.active = true;
}

void GFX_Stop(void)
{
	sdl.active = false;
}

Bitu GFX_GetBestMode(Bitu flags)
{
	return OUTPUT_SURFACE_GetBestMode(flags);
}

Bitu GFX_SetSize(Bitu width, Bitu height, Bitu flags,
		 double scalex, double scaley, GFX_CallBack_t callback)
{
	if (width == 0 || height == 0) {
		E_Exit("GFX_SetSize with zero dimensions");
		return 0;
	}
	if (sdl.updating) {
		GFX_EndUpdate(nullptr);
	}
	sdl.must_redraw_all = true;
	sdl.draw.width = (uint32_t)width;
	sdl.draw.height = (uint32_t)height;
	sdl.draw.flags = flags;
	sdl.draw.callback = callback;
	sdl.draw.scalex = scalex;
	sdl.draw.scaley = scaley;

	Bitu retFlags = OUTPUT_SURFACE_SetSize();

	sdl.desktop.type = sdl.desktop.want_type;
	gfx_update_color_contract();
	if (retFlags) {
		GFX_Start();
	}
	printk("[dbx] GFX_SetSize %ux%u flags=0x%x -> 0x%x\n",
	       (unsigned)width, (unsigned)height, (unsigned)flags,
	       (unsigned)retFlags);
	return retFlags;
}

bool GFX_StartUpdate(uint8_t *&pixels, Bitu &pitch)
{
	if (!sdl.active || sdl.updating) {
		return false;
	}
	sdl.updating = OUTPUT_SURFACE_StartUpdate(pixels, pitch);
	return sdl.updating;
}

void GFX_EndUpdate(const uint16_t *changedLines)
{
	if (!sdl.updating) {
		return;
	}
	sdl.updating = false;
	OUTPUT_SURFACE_EndUpdate(changedLines);
	sdl.must_redraw_all = false; /* upstream clears after dispatch (3398) */
}

void GFX_SetPalette(Bitu start, Bitu count, GFX_PalEntry *entries)
{
	/* 32bpp surface: render maps colors through GFX_GetRGB */
	(void)start; (void)count; (void)entries;
}

Bitu GFX_GetRGB(uint8_t red, uint8_t green, uint8_t blue)
{
	return ((Bitu)red << GFX_Rshift) | ((Bitu)green << GFX_Gshift) |
	       ((Bitu)blue << GFX_Bshift);
}

Bitu GFX_GetBShift(void) { return GFX_Bshift; }

void GFX_SetTitle(int32_t cycles, int frameskip, Bits timing, bool paused)
{
	(void)cycles; (void)frameskip; (void)timing; (void)paused;
}

void GFX_ResetScreen(void)
{
	GFX_Start();
	if (sdl.draw.callback) {
		(sdl.draw.callback)(GFX_CallBackReset);
	}
}

void GFX_SwitchFullScreen(void) {}
bool GFX_IsFullscreen(void) { return false; }
void GFX_LosingFocus(void)
{
	MAPPER_LosingFocus();
}
void GFX_ReleaseMouse(void) {}

/* ── event pump (called from the core loop, dosbox.cpp:515) ─────── */
void GFX_Events(void)
{
	SDL_Event event;

#ifdef CONFIG_DOSBOX_SCRIPTED_INPUT
	dbx_scripted_pump();
#endif
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		case SDL_JOYAXISMOTION:
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			MAPPER_CheckEvent(&event);
			break;
		case SDL_MOUSEMOTION:
			/* wired in P2c alongside mouse capture */
			break;
		default:
			break;
		}
	}
}

/* ── [sdl] config section: only what kept TUs read ──────────────── */
extern void Null_Init(Section *sec);

static void pizza_sdl_config_section(void)
{
	Section_prop *sec = control->AddSection_prop("sdl", &Null_Init);

	sec->Add_string("output", Property::Changeable::Always, "surface");
	sec->Add_bool("fullscreen", Property::Changeable::Always, false);
	sec->Add_bool("autolock", Property::Changeable::Always, false);
	sec->Add_int("sensitivity", Property::Changeable::Always, 100);
	/* the mapper fetches these via Get_path — must be path props */
	sec->Add_path("mapperfile", Property::Changeable::Always, "mapper.map");
	sec->Add_path("mapperfile_sdl1", Property::Changeable::Always, "");
	sec->Add_path("mapperfile_sdl2", Property::Changeable::Always, "");
	sec->Add_bool("showdetails", Property::Changeable::Always, false);
}

/* ── sdlmain odds referenced across the tree ────────────────────── */
Bitu frames = 0;
Bitu currentWindowWidth = 640, currentWindowHeight = 400;
Bitu userResizeWindowWidth = 0, userResizeWindowHeight = 0;
bool mouselocked = false;
bool clipboard_dosapi = false, clipboard_biospaste = false;
int wheel_key = 0;
bool wheel_guest = false;
int mbutton = 3;
unsigned int sendkeymap = 0, hostkeyalt = 0, maincp = 0;
bool dos_shell_running_program = false;
bool enableime = false, tonoime = false, tooutttf = false, kana_input = false;
int mouse_start_x = -1, mouse_start_y = -1, mouse_end_x = -1, mouse_end_y = -1;
int selsrow = -1, selscol = -1, selerow = -1, selecol = -1;
bool user_cursor_locked = false;
int user_cursor_x = 0, user_cursor_y = 0;
int user_cursor_sw = 640, user_cursor_sh = 480;
MOUSE_EMULATION user_cursor_emulation = (MOUSE_EMULATION)0;
bool usesystemcursor = false;
bool startup_state_capslock = false, startup_state_numlock = false;
bool startup_state_scrlock = false;
double rtdelta = 0.0;
Bitu time_limit_ms = 0;

/* BIOS boot-image plumbing (BOOT command / bios image boot) */
std::vector<uint8_t> boot_code_image;
PhysPt boot_code_image_load_to = 0;
uint16_t boot_code_image_stack_ss = 0, boot_code_image_stack_sp = 0;
bool boot_debug_break = false;

bool dos_kernel_disabled = true;
bool winrun = false;
bool use_save_file = false;
bool pause_on_vsync = false;
bool checkmenuwidth = false;
unsigned int page = 0;
bool mountiro[26] = {false};
bool mountfro[26] = {false};
void BlankDisplay(void) {}
bool guest_machine_power_on = false;

bool custom_bios = false;
size_t custom_bios_image_size = 0;
Bitu custom_bios_image_offset = 0;
unsigned char *custom_bios_image = NULL;

GLIDE_Block glide;

/* codepage/boxdraw tables (sdl_gui/sdlmain owned upstream) */
uint16_t cpMap_AX[32] = {0};
std::map<int, int> lowboxdrawmap, pc98boxdrawmap;

void PauseDOSBox(bool pressed) { (void)pressed; }
bool systemmessagebox(const char *aTitle, const char *aMessage,
		      const char *aDialogType, const char *aIconType,
		      int aDefaultButton)
{
	(void)aDialogType; (void)aIconType; (void)aDefaultButton;
	printk("[msgbox] %s: %s\n", aTitle ? aTitle : "", aMessage ? aMessage : "");
	return true;
}

bool sdl_wait_on_error(void) { return false; }
bool TTF_using(void) { return false; }

bool addovl = false, addne2k = false, window_was_maximized = false;
const char *modifier = "";
int paste_speed = 30;
std::string strPasteBuffer = "";
std::string configfile = "";

void CaptureMouseNotify(bool capture) { (void)capture; }
void GFX_CaptureMouse(bool capture) { (void)capture; }
void GFX_ForceRedrawScreen(void) { sdl.must_redraw_all = true; }
SDL_Window *GFX_GetSDLWindow(void) { return sdl.window; }
void GFX_RestoreMode(void) {}
void GFX_SetResizeable(bool enable) { (void)enable; }
SDL_Window *GFX_SetSDLSurfaceWindow(uint16_t width, uint16_t height)
{
	return GFX_SetSDLWindowMode(width, height, SCREEN_SURFACE);
}
SDL_Rect GFX_GetSDLSurfaceSubwindowDims(uint16_t width, uint16_t height)
{
	SDL_Rect r;

	r.x = 0; r.y = 0; r.w = width; r.h = height;
	return r;
}
Bitu GUI_JoystickCount(void) { return 0; }
void GUI_Run(bool pressed) { (void)pressed; }
int GetDisplayNumber(void) { return 0; }
void SetDisplayNumber(int display) { (void)display; }
void GetDrawWidthHeight(unsigned int *pdrawWidth, unsigned int *pdrawHeight)
{
	*pdrawWidth = sdl.draw.width;
	*pdrawHeight = sdl.draw.height;
}
void GetMaxWidthHeight(unsigned int *pmaxWidth, unsigned int *pmaxHeight)
{
	*pmaxWidth = 1280;
	*pmaxHeight = 1024;
}
bool Mouse_IsLocked(void) { return false; }
bool OpenGL_using(void) { return false; }
void SetWindowTransparency(int trans) { (void)trans; }
void ETHNET_ProgramStart(Program **make) { *make = NULL; }

bool DOSBox_Paused(void) { return false; }
bool GFX_GetPreventFullscreen(void) { return false; }
void GFX_LogSDLState(void) {}
void GFX_SDL_Overscan(void) {}
void GFX_CaptureMouse(void) {}
void GUI_Shortcut(int select) { (void)select; }
void KeyboardLayoutDetect(void) {}
bool MOUSE_IsLocked(void) { return false; }
void Mouse_AutoLock(bool enable) { (void)enable; }
void SendKey(std::string meth) { (void)meth; }
void RebootConfig(std::string filename, bool confirm)
{
	(void)filename; (void)confirm;
}
void RebootLanguage(std::string filename, bool confirm)
{
	(void)filename; (void)confirm;
}
bool is_always_on_top(void) { return false; }
void UpdateKeyboardLEDState(Bitu led_state) { (void)led_state; }
void UpdateWindowDimensions(void) {}
int res_init(void) { return -1; }

void SDL_rect_cliptoscreen(SDL_Rect &r)
{
	int sw = sdl.surface ? sdl.surface->w : 640;
	int sh = sdl.surface ? sdl.surface->h : 400;

	if (r.x < 0) { r.w += r.x; r.x = 0; }
	if (r.y < 0) { r.h += r.y; r.y = 0; }
	if (r.x + r.w > sw) r.w = sw - r.x;
	if (r.y + r.h > sh) r.h = sh - r.y;
	if (r.w < 0) r.w = 0;
	if (r.h < 0) r.h = 0;
}

std::string GetDOSBoxXPath(bool withexe)
{
	(void)withexe;
	return std::string("/");
}

void Get_Custom_SaveDir(std::string &savedir) { (void)savedir; }

/* savestate hooks for the replaced sdlmain */
void POD_Save_Sdlmain(std::ostream &stream) { (void)stream; }
void POD_Load_Sdlmain(std::istream &stream) { (void)stream; }

/* ── dormant-hardware externs (pc98_fm not built) ───────────────── */
bool PC98_FM_SoundBios_Enabled(void) { return false; }
void *fmport_a_pic_event_PIC_Event = NULL;
void *fmport_b_pic_event_PIC_Event = NULL;

/* ── DOS kernel boot (transcribed from upstream sdlmain.cpp:7885,
 *    PC-98 and HMENU chrome dropped) ──────────────────────────────── */
bool VM_Boot_DOSBox_Kernel(void)
{
	if (!dos_kernel_disabled) {
		void RemoveEMSPageFrame(void), RemoveUMBBlock(void), DisableINT33(void);
		void DOS_GetMemory_unmap(void), VFILE_Shutdown(void), PROGRAMS_Shutdown(void);
		void DOS_UninstallMisc(void), SBLASTER_DOS_Shutdown(void), GUS_DOS_Shutdown(void);
		void EMS_DoShutDown(void), XMS_DoShutDown(void), DOS_DoShutDown(void);

		RemoveEMSPageFrame();
		RemoveUMBBlock();
		DisableINT33();
		DOS_GetMemory_unmap();
		VFILE_Shutdown();
		PROGRAMS_Shutdown();
		DOS_UninstallMisc();
		SBLASTER_DOS_Shutdown();
		GUS_DOS_Shutdown();
		EMS_DoShutDown();
		XMS_DoShutDown();
		DOS_DoShutDown();
		DispatchVMEvent(VM_EVENT_DOS_SURPRISE_REBOOT);
		dos_kernel_disabled = true;
	}
	if (dos_kernel_disabled) {
		void Init_MemHandles(void);
		Init_MemHandles();
		DispatchVMEvent(VM_EVENT_DOS_BOOT);
		dos_kernel_disabled = false;

		void DOS_Startup(Section *sec);
		DOS_Startup(NULL);
		maincp = 0;

		void DRIVES_Startup(Section *s);
		DRIVES_Startup(NULL);
		DispatchVMEvent(VM_EVENT_DOS_INIT_KERNEL_READY);

		void DOS_InitClock(void);
		DOS_InitClock();

		void DOS_KeyboardLayout_Startup(Section *sec);
		DOS_KeyboardLayout_Startup(NULL);

		void XMS_Startup(Section *sec);
		XMS_Startup(NULL);
		void EMS_Startup(Section *sec);
		EMS_Startup(NULL);

		if (mainMenu.item_exists("HelpCommandMenu")) {
			mainMenu.get_item("HelpCommandMenu").enable();
		}

		void SHELL_MessagesInit(void), CONFIGSHELL_Init(void), CONFIGSHELL_Run(void);
		SHELL_MessagesInit();
		CONFIGSHELL_Init();
		CONFIGSHELL_Run();
		DispatchVMEvent(VM_EVENT_DOS_INIT_CONFIG_SYS_DONE);

		void SHELL_Init(void);
		SHELL_Init(); /* changes the CPU instruction pointer */
		DispatchVMEvent(VM_EVENT_DOS_INIT_SHELL_READY);

		void AUTOEXEC_Startup(Section *sec);
		AUTOEXEC_Startup(NULL);
		void MSCDEX_Startup(Section *sec);
		MSCDEX_Startup(NULL);
		void MOUSE_Startup(Section *sec);
		MOUSE_Startup(NULL);
		DispatchVMEvent(VM_EVENT_DOS_INIT_AUTOEXEC_BAT_DONE);
		DispatchVMEvent(VM_EVENT_DOS_INIT_AT_PROMPT);

#ifdef CONFIG_DOSBOX_SCRIPTED_INPUT
		dbx_scripted_at_prompt();
#endif

		void SHELL_Run(void);
		SHELL_Run();
	}
	return true;
}

/* ── VM power (upstream sdlmain.cpp:7993) ───────────────────────── */
bool VM_PowerOn(void)
{
	if (!guest_machine_power_on) {
		guest_machine_power_on = true;
		DispatchVMEvent(VM_EVENT_POWERON);
		DispatchVMEvent(VM_EVENT_RESET);
		DispatchVMEvent(VM_EVENT_RESET_END);
	}
	return true;
}

/* ── init cascade externs (transcribed from upstream main) ──────── */
void DOSBOX_SetupConfigSections(void);
void MSG_Init(void);
void DOSBOX_InitTickLoop(void);
void DOSBOX_RealInit(void);
void RENDER_Init(void);
void CAPTURE_Init(void);
void IO_Init(void);
void HARDWARE_Init(void);
void CPU_PreInit(void);
void Init_AddressLimitAndGateMask(void);
void Init_MemoryAccessArray(void);
void Init_A20_Gate(void);
void Init_PS2_Port_92h(void);
void Init_RAM(void);
void Init_DMA(void);
void Init_PIC(void);
void TIMER_Init(void);
void PCIBUS_Init(void);
void PAGING_Init(void);
void CMOS_Init(void);
void ROMBIOS_Init(void);
void CALLBACK_Init(void);
void Init_VGABIOS(void);
void PROGRAMS_Init(void);
void PCSPEAKER_Init(void);
void TANDYSOUND_Init(void);
void MPU401_Init(void);
void MIXER_Init(void);
void MIDI_Init(void);
void CPU_Init(void);
void Weitek_Init(void);
#if C_FPU
void FPU_Init(void);
#endif
void VGA_Init(void);
void ISAPNP_Cfg_Init(void);
void FDC_Primary_Init(void);
void KEYBOARD_Init(void);
void SBLASTER_Init(void);
void JOYSTICK_Init(void);
void PS1SOUND_Init(void);
void DISNEY_Init(void);
void GUS_Init(void);
void IDE_Init(void);
void BIOS_Init(void);
void INT10_Init(void);
void SERIAL_Init(void);
void PARALLEL_Init(void);
void DOS_Init(void);
void DRIVES_Init(void);
void DOS_KeyboardLayout_Init(void);
void MOUSE_Init(void);
void XMS_Init(void);
void EMS_Init(void);
void AUTOEXEC_Init(void);
void MSCDEX_Init(void);
void CDROM_Image_Init(void);
void Init_MemHandles(void);
void AllocCallback1(void);
void AllocCallback2(void);
void ConstructMenu(void);

int main(void)
{
	printk("[dbx] PiZZa DOSBox-X: P2b bring-up, board %s\n", CONFIG_BOARD);

	/* CPU-bound emulator: pin the A53 turbo before the heavy init so
	 * the whole run (dynrec included) gets the full clock.
	 */
	dbx_bump_arm_clock();

	SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO);

	/* sdl is a default-constructed global (NSDMIs zero it); do NOT
	 * memset — SDL_Block owns C++ members.
	 */
	sdl.desktop.want_type = SCREEN_SURFACE;
	sdl.desktop.bpp = 32;

	static char arg0[] = "dosbox-x";
	static char *argv[] = { arg0, NULL };
	CommandLine com_line(1, argv);
	Config myconf(&com_line);

	control = &myconf;

	pizza_sdl_config_section();
	LOG::EarlyInit();
	LOG::SetupConfigSection();
	DOSBOX_SetupConfigSections();
	LOG::Init();

	/* P2d: the dynamic recompiler is mandatory for DOS/4GW protected
	 * mode. cputype=auto lets the 386+ features the extender needs
	 * light up.
	 */
	{
		Section *cpu = control->GetSection("cpu");

		if (cpu != NULL) {
			cpu->HandleInputline("core=dynamic");
			cpu->HandleInputline("cputype=auto");
		}
		/* 16 MiB guest is plenty for DOOM + DOS/4GW. */
		Section *ds = control->GetSection("dosbox");

		if (ds != NULL) {
			ds->HandleInputline("memsizekb=0");
			ds->HandleInputline("memsize=16");
		}
	}

	printk("[dbx] config sections up (core=dynamic), starting init cascade\n");

	/* upstream cascade, dropped subsystems omitted (voodoo, glide,
	 * imfc, innova, dongle, ne2000, printer, debugger, ipx)
	 */
	AllocCallback1(); /* base menu tree; cascade items get_item() into it */

	/* upstream sdlmain registers these host-side hotkey handlers; kept
	 * TUs get_item() their "mapper_<event>" menu items, so register
	 * them all as no-ops (real ones return with the chrome they drive)
	 */
	{
		static const struct {
			const char *evt, *txt;
		} noop_evts[] = {
			{ "reset", "Reset DOSBox-X" },
			{ "reboot", "Reboot DOS system" },
			{ "loadmap", "Load mapper file" },
			{ "quickrun", "Quick launch program" },
			{ "newinst", "Start new instance" },
			{ "shutdown", "Quit from DOSBox-X" },
			{ "capmouse", "Capture mouse" },
			{ "capkeyboard", "Capture keyboard" },
			{ "fastedit", "Quick edit mode" },
			{ "copyall", "Copy to clipboard" },
			{ "paste", "Paste from clipboard" },
			{ "pasteend", "Stop clipboard paste" },
			{ "fullscr", "Toggle fullscreen" },
			{ "sendkey_mapper", "Send special key" },
		};
		for (unsigned i = 0; i < sizeof(noop_evts) / sizeof(noop_evts[0]); i++) {
			MAPPER_AddHandler([](bool) {}, MK_nothing, 0,
					  noop_evts[i].evt, noop_evts[i].txt);
		}
	}

	MAPPER_StartUp();
	DOSBOX_InitTickLoop();
	DOSBOX_RealInit();
	RENDER_Init();
	CAPTURE_Init();
	IO_Init();
	HARDWARE_Init();
	CPU_PreInit();
	Init_AddressLimitAndGateMask();
	Init_MemoryAccessArray();
	Init_A20_Gate();
	Init_PS2_Port_92h();
	Init_RAM();
	Init_DMA();
	Init_PIC();
	TIMER_Init();
	PCIBUS_Init();
	PAGING_Init();
	CMOS_Init();
	ROMBIOS_Init();
	CALLBACK_Init();
	Init_VGABIOS();
	PROGRAMS_Init();
	PCSPEAKER_Init();
	TANDYSOUND_Init();
	MPU401_Init();
	MIXER_Init();
	MIDI_Init();
	CPU_Init();
	Weitek_Init();
#if C_FPU
	FPU_Init();
#endif
	VGA_Init();
	ISAPNP_Cfg_Init();
	FDC_Primary_Init();
	KEYBOARD_Init();
	SBLASTER_Init();
	JOYSTICK_Init();
	PS1SOUND_Init();
	DISNEY_Init();
	GUS_Init();
	IDE_Init();
	BIOS_Init();
	INT10_Init();
	SERIAL_Init();
	PARALLEL_Init();

	MEM_A20_Enable(true);

	printk("[dbx] hardware up, DOS kernel init\n");

	DOS_Init();
	DRIVES_Init();
	DOS_KeyboardLayout_Init();
	MOUSE_Init();
	XMS_Init();
	EMS_Init();
	AUTOEXEC_Init();
	MSCDEX_Init();
	CDROM_Image_Init();
	Init_MemHandles();

	MAPPER_Init();
	AllocCallback2();
	MSG_Init();

	if (!VM_PowerOn()) {
		E_Exit("VM failed to power on");
	}

	ConstructMenu();
	mainMenu.rebuild();
#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW
	mainMenu.showMenu(false);
	mainMenu.screenWidth = sdl.surface ? (unsigned int)sdl.surface->w : 640;
	mainMenu.screenHeight = sdl.surface ? (unsigned int)sdl.surface->h : 400;
	mainMenu.updateRect();
#endif

	printk("[dbx] entering DOSBOX_RunMachine\n");

	try {
		DOSBOX_RunMachine();
	} catch (int x) {
		printk("[dbx] machine threw signal %d (reboot/shutdown), stopping\n", x);
	} catch (...) {
		printk("[dbx] machine threw unknown exception\n");
	}

	printk("[dbx] exited\n");
	return 0;
}
