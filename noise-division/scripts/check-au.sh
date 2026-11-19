#!/usr/bin/env bash
# Runs Apple's Audio Unit validator (auval) on each built AU, the same test Logic runs.
set -uo pipefail
cd "$(dirname "$0")/.."
source scripts/plugins.sh
mkdir -p ~/Library/Audio/Plug-Ins/Components
failed=0
for entry in $PLUGINS; do
  IFS=: read -r target product code <<< "$entry"
  cp -R "stage/$product/root/Library/Audio/Plug-Ins/Components/$product.component" ~/Library/Audio/Plug-Ins/Components/
done
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 3
for entry in $PLUGINS; do
  IFS=: read -r target product code <<< "$entry"
  echo "::group::auval $product"
  if auval -v aufx "$code" "$AU_MANUFACTURER"; then echo "$product: AU validation PASSED"; else echo "::warning::$product: AU validation FAILED"; failed=1; fi
  echo "::endgroup::"
done
exit $failed
