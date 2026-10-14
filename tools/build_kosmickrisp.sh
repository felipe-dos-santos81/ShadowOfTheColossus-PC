#!/usr/bin/env bash
# macOS: builds Mesa's KosmicKrisp Vulkan driver for x86-64 (the game process runs under Rosetta 2)
# and puts it next to the executables in out/, where sotc.py and run.sh prefer it over MoltenVK.
# MoltenVK has no sparse buffer binding, which the buffer cache's arenas need; the LunarG SDK ships
# KosmicKrisp as arm64 only. shadPS4's fork of Mesa (shadexternals/mesa-kosmickrisp) carries a CMake
# wrapper that builds the host tools and then cross-compiles the driver; 20-40 minutes.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
[[ $(uname -s) == Darwin ]] || { echo 'KosmicKrisp is macOS only.' >&2; exit 1; }
mkdir -p out
# Reuse a locally built KosmicKrisp driver if one already exists in a sibling bbport checkout.
for cached in ../../theirs/bloodborne_pc/out; do
    if [[ -f $cached/libvulkan_kosmickrisp.dylib && -f $cached/libz.1.dylib && -f $cached/kosmickrisp_mesa_icd.json ]]; then
        if lipo -archs "$cached/libvulkan_kosmickrisp.dylib" | grep -q x86_64; then
            cp -L "$cached/libz.1.dylib" out/libz.1.dylib
            cp "$cached/libvulkan_kosmickrisp.dylib" out/libvulkan_kosmickrisp.dylib
            cp "$cached/kosmickrisp_mesa_icd.json" out/kosmickrisp_mesa_icd.json
            echo "Copied x86-64 KosmicKrisp driver into $PWD/out/libvulkan_kosmickrisp.dylib"
            exit 0
        fi
    fi
done
for tool in cmake ninja pkg-config git; do
    command -v "$tool" >/dev/null || { echo "Need $tool (brew install cmake ninja pkgconf)." >&2; exit 1; }
done
for formula in llvm spirv-tools spirv-llvm-translator libclc bison flex; do
    [[ -d /opt/homebrew/opt/$formula ]] || { echo "Need Homebrew $formula (brew install llvm spirv-tools spirv-llvm-translator libclc bison flex)." >&2; exit 1; }
done
if [[ ! -x out/mesa-venv/bin/meson ]]; then
    python3 -m venv out/mesa-venv
    out/mesa-venv/bin/pip install -q meson mako pyyaml packaging
fi
KOSMICKRISP_COMMIT=${KOSMICKRISP_COMMIT:-af7dd0a35c071cecbfff32662675641d19e8e4db}
fork=out/mesa-kosmickrisp
if [[ ! -d $fork/.git ]]; then
    git init -q "$fork"
    git -C "$fork" remote add origin https://github.com/shadexternals/mesa-kosmickrisp
fi
if [[ $(git -C "$fork" rev-parse HEAD 2>/dev/null || true) != "$KOSMICKRISP_COMMIT" ]]; then
    git -C "$fork" fetch -q --depth 1 origin "$KOSMICKRISP_COMMIT"
    git -C "$fork" checkout -q FETCH_HEAD
fi
git -C "$fork" submodule update --init --depth 1 externals/mesa
export PATH=$PWD/out/mesa-venv/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:$PATH
cmake -S out/mesa-kosmickrisp -B out/kosmickrisp-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
ninja -C out/kosmickrisp-build kosmickrisp
driver=out/kosmickrisp-build/outputs/libvulkan_kosmickrisp.dylib
lipo -archs "$driver" | grep -q x86_64 || { echo "$driver has no x86_64 slice (CMAKE_OSX_ARCHITECTURES was not honoured)." >&2; exit 1; }
zlib=$(find out/kosmickrisp-build/externals/mesa/build-target -name libz.1.dylib | head -1)
[[ -n $zlib ]] || { echo 'libz.1.dylib not found in the Mesa build tree; the driver would not load.' >&2; exit 1; }
cp -L "$zlib" out/libz.1.dylib
cp "$driver" out/libvulkan_kosmickrisp.dylib
cp out/kosmickrisp-build/outputs/kosmickrisp_mesa_icd.json out/kosmickrisp_mesa_icd.json
echo "Built $PWD/out/libvulkan_kosmickrisp.dylib (sotc.py and run.sh use it when present)"
