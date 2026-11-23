#!/usr/bin/env bash
# Makes <PRODUCT>-Windows.zip for each plugin (the .vst3, the standalone .exe and install notes),
# and NoiseDivision-Suite-Windows.zip with all of them.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/plugins.sh
rm -rf stage dist && mkdir -p stage dist

for entry in $PLUGINS; do
  IFS=: read -r target product code <<< "$entry"
  art="build/$target/${target}_artefacts/Release"
  dir="stage/$product"
  mkdir -p "$dir"
  cp -R "$art/VST3/$product.vst3" "$dir/"
  cp "$art/Standalone/$product.exe" "$dir/"
  sed "s/{PRODUCT}/$product/g" scripts/install-windows.txt > "$dir/How to install.txt"
  (cd stage && 7z a -tzip "../dist/$product-Windows.zip" "$product" > /dev/null)
  echo "made dist/$product-Windows.zip"
done

# the full suite: one folder per plugin, one set of install notes
suite="stage/suite/Noise Division Suite"
mkdir -p "$suite"
for entry in $PLUGINS; do
  IFS=: read -r target product code <<< "$entry"
  mkdir -p "$suite/$product"
  cp -R "stage/$product/$product.vst3" "stage/$product/$product.exe" "$suite/$product/"
done
cp scripts/install-windows-suite.txt "$suite/How to install.txt"
(cd stage/suite && 7z a -tzip "../../dist/NoiseDivision-Suite-Windows.zip" "Noise Division Suite" > /dev/null)
echo "made dist/NoiseDivision-Suite-Windows.zip"
