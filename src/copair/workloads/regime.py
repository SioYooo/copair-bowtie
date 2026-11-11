
from copair.workloads.synthetic import _w

NS = (10, 100, 1000, 10000)
QS = ("0.0001", "0.001", "0.01", "0.1")
OPS = 200000


def hub_regime(out, rng, n=10, H=16, q=0.4, ops=OPS, enum_every=1000):
    n, H, ops, enum_every, q = int(n), int(H), int(ops), int(enum_every), float(q)
    r1 = [rng.random() < 0.5 for _ in range(H)]
    for _ in range(H):
        rng.random()
    mx, mh, r5on = [], [], []
    nxt = 1
    out.write("# hub_regime n=%d H=%d q=%g ops=%d enum_every=%d (hub i: a=b=i; member x: d=e=x)\n" % (n, H, q, ops, enum_every))
    def member(x, h, pre, c):
        _w(out, c, 3, x, h)
        _w(out, c, 2, h, x)
        _w(out, c, 4, x, x)
        _w(out, c, 6, x, x)
        if pre: _w(out, c, 5, x, x)
    for h in range(H):
        if r1[h]: _w(out, "L", 1, h, h)
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
    p5 = q + 0.2 * (1 - q)
    while done < ops:
        p = rnd()
        if p < q:
            h = ri(H)
            r1[h] = not r1[h]
            _w(out, "+" if r1[h] else "-", 1, h, h)
            done += 1
        elif p < p5:
            i = ri(nm)
            r5on[i] = not r5on[i]
            x = mx[i]
            _w(out, "+" if r5on[i] else "-", 5, x, x)
            done += 1
        else:
            i = ri(nm)
            x, h = mx[i], mh[i]
            _w(out, "-", 3, x, h)
            _w(out, "-", 2, h, x)
            done += 2
            _w(out, "-", 4, x, x)
            _w(out, "-", 6, x, x)
            done += 2
            if r5on[i]:
                _w(out, "-", 5, x, x)
                done += 1
            h2 = ri(H)
            pre = rnd() < 0.5
            member(nxt, h2, pre, "+")
            done += 4 + (1 if pre else 0)
            mx[i], mh[i], r5on[i] = nxt, h2, pre
            nxt += 1
        while done >= nxtE:
            out.write("E\n")
            nxtE += enum_every
    out.write("E\n")


def parse_cell(cell):
    n, q = cell.split("_")
    assert int(n) in NS and q in QS, cell
    return int(n), q


def cell_label(lo, hi):
    return "BETTER" if hi < 1 else ("WORSE" if lo > 1 else "TIE")
