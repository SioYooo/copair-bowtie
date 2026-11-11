#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
B="${COPAIR_BUILD:-$root/build}"
T="${1:-$root/results/timing}"
mkdir -p "$T"
export PYTHONPATH="$root/src"
gen() { python3 -m copair.workloads.synthetic "$@" --seed 0; }
[ -s $T/hub-distinct.trace ] || gen hub --n 10000 --H 16 --variant d --ops 200000 --enum_every 20000 --out $T/hub-distinct.trace
[ -s $T/hub-shared.trace ] || gen hub --n 10000 --H 16 --variant s --ops 20000 --enum_every 2000 --out $T/hub-shared.trace
[ -s $T/mid-degree.trace ] || gen mid_degree --n 1000 --H 16 --k 32 --ops 200000 --enum_every 20000 --out $T/mid-degree.trace
[ -s $T/pool-light.trace ] || gen pool --n 20000 --s 2 --pool 200 --ops 20000 --enum_every 2000 --out $T/pool-light.trace
[ -s $T/pool-heavy.trace ] || gen pool --n 20000 --s 3 --pool 60 --ops 20000 --enum_every 2000 --out $T/pool-heavy.trace
[ -s $T/zipf.trace ] || gen zipf --D 100000 --alpha 1.1 --init 100000 --ops 200000 --enum_every 50000 --out $T/zipf.trace
[ -s $T/heavy-centre.trace ] || gen heavy_centre --K 100 --s 50 --density 0.02 --ops 200000 --enum_every 50000 --out $T/heavy-centre.trace
[ -s $T/hub-distinct-right.trace ] || python3 "$root/scripts/mirror_right.py" < $T/hub-distinct.trace > $T/hub-distinct-right.trace
[ -s $T/hub-shared-right.trace ] || python3 "$root/scripts/mirror_right.py" < $T/hub-shared.trace > $T/hub-shared-right.trace
TRACES="${TRACES:-hub-distinct hub-shared hub-distinct-right hub-shared-right mid-degree pool-light pool-heavy zipf heavy-centre}"
arms=("copair|copair|--algo copair" "copair-simple|copair|--algo copair-simple" "copair-deg|copair|--algo copair-deg" "perx-layout|perx|--algo perx --variant layout")
for rep in 1 2 3; do
  for tr in $TRACES; do
    for a in "${arms[@]}"; do
      IFS='|' read -r name bin args <<< "$a"
      "$B/$bin-perf" $args --mode perf --hash-seed 1 --repeat-tag "r$rep" --trace $T/$tr.trace --out $T/$tr.$name.$rep.json
    done
  done
done
python3 - "$T" <<'PY'
import glob, json, os, statistics, sys
T = sys.argv[1]
rows = {}
for f in glob.glob(T + "/*.json"):
    tr, arm, rep, _ = os.path.basename(f).rsplit(".", 3)
    d = json.load(open(f))
    en = d["enumerations"]
    ans = sum(e["count"] for e in en)
    r = rows.setdefault((tr, arm), {"upd": [], "rss": [], "ens": []})
    r["upd"].append(d["update_total_ns"] / 1e9)
    r["rss"].append(d["peak_rss_kib"] / 1024)
    if ans: r["ens"].append(sum(e["total_ns"] for e in en) / ans)
for k in sorted(rows):
    r = rows[k]; u = r["upd"]
    spread = (max(u) - min(u)) / statistics.median(u) if len(u) > 1 and statistics.median(u) > 0 else 0
    print("%-22s %-20s upd median %.4f s  spread %3.0f%%%s  rss %.0f MiB  enum %.1f ns/answer  (n=%d)" % (
        k[0], k[1], statistics.median(u), 100 * spread, " REPEAT" if spread > 0.2 else "", statistics.median(r["rss"]),
        statistics.median(r["ens"]) if r["ens"] else 0, len(u)))
PY
