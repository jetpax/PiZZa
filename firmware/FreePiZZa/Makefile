# =============================================================================
# Free PiZZa — suspend-to-RAM bootcode.bin for Pi Zero 2 W
# =============================================================================
# Self-contained Docker build. Clones librerpi/rpi-open-firmware at a pinned
# SHA, applies the integration/ patches, compiles the vendored src/, and links
# bootcode.bin. No third-party checkout required. See README.md + src/NOTICE.md.
# =============================================================================

ROOT      := $(shell pwd)
BUILD_DIR := $(ROOT)/build
IMAGE     := freepizza-firmware

.PHONY: docker-build
docker-build:
	@echo "=== Building Free PiZZa bootcode.bin via Docker ==="
	@echo "First build takes ~30-60 min (vc4 toolchain). Subsequent builds are cached."
	@mkdir -p $(BUILD_DIR)
	docker build -t $(IMAGE) . 2>&1 | tee /tmp/freepizza-docker-build.log || \
		(echo "FAILED — see /tmp/freepizza-docker-build.log"; exit 1)
	docker run --rm -v "$(BUILD_DIR):/out" $(IMAGE)
	@echo ""
	@echo "=== Build complete ==="
	@echo "Output: $(BUILD_DIR)/bootcode.bin"

.PHONY: flash
flash:
	./flash-freepizza.sh

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned build/."

.PHONY: help
help:
	@echo "Free PiZZa firmware build"
	@echo ""
	@echo "  make docker-build   Build build/bootcode.bin via Docker (recommended)"
	@echo "  make flash          Copy build/bootcode.bin to the SD card (see flash-freepizza.sh)"
	@echo "  make clean          Remove build/ artifacts"
