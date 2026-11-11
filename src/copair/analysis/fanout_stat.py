import argparse
import json
import time
from collections import defaultdict


def _add(d, k, v):
    d[k].add(v)


def _rm(d, k, v):
    s = d[k]
    s.discard(v)
    if not s: del d[k]


def stat(path, ret_f=False):
    t0 = time.time()
    R = {r: set() for r in range(1, 7)}

    adj = {2: defaultdict(set), 3: defaultdict(set), 4: defaultdict(set), 6: defaultdict(set)}
    deg = defaultdict(int)
    heavy = set()
    N = 0
    st = {"N0": None, "psi": None}
    fs, nup = [], 0

    def epoch():
        st["N0"] = max(N, 1)
        st["psi"] = st["N0"] ** 0.25
        heavy.clear()
        heavy.update(x for x, d in deg.items() if d > st["psi"])

    def apply(r, u, v, ins):
        nonlocal N
        S = R[r]
        if ins:
            if (u, v) in S: return False
            S.add((u, v))
            N += 1
        else:
            if (u, v) not in S: return False
            S.discard((u, v))
            N -= 1

        if r in adj:
            x, key = (v, u) if r in (2, 6) else (u, v)
            (_add if ins else _rm)(adj[r], key, x)
            deg[x] += 1 if ins else -1
            if deg[x] == 0: del deg[x]
            d = deg.get(x, 0)
            if st["N0"] is not None:
                if x in heavy:
                    if d < st["psi"] / 2: heavy.discard(x)
                elif d > 2 * st["psi"]: heavy.add(x)
        return True

    def fan(s1, s2):
        if len(s1) > len(s2): s1, s2 = s2, s1
        return sum(1 for x in s1 if x in s2 and x not in heavy)

    with open(path) as fh:
        for line in fh:
            if line[0] in "#E\n":
                continue
            c, r, u, v = line.split()
            r, u, v = int(r[1:]), int(u), int(v)
            if c == "L":
                apply(r, u, v, True)
                continue
            if st["N0"] is None: epoch()
            ins = c == "+"
            if r == 1:
                f = fan(adj[2].get(v, ()), adj[3].get(u, ()))
            elif r == 5:
                f = fan(adj[4].get(u, ()), adj[6].get(v, ()))
            else:
                f = None
            if apply(r, u, v, ins):
                nup += 1
                if f is not None: fs.append(f)
                if not (st["N0"] / 4 <= N <= 2 * st["N0"]): epoch()

    n = len(fs)
    s = sorted(fs)
    q = lambda p: s[int(p * (n - 1))] if n else None
    return {"fs": fs} if ret_f else {"n_updates": nup, "n_R1R5": n, "s_100": sum(1 for f in fs if f >= 100) / nup if nup else None,
            "share_R1R5_f>=10": sum(1 for f in fs if f >= 10) / n if n else None,
            "mean_f": sum(fs) / n if n else None, "p50": q(.5), "p90": q(.9), "p99": q(.99), "max_f": s[-1] if n else None,
            "runtime_s": round(time.time() - t0, 2)}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    r = stat(a.trace)
    json.dump(r, open(a.out, "w"), indent=1)
    print(r)
