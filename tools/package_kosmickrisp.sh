#!/usr/bin/env bash
# macOS: package the built KosmicKrisp driver as a release asset.
#
#     tools/package_kosmickrisp.sh [out-dir] [dist-dir]
#
# Writes dist/kosmickrisp-macos-x86_64.tar.gz and its .sha256 from the driver in out/. Attach both
# to a release, and onboarding stops needing Mesa: whoever runs `BB_KOSMICKRISP_URL=<url> make
# driver` (with BB_KOSMICKRISP_SHA256 from the .sha256 file) gets the driver in seconds instead of
# building it for 20-40 minutes. Mesa is MIT-licensed, so redistributing the driver is fine.
#
# The set is staged through tools/install_kosmickrisp.sh, which owns the layout contract (the three
# names, the x86-64 slice), so the archive only ever holds files that the installer accepts: what
# is shipped is what lands. Do not restate that contract here.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
out=${1:-out}
dist=${2:-dist}

# The installer's message names the missing file; this says what to do about it.
[[ -f $out/libvulkan_kosmickrisp.dylib ]] || { echo "$out/libvulkan_kosmickrisp.dylib is missing: run 'make driver' first." >&2; exit 1; }

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
bash tools/install_kosmickrisp.sh "$out" "$stage" >/dev/null

mkdir -p "$dist"
asset=$dist/kosmickrisp-macos-x86_64.tar.gz
rm -f "$asset" "$asset.sha256"
tar -czf "$asset" -C "$stage" .
shasum -a 256 "$asset" | tee "$asset.sha256" >/dev/null
echo "Wrote $PWD/$asset"
echo "Attach it and its .sha256 to a release, then: BB_KOSMICKRISP_URL=<url> BB_KOSMICKRISP_SHA256=<sum> make driver"
