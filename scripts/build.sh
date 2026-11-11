#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$(realpath -m "${1:-${COPAIR_BUILD:-$root/build}}")"
mkdir -p "$out"
cd "$root/src/cpp/tools"
common="-std=c++17 -Wall -Wextra"
ver="$(g++ --version | head -1)"
declare -A hdrs=(
  [copair]="copair_core perx_layout copair_final copair_fanout_base copair_fanout"
  [perx]="copair_core perx_tuned perx_tuned_index perx_layout"
  [hl]="copair_core hl"
)
for mode in perf debug stats; do
  case "$mode" in
    debug) flags="-O1 -g -fsanitize=address,undefined -D_GLIBCXX_ASSERTIONS -DBT_STATS" ;;
    perf)  flags="-O2 -DNDEBUG" ;;
    stats) flags="-O2 -DNDEBUG -DBT_STATS" ;;
  esac
  for drv in copair perx hl; do
    g++ $common $flags ${drv}_cli.cpp -o "$out/$drv-$mode"
    {
      echo "{"
      echo "  \"compiler\": \"$ver\","
      echo "  \"flags\": \"$common $flags\","
      echo "  \"utc\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
      echo "  \"sources\": {"
      for h in ${hdrs[$drv]}; do echo "    \"$h.hpp\": \"$(sha256sum ../include/$h.hpp | cut -d' ' -f1)\","; done
      echo "    \"driver.hpp\": \"$(sha256sum driver.hpp | cut -d' ' -f1)\","
      echo "    \"${drv}_cli.cpp\": \"$(sha256sum ${drv}_cli.cpp | cut -d' ' -f1)\""
      echo "  }"
      echo "}"
    } > "$out/$drv-$mode.build.json"
    echo "built $out/$drv-$mode"
  done
done
g++ $common -O2 -o "$out/oracle" oracle.cpp
echo "built $out/oracle"
