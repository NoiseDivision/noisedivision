#!/usr/bin/env bash
# Makes <PRODUCT>-macOS.pkg for each plugin, and NoiseDivision-Suite-macOS.pkg with all of them
# (Customize in the installer lets you leave some out). The installers put:
#   the Audio Unit  in /Library/Audio/Plug-Ins/Components   (Logic, GarageBand, ...)
#   the VST3        in /Library/Audio/Plug-Ins/VST3         (Ableton, FL Studio, Reaper, ...)
#   the standalone  in /Applications   (not for the plugins in NO_STANDALONE)
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
  app=true; [[ " $NO_STANDALONE " == *" $product "* ]] && app=false
  mkdir -p "$root/Library/Audio/Plug-Ins/VST3" "$root/Library/Audio/Plug-Ins/Components" "$work/scripts" "$work/packages"

  ditto "$art/VST3/$product.vst3"     "$root/Library/Audio/Plug-Ins/VST3/$product.vst3"
  ditto "$art/AU/$product.component"  "$root/Library/Audio/Plug-Ins/Components/$product.component"
  if $app; then mkdir -p "$root/Applications"; ditto "$art/Standalone/$product.app" "$root/Applications/$product.app"; fi

  # Apple Silicon Macs only run signed code. Without a paid Apple Developer ID this is an
  # "ad-hoc" signature: enough for the plugins to load once the installer has run.
  for bundle in "$root/Library/Audio/Plug-Ins/VST3/$product.vst3" \
                "$root/Library/Audio/Plug-Ins/Components/$product.component" \
                "$root/Applications/$product.app"; do
    [ -e "$bundle" ] || continue
    codesign --force --deep --sign - "$bundle"
  done
  echo "$product architectures:"; lipo -info "$root/Library/Audio/Plug-Ins/Components/$product.component/Contents/MacOS/$product"

  # install exactly where stated, even if the user has another copy somewhere else
  pkgbuild --analyze --root "$root" "$work/component.plist"
  # (newer pkgbuild versions leave the key out for some bundles, so delete it if present and add it fresh)
  i=0
  while /usr/libexec/PlistBuddy -c "Print :$i" "$work/component.plist" > /dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$work/component.plist" > /dev/null 2>&1 || true
    /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$work/component.plist"
    i=$((i + 1))
  done
  echo "$product: $i bundles set to install in place"

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

# the full suite: one installer holding every plugin's package, each a choice that is on by default
mkdir -p stage/suite/packages
lines=""; choices=""; refs=""
for entry in $PLUGINS; do
  IFS=: read -r target product code <<< "$entry"
  id="com.noisedivision.$(echo "$product" | tr '[:upper:]' '[:lower:]')"
  cp "stage/$product/packages/$product.pkg" stage/suite/packages/
  what="Audio Unit, VST3 and the standalone app"; [[ " $NO_STANDALONE " == *" $product "* ]] && what="Audio Unit and VST3"
  lines="$lines    <line choice=\"$id.choice\"/>\n"
  choices="$choices  <choice id=\"$id.choice\" title=\"$product\" description=\"$product: $what.\"><pkg-ref id=\"$id\"/></choice>\n"
  refs="$refs  <pkg-ref id=\"$id\" version=\"$version\" onConclusion=\"none\">$product.pkg</pkg-ref>\n"
done
printf '%b' "<?xml version=\"1.0\" encoding=\"utf-8\"?>
<installer-gui-script minSpecVersion=\"2\">
  <title>Noise Division plugin suite</title>
  <options customize=\"allow\" require-scripts=\"false\" hostArchitectures=\"arm64,x86_64\"/>
  <domains enable_anywhere=\"false\" enable_currentUserHome=\"false\" enable_localSystem=\"true\"/>
  <choices-outline>
$lines  </choices-outline>
$choices$refs</installer-gui-script>
" > stage/suite/distribution.xml
productbuild --distribution stage/suite/distribution.xml --package-path stage/suite/packages \
             "dist/NoiseDivision-Suite-macOS.pkg"
echo "made dist/NoiseDivision-Suite-macOS.pkg"
