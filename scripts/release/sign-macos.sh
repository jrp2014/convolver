#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Identities already exist in the release keychain; no private keys/passwords here.
set -eu
if [ "$#" -ne 3 ]; then
    echo "Usage: sign-macos.sh STAGE NEW_WORK_DIRECTORY OUTPUT.pkg" >&2
    exit 2
fi
: "${CONVOLVER_APPLICATION_IDENTITY:?Developer ID Application identity required}"
: "${CONVOLVER_INSTALLER_IDENTITY:?Developer ID Installer identity required}"
: "${CONVOLVER_NOTARY_PROFILE:?notarytool keychain profile required}"
case "${GITHUB_ACTIONS:-false}:${GITHUB_REF:-}" in
    true:refs/tags/v*) ;;
    true:*) echo "CI signing requires a trusted release tag" >&2; exit 1 ;;
esac
stage=$(cd "$1" && pwd)
work=$2
output=$3
script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
repo=$(cd "$script_dir/../.." && pwd)
[ ! -e "$work" ] && [ ! -e "$output" ] || { echo "Use new work/output paths" >&2; exit 1; }
[ ! -e "$stage/artifact-manifest.json" ] || { echo "Sign before sealing the final manifest" >&2; exit 1; }
# Current release payload is static: one native bundle executable and one tool.
bundle="$stage/VST3/Convolver.vst3"
codesign --force --options runtime --timestamp --sign "$CONVOLVER_APPLICATION_IDENTITY" "$bundle/Contents/MacOS/Convolver"
codesign --force --options runtime --timestamp --sign "$CONVOLVER_APPLICATION_IDENTITY" "$stage/bin/convolver-vst3-preset"
codesign --force --options runtime --timestamp --sign "$CONVOLVER_APPLICATION_IDENTITY" "$bundle"
codesign --verify --deep --strict "$bundle"
codesign --verify --strict "$stage/bin/convolver-vst3-preset"
python3 "$script_dir/installers.py" --stage "$stage" --output "$work" --system Darwin
version=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$repo/packaging/release-policy.json")
pkgbuild --root "$work/plugin-payload" --component-plist "$work/component.plist" \
    --identifier org.convolver.pkg.vst3 --version "$version" \
    --install-location /Library/Audio/Plug-Ins/VST3 "$work/Convolver-vst3.pkg"
pkgbuild --root "$work/tools-payload" --identifier org.convolver.pkg.tools --version "$version" \
    --install-location '/Library/Application Support/Convolver' "$work/Convolver-tools.pkg"
productbuild --distribution "$repo/packaging/macos/Distribution.xml" --package-path "$work" \
    --sign "$CONVOLVER_INSTALLER_IDENTITY" "$output"
xcrun notarytool submit "$output" --keychain-profile "$CONVOLVER_NOTARY_PROFILE" --wait --output-format json > "$work/notary-result.json"
python3 -c 'import json,sys; r=json.load(open(sys.argv[1])); sys.exit(0 if r.get("status")=="Accepted" else 1)' "$work/notary-result.json"
xcrun stapler staple "$output"
xcrun stapler validate "$output"
pkgutil --check-signature "$output"
spctl --assess --type install --verbose=2 "$output"
# Hash and test these exact bytes next. No publication is performed here.
