import argparse
import json
import os
import tempfile

from copair import paths
from copair.experiments import confirm as C
from copair.experiments.fanout_confirm import PARAMSETS
from copair.workloads import synthetic as gen

ap = argparse.ArgumentParser()
ap.add_argument("--seeds", default="0-1")
ap.add_argument("--stats", default=str(paths.BUILD / "copair-stats"))
a = ap.parse_args()
tot = {}
os.makedirs(paths.RESULTS, exist_ok=True)
with tempfile.TemporaryDirectory(dir=str(paths.RESULTS)) as td:
    for fam in gen.CORRECTNESS:
        for seed, kw in C.family_runs(fam, C.seeds_of(a.seeds)):
            tr = os.path.join(td, "t.trace")
            with open(tr, "w") as f:
                gen.generate(fam, seed, f, **kw)
            for pn, flags in PARAMSETS.items():
                rc, so, se = C.execute([a.stats, "--algo", "copair", "--mode", "perf", *flags, "--hash-seed", str(C.hseed(tr)), "--trace", tr])
                if rc:
                    raise SystemExit("rc %s %s" % (rc, se[-200:]))
                for k, v in json.loads(so)["counters"].items():
                    tot.setdefault(k, {}).setdefault(pn, 0)
                    tot[k][pn] += v
for k, d in tot.items():
    print("%-18s total %-9d fired in %2d/%d param sets" % (k, sum(d.values()), sum(1 for v in d.values() if v), len(d)))
