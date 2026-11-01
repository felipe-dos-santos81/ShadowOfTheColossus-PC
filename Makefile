# Makefile for Shadow of the Colossus PC — native PS4 port (Windows / macOS under Rosetta 2)
# Typical flow:  deps → build → run   (game dump: SOTC_GAME_DIR / sotc.ini, default ../CUSA08809)
SERVICE = Shadow of the Colossus PC

# Variables
PYTHON = python3
EXE = $(if $(filter Windows_NT,$(OS)),.exe)
# GAME_DIR=... overrides the game dump only when given (else sotc.py: env, sotc.ini, defaults)
RUN = $(if $(GAME_DIR),SOTC_GAME_DIR="$(abspath $(GAME_DIR))" )$(PYTHON) sotc.py

.PHONY: help deps build rebuild run test clean clean-all

# ── Build ────────────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE}\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'

deps: ## [macOS] Build x86_64 deps + KosmicKrisp Vulkan driver into out/deps-x86_64 (skips what is built)
	bash tools/macos_deps.sh

build: ## Build bb-probe (CMake + Ninja; macOS: x86_64 for Rosetta 2, runs deps first)
	bash build.sh

rebuild: ## Incremental rebuild of bb-probe after the first build
ifeq ($(EXE),.exe)
	ninja -C out/gpu bb-probe bb-gpu-capabilities && cp out/gpu/bb-probe.exe out/gpu/bb-gpu-capabilities.exe out/
else ifeq ($(shell uname -s),Darwin)
	ninja -C out/gpu bb-probe && cp out/gpu/bb-probe out/
else
	bash build.sh # Linux: build.sh links bb-probe itself (no CMake target)
endif

# ── Run ──────────────────────────────────────────────────────────────────────

run: ## Prepare the image if needed and start the game (windowed, free cursor on macOS; usage: make run [GAME_DIR=...])
	$(RUN)

# ── Development ──────────────────────────────────────────────────────────────

test: ## Run the Python unit tests
	$(PYTHON) -m unittest discover -s tests

clean: ## Remove build outputs and the prepared image (keeps out/deps-* and user/)
	rm -rf out/gpu out/bb-probe$(EXE) out/*.bin out/*.dylib out/*.json out/prepared.txt out/eboot.elf

clean-all: clean ## Also remove the built dependencies (out/deps-x86_64, out/deps-src)
	rm -rf out/deps-x86_64 out/deps-src
