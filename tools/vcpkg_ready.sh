#!/usr/bin/env bash
# macOS: is the vcpkg x64-osx install complete? A directory can survive an interrupted install
# while the static libraries the build links are still missing, so readiness is decided by the
# artifacts and not by the directory. build.sh uses this to decide whether to let vcpkg install
# again (and to keep the manifest install enabled when it must).
set -euo pipefail
dir=${1:?usage: vcpkg_ready.sh <installed-triplet-dir>}
missing=''
for name in libSDL3.a libZydis.a; do
    [[ -f $dir/lib/$name ]] || missing="${missing:+$missing }$name"
done
if [[ -n $missing ]]; then
    printf 'vcpkg install incomplete in %s: %s missing\n' "$dir" "$missing" >&2
    exit 1
fi
exit 0
