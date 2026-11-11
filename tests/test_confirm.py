import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))
from copair import paths
from copair.experiments import confirm as c


def test_argv():
    assert c.algo_argv("copair-deg", 255, "x") == ["--algo", "copair-deg", "--opts", "255"]
    assert c.algo_argv("copair-deg-nolight", 159, "x")[-2:] == ["--psi", "0"]
    assert c.algo_argv("perx", 159, "layout") == ["--algo", "perx", "--variant", "layout"]
    assert c.seeds_of("3-5") == [3, 4, 5] and c.seeds_of("7") == [7]


def test_confirm_and_coverage():
    cand, stats = paths.BUILD / "copair-debug", paths.BUILD / "copair-stats"
    if not (cand.exists() and stats.exists() and (paths.BUILD / "oracle").exists()):
        print("skipped: build first (scripts/build.sh)")
        return
    os.makedirs(paths.RESULTS, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=str(paths.RESULTS)) as td:
        o = os.path.join(td, "o.json")
        rc = c.main(["confirm", "--set", "dev", "--seeds", "0-1", "--candidate", str(cand), "--opts", "255", "--jobs", "4", "--out", o])
        r = json.load(open(o))
        assert rc == 0 and not r["failures"], r["failures"][:3]
        o2 = os.path.join(td, "c.json")
        c.main(["coverage", "--seeds", "301-302", "--opts", "255", "--stats", str(stats), "--out", o2])
        rows = json.load(open(o2))["rows"]
        assert all(x["covered"] for x in rows if x["family"] == "abflip" and x["params"] in ("default", "psi1"))


if __name__ == "__main__":
    test_argv()
    test_confirm_and_coverage()
    print("ok")
