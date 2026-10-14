#!/usr/bin/env bash
# macOS: builds Mesa's KosmicKrisp Vulkan driver for x86-64 (the game process runs under Rosetta 2)
# and puts it next to the executables in out/, where sotc.py and run.sh prefer it over MoltenVK.
# MoltenVK does not expose buffer device address (vkGetBufferDeviceAddress returns 0 and the
# renderer stops in buffer_cache/buffer.cpp), and the LunarG SDK ships KosmicKrisp as arm64 only.
# shadPS4's fork of Mesa (shadexternals/mesa-kosmickrisp) carries a CMake wrapper that builds the
# host tools and then cross-compiles the driver; 20-40 minutes.
#
# That build is skipped when a prebuilt driver is available:
#   BB_KOSMICKRISP_TARBALL=<file.tar.gz>   a local package (see tools/package_kosmickrisp.sh)
#   BB_KOSMICKRISP_URL=<url>               a release asset to fetch
#   BB_KOSMICKRISP_SHA256=<sum>            optional: verify the fetched tarball against it
# and, as before, a sibling bbport checkout's copy is reused when it is there.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
[[ $(uname -s) == Darwin ]] || { echo 'KosmicKrisp is macOS only.' >&2; exit 1; }
mkdir -p out
installer=$PWD/tools/install_kosmickrisp.sh

# Prebuilt driver: a local tarball, or one fetched from a URL.
tarball=${BB_KOSMICKRISP_TARBALL:-}
if [[ -z $tarball && -n ${BB_KOSMICKRISP_URL:-} ]]; then
    tarball=out/kosmickrisp-prebuilt.tar.gz
    echo "Fetching $BB_KOSMICKRISP_URL"
    curl -fL --retry 3 -o "$tarball" "$BB_KOSMICKRISP_URL"
fi
if [[ -n $tarball ]]; then
    if [[ -n ${BB_KOSMICKRISP_SHA256:-} ]]; then
        actual=$(shasum -a 256 "$tarball" | awk '{print $1}')
        [[ $actual == "${BB_KOSMICKRISP_SHA256}" ]] || {
            echo "$tarball does not match BB_KOSMICKRISP_SHA256 (got $actual)." >&2; exit 1; }
    fi
    bash "$installer" "$tarball" out
    echo 'Skipped the Mesa build (prebuilt driver).'
    exit 0
fi
# Reuse a locally built KosmicKrisp driver if one already exists in a sibling bbport checkout.
# Silent: an unusable cache is not an error, the build below is the fallback.
for cached in ../../theirs/bloodborne_pc/out; do
    if bash "$installer" "$cached" out 2>/dev/null; then
        echo "Copied x86-64 KosmicKrisp driver into $PWD/out/libvulkan_kosmickrisp.dylib"
        exit 0
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
zlib=$(find out/kosmickrisp-build/externals/mesa/build-target -name libz.1.dylib | head -1)
[[ -n $zlib ]] || { echo 'libz.1.dylib not found in the Mesa build tree; the driver would not load.' >&2; exit 1; }
# Stage the three files and install them through the same check every other source goes through
# (the slice check in particular: CMAKE_OSX_ARCHITECTURES can silently not be honoured).
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
cp -L "$zlib" "$stage/libz.1.dylib"
cp out/kosmickrisp-build/outputs/libvulkan_kosmickrisp.dylib "$stage/libvulkan_kosmickrisp.dylib"
cp out/kosmickrisp-build/outputs/kosmickrisp_mesa_icd.json "$stage/kosmickrisp_mesa_icd.json"
bash "$installer" "$stage" out
echo "Built $PWD/out/libvulkan_kosmickrisp.dylib (sotc.py and run.sh use it when present)"
