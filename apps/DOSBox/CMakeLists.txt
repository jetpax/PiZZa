# SPDX-License-Identifier: Apache-2.0
#
# PiZZa -- DOSBox-X on the shared SDL2 shim (apps/lib/sdl2shim).
#
# Links the DOSBox-X machine core (untouched checkout at
# ~/github/dosbox-x) against the shim, with the desktop chrome left out
# of the TU list and sdlmain.cpp replaced wholesale by
# src/sdlmain_embedded.cpp. TU selection and macro choices per
# notes/P2_SCOPE.md.
#
# DOSBox-X is GPL-2.0-or-later; this binary is GPL-2.0 by linking it.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza-dosbox LANGUAGES C CXX ASM)

if(CONFIG_USB_DEVICE_STACK_NEXT)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

# ── DOSBox-X tree (untouched) ───────────────────────────────────────
if(NOT DEFINED DBX)
  set(DBX "$ENV{HOME}/github/dosbox-x")
endif()
if(NOT EXISTS "${DBX}/src/dosbox.cpp")
  message(FATAL_ERROR
    "DOSBox-X sources not found at ${DBX}. Clone "
    "https://github.com/joncampbell123/dosbox-x or pass -DDBX=<path>.")
endif()
set(S ${DBX}/src)

# ── KEEP list (P2_SCOPE.md §5) ──────────────────────────────────────
# cpu: all cores except the x86-host-only dynamic_x86.
set(DBX_CPU
  ${S}/cpu/callback.cpp ${S}/cpu/cpu.cpp ${S}/cpu/flags.cpp
  ${S}/cpu/modrm.cpp ${S}/cpu/paging.cpp ${S}/cpu/mmx.cpp
  ${S}/cpu/core_full.cpp ${S}/cpu/core_simple.cpp
  ${S}/cpu/core_normal.cpp ${S}/cpu/core_normal_286.cpp
  ${S}/cpu/core_normal_8086.cpp ${S}/cpu/core_prefetch.cpp
  ${S}/cpu/core_prefetch_286.cpp ${S}/cpu/core_prefetch_8086.cpp
  ${S}/cpu/core_dynrec.cpp
)

set(DBX_FPU ${S}/fpu/fpu.cpp)

# dos: platform cdrom-ioctl variants and physfs dropped.
# cdrom_image.cpp rewritten in the build dir: the CHD prefetch thread
# (std::thread, no gthreads on Zephyr) is compiled out, taking the
# synchronous decode path instead. Checkout stays pristine (Doom
# i_video precedent).
set(DBX_CDIMG_GEN ${CMAKE_BINARY_DIR}/gen/cdrom_image_nothread.cpp)
add_custom_command(
  OUTPUT ${DBX_CDIMG_GEN}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
  COMMAND ${CMAKE_COMMAND} -E copy ${S}/dos/cdrom_image.cpp ${DBX_CDIMG_GEN}
  COMMAND sed -i.bak
          -e "s@#if !defined(HX_DOS) && !(defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR))@#if 0 /* no threads on Zephyr */@"
          -e "s@#if defined(HX_DOS) || defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR)@#if 1 /* sync CHD decode */@"
          ${DBX_CDIMG_GEN}
  DEPENDS ${S}/dos/cdrom_image.cpp
  COMMENT "Rewriting cdrom_image.cpp: CHD prefetch thread -> sync decode"
  VERBATIM
)

# dos_programs.cpp rewritten in the build dir: the physfs:// mount
# branches reference physfsDrive (vendored PhysFS not built); NULL the
# construction and casts, MOUNT reports failure on such paths.
set(DBX_DOSPROG_GEN ${CMAKE_BINARY_DIR}/gen/dos_programs_nophysfs.cpp)
add_custom_command(
  OUTPUT ${DBX_DOSPROG_GEN}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
  COMMAND ${CMAKE_COMMAND} -E copy ${S}/dos/dos_programs.cpp ${DBX_DOSPROG_GEN}
  COMMAND sed -i.bak
          -e "s@new physfscdromDrive@NULL; (void)@"
          -e "s@new physfsDrive@NULL; (void)@"
          -e "s@dynamic_cast<physfsDrive\\*>(Drives\\[drive-'A'\\])@NULL@"
          -e "s@dynamic_cast<physfscdromDrive\\*>(Drives\\[drive-'A'\\])@NULL@"
          ${DBX_DOSPROG_GEN}
  DEPENDS ${S}/dos/dos_programs.cpp
  COMMENT "Rewriting dos_programs.cpp: physfs mounts -> NULL"
  VERBATIM
)

