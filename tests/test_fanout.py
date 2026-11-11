import io
import json
import sys
import tarfile
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from copair.analysis import fanout as F
from copair.analysis.fanout_stat import stat

tmp = Path(tempfile.mkdtemp())
lines = ["% sym unweighted"] + ["%d %d %s1 %d" % (u, v, s, t) for t, (u, v, s) in enumerate(
    [(1, 2, "+"), (1, 3, "+"), (2, 3, "+"), (3, 4, "+"), (1, 2, "-"), (4, 5, "+"), (2, 3, "-"), (1, 2, "+"), (5, 1, "+"), (3, 4, "-")], 1)]
member = "link-dynamic-tiny/out.link-dynamic-tiny"
tarp = tmp / "tiny.tar.bz2"
with tarfile.open(tarp, "w:bz2") as tf:
    data = ("\n".join(lines) + "\n").encode()
    ti = tarfile.TarInfo(member)
    ti.size = len(data)
    tf.addfile(ti, io.BytesIO(data))
good = {"name": "tiny", "kind": "konect", "path": tarp, "url": "x", "member": member}
bad = {**good, "name": "bad", "member": "nope/missing"}
gone = {**good, "name": "gone", "path": tmp / "absent.tar.bz2"}
F.STREAMS[:] = [gone, bad, good]
out = tmp / "out"
rs = F.run_all(F.STREAMS, out, do_download=False)
assert [r["status"] for r in rs] == ["DOWNLOAD_FAIL", "PARSE_FAIL", "OK"], rs
row = json.load(open(out / "tiny.json"))["rows"]["full"]
for k in ("gen_info", "N0", "n_updates", "s_100", "s_10", "s_100_recomputed", "n_R1R5_f_ge", "top20_f", "runtime_s"):
    assert k in row, k
tp = tmp / "t.trace"
with open(tp, "w") as f: F.native_deletions.events(f, str(tarp), member)
assert row["s_100"] == stat(str(tp))["s_100"]
summ = json.load(open(out / "summary.json"))
assert summ["bad"]["error_class"] == "PARSE_FAIL" and "full" in summ["tiny"]
assert json.load(open(out / "selection.json"))["selected"] == []

import csv
import zipfile
from copair.datasets import adapters as A
DAY = 86400
def loadtrace(fn, *a, **k):
    tp = tmp / (fn + ".trace")
    with open(tp, "w") as f: info = getattr(A, fn)(f, *a, **k)
    return tp, info
def ident(tp):
    s = stat(str(tp))
    fs = stat(str(tp), ret_f=True)["fs"]
    assert s["n_updates"] > 0 and s["s_100"] == sum(1 for x in fs if x >= 100) / s["n_updates"]
    return s, fs
def lines(tp): return [l.split() for l in open(tp) if l[0] in "L+-"]

def stamp(sec): return time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(sec))
import time
hd = "SOURCE_SUBREDDIT\tTARGET_SUBREDDIT\tPOST_ID\tTIMESTAMP\tLINK_SENTIMENT\tPROPERTIES\n"
T0 = 1500000000
rb = [("a", "b", 0), ("b", "c", 1 * DAY), ("a", "b", 20 * DAY), ("c", "a", 31 * DAY), ("b", "a", 45 * DAY), ("a", "c", 80 * DAY)]
rt = [("c", "b", 1 * DAY), ("z", "a", 50 * DAY)]
for nm, rows in (("body", rb), ("title", rt)):
    open(tmp / (nm + ".tsv"), "w").write(hd + "".join("%s\t%s\tp%d\t%s\t1\tx\n" % (u, v, i, stamp(T0 + t)) for i, (u, v, t) in enumerate(rows)))
