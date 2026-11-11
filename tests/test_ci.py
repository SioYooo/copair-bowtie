import math
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))
from copair.experiments import ci as C


def row(cell, mode, blk, arm, T, inst=None, h=1, **kw):
    r = dict(cell=cell, mode=mode, block_id=blk, arm=arm, inst=inst, hash=h, overall={"mean_ns": T, "p99_ns": T * 2},
             update_total_ns=T, preprocessing_ns=100, peak_rss_kib=50, enumerations=[{"total_ns": 10, "count": 5, "start_to_first_ns": 3}])
    r.update(kw)
    return r


def const_rows(cell, mode, ratio, n=3, arms=("a", "b")):
    out = []
    for u in range(n):
        for rep in range(2):
            blk = "%s/%d/%d" % (cell, u, rep)
            out += [row(cell, mode, blk, arms[0], 100 * ratio, h=u), row(cell, mode, blk, arms[1], 100, h=u)]
    return out


assert C.median([3, 1, 2]) == 2 and C.median([1, 2, 3, 10]) == 2.5
assert C.mad([1, 2, 3, 4, 100]) == 1
assert C.tq(1) == 12.706 and C.tq(30) == 2.042 and abs(C.tq(40) - (1.96 + 0.06)) < 1e-12

iv = C.interval([math.log(0.9), math.log(1.1)])
assert abs(iv[0] - 0.99499) < 1e-4 and abs(iv[1] - 0.2782) < 2e-3 and abs(iv[2] - 3.5586) < 2e-3, iv
assert C.interval([0.1]) is None

assert C.classify(0.8, 0.94) == "FASTER" and C.classify(0.96, 1.04) == "PARITY" and C.classify(0.95, 1.05) == "PARITY"
assert C.classify(0.90, 1.00) == "NONINFERIOR-ONLY" and C.classify(1.06, 1.2) == "SLOWER" and C.classify(0.9, 1.2) == "UNCERTAIN"

for r, cls in ((0.9, "FASTER"), (1.0, "PARITY"), (1.2, "SLOWER")):
    res = C.analyze(const_rows("m", "perop", r), [("a", "b")])
    assert len(res) == 1 and res[0]["cls"] == cls and abs(res[0]["ratio"] - r) < 1e-9, res
    assert res[0]["n_blocks"] == 6 and res[0]["n_units"] == 3 and res[0]["aa_spread"] < 1e-12
    assert abs(res[0]["cost_ratios"]["peak_rss_kib"] - 1) < 1e-12 and abs(res[0]["cost_ratios"]["p99_ns"] - r) < 1e-9
    assert res[0]["cost_ratios"]["enum_per_answer_ns"] == 1 and res[0]["cost_ratios"]["first_answer_ns"] == 1
assert C.analyze(const_rows("m", "perop", 1.0, n=1), [("a", "b")])[0]["cls"] == "NA"
assert C.analyze(const_rows("m", "perop", 1.0), [("a", "b")], invalid=["b"])[0]["cls"] == "INVALID"
rs = const_rows("m", "perop", 1.0) + [row("m", "perop", "orphan", "a", 5)]
x = C.analyze(rs, [("a", "b")])[0]
assert x["n_skipped"] == 1 and x["n_blocks"] == 6

rs = []
for u in range(2):
    for rep, T in enumerate((100, 200)):
        rs += [row("m", "perop", "m/%d/%d" % (u, rep), "a", T, h=u), row("m", "perop", "m/%d/%d" % (u, rep), "b", 100, h=u)]
x = C.analyze(rs, [("a", "b")])[0]
assert abs(x["ratio"] - 1.5) < 1e-12, x
assert abs(x["aa_spread"] - math.log(2) / 2) < 1e-12

rs = []
for inst, h, ratio in ((1, 1, 1.0), (1, 2, 4.0), (2, 3, 2.0), (3, 4, 2.0)):
    rs += [row("m", "perop", "m/%d" % h, "a", 100 * ratio, inst=inst, h=h), row("m", "perop", "m/%d" % h, "b", 100, inst=inst, h=h)]
assert C.analyze(rs, [("a", "b")])[0]["n_units"] == 4
x = C.analyze(rs, [("a", "b")], cluster="inst")[0]
assert x["n_units"] == 3 and abs(x["ratio"] - 2.0) < 1e-9 and abs(x["lo"] - 2.0) < 1e-9, x

rs = []
for u in range(3):
    for mode in ("perop", "noperop"):
        blk = "mathoverflow/%d/%s" % (u, mode)
        rs += [row("mathoverflow", mode, blk, "a", 100, h=u, update_total_ns=150), row("mathoverflow", mode, blk, "b", 100, h=u, update_total_ns=100)]
res = {r["mode"]: r for r in C.analyze(rs, [("a", "b")])}
assert res["noperop"]["primary"] and not res["perop"]["primary"]
assert abs(res["noperop"]["ratio"] - 1.5) < 1e-9 and abs(res["perop"]["ratio"] - 1.0) < 1e-9
res = {r["mode"]: r for r in C.analyze(rs, [("a", "b")], noperop_cells=())}
assert res["perop"]["primary"] and not res["noperop"]["primary"]
print("test_ci: all asserts passed")
