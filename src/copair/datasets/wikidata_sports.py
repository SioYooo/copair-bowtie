import argparse
import hashlib
import json
from pathlib import Path


def rows(path, bad):
    with open(path, encoding="utf-8") as f:
        next(f)
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) < 2 or not p[0].startswith("<") or not p[1].startswith("<"):
                bad[0] += 1
                continue
            yield p[0], p[1], (p[2] if len(p) > 2 else None)


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def toggle_lines(rels, u, v):
    return ["- %s %d %d" % (r, u, v) for r in rels] + ["+ %s %d %d" % (r, u, v) for r in rels]


def build(a):
    data, out = Path(a.data), Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    ids = {}
    idof = lambda s: ids.setdefault(s, len(ids) + 1)
    p17, p54 = data / "p17_pairs.tsv", data / "p54.tsv"
    p27 = data / ("p27_%s.tsv" % a.variant)
    bad = {}
    seen = {k: set() for k in ("R1", "R3", "R2")}
    persons, teams, countries = set(), set(), set()
    n = {"L": 0}
    tp = out / "init_and_toggles.trace"
    t = open(tp, "w")
    for path, rels, key, swap in ((p17, ("R1", "R5"), "R1", False), (p54, ("R3", "R4"), "R3", False),
                                  (p27, ("R2", "R6"), "R2", True)):
        bad[path.name] = [0]
        nrows = 0
        for s1, s2, _ in rows(path, bad[path.name]):
            nrows += 1
            u, v = idof(s1), idof(s2)
            if key == "R1":
                teams.add(u)
                countries.add(v)
            elif key == "R3":
                persons.add(u)
                teams.add(v)
            else:
                persons.add(u)
                countries.add(v)
            if swap:
                u, v = v, u
            k = (u << 32) | v
            if k in seen[key]:
                continue
            seen[key].add(k)
            for r in rels:
                t.write("L %s %d %d\n" % (r, u, v))
                n["L"] += 1
        bad[path.name].append(nrows)
    n54 = bad[p54.name][1]
    st = {"opi": 0, "upd": 0, "nE": 0, "last": None}

    def emit(line):
        t.write(line + "\n")
        st["opi"] += 1
        st["last"] = line
        if line == "E":
            st["nE"] += 1
            return
        st["upd"] += 1
        if st["upd"] % a.enum_every == 0:
            t.write("E\n")
            st["opi"] += 1
            st["nE"] += 1
            st["last"] = "E"

    emit("E")
    arms, skipped = {}, {"H": 0, "C": 0}

    def run_arm(name, items):
        start = st["opi"]
        for rels, u, v in items:
            for ln in toggle_lines(rels, u, v):
                emit(ln)
        if st["last"] != "E":
            emit("E")
        arms[name] = {"op_index_start": start, "op_index_end_exclusive": st["opi"], "n_items": len(items)}

    pairs = []
    pbad = [0]
    for s1, s2, f in rows(a.pairs, pbad):
        try:
            fv = int(float(f))
        except (TypeError, ValueError):
            pbad[0] += 1
            continue
        pairs.append((s1, s2, fv))
    hot, ctl = [], []
    for s1, s2, fv in pairs:
        ok = s1 in ids and s2 in ids
        if len(hot) < a.topk:
            if ok:
                hot.append((("R1", "R5"), ids[s1], ids[s2]))
            else:
                skipped["H"] += 1
        if 1 <= fv <= 9 and len(ctl) < a.control_k:
            if ok:
                ctl.append((("R1", "R5"), ids[s1], ids[s2]))
            else:
                skipped["C"] += 1
    mem = []
    if a.member_k:
        step = max(1, n54 // a.member_k)
        for i, (s1, s2, _) in enumerate(rows(p54, [0])):
            if i % step == 0 and len(mem) < a.member_k:
                mem.append((("R3", "R4"), ids[s1], ids[s2]))
    run_arm("H", hot)
    run_arm("C", ctl)
    run_arm("M", mem)
    t.close()
    with open(out / "dict.tsv", "w") as f:
        for s, i in ids.items():
            f.write("%d\t%s\n" % (i, s))
    n0 = n["L"]
    files = {p.name: {"sha256": sha(p), "data_rows": bad[p.name][1], "malformed": bad[p.name][0]} for p in (p17, p54, p27)}
    files[Path(a.pairs).name] = {"sha256": sha(a.pairs), "data_rows": len(pairs), "malformed": pbad[0]}
    summ = {"variant": a.variant, "files": files, "skipped_pairs_not_in_dictionary": skipped,
            "distinct_tuples": {"R1": len(seen["R1"]), "R5": len(seen["R1"]), "R2": len(seen["R2"]), "R6": len(seen["R2"]),
                                "R3": len(seen["R3"]), "R4": len(seen["R3"])},
            "N0": n0, "psi": n0 ** 0.2, "persons": len(persons), "teams": len(teams), "countries": len(countries),
            "K_emitted": len(hot), "K2_emitted": len(ctl), "K3_emitted": len(mem), "enum_every": a.enum_every,
            "n_updates": st["upd"], "n_non_L_ops": st["opi"], "n_E": st["nE"], "trace_sha256": sha(tp), "arms": arms,
            "arm_note": "op_index = 0-based among non-L non-comment lines (E included); end exclusive; op_index 0 is the post-load E"}
    json.dump(summ, open(out / "build_summary.json", "w"), indent=1)
    print("built", tp, "N0=%d ops=%d E=%d" % (n0, st["opi"], st["nE"]))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--data", required=True)
    b.add_argument("--pairs", required=True)
    b.add_argument("--variant", choices=["reduced", "full"], required=True)
    b.add_argument("--out", required=True)
    b.add_argument("--topk", type=int, default=1000)
    b.add_argument("--control-k", type=int, default=1000, dest="control_k")
    b.add_argument("--member-k", type=int, default=1000, dest="member_k")
    b.add_argument("--enum-every", type=int, default=100, dest="enum_every")
    a = ap.parse_args()
    build(a)


if __name__ == "__main__":
    main()