tp, info = loadtrace("reddit", [str(tmp / "body.tsv"), str(tmp / "title.tsv")])
ident(tp)
ls = lines(tp)
assert len({tuple(l[2:]) for l in ls if l[1] == "R1"}) == len({tuple(l[2:]) for l in ls if l[1] == "R6"})
ids = {}
assert info["subreddits"] == 4 and set(x for l in ls for x in l[2:]) <= {"0", "1", "2", "3"}
assert [l[2:] for l in ls if l[0] == "L" and l[1] == "R1"][0] == ["0", "1"]

def mkzip(name, member, text):
    zp = tmp / name
    with zipfile.ZipFile(zp, "w") as z: z.writestr(member, text)
    return zp
def csvtext(rows, sep, hdr):
    b = io.StringIO()
    w = csv.writer(b, delimiter=sep, lineterminator="\n")
    w.writerow(hdr)
    for r in rows: w.writerow(r)
    return b.getvalue()

W = 30 * DAY
def case(sec_of):
    rows = [("A", "ai", "at", 0)] + [("F%d" % k, "fi%d" % k, "ft%d" % k, 20000 + k) for k in range(6000)]
    for n, i in enumerate(("i1", "i2", "i3")):
        rows += [("U", i, "S", W + 100 + n), ("V", i, "T", W + 200 + n), ("U", i, "T", W + 300 + n)]
    return rows
rows = case(None)
def check_tag(fmt, member, text, mem_query):
    zp = mkzip(fmt + ".zip", member, text)
    tp, info = loadtrace("tagging", "%s::%s" % (zp, mem_query), fmt)
    s, fs = ident(tp)
    ls = lines(tp)
    kinds = {"u": {int(l[2]) for l in ls if l[1] in ("R1", "R5")} | {int(l[3]) for l in ls if l[1] in ("R3", "R4")},
             "t": {int(l[3]) for l in ls if l[1] in ("R1", "R5")} | {int(l[2]) for l in ls if l[1] in ("R2", "R6")},
             "i": {int(l[3]) for l in ls if l[1] in ("R2", "R6")} | {int(l[2]) for l in ls if l[1] in ("R3", "R4")}}
    assert not (kinds["u"] & kinds["t"]) and not (kinds["u"] & kinds["i"]) and not (kinds["t"] & kinds["i"]), fmt
    assert (len(kinds["u"]), len(kinds["t"]), len(kinds["i"])) == (6002, 6002, 6003) == (info["users"] - 1, info["tags"] - 1, info["items"] - 1), (fmt, [len(v) for v in kinds.values()], info)
    uid, tid = 6001, info["users"] + 6002
    seq = [(l[1], int(l[2]), int(l[3])) for l in ls if l[0] in "+-" and l[1] in ("R1", "R5")]
    assert ("R1", uid, tid) in seq, (fmt, [x for x in seq if x[1] >= 6000][:8])
    k = seq.index(("R1", uid, tid))
    assert fs[k] == 3, (fmt, fs[k])
    return tp, info
mlrows = [(r[0], r[1], r[2], r[3]) for r in rows]
mlrows[2] = ("F1", "fi1", "  FT1 ", 20001)
mlrows[3] = ("F2", "fi2", "ft2, extra", 20002)
mlrows = [(u, i, "Ss" if t == "S" else "tT " if t == "T" else t, ts) for u, i, t, ts in mlrows]
check_tag("movielens", "ml-latest/tags.csv", csvtext(mlrows, ",", ["userId", "movieId", "tag", "timestamp"]), "ml-latest/tags.csv")
hd_h = ["userID", "bookmarkID", "tagID", "timestamp"]
hrows = [(u, i, t, ts * 1000 + 7) for u, i, t, ts in rows]
check_tag("hetrec-delicious", "user_taggedbookmarks-timestamps.dat", csvtext(hrows, "\t", hd_h), "user_taggedbookmarks-timestamps.dat")
check_tag("hetrec-lastfm", "sub/user_taggedartists-timestamps.dat", csvtext(hrows, "\t", ["userID", "artistID", "tagID", "timestamp"]), "user_taggedartists-timestamps.dat")
try:
    A.tagging(io.StringIO(), "%s::nope.dat" % (tmp / "hetrec-lastfm.zip"), "hetrec-lastfm")
    raise SystemExit("expected KeyError")
