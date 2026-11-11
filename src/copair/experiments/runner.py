import argparse
import collections
import hashlib
import json
import os
import platform
import random
import shutil
import subprocess
import sys
import time

from copair import paths

ROOT = str(paths.PACKAGE_ROOT)
SRC = os.path.join(ROOT, "src")
RUN_DIR = str(paths.RUNS / "scratch")
ARM_ORDER_SALT = "copair"
TIME_FMT = "RSS_KIB=%M WALL_S=%e"


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for ch in iter(lambda: f.read(1 << 20), b""):
            h.update(ch)
    return h.hexdigest()


def utc():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def jwrite(path, rows):
    with open(path, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")


def jread(path):
    return [json.loads(l) for l in open(path) if l.strip()]


def abspath(p):
    return p if os.path.isabs(p) else os.path.join(ROOT, p)


def resolve_binary(b):
    p = abspath(b["path"])
    if not os.path.isfile(p):
        raise SystemExit("binary not found: %s (build it first, see README)" % p)
    got, rec = sha256_file(p), None
    bj = p + ".build.json"
    if os.path.exists(bj):
        rec = json.load(open(bj))
    return dict(path=b["path"], sha256=got, build_record=rec)


def load_spec(name):
    if name.endswith(".json"):
        return json.load(open(name))
    from copair.experiments.specs import SPECS
    return SPECS[name]


def plan(spec, out):
    bins = {n: resolve_binary(b) for n, b in spec["binaries"].items()}
    K, ph, blocks = spec["shards"], spec["phase"], []
    for cell, c in spec["cells"].items():
        for u in c["units"]:
            for rep in range(c["reps"]):
                inst, hs = u.get("inst"), u["hash"]
                bid = "%s/%s/i%s/h%d/r%d" % (ph, cell, "x" if inst is None else inst, hs, rep)
                pairs = [(a, m) for a in c["arms"] for m in c["modes"] if not (spec["arms"][a].get("plain") and m == "noperop")]
                random.Random("%s-%s-%s" % (ARM_ORDER_SALT, ph, bid)).shuffle(pairs)
                cost = sum(c.get("cost_s", {}).get(a, 1.0) for a, _ in pairs)
                blocks.append(dict(bid=bid, cell=cell, c=c, inst=inst, hash=hs, rep=rep, pairs=pairs, cost=cost))
    assign = []
    load = [0.0] * K
    if spec.get("replicate_all"):
        for k in range(K):
            for b in blocks:
                assign.append((k, "s%d/" % k, b))
                load[k] += b["cost"]
    else:
        for b in sorted(blocks, key=lambda b: -b["cost"]):
            k = min(range(K), key=lambda i: (load[i], i))
            assign.append((k, "", b))
            load[k] += b["cost"]
    rows = []
    for k, pre, b in assign:
        c = b["c"]
        for i, (arm, mode) in enumerate(b["pairs"]):
            bn = spec["arms"][arm]["bin"]
            argv = [bins[bn]["path"]] + spec["arms"][arm]["argv"] + [
                "--mode", "perf", "--hash-seed", str(b["hash"]), "--trace", "{trace}", "--out", "{out}"] + (
                [] if spec["arms"][arm].get("plain") else ["--repeat-tag", "r%d" % b["rep"]]) + (["--no-per-op"] if mode == "noperop" else [])
            bid = pre + b["bid"]
            rows.append(dict(kind="run", run_id="%s/%s/%s" % (bid, arm, mode), phase=ph, block_id=bid, shard=k,
                             cell=b["cell"], inst=b["inst"], hash=b["hash"], rep=b["rep"], arm=arm, mode=mode,
                             order=i, bin=bins[bn]["path"], bin_sha256=bins[bn]["sha256"],
                             argv=argv, trace=c.get("trace"), gen=c.get("gen"), timeout_s=spec.get("timeout_s", 1200)))
    nchk = 0
    for ch in spec.get("checks", []):
        c = spec["cells"][ch["cell"]]
        u = c["units"][ch["unit"]]
        for arm, ref in ch["pairs"]:
            k = min(range(K), key=lambda i: (load[i], i))
            load[k] += sum(c.get("cost_s", {}).get(x, 1.0) for x in (arm, ref))
            sp = {}
            for tag, a in (("arm", arm), ("ref", ref)):
                bn = spec["arms"][a]["bin"]
                sp[tag + "_bin"], sp[tag + "_sha256"], sp[tag + "_argv"] = (
                    bins[bn]["path"], bins[bn]["sha256"], spec["arms"][a]["argv"])
            rows.append(dict(kind="check", check_id="%s/check/%s/u%d/%s~%s" % (ph, ch["cell"], ch["unit"], arm, ref),
                             phase=ph, shard=k, cell=ch["cell"], unit=ch["unit"], arm=arm, ref=ref, inst=u.get("inst"),
                             hash=u["hash"], trace=c.get("trace"), gen=c.get("gen"),
                             timeout_s=spec.get("timeout_s", 1200), **sp))
            nchk += 1
    os.makedirs(out, exist_ok=True)
    jwrite(os.path.join(out, "manifest.jsonl"), rows)
    nrun = sum(r["kind"] == "run" for r in rows)
    info = dict(spec=spec, binaries_resolved=bins, shard_loads_s=load, runs=nrun, checks=nchk,
                blocks=len(assign), runs_per_shard=[sum(r["kind"] == "run" and r["shard"] == k for r in rows) for k in range(K)])
    json.dump(info, open(os.path.join(out, "plan.json"), "w"), indent=1)
    return info


def lscpu_info(cpus):
    d = {}
    try:
        for l in subprocess.run(["lscpu", "-p=CPU,CORE,SOCKET,NODE,CACHE"], capture_output=True, text=True, timeout=20).stdout.splitlines():
            if l and not l.startswith("#"):
                f = l.split(",")
                if int(f[0]) in cpus:
                    try:
                        l3 = open("/sys/devices/system/cpu/cpu%s/cache/index3/id" % f[0]).read().strip()
                    except OSError:
                        l3 = None
                    d[f[0]] = dict(core=f[1], socket=f[2], node=f[3], cache=f[4], l3_id=l3)
    except Exception as e:
        d["error"] = repr(e)
    return d


def env_row(k):
    cpus = sorted(os.sched_getaffinity(0))
    model = ""
    try:
        model = [l.split(":", 1)[1].strip() for l in subprocess.run(["lscpu"], capture_output=True, text=True, timeout=20).stdout.splitlines()
                 if l.startswith("Model name")][0]
    except Exception:
        pass
    try:
        smt = open("/sys/devices/system/cpu/smt/active").read().strip()
    except OSError:
        smt = None
    return dict(kind="env", shard=k, hostname=platform.node(), cpu_model=model, affinity=cpus, cpu_topology=lscpu_info(set(cpus)),
                smt_active=smt, start_utc=utc())


def parse_time(err):
    rss = wall = None
    for l in err.splitlines():
        if "RSS_KIB=" in l:
            try:
                p = dict(x.split("=") for x in l.split() if "=" in x)
                rss, wall = int(p["RSS_KIB"]), float(p["WALL_S"])
            except (KeyError, ValueError):
                pass
    return rss, wall


def status_of(rc, err):
    if rc == 124:
        return "TIMEOUT"
    if rc == 137 or "Killed" in err or "std::bad_alloc" in err:
        return "OOM"
    return "CRASH"


def blockdir(phase, k, bid):
    base = os.path.join(RUN_DIR, phase, "s%d" % k, bid.replace("/", "_"))
    d, n = base, 0
    while os.path.exists(d):
        n += 1
        d = "%s.retry%d" % (base, n)
    os.makedirs(d)
    return d


def generate(gen, inst, path):
    env = dict(os.environ, PYTHONPATH=SRC + (":" + os.environ["PYTHONPATH"] if os.environ.get("PYTHONPATH") else ""))
    r = subprocess.run([sys.executable, "-B", "-m", "copair.workloads.synthetic"] + list(gen) + ["--seed", str(inst), "--out", path],
                       capture_output=True, text=True, timeout=1800, env=env)
    return r.returncode == 0 and os.path.exists(path), r.stderr[-600:]


def pin_cmd(cpu):
    return ["taskset", "-c", str(cpu)] if shutil.which("taskset") else []


def run_block(rows, k, cpu, sink, log, node, fixed_sha):
    r0 = rows[0]
    d = blockdir(r0["phase"], k, r0["block_id"])
    gen_err, tsha, trace = None, None, None
    if r0["gen"]:
        trace = os.path.join(d, "t.trace")
        ok, gen_err = generate(r0["gen"], r0["inst"], trace)
        tsha = sha256_file(trace) if ok else None
        gen_err = None if ok else (gen_err or "generator failed")
    else:
        trace = abspath(r0["trace"])
        if trace not in fixed_sha:
            fixed_sha[trace] = sha256_file(trace)
        tsha = fixed_sha[trace]
    for n, r in enumerate(rows):
        base = {x: r[x] for x in ("run_id", "phase", "block_id", "shard", "cell", "inst", "hash", "rep", "arm", "mode")}
        base.update(kind="run", per_op=r["mode"] == "perop", order=r["order"], node=node, cpu=cpu, trace_sha256=tsha,
                    bin_sha256=r["bin_sha256"], utc_start=utc())
        if gen_err is not None:
            row = dict(base, status="GENFAIL", rc=None, stderr_tail=gen_err[-600:], utc_end=utc())
        elif sha256_file(abspath(r["bin"])) != r["bin_sha256"]:
            row = dict(base, status="SHA", rc=None, stderr_tail="binary sha256 mismatch", utc_end=utc())
        else:
            out_json = os.path.join(d, "%d.json" % n)
            argv = [abspath(r["argv"][0])] + [a.replace("{trace}", trace).replace("{out}", out_json) for a in r["argv"][1:]]
            cmd = pin_cmd(cpu) + ["/usr/bin/time", "-f", TIME_FMT, "timeout", str(r["timeout_s"])] + argv
            errp = os.path.join(d, "%d.err" % n)
            base["load1"] = float(open("/proc/loadavg").read().split()[0])
            with open(errp, "w") as ef:
                rc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=ef, cwd=ROOT).returncode
            err = open(errp, errors="replace").read()
            rss, wall = parse_time(err)
            js = None
            if rc == 0:
                try:
                    js = json.load(open(out_json))
                except (OSError, ValueError):
                    js = None
            base.update(rc=rc, wall_s=wall, rss_kib=rss, utc_end=utc())
            if js is not None:
                row = dict(js, **base, status="OK")
            else:
                row = dict(base, status=status_of(rc, err) if rc != 0 else "CRASH", stderr_tail=err[-600:])
        sink(row)
        log.write("%s %s %s\n" % (row.get("rc"), row["run_id"], row.get("wall_s")))
        log.flush()
    shutil.rmtree(d, ignore_errors=True)


