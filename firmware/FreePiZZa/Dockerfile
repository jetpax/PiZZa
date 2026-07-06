# =============================================================================
# Dockerfile — Free PiZZa suspend-to-RAM bootcode.bin (self-contained)
# =============================================================================
# Multi-stage build:
#   Stage 1 (toolchain): Build vc4-elf-gcc from source (~30min, cached)
#   Stage 2 (firmware):  Clone librerpi/rpi-open-firmware @ pinned SHA, apply
#                        our integration/ patches, compile vendored src/,
#                        link bootcode.bin.
#
# Unlike the original bhoot1234567890/vc4-suspend build, this pulls
# rpi-open-firmware directly (no submodule, no third-party checkout) and takes
# the suspend sources from ./src (vendored, fixes baked in). See src/NOTICE.md.
#
# Usage (via Makefile):
#   make docker-build          # -> build/bootcode.bin
#
# Or directly:
#   docker build -t freepizza-firmware .
#   docker run --rm -v "$PWD/build:/out" freepizza-firmware
# =============================================================================

# ---------------------------------------------------------------------------
# Stage 1: Build vc4-elf cross-compiler toolchain
# ---------------------------------------------------------------------------
# NOTE: keep this stage byte-identical to any prior vc4 toolchain build so the
# ~30 min layer cache-hits. The rpi-open-firmware pin lives in the firmware
# stage only, so it never touches this stage's cache.
FROM debian:bookworm-slim AS toolchain

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential gcc g++ \
    texinfo flex bison \
    libgmp-dev libmpfr-dev libmpc-dev \
    git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Clone vc4-toolchain (includes gcc-vc4, binutils-vc4, newlib-vc4 submodules)
WORKDIR /build
RUN git clone --depth 1 https://github.com/itszor/vc4-toolchain.git && \
    cd vc4-toolchain && \
    git submodule update --init --recursive --depth 1

# Build the toolchain with Linux-appropriate flags.
# LC_ALL=C is needed for deterministic sort order in GCC option generation.
WORKDIR /build/vc4-toolchain
ENV LC_ALL=C
RUN sed -i 's/-j 2/-j $(nproc)/' build-all.sh && \
    sed -i 's/--enable-sjlj-exceptions$/--enable-sjlj-exceptions --disable-werror --disable-bootstrap/' build-all.sh && \
    (./build-all.sh > /tmp/toolchain.log 2>&1 || \
     (echo "=== TOOLCHAIN BUILD FAILED ===" && tail -80 /tmp/toolchain.log && exit 1)) && \
    echo "Toolchain build complete" && \
    ls -la prefix/bin/vc4-elf-gcc && \
    rm -f /tmp/toolchain.log

# ---------------------------------------------------------------------------
# Stage 2: Build firmware
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS firmware
# Pinned rpi-open-firmware commit (was the vc4-suspend upstream submodule pin).
ARG RPI_OPEN_FW_SHA=2725d9447c4305731f8d78f5effcbd70abff8d58

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-dev \
    libstdc++-arm-none-eabi-newlib \
    git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# gsed compatibility: upstream Makefiles use gsed (macOS habit).
# On Linux, GNU sed IS sed, so symlink.
RUN ln -s /usr/bin/sed /usr/local/bin/gsed

# Copy vc4 toolchain from stage 1.
# The toolchain was configured with --prefix=/build/vc4-toolchain/prefix,
# so GCC looks for its sysroot (newlib libc/libgloss) at that path.
# We symlink so it resolves correctly at the new location.
COPY --from=toolchain /build/vc4-toolchain/prefix /opt/vc4
RUN mkdir -p /build/vc4-toolchain && ln -s /opt/vc4 /build/vc4-toolchain/prefix
ENV PATH="/opt/vc4/bin:${PATH}"

WORKDIR /firmware

# --- Fetch rpi-open-firmware at the pinned SHA and apply integration patches ---
# This replaces the old "COPY upstream/ upstream/" submodule copy. We own the
# integration patches (romstage hook, Makefile SUSPEND_OBJ link, trap.c GPIO
# wake handler); the suspend sources themselves are vendored under src/.
RUN git clone https://github.com/librerpi/rpi-open-firmware.git upstream && \
    git -C upstream checkout "${RPI_OPEN_FW_SHA}"