except KeyError: pass

a = 50
rows4 = [("Z", "zi", "zt", 0), ("U", "iA", "T", a), ("U", "iB", "T", a + 10 * DAY), ("E", "iE", "tE", W + 1), ("P", "iP", "tP", a + W + 5), ("Q", "iQ", "tQ", a + 40 * DAY),
         ("X", "iX", "tX", a + 80 * DAY)]
zp = mkzip("iv.zip", "user_taggedbookmarks-timestamps.dat", csvtext([(u, i, t, ts * 1000) for u, i, t, ts in rows4], "\t", hd_h))
tp = tmp / "iv.trace"
with open(tp, "w") as f: A.tagging(f, "%s::user_taggedbookmarks-timestamps.dat" % zp, "hetrec-delicious")
ls = [l for l in lines(tp)]
rm = [n for n, l in enumerate(ls) if l[:4] == ["-", "R1", "1", "7"]]
ra = [n for n, l in enumerate(ls) if l[:4] == ["-", "R2", "7", "13"]]
pr = [n for n, l in enumerate(ls) if l[:4] == ["+", "R1", "3", "9"]]
assert len(rm) == len(ra) == len(pr) == 1 and ra[0] < pr[0] < rm[0], (ra, pr, rm)

import gzip
from copair.datasets import prepare as P
def iso_of(sec): return time.strftime("%Y-%m-%dT%H:%M:%S.123", time.gmtime(sec))
def ids_disjoint(ls, groups):
    sets = [set() for _ in groups]
    for l in ls:
        for gi, (rels, col) in enumerate(groups):
            if l[1] in rels: sets[gi].add(l[col])
    for a in range(len(sets)):
        for b in range(a + 1, len(sets)): assert not (sets[a] & sets[b]), (a, b)
def fan_of(tp, key):
    fs = stat(str(tp), ret_f=True)["fs"]
    seq = [(l[1], int(l[2]), int(l[3])) for l in lines(tp) if l[0] in "+-" and l[1] in ("R1", "R5")]
    return fs[seq.index(key)]

assert P.tags_of("<a><b-c>") == ["a", "b-c"] and P.tags_of("|a|b|") == ["a", "b"] and P.tags_of(None) == []
assert P.iso("2010-07-21T20:15:55.123") == 1279743355

T0 = 1400000000
import html
def row(**a): return "<row " + " ".join('%s="%s"' % (k, html.escape(str(v))) for k, v in a.items()) + " />\n"
def se_files(tagfmt):
    fmt = (lambda ts: "".join("<%s>" % t for t in ts)) if tagfmt == "angle" else (lambda ts: "|" + "|".join(ts) + "|")
    px = "<posts>\n"
    for k in range(6000):
        px += row(Id=k + 1, PostTypeId=1, OwnerUserId=1000 + k, CreationDate=iso_of(T0 + 1 + k), Tags=fmt(["ft%d" % k]))
    px += row(Id=10001, PostTypeId=1, OwnerUserId=1, CreationDate=iso_of(T0 + 20000), Tags=fmt(["S", "T"]))
    px += row(Id=10002, PostTypeId=1, OwnerUserId=2, CreationDate=iso_of(T0 + 20001), Tags=fmt(["T"]))
    px += row(Id=10003, PostTypeId=2, ParentId=10001, OwnerUserId=2, CreationDate=iso_of(T0 + 20002))
    px += row(Id=10004, PostTypeId=2, ParentId=10001, OwnerUserId=2, CreationDate=iso_of(T0 + 20003))
    px += row(Id=10005, PostTypeId=2, ParentId=99999, OwnerUserId=2, CreationDate=iso_of(T0 + 20004))
    px += row(Id=10006, PostTypeId=1, CreationDate=iso_of(T0 + 20005), Tags=fmt(["T"]))
    px += row(Id=10007, PostTypeId=1, OwnerUserId=3, CreationDate=iso_of(T0 + 100000), Tags=fmt(["zz"])) + "</posts>\n"
    bx = "<badges>\n" + row(Id=1, UserId=2, Name="T", Date=iso_of(T0 + 30000), TagBased="True") + row(Id=2, UserId=2, Name="Teacher", Date=iso_of(T0 + 30001), TagBased="False") + "</badges>\n"
    return px, bx
