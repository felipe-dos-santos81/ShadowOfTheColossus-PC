#!/usr/bin/env bash
# macOS: install a prepared KosmicKrisp driver into a destination directory (out/ by default).
#
#     tools/install_kosmickrisp.sh <source-dir | tarball> [dest]
#
# The source is either a directory holding the three files below or a .tar.gz of one (built by
# tools/package_kosmickrisp.sh). This script owns the layout contract -- the three names, an
# x86-64 driver slice -- so that build_kosmickrisp.sh, `make driver-dist` and the test all agree.
# Used by tools/build_kosmickrisp.sh to install a prebuilt driver (BB_KOSMICKRISP_TARBALL /
# BB_KOSMICKRISP_URL) or a sibling checkout's copy, skipping the 20-40 minute Mesa build.
set -euo pipefail
source=${1:?usage: install_kosmickrisp.sh <source-dir | tarball> [dest]}
dest=${2:-out}
files=(libvulkan_kosmickrisp.dylib libz.1.dylib kosmickrisp_mesa_icd.json)

command -v lipo >/dev/null || { echo 'Need lipo to check the driver slice (Xcode Command Line Tools: xcode-select --install).' >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# A source that holds the three files, directly or as the single directory a tarball usually has.
is_driver_dir() {
    local dir=$1 name
    for name in "${files[@]}"; do
        [[ -f $dir/$name ]] || return 1
    done
}

if [[ -d $source ]]; then
    from=$source
elif [[ -f $source ]]; then
    tar -xzf "$source" -C "$work"
    from=''
    for candidate in "$work" "$work"/*/; do
        if is_driver_dir "$candidate"; then from=$candidate; break; fi
    done
    [[ -n $from ]] || { echo "$source does not hold ${files[*]} (directly or one directory down)." >&2; exit 1; }
else
    echo "KosmicKrisp source not found: $source" >&2
    exit 1
fi

missing=''
for name in "${files[@]}"; do
    [[ -f $from/$name ]] || missing="${missing:+$missing }$name"
done
if [[ -n $missing ]]; then
    echo "$source is not a KosmicKrisp driver: missing $missing (expected ${files[*]})." >&2
    exit 1
fi

archs=$(lipo -archs "$from/libvulkan_kosmickrisp.dylib" 2>&1) || {
    echo "$source/libvulkan_kosmickrisp.dylib is not a Mach-O library: $archs" >&2
    exit 1
}
if ! grep -q x86_64 <<<"$archs"; then
    echo "$source has no x86_64 slice (found: $archs); the game process runs under Rosetta 2." >&2
    exit 1
fi

mkdir -p "$dest"
# -L: a driver copied out of a Mesa build tree can be (or depend on) a symlink, and out/ wants files.
for name in "${files[@]}"; do
    cp -fL "$from/$name" "$dest/$name"
done
echo "Installed x86-64 KosmicKrisp driver into $dest"