def digests(path):
    return {d["op_index"]: (d["count"], d["sum"], d["xor"]) for d in (json.loads(l) for l in open(path) if l.startswith("{"))}


def run_check(r, k, sink, log):
    d = blockdir(r["phase"], k, r["check_id"])
    if r["gen"]:
        trace = os.path.join(d, "t.trace")
        ok, err = generate(r["gen"], r["inst"], trace)
        if not ok:
            sink(dict(kind="check", check_id=r["check_id"], phase=r["phase"], shard=k, cell=r["cell"], arm=r["arm"], ref=r["ref"],
                      inst=r["inst"], status="CHECK_ERROR", rc=None, stderr_tail=err, trace_sha256=None))
            shutil.rmtree(d, ignore_errors=True)
            return
    else:
        trace = abspath(r["trace"])
    tsha = sha256_file(trace)
    outs, rcs, status = [], [], None
    for tag in ("arm", "ref"):
        if sha256_file(abspath(r[tag + "_bin"])) != r[tag + "_sha256"]:
            status = "CHECK_ERROR"
        o = os.path.join(d, tag + ".txt")
        outs.append(o)
        if status is None:
            cmd = [abspath(r[tag + "_bin"])] + r[tag + "_argv"] + ["--mode", "check", "--audit-every", "1000000000",
                    "--hash-seed", str(r["hash"]), "--trace", trace, "--out", o]
            try:
                rcs.append(subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=ROOT,
                                          timeout=r["timeout_s"]).returncode)
            except subprocess.TimeoutExpired:
                rcs.append(124)
            if rcs[-1] != 0:
                status = "CHECK_ERROR"
    rc = None
    if status is None:
        try:
            rc = 0 if digests(outs[0]) == digests(outs[1]) else 1
        except (ValueError, KeyError, OSError):
            rc = 2
        status = "CHECK_OK" if rc == 0 else ("CHECK_FAIL" if rc == 1 else "CHECK_ERROR")
        if status == "CHECK_FAIL":
            shutil.copy(trace, os.path.join(r["_out"], "failed_%s_%s.trace" % (r["cell"], r["arm"])))
    row = dict(kind="check", check_id=r["check_id"], phase=r["phase"], shard=k, cell=r["cell"], unit=r["unit"], arm=r["arm"],
               ref=r["ref"], inst=r["inst"], hash=r["hash"], status=status, rc=rc if rc is not None else (rcs or [None])[-1],
               trace_sha256=tsha, utc=utc())
    sink(row)
    log.write("%s %s %s\n" % (row["rc"], r["check_id"], status))
    log.flush()
    shutil.rmtree(d, ignore_errors=True)


