import argparse
import hashlib
import json
import os
import resource
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

from copair import paths
from copair.workloads import synthetic as gen

ORACLE = str(paths.BUILD / "oracle")
CPU = 55
PARAMSETS = {"default": [], "psi1_theta1_mu1": ["--psi", "1", "--theta", "1", "--mu", "1"],
             "psi2_theta1_mu2": ["--psi", "2", "--theta", "1", "--mu", "2"], "psi0.5_theta2_mu1": ["--psi", "0.5", "--theta", "2", "--mu", "1"],
             "psi3_theta3_mu3": ["--psi", "3", "--theta", "3", "--mu", "3"], "psi1": ["--psi", "1"]}
COVERED_FAMILIES = ("abflip", "growshrink_large")
COUNTERS = ("x_to_heavy", "x_to_light", "a_to_heavy", "a_to_light", "b_to_heavy", "b_to_light", "vertex_flips", "mu_rebuilds", "global_rebuilds")
ALGOS = ("copair-deg", "copair-deg-nolight", "perx")


def hseed(trace):
    return 1 + int(hashlib.sha256(os.path.basename(trace).encode()).hexdigest()[:8], 16)


def seeds_of(s):
    a, _, b = s.partition("-")
    return list(range(int(a), int(b or a) + 1))


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (CPU, CPU + 5))


def execute(cmd):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, preexec_fn=limits, timeout=CPU * 4)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -999, "", "TIMEOUT"


def records(text):
    return [json.loads(l) for l in text.splitlines() if l.startswith("{")]


def oracle_records(trace):
    if not os.path.exists(ORACLE):
        raise SystemExit("oracle not found: %s (run scripts/build.sh)" % ORACLE)
    rc, so, se = execute([ORACLE, trace])
    if rc != 0:
        raise RuntimeError("oracle failed rc=%s %s" % (rc, se[-300:]))
    return records(so)


def algo_argv(algo, opts, variant):
    if algo == "perx":
        return ["--algo", "perx", "--variant", variant]
    return ["--algo", "copair-deg", "--opts", str(opts)] + (["--psi", "0"] if algo == "copair-deg-nolight" else [])


def compare(cand, trace, argv, flags, orc):
    rc, so, se = execute([cand, *argv, "--mode", "check", "--audit-every", "1", *flags, "--hash-seed", str(hseed(trace)), "--trace", trace])
    low = (so + se).lower()
    if rc != 0:
        return "exit %s: %s" % (rc, se.strip().splitlines()[-1][:200] if se.strip() else ""), 0
    if "error: addresssanitizer" in low or "runtime error" in low:
        return "sanitizer report", 0
    if "duplicate" in low:
        return "duplicate report", 0
    got = records(so)
    for i in range(max(len(got), len(orc))):
        g, o = (got[i] if i < len(got) else None), (orc[i] if i < len(orc) else None)
        if g is None or o is None or any(g[k] != o[k] for k in ("op_index", "count", "sum", "xor")):
            return "mismatch at E #%d: cand=%s oracle=%s" % (i, g, o), 0
    return None, len(orc)


def family_runs(fam, seeds):
    if fam == "walk":
        return [(0, {"L": 3}), (1, {"L": 3, "start": "rand"})]
    return [(s, {}) for s in seeds]


def confirm(a):
    cand = os.path.abspath(a.candidate)
    seeds = seeds_of(a.seeds) if a.set == "dev" else seeds_of("301-320")
    if a.set == "dev" and max(seeds) > 199:
        raise SystemExit("dev uses seeds 0-199 only")
    families = list(gen.CORRECTNESS)
    t0, res, work = time.time(), {"runs": 0, "enumerations_compared": 0, "failures": []}, []
    for fam in families:
        work += [(fam, s, kw) for s, kw in family_runs(fam, seeds)]

    def one(w):
        fam, seed, kw = w
        with tempfile.TemporaryDirectory(dir=str(paths.RESULTS)) as td:
            tr = os.path.join(td, "cur_%s_%s.trace" % (fam, seed))
            with open(tr, "w") as f:
                gen.generate(fam, seed, f, **kw)
            orc = oracle_records(tr)
            out = []
            for algo in ALGOS:
                for pn, flags in PARAMSETS.items():
                    reason, n = compare(cand, tr, algo_argv(algo, a.opts, a.perx_variant), flags, orc)
                    out.append((fam, seed, algo, pn, reason, n))
            return out

    os.makedirs(paths.RESULTS, exist_ok=True)
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for out in ex.map(one, work):
            for fam, seed, algo, pn, reason, n in out:
                res["runs"] += 1
                res["enumerations_compared"] += n
                if reason:
                    res["failures"].append({"family": fam, "seed": seed, "algo": algo, "params": pn, "reason": reason})
    res.update(wall_s=round(time.time() - t0, 1), seeds=seeds, families=families, algos=list(ALGOS), params=list(PARAMSETS),
               candidate=cand, candidate_sha256=sha(cand), oracle_sha256=sha(ORACLE), build_record=json.load(open(cand + ".build.json")))
    return res


