import argparse
import json
import math
import os
import random
import socket
import subprocess
import sys
import time
from pathlib import Path

from copair.experiments import runner
from copair.experiments import specs
from copair.experiments import badge_events
from copair.workloads import regime
from copair import paths
from copair.experiments.badge_events import TR, RunError, boot_median_ci, digests, median, sh
from copair.workloads.regime import cell_label

FINAL = "copair"
REF = "copair-deg"
PERX = specs.PERX
HL = "hl-0.5"
ARMS = [FINAL, REF] + PERX + [HL]
BINS = {k: v for k, v in specs.BINARIES.items() if k in {specs.ARMS[a][0] for a in ARMS}}
OUT_E, OUT_R = paths.RESULTS / "badges", paths.RESULTS / "regime"
SCR = paths.RUNS / "rerun-scratch"
SEEDS_E = (1, 2, 3)
SEEDS_R = (7101, 7102, 7103, 7104, 7105)
ARM_ORDER_SALT = "copair"
BOOT_SEED = 115


def resolved_binaries():
    return {k: runner.resolve_binary(v) for k, v in BINS.items()}


def arm_argv(arm):
    t = specs.ARMS[arm]
    return t[0], list(t[1]), len(t) > 2


def argv_template(arm, hash_seed):
    bn, av, plain = arm_argv(arm)
    return [BINS[bn]["path"]] + av + ["--mode", "perf", "--hash-seed", str(hash_seed), "--trace", "{trace}", "--out", "{out}"] + (
        [] if plain else ["--repeat-tag", "r0"])


def mean_ns(updates, keys):
    n = sum(updates[k]["count"] - updates[k]["noop"] for k in keys if k in updates)
    return sum(updates[k]["total_ns"] for k in keys if k in updates) / n if n else None


def grant_mean(updates): return mean_ns(updates, ("R1+", "R5+"))


def r15_mean(updates): return mean_ns(updates, ("R1+", "R1-", "R5+", "R5-"))


def has_minus(updates): return any(updates.get(k, {}).get("count", 0) > 0 for k in ("R1-", "R5-"))


def fastest_perx(med):
    c = {a: med[a] for a in PERX if med.get(a) is not None}
    return min(c, key=c.get) if c else None


def ratio_label(ratios, rng):
    lo, hi, nres = boot_median_ci(list(ratios), rng)
    return {"median": median(list(ratios)), "ci95": [lo, hi], "boot_resamples": nres, "label": cell_label(lo, hi)}


def predicted(qn):
    return "BETTER" if qn >= 10 else ("WORSE_OR_TIE" if qn <= 1 else None)


def check_prediction(cells):
    contra, checked = [], 0
    for c, v in cells.items():
        need = predicted(round(v["q"] * v["n"], 9))
        if need is None or v.get("label") is None: continue
        checked += 1
        if (v["label"] == "BETTER") != (need == "BETTER"): contra.append({"cell": c, "predicted": need, "label": v["label"]})
    return {"verdict": "CONFIRMED" if not contra else "NOT CONFIRMED", "constrained_cells_checked": checked, "contradicting": contra}


def cpu_model():
    for l in subprocess.run(["lscpu"], capture_output=True, text=True).stdout.splitlines():
        if l.startswith("Model name"): return l.split(":", 1)[1].strip()


def slim(row, cpu_name):
    keep = ("run_id", "block_id", "cell", "inst", "hash", "arm", "mode", "order", "node", "cpu", "status", "rc", "wall_s", "rss_kib",
            "bin_sha256", "trace_sha256", "utc_start", "utc_end", "stderr_tail", "updates", "overall", "noop_count", "update_total_ns", "load1")
    r = {k: row[k] for k in keep if k in row}
    r["cpu_model"] = cpu_name
    if row.get("status") == "OK":
        r["grant_mean_ns"], r["r15_mean_ns"], r["mean_update_ns"] = grant_mean(row["updates"]), r15_mean(row["updates"]), row["overall"]["mean_ns"]
        r["has_minus"] = has_minus(row["updates"])
    return r


def digest_check(trace, arms, deadline, scr):
    dg = {}
    for a in arms:
        bn, av, _ = arm_argv(a)
        cp = scr / ("chk-%s-%s.jsonl" % (Path(trace).stem, a))
        sh([abspath_bin(bn), "--mode", "check", *av, "--hash-seed", "1", "--trace", str(trace), "--out", str(cp), "--audit-every", str(10 ** 9)], deadline)
        dg[a] = digests(cp)
        cp.unlink()
    return {"n_enum": len(dg[arms[0]]), "mismatch_arms": [a for a in dg if dg[a] != dg[arms[0]]]}


