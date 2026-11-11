import math
import bisect
import gzip
import hashlib
import itertools
import os
import random
import sys

from copair import paths

VARS = [("a", "b"), ("b", "x"), ("x", "a"), ("x", "d"), ("d", "e"), ("e", "x")]


class T:

    def __init__(self, out, every=0):
        self.out, self.every, self.S, self.nupd, self.started, self.nE = out, every, set(), 0, False, 0

    def L(self, r, u, v):
        assert not self.started
        if (r, u, v) not in self.S:
            self.S.add((r, u, v))
            self.out.write("L R%d %d %d\n" % (r, u, v))

    def _after(self):
        self.started = True
        self.nupd += 1
        if self.every and self.nupd % self.every == 0:
            self.E()

    def ins(self, r, u, v):
        self.out.write("+ R%d %d %d\n" % (r, u, v))
        self.S.add((r, u, v))
        self._after()

    def dele(self, r, u, v):
        self.out.write("- R%d %d %d\n" % (r, u, v))
        self.S.discard((r, u, v))
        self._after()

    def tog(self, r, u, v):
        (self.dele if (r, u, v) in self.S else self.ins)(r, u, v)

    def E(self):
        self.started = True
        self.nE += 1
        self.out.write("E\n")

    def goto(self, rng, target):
        ds, is_ = sorted(self.S - target), sorted(target - self.S)
        ops = [(0, t) for t in ds] + [(1, t) for t in is_]
        rng.shuffle(ops)
        for k, t in ops:
            (self.ins if k else self.dele)(*t)


def _plant(t, rng, dom):
    a, b, x, d, e = (rng.randrange(dom[v]) for v in "abxde")
    for tu in ((1, a, b), (2, b, x), (3, x, a), (4, x, d), (5, d, e), (6, e, x)):
        t.ins(*tu)


def _rt(rng, r, dom):
    return rng.randrange(dom[VARS[r - 1][0]]), rng.randrange(dom[VARS[r - 1][1]])


def rand(out, rng, n=None, dom=None, pdel=None, pdup=None, pabs=None, every=None, linit=None, plant=None):
    d = {v: rng.randint(2, 8) for v in "abxde"}
    if dom is not None:
        d = {v: int(dom) for v in "abxde"}
    n = n or rng.randint(300, 3000)
    pdel = rng.uniform(0.25, 0.5) if pdel is None else pdel
    pdup = rng.uniform(0, 0.3) if pdup is None else pdup
    pabs = rng.uniform(0, 0.3) if pabs is None else pabs
    every = every or rng.choice([1, 1, 2, 5])
    plant = rng.choice([0.05, 0.1, 0.2]) if plant is None else plant
    linit = rng.choice([0, 0, 10, 40]) if linit is None else linit
    t = T(out, every)
    out.write("# rand dom=%s n=%d pdel=%.3f pdup=%.3f pabs=%.3f every=%d\n" % (d, n, pdel, pdup, pabs, every))
    for _ in range(linit):
        r = rng.randint(1, 6)
        t.L(r, *_rt(rng, r, d))
    t.E()
    for _ in range(n):
        if rng.random() < plant:
            _plant(t, rng, d)
            continue
        r = rng.randint(1, 6)
        pres = [x for x in t.S if x[0] == r] if t.S else []
        if rng.random() < pdel:
            if pres and rng.random() >= pabs:
                t.dele(*rng.choice(sorted(pres)))
            else:
                t.dele(r, *_rt(rng, r, d))
        else:
            if pres and rng.random() < pdup:
                t.ins(*rng.choice(sorted(pres)))
            else:
                t.ins(r, *_rt(rng, r, d))
    t.E()


