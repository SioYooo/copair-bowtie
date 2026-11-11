import argparse
import collections
import json
import math
import os
import sys

T975 = [None, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228, 2.201, 2.179, 2.160, 2.145, 2.131,
        2.120, 2.110, 2.101, 2.093, 2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042]
LO, HI = 0.95, 1.05


def median(x):
    x = sorted(x)
    n = len(x)
    return x[n // 2] if n % 2 else (x[n // 2 - 1] + x[n // 2]) / 2


def mad(x):
    m = median(x)
    return median([abs(v - m) for v in x])


def tq(df):
    return T975[df] if df <= 30 else 1.96 + 2.4 / df


def interval(logs):
    n = len(logs)
    if n < 2:
        return None
    m = sum(logs) / n
    se = math.sqrt(sum((v - m) ** 2 for v in logs) / (n - 1) / n)
    h = tq(n - 1) * se
    return math.exp(m), math.exp(m - h), math.exp(m + h)


def classify(lo, hi):
    if hi < LO:
        return "FASTER"
    if lo >= LO and hi <= HI:
        return "PARITY"
    if lo > HI:
        return "SLOWER"
    if hi <= HI:
        return "NONINFERIOR-ONLY"
    return "UNCERTAIN"


def stat(r, mode):
    return r["overall"]["mean_ns"] if mode == "perop" else r["update_total_ns"]


def _get(r, path):
    for p in path.split("."):
        r = r.get(p) if isinstance(r, dict) else None
        if r is None:
            return None
    return r


def _enum_per_answer(r):
    e = r.get("enumerations") or []
    c = sum(x["count"] for x in e)
    return sum(x["total_ns"] for x in e) / c if c else None


def _first(r):
    e = r.get("enumerations") or []
    return median([x["start_to_first_ns"] for x in e]) if e else None


COSTS = {"peak_rss_kib": lambda r: r.get("peak_rss_kib", r.get("rss_kib")), "preprocessing_ns": lambda r: r.get("preprocessing_ns"),
         "p99_ns": lambda r: _get(r, "overall.p99_ns"), "enum_per_answer_ns": _enum_per_answer, "first_answer_ns": _first,
         "capacity_total_bytes": lambda r: _get(r, "capacity.total_bytes")}


def load_runs(d):
    return [json.loads(l) for l in open(os.path.join(d, "runs.jsonl")) if l.strip()]


def analyze(rows, pairs, cells=None, noperop_cells=("mathoverflow",), cluster="unit", invalid=()):
    blocks = collections.defaultdict(dict)
    for r in rows:
        if cells is None or r["cell"] in cells:
            blocks[(r["cell"], r["mode"], r["block_id"])][r["arm"]] = r
    out = []
    for a, b in pairs:
        for cell, mode in sorted({(k[0], k[1]) for k in blocks}):
            units, skipped, logs, costs = collections.defaultdict(list), 0, [], collections.defaultdict(list)
            for (c, m, bid), arms in blocks.items():
                if (c, m) != (cell, mode):
                    continue
                if a not in arms or b not in arms or not (stat(arms[a], m) > 0 and stat(arms[b], m) > 0):
                    skipped += 1
                    continue
                ra, rb = arms[a], arms[b]
                lr = math.log(stat(ra, m) / stat(rb, m))
                logs.append(lr)
                units[(ra["inst"], ra["hash"])].append(math.exp(lr))
                for n, f in COSTS.items():
                    x, y = f(ra), f(rb)
                    if x is not None and y:
                        costs[n].append(x / y)
            ulog = {u: math.log(median(v)) for u, v in units.items()}
            if cluster == "inst":
                g = collections.defaultdict(list)
                for (inst, h), v in ulog.items():
                    g[("inst", inst) if inst is not None else ("unit", h)].append(v)
                ulog = {k: sum(v) / len(v) for k, v in g.items()}
            iv = interval(list(ulog.values()))
            if a in invalid or b in invalid:
                cls = "INVALID"
            elif iv is None:
                cls = "NA"
            else:
                cls = classify(iv[1], iv[2])
            out.append(dict(pair="%s/%s" % (a, b), cell=cell, mode=mode, primary=mode == ("noperop" if cell in noperop_cells else "perop"),
                            n_blocks=len(logs), n_skipped=skipped, n_units=len(ulog), ratio=iv and iv[0], lo=iv and iv[1], hi=iv and iv[2],
                            cls=cls, aa_spread=mad(logs) if logs else None,
                            cost_ratios={n: median(v) for n, v in costs.items()}))
    return out


def _f(x, p=4):
    return "-" if x is None else "%.*f" % (p, x)


def to_md(res):
    h = "| pair | cell | mode | blocks (skipped) | units | ratio | 95% CI | class | A/A MAD(log) | cost ratios |\n|---|---|---|---|---|---|---|---|---|---|\n"
    rows = sorted(res, key=lambda r: (r["pair"], r["cell"], not r["primary"]))
    return h + "\n".join("| %s | %s | %s%s | %d (%d) | %d | %s | [%s, %s] | %s | %s | %s |" % (
        r["pair"], r["cell"], r["mode"], "" if r["primary"] else " (secondary)", r["n_blocks"], r["n_skipped"], r["n_units"],
        _f(r["ratio"]), _f(r["lo"]), _f(r["hi"]), r["cls"], _f(r["aa_spread"]),
        ", ".join("%s %s" % (k, _f(v, 3)) for k, v in r["cost_ratios"].items())) for r in rows) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(prog="copair.experiments.ci")
    ap.add_argument("--dir")
    ap.add_argument("--aa")
    ap.add_argument("--pairs")
    ap.add_argument("--pair")
    ap.add_argument("--cells")
    ap.add_argument("--noperop-cells", default="mathoverflow")
    ap.add_argument("--cluster", choices=["unit", "inst"], default="unit")
    ap.add_argument("--exclude-invalid")
    ap.add_argument("--out")
    ap.add_argument("--md")
    a = ap.parse_args(argv)
    cells = a.cells.split(",") if a.cells else None
    npc = [c for c in a.noperop_cells.split(",") if c]
    if a.aa:
        pr = [tuple(a.pair.split("/"))]
        rs = [[r for r in analyze(load_runs(d), pr, cells, npc, a.cluster) if r["primary"]] for d in a.aa.split(",")]
        print("| cell | CI1 | MAD1 | CI2 | MAD2 | MAD2/MAD1 |\n|---|---|---|---|---|---|")
        for r1 in rs[0]:
            r2 = next((r for r in rs[1] if r["cell"] == r1["cell"]), None)
            if r2:
                print("| %s | [%s, %s] | %s | [%s, %s] | %s | %s |" % (r1["cell"], _f(r1["lo"]), _f(r1["hi"]), _f(r1["aa_spread"]), _f(r2["lo"]), _f(r2["hi"]),
                      _f(r2["aa_spread"]), _f(r2["aa_spread"] / r1["aa_spread"] if r1["aa_spread"] else None, 2)))
        return 0
    inv = json.load(open(a.exclude_invalid))["invalid_arms"] if a.exclude_invalid else []
    res = analyze(load_runs(a.dir), [tuple(p.split("/")) for p in a.pairs.split(",")], cells, npc, a.cluster, inv)
    if a.out:
        json.dump(res, open(a.out, "w"), indent=1)
    md = to_md(res)
    if a.md:
        open(a.md, "w").write(md)
    print(md)
    return 0


if __name__ == "__main__":
    sys.exit(main())
