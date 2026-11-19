#!/usr/bin/env bash
# Makes <PRODUCT>-macOS.pkg for each plugin. The installer puts:
#   the Audio Unit  in /Library/Audio/Plug-Ins/Components   (Logic, GarageBand, ...)
#   the VST3        in /Library/Audio/Plug-Ins/VST3         (Ableton, FL Studio, Reaper, ...)
#   the standalone  in /Applications
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/plugins.sh
version="${GITHUB_REF_NAME:-v1.0.0}"; version="${version#v}"
rm -rf stage dist && mkdir -p stage dist

for entry in $PLUGINS; do
  IFS=: read -r target product code <<< "$entry"
  art="build/$target/${target}_artefacts/Release"
  id="com.noisedivision.$(echo "$product" | tr '[:upper:]' '[:lower:]')"
  work="stage/$product"; root="$work/root"
  mkdir -p "$root/Library/Audio/Plug-Ins/VST3" "$root/Library/Audio/Plug-Ins/Components" "$root/Applications" "$work/scripts" "$work/packages"

  ditto "$art/VST3/$product.vst3"     "$root/Library/Audio/Plug-Ins/VST3/$product.vst3"
  ditto "$art/AU/$product.component"  "$root/Library/Audio/Plug-Ins/Components/$product.component"
  ditto "$art/Standalone/$product.app" "$root/Applications/$product.app"

  # Apple Silicon Macs only run signed code. Without a paid Apple Developer ID this is an
  # "ad-hoc" signature: enough for the plugins to load once the installer has run.
  for bundle in "$root/Library/Audio/Plug-Ins/VST3/$product.vst3" \
                "$root/Library/Audio/Plug-Ins/Components/$product.component" \
                "$root/Applications/$product.app"; do
    codesign --force --deep --sign - "$bundle"
  done
  echo "$product architectures:"; lipo -info "$root/Library/Audio/Plug-Ins/Components/$product.component/Contents/MacOS/$product"

  # install exactly where stated, even if the user has another copy somewhere else
  pkgbuild --analyze --root "$root" "$work/component.plist"
  count=$(/usr/libexec/PlistBuddy -c "Print" "$work/component.plist" | grep -c "RootRelativeBundlePath" || true)
  for ((i = 0; i < count; i++)); do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$work/component.plist"
  done

  cp scripts/mac-postinstall "$work/scripts/postinstall"; chmod +x "$work/scripts/postinstall"
  pkgbuild --root "$root" --install-location / --component-plist "$work/component.plist" \
           --scripts "$work/scripts" --identifier "$id" --version "$version" \
           "$work/packages/$product.pkg"

  # wrap it in an installer with a proper title
  productbuild --synthesize --package "$work/packages/$product.pkg" "$work/distribution.xml"
  sed -i '' "s|<installer-gui-script minSpecVersion=\"[0-9]*\">|&<title>$product by Noise Division</title><options customize=\"never\" require-scripts=\"false\" hostArchitectures=\"arm64,x86_64\"/>|" "$work/distribution.xml"
  productbuild --distribution "$work/distribution.xml" --package-path "$work/packages" "dist/$product-macOS.pkg"
  echo "made dist/$product-macOS.pkg"
done
