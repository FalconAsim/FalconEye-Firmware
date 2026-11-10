#!/bin/sh
# Devreye alma firmware'i için CMSIS başlıkları (Apache-2.0). Sabit sürümler; third_party/ git'e eklenmez.
set -e
D="$(cd "$(dirname "$0")/.." && pwd)/third_party"
mkdir -p "$D" && cd "$D"
get() {  # depo, kayıt
  n=$(basename "$1" .git)
  [ -d "$n" ] || git clone -q --filter=blob:none --no-checkout "$1" "$n"
  git -C "$n" fetch -q --depth 1 origin "$2" && git -C "$n" checkout -q "$2"
}
get https://github.com/STMicroelectronics/cmsis_device_g4.git 626ee412334a5ed2e5b320af5a8d77d69f03a558
get https://github.com/STMicroelectronics/cmsis_device_g0.git 7c77b7cbcee497dfbb5cc08f9562a80515e78013
[ -d CMSIS_5 ] || git clone -q --filter=blob:none --no-checkout https://github.com/ARM-software/CMSIS_5.git CMSIS_5
git -C CMSIS_5 sparse-checkout set CMSIS/Core/Include
git -C CMSIS_5 fetch -q --depth 1 origin 55b19837f5703e418ca37894d5745b1dc05e4c91 && git -C CMSIS_5 checkout -q 55b19837f5703e418ca37894d5745b1dc05e4c91
echo "CMSIS hazir: $D"