def coverage_run(stats, fam, seed, pname, opts, tr):
    cmd = [stats, "--algo", "copair-deg", "--opts", str(opts), "--mode", "perf", *PARAMSETS[pname], "--hash-seed", str(hseed(tr)), "--trace", tr]
    rc, so, se = execute(cmd)
    if rc != 0:
        return dict(family=fam, seed=seed, params=pname, error="rc %s: %s" % (rc, se[-200:]), covered=False)
    d = json.loads(so)
    c, cap = d["counters"], d["capacity"]
    r = dict(family=fam, seed=seed, params=pname, **{k: c[k] for k in COUNTERS}, rehash_up=cap["rehash_up"], rehash_down=cap["rehash_down"])
    r["ab_transitions"] = sum(c[k] for k in ("a_to_heavy", "a_to_light", "b_to_heavy", "b_to_light"))
    if fam in COVERED_FAMILIES:
        r["covered"] = r["ab_transitions"] > 0 if fam == "abflip" else r["global_rebuilds"] >= 2
    return r


def coverage(a):
    stats = os.path.abspath(a.stats)
    seeds = seeds_of(a.seeds or "301-320")
    families = a.families.split(",") if a.families else list(COVERED_FAMILIES)
    rows = []
    os.makedirs(paths.RESULTS, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=str(paths.RESULTS)) as td:
        for fam in families:
            for seed, kw in family_runs(fam, seeds):
                tr = os.path.join(td, "cur_%s_%s.trace" % (fam, seed))
                with open(tr, "w") as f:
                    gen.generate(fam, seed, f, **kw)
                rows += [coverage_run(stats, fam, seed, pn, a.opts, tr) for pn in PARAMSETS]
    reg = [r for r in rows if r["family"] in COVERED_FAMILIES]
    bad = [r for r in reg if not r["covered"]]
    keys = (*COUNTERS, "rehash_up", "rehash_down")
    return dict(
        criterion="abflip: a+b transitions > 0; growshrink_large: global_rebuilds >= 2; per seed and parameter set",
        opts=a.opts, seeds=seeds, families=families, runs=len(rows), covered_cells=len(reg) - len(bad),
        uncovered=[{k: r.get(k) for k in ("family", "seed", "params", "ab_transitions", "global_rebuilds", "error")} for r in bad],
        verdict="COVERED" if not bad else "UNCOVERED", rows=rows,
        by_family={f: {k: sum(r[k] for r in rows if r["family"] == f and "error" not in r) for k in keys} for f in families},
        binary=stats, binary_sha256=sha(stats), build_record=json.load(open(stats + ".build.json")))


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("confirm")
    p.add_argument("--set", choices=["B", "dev"], required=True)
    p.add_argument("--seeds", default="0-1")
    p.add_argument("--opts", default="159")
    p.add_argument("--candidate", required=True)
    p.add_argument("--perx-variant", default="layout")
    p.add_argument("--jobs", type=int, default=1)
    p.add_argument("--out", required=True)
    p = sub.add_parser("coverage")
    p.add_argument("--seeds")
    p.add_argument("--opts", default="159")
    p.add_argument("--stats", required=True)
    p.add_argument("--families")
    p.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "confirm":
        if a.jobs > 4:
            ap.error("at most 4 jobs")
        res = confirm(a)
        print(json.dumps({k: res[k] for k in ("runs", "enumerations_compared", "wall_s")}), "failures:", len(res["failures"]))
        rc = 1 if res["failures"] else 0
    else:
        res = coverage(a)
        print(res["verdict"], res["covered_cells"], "/", res["runs"])
        rc = 0 if res["verdict"] == "COVERED" else 1
    with open(a.out, "w") as f:
        f.write(json.dumps(res, indent=1) + "\n")
    return rc


if __name__ == "__main__":
    sys.exit(main())