# dos_files.cpp gets the same physfs treatment (savestate restore paths).
set(DBX_DOSFILES_GEN ${CMAKE_BINARY_DIR}/gen/dos_files_nophysfs.cpp)
add_custom_command(
  OUTPUT ${DBX_DOSFILES_GEN}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
  COMMAND ${CMAKE_COMMAND} -E copy ${S}/dos/dos_files.cpp ${DBX_DOSFILES_GEN}
  COMMAND sed -i.bak
          -e "s@if (strlen(overlaydir)) dynamic_cast<physfsDrive\\*>(Drives\\[lcv\\])->setOverlaydir(overlaydir);@;@"
          -e "s@dynamic_cast<physfsDrive\\*>(Drives\\[lcv\\])@NULL@g"
          -e "s@new physfscdromDrive@NULL; (void)@"
          -e "s@new physfsDrive@NULL; (void)@"
          ${DBX_DOSFILES_GEN}
  DEPENDS ${S}/dos/dos_files.cpp
  COMMENT "Rewriting dos_files.cpp: physfs savestate paths -> NULL"
  VERBATIM
)

set(DBX_DOS
  ${S}/dos/cdrom.cpp ${DBX_CDIMG_GEN}
  ${S}/dos/dos.cpp ${S}/dos/dos_classes.cpp ${S}/dos/dos_devices.cpp
  ${S}/dos/dos_execute.cpp ${DBX_DOSFILES_GEN} ${S}/dos/dos_ioctl.cpp
  ${S}/dos/dos_keyboard_layout.cpp ${S}/dos/dos_memory.cpp
  ${S}/dos/dos_misc.cpp ${S}/dos/dos_mscdex.cpp ${DBX_DOSPROG_GEN}
  ${S}/dos/dos_tables.cpp ${S}/dos/drive_cache.cpp ${S}/dos/drive_fat.cpp
  ${S}/dos/drive_iso.cpp ${S}/dos/drive_local.cpp
  ${S}/dos/drive_overlay.cpp ${S}/dos/drive_virtual.cpp
  ${S}/dos/drives.cpp ${S}/dos/eltorito.cpp
)

set(DBX_INTS
  ${S}/ints/bios.cpp ${S}/ints/bios_disk.cpp ${S}/ints/bios_keyboard.cpp
  ${S}/ints/bios_memdisk.cpp ${S}/ints/bios_vhd.cpp ${S}/ints/ems.cpp
  ${S}/ints/int10.cpp ${S}/ints/int10_char.cpp ${S}/ints/int10_memory.cpp
  ${S}/ints/int10_misc.cpp ${S}/ints/int10_modes.cpp
  ${S}/ints/int10_pal.cpp ${S}/ints/int10_put_pixel.cpp
  ${S}/ints/int10_vesa.cpp ${S}/ints/int10_video_state.cpp
  ${S}/ints/int10_vptable.cpp ${S}/ints/int_dosv.cpp ${S}/ints/mouse.cpp
  ${S}/ints/pc98_lio.cpp ${S}/ints/qcow2_disk.cpp ${S}/ints/xms.cpp
)

set(DBX_SHELL
  ${S}/shell/shell.cpp ${S}/shell/shell_batch.cpp
  ${S}/shell/shell_cmds.cpp ${S}/shell/shell_misc.cpp
)

# misc: ethernet dispatcher kept with the "nothing" backend only;
# pcap/slirp/ethnet and iconvpp dropped.
set(DBX_MISC
  ${S}/misc/clipboard.cpp ${S}/misc/cross.cpp ${S}/misc/ethernet.cpp
  ${S}/misc/ethernet_nothing.cpp ${S}/misc/messages.cpp
  ${S}/misc/mkdir_p.cpp ${S}/misc/programs.cpp
  ${S}/misc/regionalloctracking.cpp ${S}/misc/savestates.cpp
  ${S}/misc/setup.cpp ${S}/misc/shiftjis.cpp ${S}/misc/support.cpp
)

set(DBX_OUTPUT ${S}/output/output_surface.cpp ${S}/output/output_tools.cpp)