def abspath_bin(bn): return runner.abspath(BINS[bn]["path"])


def run_blocks(blocks, out_jsonl, deadline, timeout_s):
    cpus = sorted(os.sched_getaffinity(0))
    orig = os.sched_getaffinity(0)
    cpu = cpus[0]
    node = socket.gethostname()
    cm = cpu_model()
    fixed = {}
    rb = resolved_binaries()
    if len(cpus) >= 2: os.sched_setaffinity(0, cpus[1:])
    out_jsonl.parent.mkdir(parents=True, exist_ok=True)
    fp = open(out_jsonl, "w")
    log = open(os.devnull, "w")
    status = "OK"
    runner.RUN_DIR = str(paths.RUNS / "rerun")
    try:
        for bid, cell, hs, trace, okey in blocks:
            if time.time() > deadline:
                status = "TIMEOUT"
                break
            order = list(ARMS)
            random.Random(okey).shuffle(order)
            rows = []
            for i, a in enumerate(order):
                bn, _, _ = arm_argv(a)
                rows.append(dict(run_id="%s/%s" % (bid, a), phase="reruns", block_id=bid, shard=0, cell=cell, inst=hs, hash=hs, rep=0, arm=a,
                                 mode="perop", order=i, bin=rb[bn]["path"], bin_sha256=rb[bn]["sha256"], argv=argv_template(a, hs),
                                 trace=str(trace), gen=None, timeout_s=timeout_s))
            def sink(r):
                s = slim(r, cm)
                fp.write(json.dumps(s) + "\n")
                fp.flush()
            runner.run_block(rows, 0, cpu, sink, log, node, fixed)
    finally:
        os.sched_setaffinity(0, orig)
        fp.close()
    return status


def run_badges(stream, out=OUT_E, limit_s=3300, seeds=SEEDS_E, check=True, timeout_s=1800):
    t0 = time.time()
    deadline = t0 + limit_s
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    scr = SCR
    scr.mkdir(parents=True, exist_ok=True)
    tp = TR / (stream + ".trace")
    if not tp.exists(): badge_events.prep(stream)
    minus = sum(1 for t in badge_events._lines(tp) if t[0] == "-" and t[1].lstrip("Rr") in ("1", "5"))
    st = {"stream": stream, "status": "OK", "errors": [], "host": socket.gethostname(), "cpu": cpu_model(), "trace": str(tp),
          "trace_R1R5_minus_lines": minus, "seeds": list(seeds), "binaries": resolved_binaries(),
          "argv": {a: argv_template(a, "<seed>") for a in ARMS}}
    if minus: st["errors"].append("trace has R1-/R5- lines: grant metric is not the R1+/R5+ mean of all R1/R5 updates")
    try:
        if check:
            st["digest"] = digest_check(tp, ARMS, deadline, scr)
            if st["digest"]["mismatch_arms"]:
                st["status"] = "INVALID"
                st["errors"].append("DIGEST_MISMATCH %s" % st["digest"]["mismatch_arms"])
        if st["status"] == "OK":
            blocks = [("badges/%s/h%d" % (stream, s), stream, s, tp, "%s-badges-%s-%d" % (ARM_ORDER_SALT, stream, s)) for s in seeds]
            st["status"] = run_blocks(blocks, out / (stream + ".jsonl"), deadline, timeout_s)
    except RunError as e:
        st["status"] = e.code
        st["errors"].append(str(e))
    finally:
        st["wall_s"] = round(time.time() - t0, 1)
        (out / (stream + ".status.json")).write_text(json.dumps(st, indent=1))
    return st