COPY integration/ integration/
RUN git -C upstream apply --ignore-whitespace \
      ../integration/0001-romstage-suspend-hooks.patch \
      ../integration/0002-firmware-makefile-suspend-obj.patch \
      ../integration/0005-trap-gpio0-wake-handler.patch && \
    echo 'integration patches applied'

# Vendored suspend sources (fixes baked in — see src/NOTICE.md).
COPY src/ src/

# --- Patch upstream Makefiles for the Docker build ---
# 1) Use -include (dash) so missing .d files don't error on clean builds
# 2) Add include/library paths for our installed deps
# 3) Replace -lstdc++ with full path (linker won't find it with -L alone)
# 4) Fix case sensitivity: otp_asm.S -> otp_asm.s (file is lowercase on disk)
# 5) Add a build rule for lowercase .s assembly files
# The SUSPEND_OBJ line is already provided by integration patch 0002 (with
# uart_wrappers.o), so the old sed that appended it is intentionally dropped.
RUN sed -i \
      -e 's/^include \$(OBJ/-include $(OBJ/' \
      -e 's|-I../ -I./|-I../ -I/opt/deps/arm/include -I./|' \
      -e 's|LINKFLAGS = |LINKFLAGS = -L/opt/deps/arm/lib |' \
      -e 's/-lstdc++/\/usr\/lib\/arm-none-eabi\/lib\/arm\/v5te\/softfp\/libstdc++.a/' \
      upstream/arm_chainloader/Makefile && \
    sed -i \
      -e 's/^include \$(BASE_OBJ/-include $(BASE_OBJ/' \
      -e 's|-I./vc4_include/ -I./|-I./vc4_include/ -I/opt/deps/vc4/include -I./|' \
      -e 's|LINKFLAGS = |LINKFLAGS = -L/opt/deps/vc4/lib |' \
      -e 's/otp_asm\.S/otp_asm.s/' \
      upstream/firmware/Makefile && \
    printf 'build/%%.o: %%.s\n\t$(CREATE_SUBDIR)\n\t@echo CC $@\n\t$(CC) $(ASFLAGS) $< -o $@\n' >> upstream/firmware/Makefile && \
    echo 'Makefiles patched'

