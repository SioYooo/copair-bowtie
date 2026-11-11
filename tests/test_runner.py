import json
import os
import random
import shutil
import stat
import sys
import glob
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))
from copair import paths
from copair.experiments import runner as R

T = str(paths.RUNS / "runnertest")
os.makedirs(T, exist_ok=True)
FAKE = r'''#!/usr/bin/env python3
import json, os, sys, time
a = sys.argv[1:]
g = lambda k: a[a.index(k) + 1]
mode, algo, mode_env = g("--mode"), g("--algo"), os.environ.get("FAKE_MODE", "")
if mode_env == "fail":
    sys.stderr.write("boom\n"); sys.exit(3)
if mode_env == "sleep":
    time.sleep(5)
if mode == "check":
    d = 1 if (os.environ.get("FAKE_DIFF") and algo == "B") else 0
    open(g("--out"), "w").write(json.dumps({"op_index": 0, "count": 4 + d, "sum": 9, "xor": 1}) + "\n")
    sys.exit(0)
if mode_env == "badjson":
    open(g("--out"), "w").write("{nope"); sys.exit(0)
base = {"A": 100.0, "B": 150.0}[algo] + int(g("--hash-seed")) % 7
noper = "--no-per-op" in a
js = {"algo": algo, "per_op": not noper, "preprocessing_ns": 10, "overall": {"mean_ns": base, "p99_ns": 2 * base},
      "update_total_ns": base * 1000, "peak_rss_kib": 1234, "enumerations": [{"total_ns": 10, "count": 2, "start_to_first_ns": 1}],
      "aff": sorted(os.sched_getaffinity(0)), "trace": g("--trace")}
open(g("--out"), "w").write(json.dumps(js))
'''
fake = os.path.join(T, "fake.py")
open(fake, "w").write(FAKE)
os.chmod(fake, os.stat(fake).st_mode | stat.S_IXUSR)
fx = os.path.join(T, "fx.trace")
open(fx, "w").write("L R1 0 1\nE\n")
SHA = R.sha256_file(fake)


def spec(shards=2, big=True, checks=True, timeout=30):
    arms = ["A", "B"]
    cells = {"gn": dict(gen=["rand", "--n", "50"], units=[{"inst": 1, "hash": 5}, {"inst": 2, "hash": 6}], reps=1, modes=["perop"], arms=arms,
                        cost_s={"A": 1, "B": 2})}
    if big:
        cells["fx"] = dict(trace=T + "/fx.trace", units=[{"inst": None, "hash": h} for h in (1, 2, 3)], reps=2, modes=["perop", "noperop"],
                           arms=arms, cost_s={"A": 1, "B": 3})
    else:
        cells = {"fx": dict(trace=T + "/fx.trace", units=[{"inst": None, "hash": 1}], reps=1, modes=["perop"], arms=arms)}
    return dict(phase="t", shards=shards, replicate_all=False, timeout_s=timeout, binaries={"f": {"path": T + "/fake.py"}},
                arms={a: {"bin": "f", "argv": ["--algo", a]} for a in arms}, cells=cells,
                checks=[{"cell": "gn" if big else "fx", "unit": 0, "pairs": [["A", "B"]]}] if checks else [])


def fresh(name):
    d = os.path.join(T, name)
    shutil.rmtree(d, ignore_errors=True)
    return d


def rows_of(d, k):
    return R.jread(os.path.join(d, "rows_s%d.jsonl" % k))


d = fresh("p")
info = R.plan(spec(), d)
man = R.jread(os.path.join(d, "manifest.jsonl"))
runs = [r for r in man if r["kind"] == "run"]
assert len(runs) == 28 and info["runs"] == 28 and info["checks"] == 1 and sum(r["kind"] == "check" for r in man) == 1
assert len({r["run_id"] for r in runs}) == 28 and len({r["block_id"] for r in runs}) == 8
assert all(len({r["shard"] for r in runs if r["block_id"] == b}) == 1 for b in {r["block_id"] for r in runs})
assert [r for r in runs if r["cell"] == "fx"][0]["block_id"].count("/ix/") == 1
orders = {}
for r in runs:
    orders.setdefault(r["block_id"], []).append((r["order"], r["arm"], r["mode"]))
orders = {b: [x[1:] for x in sorted(v)] for b, v in orders.items()}
for b, o in orders.items():
    exp = sorted((a, m) for a, m in o)
    exp = [(a, m) for a in ("A", "B") for m in sorted({m for _, m in o}, key={"perop": 0, "noperop": 1}.get)]
    random.Random(R.ARM_ORDER_SALT + "-t-" + b).shuffle(exp)
    assert o == exp, (b, o, exp)
assert len({tuple(o) for b, o in orders.items() if "/fx/" in b}) > 1
d2 = fresh("p2")
R.plan(spec(), d2)
assert open(os.path.join(d, "manifest.jsonl")).read() == open(os.path.join(d2, "manifest.jsonl")).read()
loads = info["shard_loads_s"]
assert abs(loads[0] - loads[1]) <= 8, loads
ib = R.plan(spec(), fresh("pbad"))
assert ib["binaries_resolved"]["f"]["sha256"] == SHA
miss = spec()
miss["binaries"]["f"]["path"] = T + "/nosuchfile"
try:
    R.plan(miss, fresh("pmiss"))
    assert False
except SystemExit:
    pass
ra = spec(big=False, checks=False)
ra["replicate_all"] = True
R.plan(ra, fresh("pr"))
mr = R.jread(os.path.join(T, "pr", "manifest.jsonl"))
assert len(mr) == 4 and {r["run_id"].split("/")[0] for r in mr} == {"s0", "s1"} and {r["shard"] for r in mr} == {0, 1}