# gui: sdlmain.cpp REPLACED by sdlmain_embedded.cpp; sdl_gui/gui_tk and
# sdlmain_linux omitted.
# sdl_mapper.cpp rewritten in the build dir: the Typer autotype helper
# owns a std::thread (no gthreads on Zephyr); its start becomes a no-op.
set(DBX_MAPPER_GEN ${CMAKE_BINARY_DIR}/gen/sdl_mapper_nothread.cpp)
add_custom_command(
  OUTPUT ${DBX_MAPPER_GEN}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
  COMMAND ${CMAKE_COMMAND} -E copy ${S}/gui/sdl_mapper.cpp ${DBX_MAPPER_GEN}
  COMMAND sed -i.bak
          -e "s@m_instance = std::thread(&Typer::Callback, this);@/* autotype disabled (no threads) */@"
          -e "s@if (m_instance.joinable())@if (0)@"
          -e "s@m_instance.join();@{}@"
          -e "s@std::thread              m_instance;@/* no thread member */@"
          ${DBX_MAPPER_GEN}
  DEPENDS ${S}/gui/sdl_mapper.cpp
  COMMENT "Rewriting sdl_mapper.cpp: Typer std::thread -> no-op"
  VERBATIM
)

set(DBX_GUI
  ${S}/gui/bitop.cpp ${S}/gui/ptrop.cpp ${S}/gui/menu.cpp
  ${S}/gui/menu_callback.cpp ${S}/gui/midi.cpp ${S}/gui/render.cpp
  ${S}/gui/render_scalers.cpp ${DBX_MAPPER_GEN}
  ${S}/gui/zipcrc.c ${S}/gui/zipfile.cpp
)

# debug_gui carries LOG_MSG/logging even with C_DEBUG off.
set(DBX_DEBUG ${S}/debug/debug_gui.cpp)

# hardware: PC-98 display TUs kept dormant (symbols referenced from
# shared vga code); snd_pc98 + pc98_fm, voodoo/glide, network cards,
# serial-OPL boards, imfc (std::thread), innova (reSID) omitted.
set(DBX_HW
  ${S}/hardware/8255.cpp ${S}/hardware/adlib.cpp ${S}/hardware/cmos.cpp
  ${S}/hardware/cqm.c ${S}/hardware/dbopl.cpp ${S}/hardware/disney.cpp
  ${S}/hardware/dma.cpp ${S}/hardware/floppy.cpp
  ${S}/hardware/gameblaster.cpp ${S}/hardware/gus.cpp
  ${S}/hardware/hardware.cpp ${S}/hardware/ide.cpp
  ${S}/hardware/iohandler.cpp ${S}/hardware/joystick.cpp
  ${S}/hardware/keyboard.cpp ${S}/hardware/memory.cpp
  ${S}/hardware/mixer.cpp ${S}/hardware/mpu401.cpp
  ${S}/hardware/nukedopl.cpp ${S}/hardware/pc98.cpp
  ${S}/hardware/pc98_artic.cpp ${S}/hardware/pci_bus.cpp
  ${S}/hardware/pcspeaker.cpp ${S}/hardware/pic.cpp
  ${S}/hardware/ps1_sound.cpp ${S}/hardware/sblaster.cpp
  ${S}/hardware/tandy_sound.cpp ${S}/hardware/timer.cpp
  ${S}/hardware/vga.cpp ${S}/hardware/vga_ati.cpp
  ${S}/hardware/vga_attr.cpp ${S}/hardware/vga_crtc.cpp
  ${S}/hardware/vga_dac.cpp ${S}/hardware/vga_draw.cpp
  ${S}/hardware/vga_gfx.cpp ${S}/hardware/vga_memory.cpp
  ${S}/hardware/vga_misc.cpp ${S}/hardware/vga_other.cpp
  ${S}/hardware/vga_paradise.cpp ${S}/hardware/vga_pc98_cg.cpp
  ${S}/hardware/vga_pc98_crtc.cpp ${S}/hardware/vga_pc98_dac.cpp
  ${S}/hardware/vga_pc98_egc.cpp ${S}/hardware/vga_pc98_gdc.cpp
  ${S}/hardware/vga_pc98_gdc_draw.cpp ${S}/hardware/vga_s3.cpp
  ${S}/hardware/vga_seq.cpp ${S}/hardware/vga_tseng.cpp
  ${S}/hardware/vga_xga.cpp
  ${S}/hardware/esfmu/esfm.c ${S}/hardware/esfmu/esfm_registers.c
)

