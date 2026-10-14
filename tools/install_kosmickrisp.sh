#!/usr/bin/env bash
# macOS: install a prepared KosmicKrisp driver into a destination directory.
#
#     tools/install_kosmickrisp.sh <source-dir | tarball> [dest]
#
# The source is either a directory holding the three files below or a .tar.gz of one (built by
# tools/package_kosmickrisp.sh). This script owns the layout contract -- the three names, an
# x86-64 driver slice -- so build_kosmickrisp.sh, `make driver-dist` and the test all agree on it
# instead of each repeating the file list and the slice check. Used by
# tools/build_kosmickrisp.sh to install a prebuilt driver (BB_KOSMICKRISP_TARBALL /
# BB_KOSMICKRISP_URL) or a sibling checkout's copy, skipping the 20-40 minute Mesa build.
set -euo pipefail
source=${1:?usage: install_kosmickrisp.sh <source-dir | tarball> [dest]}
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
# An explicit dest is relative to the caller, like the source is; the default is the port's own
# out/, so a bare "install this" cannot land a driver in whatever directory happens to be current.
dest=${2:-$root/out}
files=(libvulkan_kosmickrisp.dylib libz.1.dylib kosmickrisp_mesa_icd.json)

command -v lipo >/dev/null || { echo 'Need lipo to check the driver slice (Xcode Command Line Tools: xcode-select --install).' >&2; exit 1; }

# The names `dir` lacks, space-separated; empty when it holds all three. This is the one place
# that decides "is this a KosmicKrisp driver", for the tarball's candidates and the final answer.
missing_names() {
    local dir=$1 name found=''
    for name in "${files[@]}"; do
        [[ -f $dir/$name ]] || found="${found:+$found }$name"
    done
    printf '%s' "$found"
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [[ -d $source ]]; then
    from=$source
elif [[ -f $source ]]; then
    tar -xzf "$source" -C "$work"
    from=''
    # A tarball may hold the files directly or in one top-level directory; both are accepted.
    for candidate in "$work" "$work"/*/; do
        if [[ -z $(missing_names "$candidate") ]]; then from=$candidate; break; fi
    done
    [[ -n $from ]] || { echo "$source does not hold ${files[*]} (directly or one directory down)." >&2; exit 1; }
else
    echo "KosmicKrisp source not found: $source" >&2
    exit 1
fi

missing=$(missing_names "$from")
if [[ -n $missing ]]; then
    echo "$source is not a KosmicKrisp driver (${files[*]}): missing $missing." >&2
    exit 1
fi

archs=$(lipo -archs "$from/libvulkan_kosmickrisp.dylib" 2>&1) || {
    echo "the driver in $source is not a Mach-O library: $archs" >&2
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
