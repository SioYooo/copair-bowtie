import calendar
import csv
import hashlib
import io
import os
import time
import zipfile

from copair.datasets import stackexchange
from copair.datasets.stackexchange import _w

BIG = 10**12
FMTS = {"movielens": (",", 1, "sec"), "hetrec-delicious": ("\t", 1000, "ms"), "hetrec-lastfm": ("\t", 1000, "ms")}


def _sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()[:16]


def open_text(path):
    path = str(path)
    if "::" not in path:
        return open(path, encoding="utf-8", newline=""), {"file": os.path.basename(path), "sha256": _sha(path)}
    zp, mem = path.split("::", 1)
    z = zipfile.ZipFile(zp)
    names = z.namelist()
    if mem not in names:
        cand = [n for n in names if n.rsplit("/", 1)[-1] == mem.rsplit("/", 1)[-1]]
        if len(cand) != 1: raise KeyError("member %s not in %s (matches by basename: %s)" % (mem, zp, cand))
        mem = cand[0]
    return io.TextIOWrapper(z.open(mem), encoding="utf-8", newline=""), {
        "file": os.path.basename(zp), "sha256": _sha(zp), "member": mem}


def _ts(s):
    return calendar.timegm(time.strptime(s.strip(), "%Y-%m-%d %H:%M:%S"))


def reddit(out, paths, window_days=30, max_ops=None):
    ev, hdr = [], []
    for fi, p in enumerate(paths):
        hdr.append("# file %s sha256=%s" % (os.path.basename(p), _sha(p)))
        with open(p, encoding="utf-8", newline="") as f:
            next(f)
            for li, line in enumerate(f):
                a = line.rstrip("\r\n").split("\t")
                if len(a) >= 4: ev.append((_ts(a[3]), fi, li, a[0], a[1]))
    ev.sort()
    ids = {}
    ev = [(t, fi, li, ids.setdefault(u, len(ids)), ids.setdefault(v, len(ids))) for t, fi, li, u, v in ev]
    out.write("\n".join(hdr) + "\n# mapping: subreddit->dense id in first-seen order; every event (u,v)->R1..R6(u,v) (one edge type); events by (ts, file index, line index)\n")
    stackexchange._stream(out, ev, [e[0] for e in ev], lambda e: [(r, e[3], e[4]) for r in range(1, 7)],
                  int(float(window_days) * 86400), BIG if max_ops is None else int(max_ops), 5000, window_days)
    return {"mapping": "one edge type: (u,v)->R1..R6", "window_days": window_days, "max_ops": max_ops, "subreddits": len(ids),
            "events": len(ev), "files": [h[7:] for h in hdr]}


