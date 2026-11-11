#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
B="${COPAIR_BUILD:-$root/build}"
T="${1:-$root/results/digest}"
mkdir -p "$T"
export PYTHONPATH="$root/src"
gen() { python3 -m copair.workloads.synthetic "$@" --seed 0; }
[ -s $T/hub-distinct.trace ] || gen hub --n 300 --H 16 --variant d --ops 3000 --enum_every 300 --out $T/hub-distinct.trace
[ -s $T/hub-shared.trace ] || gen hub --n 300 --H 16 --variant s --ops 3000 --enum_every 300 --out $T/hub-shared.trace
[ -s $T/zipf.trace ] || gen zipf --D 3000 --alpha 1.1 --init 3000 --ops 6000 --enum_every 1000 --out $T/zipf.trace
[ -s $T/churn-ab.trace ] || gen churn --variant ab --ops 20000 --enum_every 2000 --out $T/churn-ab.trace
[ -s $T/hub-distinct-right.trace ] || python3 "$root/scripts/mirror_right.py" < $T/hub-distinct.trace > $T/hub-distinct-right.trace
[ -s $T/stress.trace ] || python3 -c "
import sys
from copair.workloads import fanout_stress
fanout_stress.generate(open(sys.argv[1], 'w'), 0)" $T/stress.trace
for tr in hub-distinct hub-shared zipf churn-ab hub-distinct-right stress; do
  fl=""; [ $tr = stress ] && fl="--psi 6 --theta 3 --tau 1"
  "$B/copair-perf" --algo copair $fl --mode check --audit-every 1000000 --trace $T/$tr.trace --out $T/$tr.copair.out 2>/dev/null
  "$B/copair-perf" --algo copair-simple $fl --mode check --audit-every 1000000 --trace $T/$tr.trace --out $T/$tr.simple.out 2>/dev/null
  "$B/copair-perf" --algo copair-deg --mode check --audit-every 1000000 --trace $T/$tr.trace --out $T/$tr.deg.out 2>/dev/null
  "$B/perx-perf" --algo perx --variant layout --mode check --audit-every 1000000 --trace $T/$tr.trace --out $T/$tr.layout.out 2>/dev/null
  python3 - $T/$tr <<'PY'
import json, sys
p = sys.argv[1]
ks = ("copair", "simple", "deg", "layout")
r = {k: [(d["op_index"], d["count"], d["sum"], d["xor"]) for d in map(json.loads, open("%s.%s.out" % (p, k)))] for k in ks}
print(p.rsplit("/", 1)[1], len(r["copair"]), "enumerations;", "EQUAL" if all(r[k] == r["copair"] for k in ks) else "DIFFER", "(count,sum,xor; ord ignored)")
PY
done
