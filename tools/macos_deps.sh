#!/usr/bin/env bash
# macos: x86_64 dependencies for the Rosetta 2 build (Apple Silicon host), built from pinned
# sources into out/deps-x86_64. No sudo, no system changes; re-running skips what is built.
#  - SDL3, fmt, Zydis, xxHash, minimal FFmpeg (static libraries), tsl-robin-map and VMA (headers)
#  - KosmicKrisp (Mesa Vulkan-on-Metal, shadPS4's fork with sparse binding): the only Vulkan
#    driver here with sparse buffers + robustness2/nullDescriptor that loads in an x86_64 process
#  - Boost: headers only, linked from Homebrew (arch independent)
# Host tools come from Homebrew (cmake, ninja, meson, nasm, pkg-config, llvm, spirv-llvm-translator).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
P=$ROOT/out/deps-x86_64
SRC=$ROOT/out/deps-src
mkdir -p "$P/include" "$SRC"
export PATH=/usr/bin:$PATH   # Apple clang for the x86_64 target
export MACOSX_DEPLOYMENT_TARGET=26.0
CMAKE_COMMON=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET -DCMAKE_INSTALL_PREFIX="$P"
    -DCMAKE_PREFIX_PATH="$P" -DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++)

done_() { [ -f "$P/.stamp-$1" ]; }
mark() { touch "$P/.stamp-$1"; echo "deps: $1 ok"; }
fetch() { # url dir
    [ -d "$SRC/$2" ] && return
    local f="$SRC/$(basename "$1")"
    [ -f "$f" ] || curl -fL --retry 3 -o "$f" "$1"
    mkdir -p "$SRC/$2.tmp" && tar -xf "$f" -C "$SRC/$2.tmp" --strip-components 1 && mv "$SRC/$2.tmp" "$SRC/$2"
}
cmake_dep() { # name srcdir [args...]
    local name=$1 dir=$2; shift 2
    cmake -S "$dir" -B "$SRC/build-$name" "${CMAKE_COMMON[@]}" "$@" >/dev/null
    cmake --build "$SRC/build-$name" >/dev/null
    cmake --install "$SRC/build-$name" >/dev/null
    mark "$name"
}

if ! done_ sdl3; then
    fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz sdl3
    cmake_dep sdl3 "$SRC/sdl3" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
fi
if ! done_ fmt; then
    fetch https://github.com/fmtlib/fmt/archive/refs/tags/12.2.0.tar.gz fmt
    cmake_dep fmt "$SRC/fmt" -DFMT_TEST=OFF -DFMT_DOC=OFF
fi
if ! done_ zydis; then
    [ -d "$SRC/zydis" ] || git clone -q --depth 1 -b v4.1.1 --recurse-submodules --shallow-submodules \
        https://github.com/zyantific/zydis "$SRC/zydis"
    cmake_dep zycore "$SRC/zydis/dependencies/zycore" -DZYCORE_BUILD_SHARED_LIB=OFF
    cmake_dep zydis "$SRC/zydis" -DZYDIS_BUILD_TOOLS=OFF -DZYDIS_BUILD_EXAMPLES=OFF -DZYDIS_BUILD_SHARED_LIB=OFF
fi
if ! done_ xxhash; then
    fetch https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.4.tar.gz xxhash
    cmake_dep xxhash "$SRC/xxhash/build/cmake" -DXXHASH_BUILD_XXHSUM=OFF
fi
if ! done_ robin-map; then
    fetch https://github.com/Tessil/robin-map/archive/refs/tags/v1.4.1.tar.gz robin-map
    cmake_dep robin-map "$SRC/robin-map"
fi
if ! done_ vma; then
    fetch https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.3.0.tar.gz vma
    cmake_dep vma "$SRC/vma"
fi
if ! done_ boost; then
    ln -sfn /opt/homebrew/include/boost "$P/include/boost"
    [ -f "$P/include/boost/version.hpp" ] || { echo "deps: brew install boost (headers)"; exit 1; }
    mark boost
fi
if ! done_ ffmpeg; then
    # AvPlayer only: the game ships no movie files, so a minimal decoder set is enough.
    fetch https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz ffmpeg
    mkdir -p "$SRC/build-ffmpeg"
    (cd "$SRC/build-ffmpeg" && "$SRC/ffmpeg/configure" --prefix="$P" --enable-cross-compile --arch=x86_64 \
        --target-os=darwin --cc="/usr/bin/clang -arch x86_64" --cxx="/usr/bin/clang++ -arch x86_64" \
        --extra-cflags="-mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" \
        --extra-ldflags="-arch x86_64 -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" \
        --enable-static --disable-shared --disable-programs --disable-doc --disable-network \
        --disable-autodetect --disable-everything --disable-x86asm \
        --enable-decoder=h264,aac --enable-demuxer=mov --enable-parser=h264,aac --enable-protocol=file \
        --enable-swscale --enable-swresample >/dev/null && make -j"$(sysctl -n hw.ncpu)" >/dev/null && make install >/dev/null)
    mark ffmpeg
fi
if ! done_ kosmickrisp; then
    KK=$SRC/kosmickrisp
    if [ ! -d "$KK" ]; then
        git clone -q --filter=blob:none https://github.com/shadexternals/mesa-kosmickrisp "$KK"
        git -C "$KK" checkout -q 52d87b35f3a4fc8aff5acb4daaa548c8ff7870f1
        git -C "$KK" submodule update -q --init --recursive --depth 1
    fi
    cmake -S "$KK" -B "$KK/build-x86_64" -G Ninja -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_BUILD_TYPE=Release >/dev/null
    ninja -C "$KK/build-x86_64" >/dev/null
    mkdir -p "$P/vulkan"
    cp "$KK/build-x86_64/outputs/libvulkan_kosmickrisp.dylib" "$KK/build-x86_64/outputs/kosmickrisp_mesa_icd.json" "$P/vulkan/"
    mark kosmickrisp
fi
echo "deps: all in $P"
