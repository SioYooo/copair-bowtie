import bisect
import collections
import gzip
import hashlib
import os

from copair import paths
from copair.workloads.synthetic import _w

DIRS = [str(paths.SX)]
MAPS = {"a2q": (1, 4), "c2q": (2, 5), "c2a": (3, 6)}


def _find(fn):
    for d in DIRS:
        if os.path.exists(os.path.join(d, fn)): return os.path.join(d, fn)
    raise FileNotFoundError(fn)


def _stream(out, ev, ts, ups, W, max_ops, enum_every, window_days):
    j0 = bisect.bisect_left(ts, ts[0] + W)
    p = bisect.bisect_right(ts, ts[j0] - W)
    cnt = collections.Counter()
    for e in ev[p:j0]:
        for u in ups(e): cnt[u] += 1
    for u in sorted(cnt): _w(out, "L", *u)
    out.write("# initial window: %d events, %d tuples; streaming from event %d of %d; window_days=%s\n" % (j0 - p, len(cnt), j0, len(ev), window_days))
    done, j, enum_every = 0, j0, int(enum_every)
    while done < int(max_ops) and (j < len(ev) or p < j):
        if p < j and (j >= len(ev) or ts[p] + W <= ts[j]):
            for u in ups(ev[p]):
                cnt[u] -= 1
                if cnt[u] == 0:
                    _w(out, "-", *u)
                    done += 1
                    if done % enum_every == 0: out.write("E\n")
            p += 1
        else:
            for u in ups(ev[j]):
                cnt[u] += 1
                if cnt[u] == 1:
                    _w(out, "+", *u)
                    done += 1
                    if done % enum_every == 0: out.write("E\n")
            j += 1
    out.write("E\n")


def sx(out, rng, name="mathoverflow", window_days=30, max_ops=100000, enum_every=5000):
    names = ["a2q", "c2q", "c2a"]
    ev, hdr = [], []
    for fi, nm in enumerate(names):
        p = _find("sx-%s-%s.txt.gz" % (name, nm))
        hdr.append("# file %s sha256=%s" % (os.path.basename(p), hashlib.sha256(open(p, "rb").read()).hexdigest()[:16]))
        with gzip.open(p, "rt") as f:
            for li, line in enumerate(f):
                a = line.split()
                if len(a) >= 3: ev.append((int(a[2]), fi, li, int(a[0]), int(a[1])))
    ev.sort()
    out.write("\n".join(hdr) + "\n# mapping: a2q(u,v)->R1(u,v),R4(u,v); c2q->R2,R5; c2a->R3,R6; events merged by (ts, a2q<c2q<c2a, file order)\n")
    def ups(e):
        m = MAPS[names[e[1]]]
        return [(m[0], e[3], e[4]), (m[1], e[3], e[4])]
    _stream(out, ev, [e[0] for e in ev], ups, int(float(window_days) * 86400), max_ops, enum_every, window_days)


def onetype(out, rng, path, window_days=30, max_ops=100000, enum_every=5000):
    ev = []
    with gzip.open(path, "rt") as f:
        for li, line in enumerate(f):
            a = line.split()
            if len(a) >= 3: ev.append((int(a[2]), li, int(a[0]), int(a[1])))
    ev.sort()
    out.write("# file %s sha256=%s\n# mapping: every edge (u,v)->R1..R6(u,v) (one edge type); events by (ts, file order)\n" % (
        os.path.basename(path), hashlib.sha256(open(path, "rb").read()).hexdigest()[:16]))
    _stream(out, ev, [e[0] for e in ev], lambda e: [(r, e[2], e[3]) for r in range(1, 7)],
            int(float(window_days) * 86400), max_ops, enum_every, window_days)
