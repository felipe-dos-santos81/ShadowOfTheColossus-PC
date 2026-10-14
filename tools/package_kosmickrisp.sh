#!/usr/bin/env bash
# macOS: package the built KosmicKrisp driver as a release asset.
#
#     tools/package_kosmickrisp.sh [out-dir] [dist-dir]
#
# Writes dist/kosmickrisp-macos-x86_64.tar.gz and its .sha256 from the driver in out/. Attach both
# to a release, and onboarding stops needing Mesa: whoever runs `BB_KOSMICKRISP_URL=<url> make
# driver` (with BB_KOSMICKRISP_SHA256 from the .sha256 file) gets the driver in seconds instead of
# building it for 20-40 minutes. Mesa is MIT-licensed, so redistributing the driver is fine.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
out=${1:-out}
dist=${2:-dist}
files=(libvulkan_kosmickrisp.dylib libz.1.dylib kosmickrisp_mesa_icd.json)

for name in "${files[@]}"; do
    [[ -f $out/$name ]] || { echo "$out/$name is missing: run 'make driver' first." >&2; exit 1; }
done
archs=$(lipo -archs "$out/libvulkan_kosmickrisp.dylib")
grep -q x86_64 <<<"$archs" || { echo "$out/libvulkan_kosmickrisp.dylib has no x86_64 slice (found: $archs)." >&2; exit 1; }

mkdir -p "$dist"
asset=$dist/kosmickrisp-macos-x86_64.tar.gz
rm -f "$asset" "$asset.sha256"
tar -czf "$asset" -C "$out" "${files[@]}"
shasum -a 256 "$asset" | tee "$asset.sha256" >/dev/null
echo "Wrote $PWD/$asset"
echo "Attach it and its .sha256 to a release, then: BB_KOSMICKRISP_URL=<url> BB_KOSMICKRISP_SHA256=<sum> make driver"