set(DBX_MAME
  ${S}/hardware/mame/fmopl.cpp ${S}/hardware/mame/saa1099.cpp
  ${S}/hardware/mame/sn76496.cpp ${S}/hardware/mame/ymdeltat.cpp
  ${S}/hardware/mame/ymf262.cpp
)

# serialport: no SDL_net consumers (softmodem/nullmodem/misc_util out);
# directserial needs real host termios, out (C_DIRECTSERIAL off).
# libserial compiles against the compat termios (its opens fail at
# runtime) and satisfies the serial-OPL boards below.
set(DBX_SERIAL
  ${S}/hardware/serialport/serialport.cpp
  ${S}/hardware/serialport/serialdummy.cpp
  ${S}/hardware/serialport/serialmouse.cpp
  ${S}/hardware/serialport/seriallog.cpp
  ${S}/hardware/serialport/serialfile.cpp
  ${S}/hardware/serialport/libserial.cpp
)

# adlib.cpp references the serial OPL boards + RetroWave unconditionally
# (runtime-gated on oplemu= choices we never use). RetroWave links real
# (compat termios suffices); the opl2/opl3duo boards own std::threads,
# so they are method stubs in lib_stubs.cpp instead.
set(DBX_OPLBOARDS
  ${S}/hardware/RetroWaveLib/RetroWave.c
  ${S}/hardware/RetroWaveLib/Protocol/Serial.c
  ${S}/hardware/RetroWaveLib/Board/OPL3.c
  ${S}/hardware/RetroWaveLib/Platform/POSIX_SerialPort.c
  ${S}/hardware/RetroWaveLib/RetroWave_DOSBoX.cpp
)

set(DBX_PARPORT
  ${S}/hardware/parport/parport.cpp
  ${S}/hardware/parport/filelpt.cpp
  ${S}/hardware/parport/printer_charmaps.cpp
)

# builtin DOS tools (baked binaries); glide2x + ne2000 bins omitted.
set(DBX_BUILTIN
  ${S}/builtin/25_com.cpp ${S}/builtin/28_com.cpp ${S}/builtin/4dos.cpp
  ${S}/builtin/50_com.cpp ${S}/builtin/append_exe.cpp
  ${S}/builtin/buffers_com.cpp ${S}/builtin/cdplay.cpp
  ${S}/builtin/chkdsk_exe.cpp ${S}/builtin/cwsdpmi_exe.cpp
  ${S}/builtin/cwsdpmi_exe_pc98.cpp ${S}/builtin/debug_exe.cpp
  ${S}/builtin/defrag_exe.cpp ${S}/builtin/device_com.cpp
  ${S}/builtin/diskcopy_exe.cpp ${S}/builtin/dos32a_exe.cpp
  ${S}/builtin/dos4gw_exe.cpp ${S}/builtin/dosidle_exe.cpp
  ${S}/builtin/dosmid.cpp ${S}/builtin/dsxmenu_exe_pc.cpp
  ${S}/builtin/dsxmenu_exe_pc98.cpp ${S}/builtin/edit_com.cpp
  ${S}/builtin/emsmagic.cpp ${S}/builtin/eval.cpp ${S}/builtin/fc_exe.cpp
  ${S}/builtin/fcbs_com.cpp ${S}/builtin/fdisk_exe.cpp
  ${S}/builtin/files_com.cpp ${S}/builtin/find_exe.cpp
  ${S}/builtin/format_exe.cpp ${S}/builtin/glide2x.cpp
  ${S}/builtin/ne2000bin.cpp ${S}/builtin/hexmem16_exe.cpp
  ${S}/builtin/hexmem32_exe.cpp ${S}/builtin/ipsmake.cpp
  ${S}/builtin/ipspatch.cpp ${S}/builtin/lastdriv_com.cpp
  ${S}/builtin/mem_com.cpp ${S}/builtin/move_exe.cpp
  ${S}/builtin/mpxplay.cpp ${S}/builtin/print_com.cpp
  ${S}/builtin/replace_exe.cpp ${S}/builtin/shutdown.cpp
  ${S}/builtin/sort_exe.cpp ${S}/builtin/sys_com.cpp
  ${S}/builtin/textutil.cpp ${S}/builtin/xcopy_exe.cpp
  ${S}/builtin/zip.cpp
)