def walk(out, rng, L=3, start="empty", dom=2):
    tuples = [(r, u, v) for r in range(1, 7) for u in range(dom) for v in range(dom)]
    idx = {t: i for i, t in enumerate(tuples)}
    t = T(out)
    if start == "rand":
        for tu in tuples:
            if rng.random() < 0.5:
                t.L(*tu)
    state = sum(1 << idx[x] for x in t.S)
    seen, nseq = {state}, 0
    t.E()

    def rec(depth, state):
        nonlocal nseq
        if depth == 0:
            return
        for tu in tuples:
            for sign in (1, 0):
                bit = 1 << idx[tu]
                noop = bool(state & bit) == bool(sign)
                nseq += 1
                (t.ins if sign else t.dele)(*tu)
                ns = state if noop else state ^ bit
                seen.add(ns)
                t.E()
                rec(depth - 1, ns)
                if not noop:
                    (t.dele if sign else t.ins)(*tu)
                    t.E()

    rec(int(L), state)
    out.write("# walk sequences=%d distinct_states=%d of %d (L=%d dom=%d)\n" % (nseq, len(seen), 1 << len(tuples), L, dom))


def _osc_content(kind, k, K):
    s = set()
    if kind == "x":
        X = 9
        for i in range(k):
            s |= {(2, i, X), (3, X, i), (4, X, i), (6, i, X)}
    elif kind == "a":
        s = {(1, 0, b) for b in range(k)}
    elif kind == "b":
        s = {(1, a, 0) for a in range(k)}
    elif kind == "d":
        s = {(5, 0, e) for e in range(k)}
    elif kind == "e":
        s = {(5, d, 0) for d in range(k)}
    elif kind == "ab":
        for j in range(k):
            x = j + 1
            s |= {(3, x, 0), (2, 0, x), (4, x, j), (6, j, x), (5, j, j)}
    elif kind == "de":
        for j in range(k):
            x = j + 1
            s |= {(4, x, 0), (6, 0, x), (3, x, j), (2, j, x), (1, j, j)}
    return s


def _osc_bg(kind, K):
    bg, pool = set(), []
    P = list(range(min(K, 8)))
    xs = (0, 1, 2)
    if kind == "x":
        pool = [(1, i, j) for i in P for j in P] + [(5, i, j) for i in P for j in P]
    elif kind == "a":
        for x in xs: bg |= {(3, x, 0), (4, x, 0), (4, x, 1), (6, 0, x), (6, 1, x)} | {(2, b, x) for b in range(K)}
        bg |= {(5, i, j) for i in (0, 1) for j in (0, 1)}
        pool = [(1, 0, b) for b in range(K)] + [(5, 0, 1), (5, 1, 0)]
    elif kind == "b":
        for x in xs: bg |= {(2, 0, x), (4, x, 0), (4, x, 1), (6, 0, x), (6, 1, x)} | {(3, x, a) for a in range(K)}
        bg |= {(5, i, j) for i in (0, 1) for j in (0, 1)}
        pool = [(1, a, 0) for a in range(K)] + [(5, 0, 1), (5, 1, 0)]
    elif kind == "d":
        for x in xs: bg |= {(4, x, 0), (3, x, 0), (3, x, 1), (2, 0, x), (2, 1, x)} | {(6, e, x) for e in range(K)}
        bg |= {(1, i, j) for i in (0, 1) for j in (0, 1)}
        pool = [(5, 0, e) for e in range(K)] + [(1, 0, 1), (1, 1, 0)]
    elif kind == "e":
        for x in xs: bg |= {(6, 0, x), (3, x, 0), (3, x, 1), (2, 0, x), (2, 1, x)} | {(4, x, d) for d in range(K)}
        bg |= {(1, i, j) for i in (0, 1) for j in (0, 1)}
        pool = [(5, d, 0) for d in range(K)] + [(1, 0, 1), (1, 1, 0)]
    elif kind == "ab":
        bg = {(1, 0, 0)}
        pool = [(1, 0, 0)] + [(5, j, j) for j in range(K)] + [(5, j, (j + 1) % K) for j in range(K)]
    elif kind == "de":
        bg = {(5, 0, 0)}
        pool = [(5, 0, 0)] + [(1, j, j) for j in range(K)] + [(1, j, (j + 1) % K) for j in range(K)]
    return bg, pool