# --- Build libcommon.a (vc4, for firmware) ---
RUN cd upstream/common && \
    (CC=vc4-elf-gcc CXX=vc4-elf-g++ AR=vc4-elf-ar BAREMETAL=1 make libcommon.a > /tmp/build.log 2>&1 || \
     (echo '=== libcommon vc4 FAILED ===' && tail -20 /tmp/build.log && exit 1)) && \
    mkdir -p /opt/deps/vc4/include /opt/deps/vc4/lib && \
    cp libcommon.a /opt/deps/vc4/lib/ && \
    cp -r include/* /opt/deps/vc4/include/ && \
    cp hardware.h /opt/deps/vc4/include/ && \
    cp -r broadcom /opt/deps/vc4/include/ && \
    cp -r ../notc/include/* /opt/deps/vc4/include/ && \
    echo 'libcommon (vc4) OK'

# --- Build libtlsf.a (arm) FIRST — needed by libcommon ---
RUN mkdir -p /opt/deps/arm/include/tlsf /opt/deps/arm/lib && \
    cd upstream/tlsf && \
    (CC=arm-none-eabi-gcc AR=arm-none-eabi-ar make libtlsf.a > /tmp/build.log 2>&1 || \
     (echo '=== libtlsf FAILED ===' && tail -20 /tmp/build.log && exit 1)) && \
    cp libtlsf.a /opt/deps/arm/lib/ && \
    cp tlsf.h /opt/deps/arm/include/tlsf/ && \
    echo 'libtlsf OK'

# --- Build libnotc.a (arm) ---
RUN cd upstream/notc && \
    (CC=arm-none-eabi-gcc AR=arm-none-eabi-ar make libnotc.a > /tmp/build.log 2>&1 || \
     (echo '=== libnotc FAILED ===' && tail -20 /tmp/build.log && exit 1)) && \
    cp libnotc.a /opt/deps/arm/lib/ && \
    echo 'libnotc OK'

# --- Build libcommon.a (arm, for chainloader) ---
# Depends on tlsf headers, which are now installed.
RUN cd upstream/common && \
    sed -i 's|^CFLAGS = -Ilib|CFLAGS = -I/opt/deps/arm/include -Ilib|' Makefile && \
    rm -f *.o libcommon.a && \
    (CC=arm-none-eabi-gcc CXX=arm-none-eabi-g++ AR=arm-none-eabi-ar \
     BAREMETAL=1 targetArch=arm32 make libcommon.a > /tmp/build.log 2>&1 || \
     (echo '=== libcommon arm FAILED ===' && tail -20 /tmp/build.log && exit 1)) && \
    cp libcommon.a /opt/deps/arm/lib/ && \
    cp -r include/* /opt/deps/arm/include/ && \
    cp hardware.h /opt/deps/arm/include/ && \
    cp -r broadcom /opt/deps/arm/include/ && \
    echo 'libcommon (arm) OK'

# --- Build our suspend objects (vendored src/) ---
RUN mkdir -p build && \
    vc4-elf-gcc -nostdlib -D__VIDEOCORE4__ \
      -Iupstream/vc4_include -Iupstream/firmware -Isrc \
      -I/opt/deps/vc4/include -Iupstream/notc/include \
      -std=c11 -Wall -Werror -O2 -fno-builtin \
      -c src/suspend.c -o build/suspend.o && \
    vc4-elf-gcc -D__VIDEOCORE4__ -Iupstream/vc4_include \
      -c src/resume.S -o build/resume.o && \
    vc4-elf-gcc -nostdlib -D__VIDEOCORE4__ \
      -Iupstream/vc4_include -Iupstream/firmware -Isrc \
      -I/opt/deps/vc4/include -Iupstream/notc/include \
      -std=c11 -Wall -Werror -O2 -fno-builtin \
      -c src/boot_suspend_hook.c -o build/boot_suspend_hook.o && \
    vc4-elf-gcc -nostdlib -D__VIDEOCORE4__ \
      -Iupstream/vc4_include -Iupstream/firmware -Isrc \
      -I/opt/deps/vc4/include -Iupstream/notc/include \
      -std=c11 -Wall -Werror -O2 -fno-builtin \
      -c src/uart_wrappers.c -o build/uart_wrappers.o && \
    echo 'suspend objects OK'

# --- Build arm_chainloader ---
RUN ls -la /usr/lib/arm-none-eabi/lib/arm/v5te/softfp/libstdc++.a && \
    echo 'libstdc++.a found'
RUN cd upstream/arm_chainloader && \
    (CC=arm-none-eabi-gcc \
     CXX=arm-none-eabi-g++ \
     AS=arm-none-eabi-as \
     AR=arm-none-eabi-ar \
     OBJCOPY=arm-none-eabi-objcopy \
     make build/arm_chainloader.bin > /tmp/build.log 2>&1 || \
     (echo '=== ARM CHAINLOADER FAILED ===' && tail -30 /tmp/build.log && exit 1)) && \
    echo 'arm_chainloader OK'

# --- Build firmware -> bootcode.bin ---
RUN cd upstream/firmware && \
    make clean && \
    mkdir -p arm_chainloader/build && \
    cp ../arm_chainloader/build/arm_chainloader.bin arm_chainloader/build/ && \
    (CC=vc4-elf-gcc \
     CXX=vc4-elf-g++ \
     AR=vc4-elf-ar \
     OBJCOPY=vc4-elf-objcopy \
     make build/bootcode.bin > /tmp/build.log 2>&1 || \
     (echo '=== FIRMWARE BUILD FAILED ===' && tail -40 /tmp/build.log && exit 1)) && \
    echo "firmware OK — bootcode.bin: $(wc -c < build/bootcode.bin) bytes"

# Default: copy bootcode.bin to the mounted volume.
CMD ["cp", "/firmware/upstream/firmware/build/bootcode.bin", "/out/bootcode.bin"]
