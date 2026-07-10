# DOSBox-X port -- disk images

The FAT disk images baked into the firmware are **not committed** (see
`.gitignore`): `doom.img` carries id Software's `DOOM.EXE`, which is
copyrighted and not redistributable. Build them locally with `mtools`
(`brew install mtools`).

`CMakeLists.txt` bakes `assets/${DBX_DISK}` (default `doom.img`) into
rodata; the port stages it to RAM and `IMGMOUNT`s it at boot.

## doom.img -- the DOOM go/no-go image (default)

Supply your own DOS DOOM binary and the shareware IWAD. `DOOM.EXE` (the
registered v1.9 build) has DOS/4GW bound in, so no separate extender is
needed. `doom1.wad` is the freely redistributable shareware IWAD.

`DEFAULT.CFG` preselects Sound Blaster SFX + SB FM (OPL) music at the
standard A220/I7/D1 so vanilla DOOM makes sound without running SETUP,
and enables the DOS mouse driver (`use_mouse 1`) so a BT mouse
(CONFIG_BTINPUT / apps/lib/btinput) works in-game; `SETUP.EXE` is
included for interactive reconfiguration.

```sh
printf 'snd_channels 3\r\nsnd_musicdevice 3\r\nsnd_sfxdevice 3\r\nsnd_sbport 544\r\nsnd_sbirq 7\r\nsnd_sbdma 1\r\nsnd_mport 816\r\nuse_mouse 1\r\nmouse_sensitivity 5\r\nmouseb_fire 0\r\nmouseb_strafe 1\r\nmouseb_forward 2\r\n' > default.cfg
dd if=/dev/zero of=doom.img bs=1024 count=6552
mformat -i doom.img -h 16 -s 63 -t 13 ::
mcopy -i doom.img /path/to/DOOM.EXE  ::DOOM.EXE
mcopy -i doom.img /path/to/doom1.wad ::DOOM1.WAD
mcopy -i doom.img /path/to/SETUP.EXE ::SETUP.EXE
mcopy -i doom.img default.cfg ::DEFAULT.CFG
rm default.cfg
```

## floppy.img -- the storage smoke-test image

A 1.44 MB FAT12 floppy with a 41-byte hand-assembled `HELLO.COM` (INT 21h
AH=09 print-string). Build with `-DDBX_DISK=floppy.img`.

```sh
python3 -c 'open("hello.com","wb").write(bytes([0xB4,0x09,0xBA,0x08,0x01,0xCD,0x21,0xC3])+b"PiZZa DOSBox-X storage leg OK!\r\n$")'
dd if=/dev/zero of=floppy.img bs=1024 count=1440
mformat -i floppy.img -f 1440 ::
mcopy -i floppy.img hello.com ::HELLO.COM
rm hello.com
```
