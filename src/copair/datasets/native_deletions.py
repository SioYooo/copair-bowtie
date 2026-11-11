import hashlib
import os
import re
import tarfile
from array import array


def _sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def _safe(name):
    assert not os.path.isabs(name) and ".." not in name.split("/"), "unsafe member name %r" % name


class _Emit:
    def __init__(self, out, enum_every, max_ops):
        self.out, self.ee, self.max, self.n, self.edges = out, int(enum_every), max_ops, 0, 0

    def edge(self, c, u, v):
        if self.max is not None and self.n >= self.max:
            return False
        self.edges += 1
        for r in range(1, 7):
            if self.max is not None and self.n >= self.max:
                return False
            self.out.write("%s R%d %d %d\n" % (c, r, u, v))
            self.n += 1
            if self.n % self.ee == 0:
                self.out.write("E\n")
        return True

    def done(self):
        return self.max is not None and self.n >= self.max


def _load(out, edges):
    for u, v in edges:
        for r in range(1, 7):
            out.write("L R%d %d %d\n" % (r, u, v))


def snapshots(out, tar_path, enum_every=50000, max_ops=None):
    snaps = []
    with tarfile.open(tar_path, "r:*") as tf:
        for m in tf:
            if not m.isfile():
                continue
            _safe(m.name)
            d = re.search(r"(\d{8})", os.path.basename(m.name))
            if not d:
                continue
            s = set()
            for line in tf.extractfile(m):
                if line[:1] in b"#%" or not line.strip():
                    continue
                a = line.split()
                s.add((int(a[0]) << 32) | int(a[1]))
            snaps.append((d.group(1), m.name, array("q", sorted(s))))
    snaps.sort(key=lambda t: (t[0], t[1]))
    dec = lambda x: (x >> 32, x & 0xFFFFFFFF)
    out.write("# archive %s sha256=%s snapshots=%d\n# mapping: every edge (u,v) as listed -> R1..R6(u,v); load = earliest snapshot; "
              "per later snapshot vanished edges deleted (sorted) then new edges inserted (sorted)\n"
              % (os.path.basename(tar_path), _sha(tar_path)[:16], len(snaps)))
    first = snaps[0][2]
    _load(out, [dec(x) for x in first])
    em, prev = _Emit(out, enum_every, max_ops), set(first)
    for _, _, arr in snaps[1:]:
        if em.done():
            break
        cur = set(arr)
        for x in sorted(prev - cur):
            if not em.edge("-", *dec(x)): break
        for x in sorted(cur - prev):
            if not em.edge("+", *dec(x)): break
        prev = cur
    out.write("E\n")
    return {"snapshots": len(snaps), "load_edges": len(first), "edge_events": em.edges, "updates": em.n, "underflows": 0}


def events(out, tar_path, member, enum_every=50000, max_ops=None):
    ev = []
    with tarfile.open(tar_path, "r:*") as tf:
        _safe(member)
        for li, line in enumerate(tf.extractfile(member)):
            if line[:1] in b"#%" or not line.strip():
                continue
            a = line.split()
            if len(a) >= 4:
                ev.append((int(a[3]), li, int(a[0]), int(a[1]), int(a[2])))
    ev.sort()
    def run(evs, cnt, under):
        for e in evs:
            k, c = (e[2], e[3]), cnt.get((e[2], e[3]), 0)
            if e[4] > 0:
                cnt[k] = c + 1
                if c == 0: yield "+", e[2], e[3]
            elif c == 0:
                under[0] += 1
            elif c == 1:
                del cnt[k]
                yield "-", e[2], e[3]
            else:
                cnt[k] = c - 1
    h = len(ev) // 2
    tu = [0]
    for _ in run(ev, {}, tu): pass
    cnt, under = {}, [0]
    for _ in run(ev[:h], cnt, under): pass
    load = sorted(cnt)
    out.write("# archive %s member %s sha256=%s events=%d underflows=%d (whole file)\n# mapping: every edge (u,v) as listed -> R1..R6(u,v); "
              "events by (timestamp, file order); load = edges after first %d events; stream = remaining events, "
              "edge insert on count 0->1, delete on 1->0\n"
              % (os.path.basename(tar_path), os.path.basename(member), _sha(tar_path)[:16], len(ev), tu[0], h))
    _load(out, load)
    em = _Emit(out, enum_every, max_ops)
    for c, u, v in run(ev[h:], cnt, under):
        if not em.edge(c, u, v):
            break
    out.write("E\n")
    return {"events": len(ev), "load_edges": len(load), "edge_events": em.edges, "updates": em.n, "underflows": tu[0]}