# aviwriter: hardware.cpp references avi_writer_*/riff_wav_writer_*
# unconditionally (runtime-gated); self-contained, so link the real thing.
set(DBX_AVIWRITER
  ${S}/aviwriter/avi_writer.cpp ${S}/aviwriter/avi_rw_iobuf.cpp
  ${S}/aviwriter/guid.cpp ${S}/aviwriter/ksdataformat.cpp
  ${S}/aviwriter/riff.cpp ${S}/aviwriter/riff_wav_writer.cpp
)

# vendored zlib (vs/zlib): include/zip.h + savestates need it; the gz*
# file API is not used on the RAM-fd layer, left out.
set(DBX_ZLIB
  ${DBX}/vs/zlib/adler32.c ${DBX}/vs/zlib/compress.c
  ${DBX}/vs/zlib/crc32.c ${DBX}/vs/zlib/deflate.c
  ${DBX}/vs/zlib/infback.c ${DBX}/vs/zlib/inffast.c
  ${DBX}/vs/zlib/inflate.c ${DBX}/vs/zlib/inftrees.c
  ${DBX}/vs/zlib/trees.c ${DBX}/vs/zlib/uncompr.c
  ${DBX}/vs/zlib/zutil.c
)

# CD audio decoders: cdrom_image.cpp amalgamates SDL_sound.c + all
# decoder sources itself (that is also how upstream gives mp3.cpp its
# seek-table header) -- no standalone decoder TUs. Opus tracks resolve
# against the failing opusfile stubs in lib_stubs.cpp.
set(DBX_DECODERS)

# vendored libpng (vs/libpng): the bios.cpp boot splash decodes PNG
# unconditionally -- upstream treats libpng as part of the floor.
set(DBX_LIBPNG
  ${DBX}/vs/libpng/png.c ${DBX}/vs/libpng/pngerror.c
  ${DBX}/vs/libpng/pngget.c ${DBX}/vs/libpng/pngmem.c
  ${DBX}/vs/libpng/pngpread.c ${DBX}/vs/libpng/pngread.c
  ${DBX}/vs/libpng/pngrio.c ${DBX}/vs/libpng/pngrtran.c
  ${DBX}/vs/libpng/pngrutil.c ${DBX}/vs/libpng/pngset.c
  ${DBX}/vs/libpng/pngtrans.c ${DBX}/vs/libpng/pngwio.c
  ${DBX}/vs/libpng/pngwrite.c ${DBX}/vs/libpng/pngwtran.c
  ${DBX}/vs/libpng/pngwutil.c
)

set(DBX_SOURCES
  ${S}/dosbox.cpp
  ${DBX_CPU} ${DBX_FPU} ${DBX_DOS} ${DBX_INTS} ${DBX_SHELL} ${DBX_MISC}
  ${DBX_OUTPUT} ${DBX_GUI} ${DBX_DEBUG} ${DBX_HW} ${DBX_MAME}
  ${DBX_SERIAL} ${DBX_OPLBOARDS} ${DBX_PARPORT} ${DBX_BUILTIN}
  ${DBX_DECODERS} ${DBX_ZLIB} ${DBX_LIBPNG} ${DBX_AVIWRITER}
)

# ── shim + embedded platform layer ──────────────────────────────────
include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl2shim/sdl2shim.cmake)

set(DOSBOX_APP_SRC
  src/sdlmain_embedded.cpp
  src/posix_stubs.c
  src/abi_thunks.cpp
  src/lib_stubs.cpp
  src/sdl_compat.cpp
  src/dosbox_video.c
  src/dosbox_assets.c
  src/dosbox_dyncache.c
  src/dosbox_clock.c
  src/dosbox_pack.S
)

# baked FAT disk image (assets/<DBX_DISK> -> build dir for .incbin).
# DBX_DISK selects which image is baked; default is the DOOM HDD.
if(NOT DEFINED DBX_DISK)
  set(DBX_DISK doom.img)
