import random
from copair.experiments import reruns as r

U = {"R1+": {"count": 10, "noop": 2, "total_ns": 800}, "R5+": {"count": 5, "noop": 0, "total_ns": 200}, "R1-": {"count": 0, "noop": 0, "total_ns": 0},
     "R2+": {"count": 100, "noop": 0, "total_ns": 10 ** 6}}


def test_metric():
    assert r.grant_mean(U) == 1000 / 13 and r.r15_mean(U) == 1000 / 13 and not r.has_minus(U)
    assert r.has_minus({"R5-": {"count": 1, "noop": 0, "total_ns": 5}})
    assert r.grant_mean({"R2+": U["R2+"]}) is None
    assert r.r15_mean({"R1+": {"count": 4, "noop": 0, "total_ns": 40}, "R1-": {"count": 4, "noop": 0, "total_ns": 60}}) == 12.5


def test_fastest():
    med = {"copair": 1, "perx-layout": 30, "perx-tuned": 20, "perx-tuned-index": 25, "perx-known-state": 22, "perx-known-state-index": 21, "hl-0.5": 0.5}
    assert r.fastest_perx(med) == "perx-tuned"
    assert r.fastest_perx({"perx-layout": 3, "perx-tuned-index": None}) == "perx-layout"


def test_labels():
    rng = random.Random(1)
    assert r.ratio_label([0.1, 0.2, 0.3], rng)["label"] == "BETTER"
    assert r.ratio_label([1.1, 1.2, 1.3], rng)["label"] == "WORSE"
    assert r.ratio_label([0.9, 1.0, 1.1], rng)["label"] == "TIE"
    seeds = (1, 2, 3)
    runs = {(a, s): v for a, v in {"copair": 1, "perx-layout": 4, "perx-tuned": 2, "perx-tuned-index": 3, "perx-known-state": 5, "perx-known-state-index": 6, "hl-0.5": 8}.items() for s in seeds}
    u = r.summarize_unit(runs, seeds, "m", rng)
    assert u["fastest_perx"] == "perx-tuned" and u["final/fastest_perx"]["median"] == 0.5 and u["final/perx-layout"]["label"] == "BETTER"
    assert u["final/hl-0.5"]["median"] == 1 / 8


def test_prediction():
    ok = {"a": {"n": 100, "q": 0.1, "label": "BETTER"}, "b": {"n": 10, "q": 0.1, "label": "TIE"}, "c": {"n": 1000, "q": 0.001, "label": "BETTER"}}
    p = r.check_prediction(ok)
    assert p["verdict"] == "NOT CONFIRMED" and [x["cell"] for x in p["contradicting"]] == ["c"] and p["constrained_cells_checked"] == 3
    assert r.check_prediction({"a": {"n": 10, "q": 0.1, "label": "WORSE"}, "b": {"n": 100, "q": 0.1, "label": "BETTER"}})["verdict"] == "CONFIRMED"
    assert r.predicted(5) is None and r.predicted(10) == "BETTER" and r.predicted(1) == "WORSE_OR_TIE"


if __name__ == "__main__":
    for k, v in list(globals().items()):
        if k.startswith("test_"): v()
    print("ok")
