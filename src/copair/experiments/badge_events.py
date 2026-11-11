import json
import math
import statistics
import subprocess
import time

from copair import paths

TR = paths.TRACES / "badges"
SE = {"se-badges-mathoverflow": "mathoverflow.net", "se-badges-stats": "stats.stackexchange.com",
      "se-badges-askubuntu": "askubuntu.com", "se-badges-superuser": "superuser.com"}


def _lines(path):
    with open(path) as fh:
        for line in fh:
            h = line.find("#")
            if h >= 0: line = line[:h]
            t = line.split()
            if t: yield t


def prep(stream):
    from copair.datasets import adapters
    TR.mkdir(parents=True, exist_ok=True)
    with open(TR / (stream + ".trace"), "w") as f:
        adapters.se_badges(f, paths.DOWNLOADS / "se/events" / (SE[stream] + ".tsv.gz"))


class RunError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


def sh(cmd, deadline):
    left = deadline - time.time()
    if left <= 1: raise RunError("TIMEOUT", "job wall limit reached")
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=left)
    except subprocess.TimeoutExpired:
        raise RunError("TIMEOUT", "timeout running %s" % cmd[:6])
    if r.returncode != 0:
        raise RunError("MEM" if r.returncode in (-9, 137) else "INFRA", "rc=%d %s" % (r.returncode, r.stderr[-300:]))
    return r


def digests(path):
    return [(d["op_index"], d["count"], d["sum"], d["xor"]) for d in map(json.loads, open(path))]


def median(v): return statistics.median(v)


def boot_median_ci(v, rng, nres=10000, budget=2 * 10**8):
    n = len(v)
    if n == 0: return None, None, 0
    nres = int(min(nres, max(200, budget // n)))
    ms = sorted(median(rng.choices(v, k=n)) for _ in range(nres))
    return ms[int(0.025 * (nres - 1))], ms[int(math.ceil(0.975 * (nres - 1)))], nres