def osc(out, rng, kind=None, levels=None, reps=3, every=1, toggles=2, ups=None):
    kind = kind or rng.choice(["x", "a", "b", "d", "e", "ab", "de"])
    lv = [int(v) for v in str(levels).split(",")] if levels else [0, 1, 2, 3, 4, 5, 6, 8, 5, 4, 3, 2, 1, 0]
    K = max(lv)
    bg, pool = _osc_bg(kind, K)
    t = T(out, int(every))
    out.write("# osc kind=%s levels=%s reps=%s every=%s\n" % (kind, lv, reps, every))
    for tu in sorted(bg):
        t.L(*tu)
    if kind == "x":
        for tu in pool:
            if rng.random() < 0.5: t.L(*tu)
    t.E()
    allobj = _osc_content(kind, K, K)
    for _ in range(int(reps)):
        for k in lv:
            for _ in range(int(toggles)):
                if pool: t.tog(*rng.choice(pool))
            t.goto(rng, (t.S - allobj) | _osc_content(kind, k, K) | bg)
            if ups and t.nupd >= ups:
                t.E()
                return
    t.E()


def growshrink(out, rng, n0=4, cycles=2, every=3, ups=None):
    big = 8 * n0
    dm = max(2, int(big ** 0.4))
    d = {v: dm for v in "abxde"}
    t = T(out, every)
    out.write("# growshrink n0=%d sizes up to %d dom=%d\n" % (n0, big, dm))
    sizes = [n0] + [big, n0] * int(cycles)
    for tgt in sizes:
        while len(t.S) < tgt:
            if rng.random() < 0.5: _plant(t, rng, d)
            else:
                r = rng.randint(1, 6)
                t.ins(r, *_rt(rng, r, d))
        while len(t.S) > tgt:
            t.dele(*rng.choice(sorted(t.S)))
        t.E()
        if ups and t.nupd >= ups: return


def samefinal(out, rng, nh=4, ntup=None):
    d = {v: rng.randint(2, 4) for v in "abxde"}
    cap = sum(d[VARS[r][0]] * d[VARS[r][1]] for r in range(6)) // 2
    ntup = min(ntup or rng.randint(8, 30), cap)
    F = set()
    while len(F) < ntup:
        r = rng.randint(1, 6)
        F.add((r,) + _rt(rng, r, d))
    junk = [(r,) + _rt(rng, r, d) for r in rng.choices(range(1, 7), k=ntup)]
    t = T(out, 0)
    out.write("# samefinal F=%d tuples, %d histories; E after each 'final' comment is the common answer\n" % (len(F), nh))
    t.E()
    for h in range(nh):
        order = sorted(F)
        if h == 0:
            pass
        else:
            rng.shuffle(order)
        for tu in order:
            if h >= 2:
                if rng.random() < 0.3: t.ins(*tu)
                if rng.random() < 0.3:
                    t.dele(*tu)
                    t.ins(*tu)
                if rng.random() < 0.3:
                    j = rng.choice(junk)
                    if j not in F:
                        t.ins(*j)
                        t.E()
                        t.dele(*j)
                j = rng.choice(junk)
                if j not in F and rng.random() < 0.3: t.dele(*j)
            t.ins(*tu)
        if h == 3:
            ex = [j for j in junk if j not in F]
            for j in ex: t.ins(*j)
            rng.shuffle(ex)
            for j in ex: t.dele(*j)
        out.write("# final %d\n" % h)
        t.E()
        for tu in rng.sample(sorted(t.S), len(t.S)):
            t.dele(*tu)
        t.E()