def shard(manifest, k, out):
    rows = [r for r in jread(manifest) if r["shard"] == k]
    orig = os.sched_getaffinity(0)
    cpus = sorted(orig)
    envr = env_row(k)
    fp = open(os.path.join(out, "rows_s%d.jsonl" % k), "a")
    log = open(os.path.join(out, "shard_s%d.log" % k), "a")

    def sink(row):
        fp.write(json.dumps(row) + "\n")
        fp.flush()

    try:
        sink(envr)
        cpu = cpus[0]
        if len(cpus) >= 2:
            os.sched_setaffinity(0, cpus[1:])
        blocks, fixed_sha = collections.OrderedDict(), {}
        for r in rows:
            if r["kind"] == "run":
                blocks.setdefault(r["block_id"], []).append(r)
        for b in blocks.values():
            run_block(b, k, cpu, sink, log, envr["hostname"], fixed_sha)
            os.fsync(fp.fileno())
        for r in rows:
            if r["kind"] == "check":
                run_check(dict(r, _out=out), k, sink, log)
                os.fsync(fp.fileno())
    finally:
        os.sched_setaffinity(0, orig)
        fp.close()
        log.close()


def merge(d):
    man = jread(os.path.join(d, "manifest.jsonl"))
    allrows = []
    for f in sorted(os.listdir(d)):
        if f.startswith("rows_s") and f.endswith(".jsonl"):
            allrows += jread(os.path.join(d, f))
    key = lambda r: r["run_id"] if r["kind"] == "run" else r["check_id"]
    want = collections.Counter(key(r) for r in man)
    got = collections.Counter(key(r) for r in allrows if r["kind"] in ("run", "check"))
    missing = sorted((want - got).elements())
    extra = sorted(set(got) - set(want))
    dup = sorted(i for i, n in got.items() if n > 1 and i in want)
    run_rows = [r for r in allrows if r["kind"] == "run"]
    chk = [r for r in allrows if r["kind"] == "check"]
    ok_rows = [r for r in run_rows if r["status"] == "OK"]
    bad = [r for r in run_rows if r["status"] != "OK"] + [r for r in chk if r["status"] != "CHECK_OK"]
    invalid = sorted({r["arm"] for r in chk if r["status"] in ("CHECK_FAIL", "CHECK_ERROR")})
    shas = collections.defaultdict(set)
    for r in run_rows + chk:
        if r.get("trace_sha256"):
            shas[(r["cell"], r["inst"])].add(r["trace_sha256"])
    conf = sorted("%s/i%s" % k for k, v in shas.items() if len(v) > 1)
    jwrite(os.path.join(d, "runs.jsonl"), ok_rows)
    jwrite(os.path.join(d, "failures.jsonl"), bad)
    jwrite(os.path.join(d, "env.jsonl"), [r for r in allrows if r["kind"] == "env"])
    jwrite(os.path.join(d, "checks.jsonl"), chk)
    rep = dict(manifest_runs=sum(r["kind"] == "run" for r in man), manifest_checks=sum(r["kind"] == "check" for r in man),
               ok=len(ok_rows), failed=dict(collections.Counter(r["status"] for r in bad)), missing=missing, duplicate=dup,
               extra=extra, invalid_arms=invalid, trace_sha_conflicts=conf)
    pj = os.path.join(d, "plan.json")
    if os.path.exists(pj):
        rep["binaries_resolved"] = json.load(open(pj)).get("binaries_resolved")
    json.dump(rep, open(os.path.join(d, "merge_report.json"), "w"), indent=1)
    good = not (missing or dup or extra or bad or conf)
    print("merge: %d/%d timed runs OK (+%d checks); missing %d, duplicate %d, extra %d, failed %s, invalid arms %s, "
          "trace sha conflicts %d => %s" % (len(ok_rows), rep["manifest_runs"], len(chk), len(missing), len(dup), len(extra),
                                             rep["failed"], invalid, len(conf), "PASS" if good else "FAIL"))
    return 0 if good else 1


def main(argv=None):
    ap = argparse.ArgumentParser(prog="copair.experiments.runner")
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("plan")
    p.add_argument("--spec", required=True)
    p.add_argument("--out", required=True)
    s = sp.add_parser("shard")
    s.add_argument("--manifest", required=True)
    s.add_argument("--shard", type=int, required=True)
    s.add_argument("--out", required=True)
    m = sp.add_parser("merge")
    m.add_argument("--dir", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "plan":
        i = plan(load_spec(a.spec), a.out)
        print("planned %d runs, %d checks, %d blocks; runs/shard %s; loads(s) %s" % (i["runs"], i["checks"], i["blocks"], i["runs_per_shard"], [round(x, 1) for x in i["shard_loads_s"]]))
        return 0
    if a.cmd == "shard":
        shard(a.manifest, a.shard, a.out)
        return 0
    return merge(a.dir)


if __name__ == "__main__":
    sys.exit(main())