def run_regime(cell, out=OUT_R, limit_s=3300, ops=regime.OPS, check_seeds=1, timeout_s=1800):
    t0 = time.time()
    deadline = t0 + limit_s
    n, q = regime.parse_cell(cell)
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    trd = paths.TRACES / "regime"
    trd.mkdir(parents=True, exist_ok=True)
    scr = SCR
    scr.mkdir(parents=True, exist_ok=True)
    st = {"cell": cell, "n": n, "q": float(q), "ops": ops, "status": "OK", "errors": [], "host": socket.gethostname(), "cpu": cpu_model(),
          "digest": {}, "binaries": resolved_binaries(), "argv": {a: argv_template(a, "<seed>") for a in ARMS}}
    blocks = []
    for i, s in enumerate(SEEDS_R):
        tp = trd / ("%s-s%d.trace" % (cell, s))
        with open(tp, "w") as f: regime.hub_regime(f, random.Random(s), n=n, H=16, q=float(q), ops=ops)
        blocks.append(("regime/%s/s%d" % (cell, s), cell, s, tp, "%s-regime-%s-%d" % (ARM_ORDER_SALT, cell, s)))
    try:
        for bid, _, s, tp, _ in blocks[:check_seeds]:
            st["digest"][str(s)] = digest_check(tp, ARMS, deadline, scr)
            if st["digest"][str(s)]["mismatch_arms"]:
                st["status"] = "INVALID"
                st["errors"].append("DIGEST_MISMATCH %s" % bid)
        if st["status"] == "OK": st["status"] = run_blocks(blocks, out / (cell + ".jsonl"), deadline, timeout_s)
    except RunError as e:
        st["status"] = e.code
        st["errors"].append(str(e))
    finally:
        st["wall_s"] = round(time.time() - t0, 1)
        (out / (cell + ".status.json")).write_text(json.dumps(st, indent=1))
    return st


def load(jsonl):
    rows = [json.loads(l) for l in open(jsonl)]
    return {(r["arm"], r["hash"]): r for r in rows if r["status"] == "OK"}, [r for r in rows if r["status"] != "OK"]


def summarize_unit(runs, seeds, metric, rng):
    med = {a: median([runs[(a, s)] for s in seeds if (a, s) in runs]) for a in ARMS if any((a, s) in runs for s in seeds)}
    fx = fastest_perx(med)
    res = {"metric": metric, "median_by_arm": med, "fastest_perx": fx}
    for name, den in (("final/fastest_perx", fx), ("final/perx-layout", "perx-layout"), ("final/hl-0.5", HL), ("final/copair-deg", REF)):
        r = [runs[(FINAL, s)] / runs[(den, s)] for s in seeds if den and (FINAL, s) in runs and runs.get((den, s))]
        res[name] = dict(ratio_label(r, rng), per_seed=r) if r else None
    return res


def summarize_badges(d=OUT_E):
    d = Path(d)
    rng = random.Random(BOOT_SEED)
    S = {}
    for jp in sorted(d.glob("*.jsonl")):
        stt = json.load(open(d / (jp.stem + ".status.json")))
        ok, bad = load(jp)
        if stt["status"] != "OK" or bad:
            S[jp.stem] = {"status": stt["status"], "errors": stt["errors"], "failed_rows": len(bad)}
            continue
        seeds = [s for s in stt["seeds"] if all((a, s) in ok for a in ARMS)]
        s = {"status": "OK", "host": stt["host"], "cpu": stt["cpu"], "seeds": seeds, "errors": stt["errors"],
             "trace_R1R5_minus_lines": stt["trace_R1R5_minus_lines"], "any_run_has_R1R5_minus": any(r["has_minus"] for r in ok.values())}
        s["grant"] = summarize_unit({k: r["grant_mean_ns"] for k, r in ok.items()}, seeds, "mean grant-update ns (R1+, R5+)", rng)
        s["overall"] = summarize_unit({k: r["mean_update_ns"] for k, r in ok.items()}, seeds, "mean update ns, all updates", rng)
        S[jp.stem] = s
    (d / "summary.json").write_text(json.dumps(S, indent=1))
    md = ["# badges summary", ""]
    for k, s in S.items():
        md.append("## %s (%s)" % (k, s["status"]))
        for g in ("grant", "overall"):
            if g not in s:
                md.append("no reading: %s" % s.get("errors"))
                break
            u = s[g]
            md.append("- %s: fastest PerX %s; medians %s" % (u["metric"], u["fastest_perx"], {a: round(v, 1) for a, v in u["median_by_arm"].items()}))
            for r in ("final/fastest_perx", "final/perx-layout", "final/hl-0.5", "final/copair-deg"):
                x = u[r]
                if x is None: continue
                md.append("  - %s: median %.4g CI [%.4g, %.4g] %s" % (r, x["median"], *x["ci95"], x["label"]))
        md.append("")
    (d / "summary.md").write_text("\n".join(md) + "\n")
    return S