def outsize(out, rng, steps=40, m=3, s=6, every=1):
    def single(): return {(r, 0, 0) for r in range(1, 7)}
    def many():
        return {(r, u, v) for r in range(1, 7) for u in range(m) for v in range(m)}
    def bigx():
        S = {(1, a, b) for a in range(s) for b in range(s)} | {(5, d, e) for d in range(s) for e in range(s)}
        for i in range(s): S |= {(2, i, 0), (3, 0, i), (4, 0, i), (6, i, 0)}
        return S
    regimes = {"empty": set(), "single": single(), "many": many(), "bigx": bigx()}
    t = T(out, 0)
    out.write("# outsize regimes %s\n" % sorted(regimes))
    t.E()
    for _ in range(int(steps)):
        name = rng.choice(sorted(regimes))
        out.write("# regime %s\n" % name)
        t.goto(rng, regimes[name])
        t.E()
        for _ in range(rng.randint(0, 3)):
            r = rng.randint(1, 6)
            t.tog(r, rng.randrange(max(m, 2)), rng.randrange(max(m, 2)))
            if rng.random() < 0.7: t.E()
    t.E()


def shared(out, rng, nx=None, every=2):
    nx = nx or rng.randint(3, 8)
    collide = rng.random() < 0.6
    pick = (lambda: rng.randrange(0, 3)) if collide else (lambda: 100 + rng.randrange(0, 3))
    a0, b0, d0, e0 = pick(), pick(), pick(), pick()
    if rng.random() < 0.3: b0 = a0
    xs = [rng.randrange(0, 4) if collide else 200 + i for i in range(nx)]
    if collide:
        xs = list(dict.fromkeys(xs + [a0, d0]))
    S = set()
    for x in xs:
        S |= {(3, x, a0), (2, b0, x), (4, x, d0), (6, e0, x)}
        if rng.random() < 0.5:
            pa, pb, pd, pe = pick(), pick(), pick(), pick()
            S |= {(3, x, pa), (2, pb, x), (4, x, pd), (6, pe, x), (1, pa, pb), (5, pd, pe)}
    S |= {(1, a0, b0), (5, d0, e0)}
    t = T(out, every)
    out.write("# shared a0=%d b0=%d d0=%d e0=%d xs=%s collide=%s\n" % (a0, b0, d0, e0, xs, collide))
    t.E()
    L = sorted(S)
    for order in range(4):
        o = list(L)
        if order == 0: rng.shuffle(o)
        elif order == 1: o.sort(key=lambda z: (-z[0], z[1], z[2]))
        else: rng.shuffle(o)
        for tu in o: t.ins(*tu)
        t.E()
        o = list(L)
        if order == 1: o.sort(key=lambda z: (z[0] not in (1, 5), z))
        elif order == 2: o.sort(key=lambda z: (z[0] in (1, 5), z))
        else: rng.shuffle(o)
        for tu in o[: len(o) * 3 // 4]: t.dele(*tu)
        t.E()
        for tu in o[: len(o) // 3]: t.ins(*tu)
        t.E()
        for tu in sorted(t.S, key=lambda z: rng.random()): t.dele(*tu)
        t.E()


def _w(out, c, r, u, v):
    out.write("%s R%d %d %d\n" % (c, r, u, v))


def hub(out, rng, n=10, H=100, variant="d", ops=10000, enum_every=1000):
    n, H, ops, enum_every = int(n), int(H), int(ops), int(enum_every)
    dv = variant == "d"
    r1 = [rng.random() < 0.5 for _ in range(H)]
    r5s = [rng.random() < 0.5 for _ in range(H)]
    mx, mh, r5on = [], [], []
    nxt = 1
    out.write("# hub n=%d H=%d variant=%s ops=%d enum_every=%d (hub i: a=b=i; member x: d=e=x in variant d, d=e=hub in variant s)\n" % (n, H, variant, ops, enum_every))
    def member(x, h, pre, c):
        _w(out, c, 3, x, h)
        _w(out, c, 2, h, x)
        if dv:
            _w(out, c, 4, x, x)
            _w(out, c, 6, x, x)
            if pre: _w(out, c, 5, x, x)
        else:
            _w(out, c, 4, x, h)
            _w(out, c, 6, h, x)
    for h in range(H):
        if r1[h]: _w(out, "L", 1, h, h)
        if not dv and r5s[h]: _w(out, "L", 5, h, h)
        for _ in range(n):
            pre = rng.random() < 0.5
            member(nxt, h, pre, "L")
            mx.append(nxt)
            mh.append(h)
            r5on.append(pre)
            nxt += 1
    nm = len(mx)
    done, rnd, nxtE = 0, rng.random, enum_every
    ri = rng.randrange
    while done < ops:
        p = rnd()
        if p < 0.4:
            h = ri(H)
            r1[h] = not r1[h]
            _w(out, "+" if r1[h] else "-", 1, h, h)
            done += 1
        elif p < 0.6:
            if dv:
                i = ri(nm)
                r5on[i] = not r5on[i]
                x = mx[i]
                _w(out, "+" if r5on[i] else "-", 5, x, x)
            else:
                h = ri(H)
                r5s[h] = not r5s[h]
                _w(out, "+" if r5s[h] else "-", 5, h, h)
            done += 1
        else:
            i = ri(nm)
            x, h = mx[i], mh[i]
            _w(out, "-", 3, x, h)
            _w(out, "-", 2, h, x)
            done += 2
            if dv:
                _w(out, "-", 4, x, x)
                _w(out, "-", 6, x, x)
                done += 2
                if r5on[i]:
                    _w(out, "-", 5, x, x)
                    done += 1
            else:
                _w(out, "-", 4, x, h)
                _w(out, "-", 6, h, x)
                done += 2
            h2 = ri(H)
            pre = rnd() < 0.5
            member(nxt, h2, pre, "+")
            done += 4 + (1 if dv and pre else 0)
            mx[i], mh[i], r5on[i] = nxt, h2, pre
            nxt += 1
        while done >= nxtE:
            out.write("E\n")
            nxtE += enum_every
    out.write("E\n")


def heavy_centre(out, rng, K=10, s=8, density=0.5, ops=10000, enum_every=1000):
    K, s, ops, enum_every = int(K), int(s), int(ops), int(enum_every)
    P = 2 * s
    t = T(out, enum_every)
    out.write("# heavy_centre K=%d s=%d density=%s ops=%d enum_every=%d\n" % (K, s, density, ops, enum_every))
    for x in range(K):
        for v in rng.sample(range(P), s): t.L(3, x, v)
        for v in rng.sample(range(P), s): t.L(2, v, x)
        for v in rng.sample(range(P), s): t.L(4, x, v)
        for v in rng.sample(range(P), s): t.L(6, v, x)
    for i in range(P):
        for j in range(P):
            if rng.random() < density: t.L(1, i, j)
            if rng.random() < density: t.L(5, i, j)
    t.E()
    for _ in range(ops):
        r = rng.randint(1, 6)
        if r == 1 or r == 5: t.tog(r, rng.randrange(P), rng.randrange(P))
        else:
            x, v = rng.randrange(K), rng.randrange(P)
            t.tog(r, *{2: (v, x), 3: (x, v), 4: (x, v), 6: (v, x)}[r])
    t.E()


def mid_degree(out, rng, n=10, H=20, k=8, ops=10000, enum_every=1000):
    n, H, k, ops, enum_every = int(n), int(H), int(k), int(ops), int(enum_every)
    q = max(1, k // 4)
    P = 2 * q

    t = T(out, enum_every)
    out.write("# mid_degree n=%d H=%d k=%d (q=%d per set) ops=%d enum_every=%d\n" % (n, H, k, q, ops, enum_every))
    mem, nxt = [], [1]
    def mk(h, ins):
        x = nxt[0]
        nxt[0] += 1
        lo = h * P
        A = [lo + v for v in rng.sample(range(P), q)]
        B = [lo + v for v in rng.sample(range(P), q)]
        D = [lo + v for v in rng.sample(range(P), q)]
        E_ = [lo + v for v in rng.sample(range(P), q)]
        tl = [(3, x, a) for a in A] + [(2, b, x) for b in B] + [(4, x, d) for d in D] + [(6, e, x) for e in E_]
        for tu in tl: (t.ins if ins else t.L)(*tu)
        return tl
    for h in range(H):
        for i in range(P):
            for j in range(P):
                if rng.random() < 0.5: t.L(1, h * P + i, h * P + j)
                if rng.random() < 0.5: t.L(5, h * P + i, h * P + j)
        mem += [(h, mk(h, False)) for _ in range(n)]
    t.E()
    while t.nupd < ops:
        p = rng.random()
        if p < 0.4:
            h = rng.randrange(H)
            t.tog(1, h * P + rng.randrange(P), h * P + rng.randrange(P))
        elif p < 0.6:
            h = rng.randrange(H)
            t.tog(5, h * P + rng.randrange(P), h * P + rng.randrange(P))
        else:
            i = rng.randrange(len(mem))
            for tu in mem[i][1]: t.dele(*tu)
            h = rng.randrange(H)
            mem[i] = (h, mk(h, True))
    t.E()


def pool(out, rng, n=200, s=4, pool=None, frac=0.5, ops=10000, enum_every=1000):
    n, s, ops, enum_every = int(n), int(s), int(ops), int(enum_every)
    P = int(pool) if pool else 2 * s
    t = T(out, enum_every)
    out.write("# pool n=%d s=%d pool=%d frac=%s ops=%d enum_every=%d\n" % (n, s, P, frac, ops, enum_every))
    for i in range(P):
        for j in range(P):
            if rng.random() < frac: t.L(1, i, j)
            if rng.random() < frac: t.L(5, i, j)
    for x in range(n):
        for v in rng.sample(range(P), s): t.L(3, x, v)
        for v in rng.sample(range(P), s): t.L(2, v, x)
        for v in rng.sample(range(P), s): t.L(4, x, v)
        for v in rng.sample(range(P), s): t.L(6, v, x)
    t.E()
    for _ in range(ops):
        if rng.random() < 0.1:
            t.tog(rng.choice((1, 5)), rng.randrange(P), rng.randrange(P))
        else:
            r, x, v = rng.choice((2, 3, 4, 6)), rng.randrange(n), rng.randrange(P)
            t.tog(r, *{2: (v, x), 3: (x, v), 4: (x, v), 6: (v, x)}[r])
    t.E()


def zipf(out, rng, D=1000, alpha=1.1, init=1000, ops=10000, enum_every=1000):
    D, init, ops, enum_every = int(D), int(init), int(ops), int(enum_every)
    cum = list(itertools.accumulate(1.0 / (i + 1) ** float(alpha) for i in range(D)))
    tot = cum[-1]
    zipf = lambda: min(D - 1, bisect.bisect_left(cum, rng.random() * tot))
    present, lst, pos = set(), [], {}
    def add(tu):
        if tu in present: return False
        present.add(tu)
        pos[tu] = len(lst)
        lst.append(tu)
        return True
    def rem():
        tu = lst[rng.randrange(len(lst))]
        i = pos.pop(tu)
        last = lst.pop()
        if last != tu:
            lst[i] = last
            pos[last] = i
        present.discard(tu)
        return tu
    out.write("# zipf D=%d alpha=%s init=%d ops=%d enum_every=%d\n" % (D, alpha, init, ops, enum_every))
    while len(lst) < init:
        tu = (rng.randint(1, 6), zipf(), zipf())
        if add(tu): _w(out, "L", *tu)
    done = 0
    while done < ops:
        for _ in range(20):
            tu = (rng.randint(1, 6), zipf(), zipf())
            if add(tu):
                _w(out, "+", *tu)
                break
        else:
            continue
        done += 1
        if done % enum_every == 0: out.write("E\n")
        _w(out, "-", *rem())
        done += 1
        if done % enum_every == 0: out.write("E\n")
    out.write("E\n")


def churn(out, rng, variant="x", levels=None, ops=10000, enum_every=1000, n0=64, **kw):
    ops, enum_every = int(ops), int(enum_every)
    v = {"x": "x", "a": "a", "ab": "ab", "grow": "n"}[variant]
    if v == "n":
        growshrink(out, rng, n0=int(n0), cycles=max(1, ops // (16 * int(n0))), every=enum_every, ups=ops)
    else:
        osc(out, rng, kind=v, levels=levels or "3,4,5,4", reps=10 ** 9, every=enum_every, toggles=1, ups=ops)


def mathoverflow(out, rng, window_days=30, max_ops=100000, enum_every=1000):
    import collections
    D = str(paths.SX)
    names = ["a2q", "c2q", "c2a"]
    maps = {"a2q": (1, 4), "c2q": (2, 5), "c2a": (3, 6)}
    ev, hdr = [], []
    for fi, nm in enumerate(names):
        p = os.path.join(D, "sx-mathoverflow-%s.txt.gz" % nm)
        hdr.append("# file %s sha256=%s" % (os.path.basename(p), hashlib.sha256(open(p, "rb").read()).hexdigest()[:16]))
        with gzip.open(p, "rt") as f:
            for li, line in enumerate(f):
                a = line.split()
                if len(a) >= 3: ev.append((int(a[2]), fi, li, int(a[0]), int(a[1])))
    ev.sort()
    out.write("\n".join(hdr) + "\n# mapping: a2q(u,v)->R1(u,v),R4(u,v); c2q->R2,R5; c2a->R3,R6; events merged by (ts, a2q<c2q<c2a, file order)\n")
    W = int(float(window_days) * 86400)
    ts = [e[0] for e in ev]
    j0 = bisect.bisect_left(ts, ts[0] + W)
    p = bisect.bisect_right(ts, ts[j0] - W)
    cnt = collections.Counter()
    def ups(e):
        m = maps[names[e[1]]]
        return [(m[0], e[3], e[4]), (m[1], e[3], e[4])]
    for e in ev[p:j0]:
        for u in ups(e):
            cnt[u] += 1
    for u in sorted(cnt): _w(out, "L", *u)
    out.write("# initial window: %d events, %d tuples; streaming from event %d of %d; window_days=%s\n" % (j0 - p, len(cnt), j0, len(ev), window_days))
    done, j, enum_every = 0, j0, int(enum_every)
    def tick():
        nonlocal done
        done += 1
        if done % enum_every == 0: out.write("E\n")
    while done < int(max_ops) and (j < len(ev) or p < j):
        if p < j and (j >= len(ev) or ts[p] + W <= ts[j]):
            for u in ups(ev[p]):
                cnt[u] -= 1
                if cnt[u] == 0:
                    _w(out, "-", *u)
                    tick()
            p += 1
        else:
            for u in ups(ev[j]):
                cnt[u] += 1
                if cnt[u] == 1:
                    _w(out, "+", *u)
                    tick()
            j += 1
    out.write("E\n")


def abflip(out, rng, n0=1100, reps=2, hubs=3, every=0):
    t = T(out, int(every))
    for x in range(int(hubs)):
        for i in range(4):
            for tu in ((3, x, i), (2, i, x), (4, x, i), (6, i, x)):
                t.L(*tu)
    t.L(1, 0, 0)
    t.L(5, 0, 0)
    f = 0
    while len(t.S) < int(n0):
        t.L(1, 1000 + f, 5000 + f)
        if len(t.S) < int(n0): t.L(5, 1000 + f, 5000 + f)
        f += 1
    N0 = len(t.S)
    th = math.sqrt(max(2, N0))
    hi, lo, mid = int(2 * th) + 3, max(0, int(th / 2) - 1), int(th)
    out.write("# abflip N0=%d theta=%.3f levels hi=%d lo=%d mid=%d hubs=%d\n" % (N0, th, hi, lo, mid, hubs))
    t.E()
    kinds = {"a": lambda k: {(1, 0, 100 + j) for j in range(k)} | ({(1, 0, 0)} if k else set()),
             "b": lambda k: {(1, 100 + j, 0) for j in range(k)} | ({(1, 0, 0)} if k else set()),
             "d": lambda k: {(5, 0, 100 + j) for j in range(k)} | ({(5, 0, 0)} if k else set()),
             "e": lambda k: {(5, 100 + j, 0) for j in range(k)} | ({(5, 0, 0)} if k else set())}
    allk = set().union(*[v(hi + 2) for v in kinds.values()])
    for _ in range(int(reps)):
        for kind in ("a", "b", "d", "e"):
            for lv in (0, hi, lo, mid, hi, lo, 0, hi, mid, 0):
                t.goto(rng, (t.S - allk) | kinds[kind](lv))
                t.E()
                if rng.random() < 0.5:
                    r, i = rng.choice([(3, 0), (2, 1), (4, 2), (6, 3)])
                    t.tog(r, 0, i) if r in (3, 4) else t.tog(r, i, 0)
                    t.E()
    t.E()


def growshrink_large(out, rng, n0=64, cycles=2, every=7, tiny=4):
    big = 8 * int(n0)
    dm = int(big ** 0.5) + 2
    d = {"a": dm, "b": dm, "x": 4, "d": dm, "e": dm}
    t = T(out, int(every))
    nx = [0]
    out.write("# growshrink_large n0=%d sizes up to %d dom=%d x_dom=4\n" % (n0, big, dm))
    for tgt in [int(tiny)] + [big, int(tiny)] * int(cycles) + [big]:
        while len(t.S) < tgt:
            if rng.random() < 0.35:
                nx[0] += 1
                a, b, dd, e = (rng.randrange(dm) for _ in range(4))
                for tu in ((1, a, b), (2, b, 1000 + nx[0]), (3, 1000 + nx[0], a), (4, 1000 + nx[0], dd), (5, dd, e), (6, e, 1000 + nx[0])):
                    t.ins(*tu)
            elif rng.random() < 0.5: _plant(t, rng, d)
            else:
                r = rng.randint(1, 6)
                u, v = _rt(rng, r, d)
                t.ins(r, u, v)
        while len(t.S) > tgt:
            t.dele(*rng.choice(sorted(t.S)))
        t.E()
    t.E()


FAMILIES = {f.__name__: f for f in (rand, walk, osc, growshrink, samefinal, outsize, shared, hub, heavy_centre, mid_degree, pool, zipf, churn, mathoverflow, abflip, growshrink_large)}
CORRECTNESS = ["rand", "walk", "osc", "growshrink", "samefinal", "outsize", "shared", "abflip", "growshrink_large"]


def generate(family, seed, out, **params):
    FAMILIES[family](out, random.Random(seed), **params)


def _conv(s):
    for f in (int, float):
        try: return f(s)
        except ValueError: pass
    return s


def main(argv):
    if len(argv) < 2 or argv[1] not in FAMILIES:
        sys.exit("usage: python3 -m copair.workloads.synthetic <%s> --seed S [--out PATH] [--param value ...]" % "|".join(FAMILIES))
    fam, seed, outp, params, i = argv[1], 0, None, {}, 2
    while i < len(argv):
        k, v = argv[i].lstrip("-"), argv[i + 1]
        i += 2
        if k == "seed": seed = int(v)
        elif k == "out": outp = v
        else: params[k] = _conv(v)
    if outp:
        with open(outp, "w", buffering=1 << 20) as f: generate(fam, seed, f, **params)
    else:
        generate(fam, seed, sys.stdout, **params)


if __name__ == "__main__":
    main(sys.argv)