def tagging(out, path, fmt, window_days=30, max_ops=None):
    sep, div, unit = FMTS[fmt]
    f, finfo = open_text(path)
    ev = []
    with f:
        rd = csv.reader(f, delimiter=sep)
        next(rd)
        for li, a in enumerate(rd):
            if len(a) < 4: continue
            u, i, t, ts = a[0], a[1], a[2], a[3]
            if fmt == "movielens":
                t = t.lower().strip()
                if not t: continue
            ev.append((int(float(ts)) // div, 0, li, u, i, t))
    ev.sort()
    d = ({}, {}, {})
    for e in ev:
        d[0].setdefault(e[3], len(d[0]))
        d[1].setdefault(e[5], len(d[1]))
        d[2].setdefault(e[4], len(d[2]))
    ou, ot, oi = 0, len(d[0]), len(d[0]) + len(d[1])
    ev = [(e[0], e[1], e[2], d[0][e[3]] + ou, d[1][e[5]] + ot, d[2][e[4]] + oi) for e in ev]
    out.write("# file %s sha256=%s%s\n# mapping: assignment (user u, tag t, item i) -> R1(u,t),R5(u,t); R2(t,i),R6(t,i); R3(i,u),R4(i,u); "
              "ids dense per kind in first-seen order, offsets users 0, tags %d, items %d; timestamps in seconds (%s -> s); "
              "events by (ts, line index)\n" % (finfo["file"], finfo["sha256"], " member=" + finfo["member"] if "member" in finfo else "", ot, oi, unit))
    ups = lambda e: [(1, e[3], e[4]), (5, e[3], e[4]), (2, e[4], e[5]), (6, e[4], e[5]), (3, e[5], e[3]), (4, e[5], e[3])]
    stackexchange._stream(out, ev, [e[0] for e in ev], ups, int(float(window_days) * 86400), BIG if max_ops is None else int(max_ops), 5000, window_days)
    return {"mapping": "assignment (u,t,i)->R1,R5(u,t); R2,R6(t,i); R3,R4(i,u)", "window_days": window_days, "max_ops": max_ops,
            "fmt": fmt, "events": len(ev), "users": len(d[0]), "tags": len(d[1]), "items": len(d[2]), "file": finfo}


def insert_stream(out, events, ups, initial_frac=0.10, enum_every=5000, max_ops=None):
    cnt = {}
    def apply(e, emit):
        nonlocal done
        adds, dels = ups(e)
        for u in adds:
            cnt[u] = cnt.get(u, 0) + 1
            if cnt[u] == 1 and emit:
                _w(out, "+", *u)
                done += 1
                if done % enum_every == 0: out.write("E\n")
        for u in dels:
            if cnt.pop(u, 0) and emit:
                _w(out, "-", *u)
                done += 1
                if done % enum_every == 0: out.write("E\n")
    done = 0
    lim = BIG if max_ops is None else int(max_ops)
    t0, t1 = events[0][0], events[-1][0]
    cut = t0 + initial_frac * (t1 - t0)
    j = 0
    while j < len(events) and events[j][0] < cut:
        apply(events[j][1], False)
        j += 1
    for u in sorted(cnt): _w(out, "L", *u)
    out.write("# initial database: %d events (ts < %s), %d tuples; streaming from event %d of %d\n" % (j, cut, len(cnt), j, len(events)))
    while j < len(events) and done < lim:
        apply(events[j][1], True)
        j += 1
    out.write("E\n")
    return {"initial_events": sum(1 for t, _ in events if t < cut), "events": len(events), "ts_min": t0, "ts_max": t1}


def _stream_file(out, path, mk, mapping, max_ops):
    from copair.datasets import prepare
    ids = {}
    ev = []
    for r in prepare.read_events(path):
        e = mk(r, lambda k, n: ids.setdefault((k, n), len(ids)))
        if e is not None: ev.append((r[0], e))
    out.write("# file %s sha256=%s\n# mapping: %s\n" % (os.path.basename(str(path)), _sha(path), mapping))
    info = insert_stream(out, ev, lambda e: e, max_ops=max_ops)
    return {"mapping": mapping, "file": {"file": os.path.basename(str(path)), "sha256": _sha(path)}, "ids": len(ids), **info}


def se_badges(out, tsv, max_ops=None):
    def mk(r, i):
        if r[1] == "post":
            x, u = i("p", r[2]), i("u", r[3])
            tg = [i("t", t) for t in r[4].split("|")] if len(r) > 4 and r[4] else []
            return ([(3, x, u), (4, x, u)] + [(q, t, x) for t in tg for q in (2, 6)], [])
        if r[1] == "badge":
            u, t = i("u", r[2]), i("t", r[3])
            return ([(1, u, t), (5, u, t)], [])
    return _stream_file(out, tsv, mk, "post (x,u,tags)->R3,R4(x,u); R2,R6(t,x) per tag; badge (u,t)->R1,R5(u,t); ids dense in first-seen order, one counter for users, tags, posts", max_ops)


def gh_members(out, tsv, max_ops=None):
    def mk(r, i):
        if r[1] == "item":
            x, rp, u = i("x", r[2]), i("r", r[3]), i("u", r[4])
            return ([(2, rp, x), (6, rp, x), (3, x, u), (4, x, u)], [])
        if r[1] in ("member_added", "member_removed"):
            u, rp = i("u", r[2]), i("r", r[3])
            return (([(1, u, rp), (5, u, rp)], []) if r[1] == "member_added" else ([], [(1, u, rp), (5, u, rp)]))
    return _stream_file(out, tsv, mk, "item (x,repo r,author u)->R2,R6(r,x);R3,R4(x,u); member_added (u,r)->R1,R5(u,r); member_removed deletes; ids disjoint", max_ops)


def ldbc_members(out, tsv, max_ops=None):
    posts = {}
    def mk(r, i):
        if r[1] == "post":
            x, f, p = i("x", r[2]), i("f", r[3]), i("p", r[4])
            posts[x] = [(2, f, x), (6, f, x), (3, x, p), (4, x, p)]
            return (posts[x], [])
        if r[1] == "post-":
            return ([], posts.pop(i("x", r[2]), []))
        if r[1] in ("member+", "member-"):
            p, f = i("p", r[2]), i("f", r[3])
            return (([(1, p, f), (5, p, f)], []) if r[1] == "member+" else ([], [(1, p, f), (5, p, f)]))
    return _stream_file(out, tsv, mk, "post (x,forum f,person p)->R2,R6(f,x);R3,R4(x,p); member+ (p,f)->R1,R5(p,f); member- and post- delete; ids disjoint", max_ops)
