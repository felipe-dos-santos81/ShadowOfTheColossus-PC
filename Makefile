# Makefile for Shadow of the Colossus — PC (native port)
# Targets follow the port's workflow:
#   deps → build → play → verify → package → clean
SERVICE = Shadow of the Colossus

# Variables
PYTHON   ?= python3
OUT       = out
BUILD     = bash build.sh
TESTS     = $(PYTHON) -m unittest discover -s tests
GATE      = $(PYTHON) tools/macos_check.py
OS        = $(shell uname -s)
seconds  ?= 30

.PHONY: help deps build test driver driver-dist verify play launcher package clean distclean

# macOS-only targets stop with one clear message anywhere else.
define require-macos
@[ "$(OS)" = Darwin ] || { echo "This target needs macOS on Apple Silicon."; exit 1; }
endef

# ── Environment ──────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE} — PC (native port)\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'

deps: ## Install the build dependencies (macOS: brew; MSYS2 CLANG64: pacman)
	@if [ "$(OS)" = Darwin ]; then \
		brew install cmake ninja pkgconf glslang nasm python; \
	elif command -v pacman >/dev/null 2>&1; then \
		pacman -S --needed --noconfirm git patch \
			mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
			mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
			mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis} \
			mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}; \
	else \
		echo "Install cmake, ninja, pkgconf, glslang, nasm and Python 3 with your package manager."; \
	fi

# ── Build ────────────────────────────────────────────────────────────────────

build: ## Build the loader and the GPU library (out/bb-probe, out/bb-gpu-capabilities)
	$(BUILD)

test: ## Build every target, then run the C and Python test suites
	$(BUILD) --test
	$(TESTS)

driver: ## Get the x86-64 KosmicKrisp driver (macOS: prebuilt if BB_KOSMICKRISP_TARBALL/URL, else 20-40 min build)
	$(require-macos)
	bash tools/build_kosmickrisp.sh

driver-dist: driver ## Package the driver as a release asset (dist/kosmickrisp-macos-x86_64.tar.gz)
	$(require-macos)
	bash tools/package_kosmickrisp.sh

package: ## Build the Windows release zip (dist/SotC-PC.zip); needs a Windows Python
	@command -v pacman >/dev/null 2>&1 || { echo "Packaging needs MSYS2 CLANG64 on Windows."; exit 1; }
	bash packaging/windows/package_sotc.sh

# ── Run ──────────────────────────────────────────────────────────────────────

play: ## Prepare the game image and play (python3 sotc.py)
	$(PYTHON) sotc.py

launcher: ## Open the settings launcher (macOS/POSIX; on Windows use SotC.exe)
	bash launcher/sotc-launcher.sh

# ── Verify ───────────────────────────────────────────────────────────────────

verify: build ## Run the macOS acceptance gate over a live run (make verify [seconds=30])
	$(require-macos)
	$(GATE) --seconds $(seconds)

# ── Cleanup ──────────────────────────────────────────────────────────────────

clean: ## Remove build outputs; keeps saves (user/), the game and the vcpkg cache
	rm -rf $(OUT)/gpu pgo
	rm -f $(OUT)/bb-probe $(OUT)/bb-probe.exe $(OUT)/bb-gpu-capabilities \
		$(OUT)/bb-gpu-capabilities.exe $(OUT)/bb-play.exe $(OUT)/gpu-build.log
	rm -f $(OUT)/libvulkan*.dylib $(OUT)/libMoltenVK.dylib $(OUT)/libz.1.dylib
	rm -f $(OUT)/MoltenVK_icd.json $(OUT)/kosmickrisp_mesa_icd.json
	@find . -type d -name '__pycache__' -exec rm -rf {} + 2>/dev/null || true
	@echo "Removed build outputs. Kept user/ (saves), out/CUSA08809 and out/vcpkg_installed."

distclean: clean ## Also remove the vcpkg cache, the drivers, the extracted game and the package
	rm -rf $(OUT)/vcpkg $(OUT)/vcpkg_installed $(OUT)/kosmickrisp-build $(OUT)/mesa-kosmickrisp
	rm -rf $(OUT)/mesa-venv $(OUT)/moltenvk $(OUT)/pkg-extract $(OUT)/CUSA08809 $(OUT)/pkgtool
	rm -rf dist
	@echo "Removed everything generated. Kept user/ (saves)."