for tf in ("angle", "pipe"):
    px, bx = se_files(tf)
    open(tmp / "Posts.xml", "w").write(px)
    open(tmp / "Badges.xml", "w").write(bx)
    inf = P.se_events(tmp / "Posts.xml", tmp / "Badges.xml", tmp / "se.tsv.gz")
    assert inf["badges"] == 1 and inf["answers_unresolved"] == 1 and inf["rows"] == 6000 + 2 + 2 + 1 + 1, inf
    tp, info = loadtrace("se_badges", tmp / "se.tsv.gz")
    ident(tp)
    ls = lines(tp)
    L = [l for l in ls]
    badge = [(l[1], int(l[2]), int(l[3])) for l in ls if l[1] == "R1" and l[0] == "+"]
    key = badge[-1]
    assert fan_of(tp, ("R1",) + key[1:]) == 3, (tf, fan_of(tp, ("R1",) + key[1:]))
    ids_disjoint(ls, [({"R1", "R5", "R3", "R4"} , 2), ({"R2", "R6"}, 2)])
    ids_disjoint(ls, [({"R1", "R5"}, 2), ({"R1", "R5"}, 3), ({"R2", "R6"}, 3)])
    nL = sum(1 for l in ls if l[0] == "L")
    assert nL == 6000 * 4 and info["initial_events"] == 6000, (nL, info)

ghl = [{"type": "MemberEvent", "created_at": "2015-01-01T00:00:10Z", "repo": {"name": "o/r"}, "payload": {"action": "added", "member": {"login": "bob", "id": 5}}},
       {"type": "MemberEvent", "created_at": "2015-01-01T00:00:20Z", "repo": {"name": "o/r"}, "payload": {"action": "removed", "member": {"id": 6}}},
       {"type": "MemberEvent", "created_at": "2015-01-01T00:00:21Z", "repo": {"name": "o/r"}, "payload": {"action": "edited", "member": {"login": "x"}}},
       {"type": "PullRequestEvent", "created_at": "2015-01-01T00:00:30Z", "repo": {"name": "o/r"}, "payload": {"action": "opened", "number": 7, "pull_request": {"user": {"login": "amy"}}}},
       {"type": "PullRequestEvent", "created_at": "2015-01-01T00:00:31Z", "repo": {"name": "o/r"}, "payload": {"action": "closed", "number": 7, "pull_request": {"user": {"login": "amy"}}}},
       {"type": "IssuesEvent", "created_at": "2015-01-01T00:00:32Z", "repo": {"name": "o/r"}, "payload": {"action": "opened", "issue": {"number": 7, "user": {"login": "amy"}}}},
       {"type": "WatchEvent", "created_at": "2015-01-01T00:00:33Z", "repo": {"name": "o/r"}, "payload": {"action": "started"}}]
gp = tmp / "h.json.gz"
with gzip.open(gp, "wt") as f: f.write("\n".join(json.dumps(e) for e in ghl) + "\nnot json\n")
B = 1420070400
assert sorted(P.parse_gh(gp)) == sorted([(B + 10, "member_added", "bob", "o/r"), (B + 20, "member_removed", 6, "o/r"), (B + 21, "edited", "x", "o/r"),
        (B + 30, "item", "pr:o/r#7", "o/r", "amy"), (B + 32, "item", "is:o/r#7", "o/r", "amy")]), P.parse_gh(gp)
