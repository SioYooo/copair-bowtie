import argparse
import json
import os
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

from copair import paths
from copair.experiments import confirm as C
from copair.workloads import fanout_stress
from copair.workloads import synthetic as gen

PARAMSETS = dict(C.PARAMSETS)
PARAMSETS.update({"tau1": ["--tau", "1"], "tau2": ["--tau", "2"], "tau4": ["--tau", "4"],
                  "psi1_theta1_tau1": ["--psi", "1", "--theta", "1", "--tau", "1"],
                  "psi2_theta1_tau2": ["--psi", "2", "--theta", "1", "--tau", "2"],
                  "psi3_theta3_tau4": ["--psi", "3", "--theta", "3", "--tau", "4"],
                  "psi1_tau1": ["--psi", "1", "--tau", "1"]})
ARGV = ["--algo", "copair"]
STRESS = {"p6t3tau1": ["--psi", "6", "--theta", "3", "--tau", "1"], "p8t4tau2": ["--psi", "8", "--theta", "4", "--tau", "2"],
          "p100t4tau3": ["--psi", "100", "--theta", "4", "--tau", "3"], "p100t2tau1": ["--psi", "100", "--theta", "2", "--tau", "1"],
          "p6tau4": ["--psi", "6", "--tau", "4"], "p4t2tau2": ["--psi", "4", "--theta", "2", "--tau", "2"], "default": [], "tau1": ["--tau", "1"]}


def run(a):
    cand = os.path.abspath(a.candidate)
    seeds = C.seeds_of(a.seeds)
    work = []
    for fam in ["stress"] if a.stress else gen.CORRECTNESS:
        work += [(fam, s, kw) for s, kw in C.family_runs(fam, seeds)]
    sets = STRESS if a.stress else PARAMSETS
    res = {"runs": 0, "enumerations_compared": 0, "failures": []}
    t0 = time.time()

    def one(w):
        fam, seed, kw = w
        os.makedirs(paths.RESULTS, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=str(paths.RESULTS)) as td:
            tr = os.path.join(td, "cur_%s_%s.trace" % (fam, seed))
            with open(tr, "w") as f:
                if fam == "stress":
                    fanout_stress.generate(f, seed)
                else:
                    gen.generate(fam, seed, f, **kw)
            orc = C.oracle_records(tr)
            return [(fam, seed, pn) + C.compare(cand, tr, ARGV, flags, orc) for pn, flags in sets.items()]

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for out in ex.map(one, work):
            for fam, seed, pn, reason, n in out:
                res["runs"] += 1
                res["enumerations_compared"] += n
                if reason:
                    res["failures"].append({"family": fam, "seed": seed, "params": pn, "reason": reason})
    res.update(wall_s=round(time.time() - t0, 1), seeds=a.seeds, params=list(sets), candidate=cand)
    return res


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--candidate", default=str(paths.BUILD / "copair-debug"))
    ap.add_argument("--seeds", default="0-1")
    ap.add_argument("--stress", action="store_true")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--out")
    a = ap.parse_args()
    r = run(a)
    print(json.dumps({k: v for k, v in r.items() if k != "failures"}), "failures:", len(r["failures"]))
    for f in r["failures"][:20]:
        print(f)
    if a.out:
        json.dump(r, open(a.out, "w"), indent=1)
    sys.exit(1 if r["failures"] else 0)