def summarize_regime(d=OUT_R):
    d = Path(d)
    rng = random.Random(BOOT_SEED)
    cells = {}
    for n in regime.NS:
        for q in regime.QS:
            c = "%d_%s" % (n, q)
            jp = d / (c + ".jsonl")
            if not jp.exists(): continue
            stt = json.load(open(d / (c + ".status.json")))
            ok, bad = load(jp)
            if stt["status"] != "OK" or bad:
                cells[c] = {"n": n, "q": float(q), "status": stt["status"], "errors": stt["errors"], "label": None}
                continue
            seeds = [s for s in SEEDS_R if all((a, s) in ok for a in ARMS)]
            u = summarize_unit({k: r["mean_update_ns"] for k, r in ok.items()}, seeds, "mean update ns", rng)
            f = u["final/fastest_perx"]
            cells[c] = {"n": n, "q": float(q), "q_n": round(float(q) * n, 9), "status": "OK", "host": stt["host"], "cpu": stt["cpu"], "unit": u,
                        "median_ratio": f["median"], "ci95": f["ci95"], "label": f["label"], "fastest_perx": u["fastest_perx"]}
    res = {"cells": cells, "prediction": check_prediction({c: v for c, v in cells.items() if v["label"]})}
    (d / "summary.json").write_text(json.dumps(res, indent=1))
    md = ["# regime summary", "", "prediction: %s" % json.dumps(res["prediction"]), "",
          "| n | q | q*n | fastest PerX | median final/fastest | CI95 | label | vs perx-layout |", "|---|---|---|---|---|---|---|---|"]
    for v in cells.values():
        if v["label"]:
            b = v["unit"]["final/perx-layout"]
            md.append("| %d | %g | %g | %s | %.4g | [%.4g, %.4g] | %s | %.4g %s |" % (v["n"], v["q"], v["q_n"], v["fastest_perx"], v["median_ratio"], *v["ci95"], v["label"], b["median"], b["label"]))
        else: md.append("| %d | %g |  |  |  |  | %s |  |" % (v["n"], v["q"], v["status"]))
    (d / "summary.md").write_text("\n".join(md) + "\n")
    try: heat(res, d / "regime.pdf")
    except ImportError as e: print("heat map skipped (needs matplotlib):", e, file=sys.stderr)
    return res


def heat(res, out):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LogNorm
    from matplotlib.patches import Rectangle
    H = math.sqrt(10)
    fig, ax = plt.subplots(figsize=(7, 5.5))
    cmap, norm = plt.get_cmap("RdBu_r"), LogNorm(vmin=0.25, vmax=4)
    for c in res["cells"].values():
        r = c.get("median_ratio")
        ax.add_patch(Rectangle((c["q"] / H, c["n"] / H), c["q"] * (H - 1 / H), c["n"] * (H - 1 / H), fc=cmap(norm(r)) if r else "lightgrey", ec="k"))
        ax.text(c["q"], c["n"], "%s\n%s" % ("%.3g" % r if r else "-", c["label"] or c["status"]), ha="center", va="center", fontsize=7)
    fig.colorbar(plt.cm.ScalarMappable(norm=norm, cmap=cmap), ax=ax, label="median final / fastest PerX mean update time (<1: CoPair faster)")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlim(1e-4 / H, 0.1 * H)
    ax.set_ylim(10 / H, 1e4 * H)
    ax.set_xlabel("hub R1 toggle probability q")
    ax.set_ylabel("n light x per hub")
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, bbox_inches="tight")
    print("wrote", out)


def main(argv=None):
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("badges")
    p.add_argument("--stream", required=True, choices=list(badge_events.SE))
    p.add_argument("--seeds", default="1,2,3", help="smoke: e.g. 1")
    p.add_argument("--no-check", action="store_true")
    p = sp.add_parser("regime")
    p.add_argument("--cell", required=True)
    p.add_argument("--ops", type=int, default=regime.OPS, help="smoke only")
    p.add_argument("--check-seeds", type=int, default=1, help="digest check on the first K instance seeds (default 1)")
    for c in ("badges", "regime"):
        sp.choices[c].add_argument("--out")
        sp.choices[c].add_argument("--limit-s", type=int, default=3300)
    p = sp.add_parser("summarize")
    p.add_argument("which", choices=["badges", "regime"])
    p.add_argument("--out")
    a = ap.parse_args(argv)
    t0 = time.time()
    if a.cmd == "badges":
        st = run_badges(a.stream, a.out or OUT_E, a.limit_s, [int(x) for x in a.seeds.split(",")], not a.no_check)
    elif a.cmd == "regime":
        st = run_regime(a.cell, a.out or OUT_R, a.limit_s, a.ops, a.check_seeds)
    else:
        (summarize_badges if a.which == "badges" else summarize_regime)(*([a.out] if a.out else []))
        return 0
    print(st["status"], st["errors"], "wall_s %.1f" % (time.time() - t0))
    return 0 if st["status"] == "OK" else 1


if __name__ == "__main__":
    sys.exit(main())
