from copair import paths

BINARIES = {n: {"path": str(paths.BUILD / (n + "-perf"))} for n in ("perx", "copair", "hl")}


def _perx(*a):
    return ("perx", ["--algo", *a])


def _copair(*a):
    return ("copair", ["--algo", *a])


ARMS = {
    "copair-deg": _copair("copair-deg"),
    "copair-deg-nolight": ("copair", ["--algo", "copair-deg", "--psi", "0"]),
    "copair": _copair("copair"),
    "copair-simple": _copair("copair-simple"),
    "copair-base": _perx("copair-base"),
    "perx-base": _perx("perx", "--variant", "base"),
    "perx-tuned": _perx("perx", "--variant", "tuned"),
    "perx-tuned-index": _perx("perx", "--variant", "tuned-index"),
    "perx-layout": _perx("perx", "--variant", "layout"),
    "perx-known-state": _copair("perx", "--variant", "known-state"),
    "perx-known-state-index": _copair("perx", "--variant", "known-state-index"),
    "hl-0.5": ("hl", ["--algo", "hl", "--eps", "0.5"], "plain"),
}
PERX = ["perx-tuned", "perx-tuned-index", "perx-layout", "perx-known-state", "perx-known-state-index"]

GEN = {
    "hub-distinct": ["hub", "--n", "10000", "--H", "16", "--variant", "d", "--ops", "200000", "--enum_every", "20000"],
    "hub-shared": ["hub", "--n", "10000", "--H", "16", "--variant", "s", "--ops", "20000", "--enum_every", "2000"],
    "heavy-centre": ["heavy_centre", "--K", "100", "--s", "50", "--density", "0.02", "--ops", "200000", "--enum_every", "50000"],
    "mid-degree": ["mid_degree", "--n", "1000", "--H", "16", "--k", "32", "--ops", "200000", "--enum_every", "20000"],
    "pool-heavy": ["pool", "--n", "20000", "--s", "3", "--pool", "60", "--ops", "20000", "--enum_every", "2000"],
    "pool-light": ["pool", "--n", "20000", "--s", "2", "--pool", "200", "--ops", "20000", "--enum_every", "2000"],
    "zipf": ["zipf", "--D", "100000", "--alpha", "1.1", "--init", "100000", "--ops", "200000", "--enum_every", "50000"],
}
for _w, _v in (("osc-x", "x"), ("osc-a", "a"), ("osc-ab", "ab"), ("grow-shrink", "grow")):
    GEN[_w] = ["churn", "--variant", _v, "--ops", "200000", "--enum_every", "20000"]
FIXED = {k: str(v) for k, v in paths.FIXED_TRACES.items()}

_FAST = (.15, .15, .15)
_COST = {"mathoverflow": _FAST, "wikidata-member": (14, 11, 11), "wikidata-hub": (14, 11, 11), "wikidata-control": (14, 11, 11),
         "mid-degree": (5, 1.6, 1.6), "pool-light": (.5, .25, .25), "hub-distinct": (.7, 23, 14), "heavy-centre": (5, 3, 3),
         "zipf": (1.7, 1.1, 1.1), "pool-heavy": (.4, .2, .2), "hub-shared": (.3, 3.4, 1.5),
         "osc-x": _FAST, "osc-a": _FAST, "osc-ab": _FAST, "grow-shrink": _FAST}
_HL = {"hub-distinct": 36, "hub-shared": 6, "heavy-centre": 4.2, "mid-degree": 2.5, "pool-heavy": .4, "pool-light": 2.2, "zipf": 2.3,
       "mathoverflow": .2, "wikidata-member": 20, "wikidata-hub": 21, "wikidata-control": 21}


def cost(cell, arm):
    if arm == "hl-0.5": return _HL.get(cell, .2)
    c, e, g = _COST[cell]
    return c if arm.startswith("copair") else (g if arm == "perx-layout" else e)


def _cell(name, arms, units, reps, modes=("perop",)):
    d = {"trace": FIXED[name]} if name in FIXED else {"gen": list(GEN[name])}
    d.update(units=units, reps=reps, modes=list(modes), arms=list(arms), cost_s={a: cost(name, a) for a in arms})
    return d


def _hs(*h):
    return [{"inst": None, "hash": x} for x in h]


def _spec(phase, shards, cells, checks=()):
    arms = sorted({a for c in cells.values() for a in c["arms"]} | {x for ch in checks for p in ch["pairs"] for x in p})
    amap = {a: dict({"bin": ARMS[a][0], "argv": ARMS[a][1]}, **({"plain": True} if len(ARMS[a]) > 2 else {})) for a in arms}
    b = {k: v for k, v in BINARIES.items() if k in {x["bin"] for x in amap.values()}}
    return {"phase": phase, "shards": shards, "replicate_all": False, "binaries": b, "cells": cells, "checks": list(checks), "arms": amap}


def _matrix():
    ARMS["copair-deg-repeat"] = ARMS["copair-deg"]
    pri = {"mathoverflow", "wikidata-member", "mid-degree", "pool-light", "hub-distinct", "wikidata-hub"}
    cells = {}
    for name in ["hub-distinct", "hub-shared", "heavy-centre", "mid-degree", "pool-heavy", "pool-light", "zipf", "osc-a", "osc-ab", "grow-shrink", "osc-x",
                 "mathoverflow", "wikidata-hub", "wikidata-control", "wikidata-member"]:
        units = _hs(*range(9401, 9406)) if name in FIXED else [{"inst": 9401 + i, "hash": 9411 + i} for i in range(5)]
        arms = ["copair-deg", "copair", "copair-simple", "hl-0.5", *PERX] + (["copair-deg-repeat"] if name in pri else [])
        cells[name] = _cell(name, arms, units, 4 if name in pri else 3, ("perop", "noperop") if name == "mathoverflow" else ("perop",))
    checks = [{"cell": c, "unit": 0, "pairs": [["copair-deg", "copair-base"], ["copair", "copair-base"], ["copair-simple", "copair-base"]] + ([["perx-known-state", "perx-base"], ["perx-known-state-index", "perx-base"]] if c in pri else [])} for c in cells]
    return _spec("matrix", 12, cells, checks)


def _nscan(wl, variant):
    hist = {100: (.1, .3, .5, .6, .1), 1000: (.2, 2.6, 3.7, 5.7, .2), 10000: (.5, 18, 28, 48, .5), 100000: (5, 67, 108, 89, 5)}
    cells = {}
    for n in (100, 1000, 10000, 100000):
        ops, ee = (20000, 2000) if (n == 100000 or (wl == "hub-shared" and n == 10000)) else (200000, 20000)
        g = ["hub", "--n", str(n), "--H", "16", "--variant", variant, "--ops", str(ops), "--enum_every", str(ee)]
        arms = ["copair-deg", "perx-layout", "hl-0.5", "copair-deg-nolight", "copair"]
        cells["%s_n%d" % (wl, n)] = dict(gen=g, units=[{"inst": 1001 + i, "hash": 9601 + i} for i in range(5)], reps=1, modes=["perop"], arms=arms,
                                         cost_s=dict(zip(arms, hist[n])))
    return _spec("nscan-" + wl, 1, cells)


SPECS = {"matrix": _matrix(), "nscan-hub-distinct": _nscan("hub-distinct", "d"), "nscan-hub-shared": _nscan("hub-shared", "s")}