orig_aff = os.sched_getaffinity(0)
for k in (0, 1):
    R.shard(os.path.join(d, "manifest.jsonl"), k, d)
assert os.sched_getaffinity(0) == orig_aff
assert not os.path.exists(os.path.join(R.RUN_DIR, "t", "s0")) or not os.listdir(os.path.join(R.RUN_DIR, "t", "s0"))
rep_path = os.path.join(d, "merge_report.json")
assert R.merge(d) == 0
rep = json.load(open(rep_path))
assert rep["ok"] == 28 and rep["missing"] == [] and rep["duplicate"] == [] and rep["extra"] == [] and rep["failed"] == {} and rep["invalid_arms"] == []
assert len(R.jread(os.path.join(d, "runs.jsonl"))) == 28 and len(R.jread(os.path.join(d, "env.jsonl"))) == 2
chk = R.jread(os.path.join(d, "checks.jsonl"))
assert len(chk) == 1 and chk[0]["status"] == "CHECK_OK" and chk[0]["trace_sha256"]
r0 = R.jread(os.path.join(d, "runs.jsonl"))
assert all(r["status"] == "OK" and r["rc"] == 0 and r["wall_s"] is not None and r["rss_kib"] and r["trace_sha256"] for r in r0)
assert {r["algo"] for r in r0} == {"A", "B"} and all(r["per_op"] == (r["mode"] == "perop") for r in r0)
env = R.jread(os.path.join(d, "env.jsonl"))[0]
assert env["hostname"] and env["affinity"] and env["cpu_topology"] and "smt_active" in env
if shutil.which("taskset"):
    assert all(r["aff"] == [r["cpu"]] and r["cpu"] == min(orig_aff) for r in r0)
assert len({r["trace_sha256"] for r in r0 if r["cell"] == "gn" and r["inst"] == 1}) == 1
src = os.path.join(d, "rows_s0.jsonl")
lines = open(src).read().splitlines(True)
victim = next(i for i, l in enumerate(lines) if json.loads(l)["kind"] == "run")
vid = json.loads(lines[victim])["run_id"]
open(src, "w").write("".join(lines[:victim] + lines[victim + 1:]))
assert R.merge(d) != 0 and json.load(open(rep_path))["missing"] == [vid]
open(src, "w").write("".join(lines + [lines[victim]]))
assert R.merge(d) != 0 and json.load(open(rep_path))["duplicate"] == [vid]
open(src, "w").write("".join(lines + [json.dumps(dict(json.loads(lines[victim]), run_id="zzz")) + "\n"]))
assert R.merge(d) != 0 and json.load(open(rep_path))["extra"] == ["zzz"]
open(src, "w").write("".join(lines))
assert R.merge(d) == 0

def one(name, envv=None, mutate=None, timeout=30):
    dd = fresh(name)
    R.plan(spec(1, big=False, checks=False, timeout=timeout), dd)
    if mutate:
        m = R.jread(os.path.join(dd, "manifest.jsonl"))
        mutate(m)
        R.jwrite(os.path.join(dd, "manifest.jsonl"), m)
    if envv:
        os.environ["FAKE_MODE"] = envv
    try:
        R.shard(os.path.join(dd, "manifest.jsonl"), 0, dd)
    finally:
        os.environ.pop("FAKE_MODE", None)
    return dd, [r for r in rows_of(dd, 0) if r["kind"] == "run"]

_, rs = one("f1", "fail")
assert len(rs) == 2 and {r["status"] for r in rs} == {"CRASH"} and all(r["rc"] == 3 and "boom" in r["stderr_tail"] for r in rs)
_, rs = one("f2", "badjson")
assert {r["status"] for r in rs} == {"CRASH"} and all(r["rc"] == 0 for r in rs)
dd, rs = one("f3", "sleep", timeout=1)
assert {r["status"] for r in rs} == {"TIMEOUT"} and all(r["rc"] == 124 for r in rs)
assert R.merge(dd) != 0 and json.load(open(os.path.join(dd, "merge_report.json")))["failed"] == {"TIMEOUT": 2}
def corrupt(m):
    m[0]["bin_sha256"] = "1" * 64
_, rs = one("f4", mutate=corrupt)
assert sorted(r["status"] for r in rs) == ["OK", "SHA"] and [r for r in rs if r["status"] == "SHA"][0]["rc"] is None
def badgen(m):
    for r in m:
        r["gen"] = ["nosuchfamily"]
        r["trace"] = None
_, rs = one("f5", mutate=badgen)
assert {r["status"] for r in rs} == {"GENFAIL"} and len(rs) == 2

dd = fresh("chk")
R.plan(spec(1, big=False, checks=True), dd)
os.environ["FAKE_DIFF"] = "1"
try:
    R.shard(os.path.join(dd, "manifest.jsonl"), 0, dd)
finally:
    del os.environ["FAKE_DIFF"]
ck = [r for r in rows_of(dd, 0) if r["kind"] == "check"]
assert len(ck) == 1 and ck[0]["status"] == "CHECK_FAIL" and os.path.exists(os.path.join(dd, "failed_fx_A.trace"))
assert R.merge(dd) != 0 and json.load(open(os.path.join(dd, "merge_report.json")))["invalid_arms"] == ["A"]
assert not glob.glob(os.path.join(R.RUN_DIR, "t", "*", "*"))
print("test_runner: all asserts passed")