endif()
set(DBX_DISK_BIN ${CMAKE_BINARY_DIR}/dosbox_disk.img)
add_custom_command(
  OUTPUT ${DBX_DISK_BIN}
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/assets/${DBX_DISK} ${DBX_DISK_BIN}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/${DBX_DISK}
  COMMENT "Baking FAT disk image ${DBX_DISK} into rodata"
)
add_custom_target(dbx_disk_bake DEPENDS ${DBX_DISK_BIN})
add_dependencies(app dbx_disk_bake)
set_source_files_properties(src/dosbox_pack.S PROPERTIES
  OBJECT_DEPENDS ${DBX_DISK_BIN}
  COMPILE_OPTIONS "-I${CMAKE_BINARY_DIR};-Wa,-I${CMAKE_BINARY_DIR}"
)

# dynrec code-cache dual-map patch: a build-dir copy of
# cpu/dynamic_alloc_common.h with the Zephyr branch injected. gen/ is
# prepended to the include path (below), so core_dynrec.cpp's
# `#include "cpu/dynamic_alloc_common.h"` resolves to the patched copy;
# checkout stays pristine.
set(DBX_DYNALLOC_GEN ${CMAKE_BINARY_DIR}/gen/cpu/dynamic_alloc_common.h)
add_custom_command(
  OUTPUT ${DBX_DYNALLOC_GEN}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen/cpu
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/patch_dyncache.py
          ${S}/cpu/dynamic_alloc_common.h ${DBX_DYNALLOC_GEN}
  DEPENDS ${S}/cpu/dynamic_alloc_common.h ${CMAKE_CURRENT_SOURCE_DIR}/patch_dyncache.py
  COMMENT "Patching dynamic_alloc_common.h with the Zephyr dual-map branch"
)
add_custom_target(dbx_dynalloc_patch DEPENDS ${DBX_DYNALLOC_GEN})
add_dependencies(app dbx_dynalloc_patch)

if(CONFIG_DOSBOX_PRESENT_SEMIHOST)
  list(APPEND DOSBOX_APP_SRC src/dosbox_present_semihost.c)
elseif(CONFIG_DOSBOX_PRESENT_DISPLAY)
  list(APPEND DOSBOX_APP_SRC src/dosbox_present_display.c)
else()
  list(APPEND DOSBOX_APP_SRC src/dosbox_present_none.c)
endif()

if(CONFIG_DOSBOX_SCRIPTED_INPUT)
  list(APPEND DOSBOX_APP_SRC src/dosbox_scripted.c)
endif()

if(CONFIG_SDL2SHIM_AUDIO_HDMI)
  list(APPEND DOSBOX_APP_SRC ${SDL2SHIM_AUDIO_HDMI_SOURCES})
else()
  list(APPEND DOSBOX_APP_SRC ${SDL2SHIM_AUDIO_NONE})
endif()
if(CONFIG_USB_DEVICE_STACK_NEXT)
  list(APPEND DOSBOX_APP_SRC ${SDL2SHIM_USB})
endif()

target_sources(app PRIVATE
  ${DBX_SOURCES}
  ${DOSBOX_APP_SRC}
  ${SDL2SHIM_CORE_SOURCES}
)

# include order: our compat dir first (config.h + header stubs), then
# the shim SDL2 headers (both <SDL2/SDL.h> and bare "SDL.h" forms),
# then the DOSBox-X tree.
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/compat
  ${SDL2SHIM_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl2shim/include/SDL2
  ${DBX}/include
  ${DBX}/vs
  ${DBX}/vs/zlib
  ${DBX}/vs/libpng
  ${DBX}
  ${CMAKE_BINARY_DIR}/gen   # patched cpu/dynamic_alloc_common.h wins over src/
  ${S}
  ${S}/libs/decoders
  ${S}/aviwriter
  ${S}/dos
  ${S}/hardware
)

# DOSBox-X TUs assume a glibc-ish environment; keep upstream's warning
# posture and give picolibc the BSD/POSIX visibility it expects.
set(DBX_FLAGS
  "-DPNG_ARM_NEON_OPT=0;-include;${CMAKE_CURRENT_SOURCE_DIR}/src/compat/config.h;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function;-Wno-missing-braces;-Wno-implicit-fallthrough;-Wno-format;-Wno-maybe-uninitialized;-Wno-sign-compare;-Wno-parentheses;-Wno-misleading-indentation;-Wno-write-strings;-Wno-class-memaccess;-Wno-deprecated-declarations;-D_DEFAULT_SOURCE;$<$<COMPILE_LANGUAGE:C>:-Wno-int-conversion>;$<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>"
)
set_source_files_properties(${DBX_SOURCES} PROPERTIES COMPILE_OPTIONS "${DBX_FLAGS}")