ev = [(0, "item", "pr:a#1", "a", "u1"), (9, "item", "pr:a#2", "a", "u2"), (10, "member_added", "u1", "a"), (50, "member_removed", "u1", "a"), (60, "member_added", "u3", "a"),
      (100, "item", "pr:a#3", "a", "u3")]
P._write(tmp / "gh.tsv.gz", [(B + t, *r) for t, *r in ev])
tp, info = loadtrace("gh_members", tmp / "gh.tsv.gz")
ls = lines(tp)
assert sum(1 for l in ls if l[0] == "L") == 8 and info["initial_events"] == 2, (sum(1 for l in ls if l[0] == "L"), info)
assert [l[:2] for l in ls if l[0] == "-"] == [["-", "R1"], ["-", "R5"]], ls
assert [l[:2] for l in ls if l[0] == "+"].count(["+", "R1"]) == 2
ids_disjoint(ls, [({"R1", "R5"}, 2), ({"R1", "R5"}, 3), ({"R2", "R6"}, 3)])
assert open(tp).read().rstrip().endswith("E")

lx = tmp / "ldx" / "bi-x/graphs/csv/bi/composite-projected-fk"
def ldw(kind, ent, sub, hdr, rows):
    d = lx / kind / "dynamic" / ent / sub
    d.mkdir(parents=True, exist_ok=True)
    with gzip.open(d / "part-0.csv.gz", "wt") as f: f.write("|".join(hdr) + "\n" + "".join("|".join(map(str, r)) + "\n" for r in rows))
LS = lambda t: iso_of(B + t) + "+00:00"
ldw("initial_snapshot", "Forum_hasMember_Person", "", ["creationDate", "ForumId", "PersonId"], [(LS(0), 100, 1)])
ldw("initial_snapshot", "Forum_containerOf_Post", "", ["creationDate", "ForumId", "PostId"], [(LS(1), 100, 500)])
ldw("initial_snapshot", "Post_hasCreator_Person", "", ["creationDate", "PostId", "PersonId"], [(LS(1), 500, 1)])
ldw("inserts", "Forum_containerOf_Post", "batch_id=2015-01-01", ["creationDate", "ForumId", "PostId"], [(LS(20), 100, 501)])
ldw("inserts", "Post_hasCreator_Person", "batch_id=2015-01-01", ["creationDate", "PostId", "PersonId"], [(LS(20), 501, 2)])
ldw("inserts", "Forum_hasMember_Person", "batch_id=2015-01-01", ["creationDate", "ForumId", "PersonId"], [(LS(30), 100, 2)])
ldw("deletes", "Forum_hasMember_Person", "batch_id=2015-01-01", ["deletionDate", "ForumId", "PersonId"], [(LS(60), 100, 2)])
ldw("deletes", "Post", "batch_id=2015-01-01", ["deletionDate", "id"], [(LS(70), 500)])
ldw("inserts", "Post", "batch_id=2015-01-01", ["creationDate", "id", "content"], [(LS(20), 501, "x")])
ldw("deletes", "Post", "batch_id=2015-01-02", ["deletionDate", "id"], [(LS(100), 501)])
lev = P.ldbc_events(tmp / "ldx", tmp / "ld.tsv.gz")
assert lev["posts"] == 2 and lev["rows"] == 7, lev
assert [r[1] for r in P.read_events(tmp / "ld.tsv.gz")] == ["member+", "post", "post", "member+", "member-", "post-", "post-"]
tp, info = loadtrace("ldbc_members", tmp / "ld.tsv.gz")
ls = lines(tp)
assert sum(1 for l in ls if l[0] == "L") == 6 and info["initial_events"] == 2, (sum(1 for l in ls if l[0] == "L"), info)
rm = [l[1] for l in ls if l[0] == "-"]
assert sorted(rm) == ["R1", "R2", "R2", "R3", "R3", "R4", "R4", "R5", "R6", "R6"], rm
ids_disjoint(ls, [({"R1", "R5"}, 2), ({"R1", "R5"}, 3), ({"R2", "R6"}, 3)])
print("OK")
