import argparse
import calendar
import concurrent.futures as cf
import csv
import glob
import gzip
import json
import re
import shutil
import subprocess
import tarfile
import time
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

from copair import paths

D = paths.DOWNLOADS
UA = "research-bot/1.0"
KORD = {"post": 0, "item": 0, "member_added": 1, "member_removed": 2, "badge": 1, "member+": 1, "member-": 2, "post-": 3}


def _write(path, rows):
    def key(r): return (r[0], KORD.get(r[1], 9), r[1], [(0, int(x), "") if str(x).isdigit() else (1, 0, str(x)) for x in r[2:]])
    rows = sorted(rows, key=key)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".part")
    with gzip.open(tmp, "wt", encoding="utf-8", newline="") as f:
        for r in rows: f.write("\t".join(str(x) for x in r) + "\n")
    tmp.rename(path)
    return len(rows)


def read_events(path):
    with gzip.open(path, "rt", encoding="utf-8") as f:
        for line in f:
            a = line.rstrip("\n").split("\t")
            yield (int(a[0]), a[1], *a[2:])


def iso(s):
    return calendar.timegm(time.strptime(s[:19], "%Y-%m-%dT%H:%M:%S"))


def tags_of(s):
    if not s: return []
    return re.findall(r"<([^>]+)>", s) if "<" in s else [t for t in s.split("|") if t]


def _rows(path):
    for _, el in ET.iterparse(str(path), events=("end",)):
        if el.tag == "row": yield el.attrib
        el.clear()


def se_events(posts_xml, badges_xml, out_tsv):
    qtags, pending, rows = {}, [], []
    for a in _rows(posts_xml):
        pt, pid = a.get("PostTypeId"), a["Id"]
        if pt == "1":
            qtags[pid] = "|".join(tags_of(a.get("Tags")))
            if a.get("OwnerUserId"): rows.append((iso(a["CreationDate"]), "post", pid, a["OwnerUserId"], qtags[pid]))
        elif pt == "2" and a.get("OwnerUserId"):
            pending.append((iso(a["CreationDate"]), pid, a["OwnerUserId"], a.get("ParentId")))
    for ts, pid, u, par in pending:
        if par in qtags: rows.append((ts, "post", pid, u, qtags[par]))
    nb = 0
    for a in _rows(badges_xml):
        if a.get("TagBased") == "True" and a.get("UserId"):
            rows.append((iso(a["Date"]), "badge", a["UserId"], a["Name"]))
            nb += 1
    return {"rows": _write(out_tsv, rows), "questions": len(qtags), "answers_unresolved": sum(1 for p in pending if p[3] not in qtags), "badges": nb}


def se(archive, site, out=None):
    import py7zr
    x = D / "se" / site / "x"
    x.mkdir(parents=True, exist_ok=True)
    try:
        with py7zr.SevenZipFile(archive) as z: z.extract(path=x, targets=["Posts.xml", "Badges.xml"])
        return se_events(x / "Posts.xml", x / "Badges.xml", out or D / "se/events" / (site + ".tsv.gz"))
    finally:
        shutil.rmtree(x, ignore_errors=True)


def parse_gh(path):
    rows = []
    with gzip.open(path, "rt", encoding="utf-8", errors="replace") as f:
        for line in f:
            try: e = json.loads(line)
            except ValueError: continue
            t, p, repo = e.get("type"), e.get("payload") or {}, (e.get("repo") or {}).get("name")
            if not repo or not e.get("created_at"): continue
            ts = iso(e["created_at"])
            if t == "MemberEvent":
                m = p.get("member") or {}
                u = m.get("login") or m.get("id")
                if u is not None and p.get("action"): rows.append((ts, "member_" + p["action"] if p["action"] in ("added", "removed") else p["action"], u, repo))
            elif t in ("PullRequestEvent", "IssuesEvent") and p.get("action") == "opened":
                o = p.get("pull_request") if t == "PullRequestEvent" else p.get("issue")
                n = p.get("number") if t == "PullRequestEvent" else (o or {}).get("number")
                u = ((o or {}).get("user") or {}).get("login")
                if n is not None and u: rows.append((ts, "item", ("pr:" if t == "PullRequestEvent" else "is:") + "%s#%s" % (repo, n), repo, u))
    return rows


def _hour(day, h, tmpdir):
    url = "https://data.gharchive.org/%s-%d.json.gz" % (day, h)
    p = Path(tmpdir) / ("%s-%d.json.gz" % (day, h))
    err = None
    for _ in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=120) as r, open(p, "wb") as f:
                shutil.copyfileobj(r, f, 1 << 20)
            return h, parse_gh(p), None
        except Exception as e:
            err = "%s: %s" % (type(e).__name__, e)
        finally:
            p.unlink(missing_ok=True)
    return h, [], err


def gh(start, end, workers=8):
    days = []
    t = calendar.timegm(time.strptime(start, "%Y-%m-%d"))
    e = calendar.timegm(time.strptime(end, "%Y-%m-%d"))
    while t <= e:
        days.append(time.strftime("%Y-%m-%d", time.gmtime(t)))
        t += 86400
    dd = D / "gh/days"
    dd.mkdir(parents=True, exist_ok=True)
    logp = D / ("gh/download-log-%s_%s.json" % (start, end))
    log = json.load(open(logp)) if logp.exists() else {}
    tmpdir = D / "gh/hours"
    tmpdir.mkdir(parents=True, exist_ok=True)
    for d in days:
        if (dd / (d + ".tsv.gz")).exists(): continue
        rows = []
        with cf.ThreadPoolExecutor(workers) as ex:
            for h, r, err in ex.map(lambda h: _hour(d, h, tmpdir), range(24)):
                rows += r
                if err: log.setdefault(d, {})[str(h)] = err
        _write(dd / (d + ".tsv.gz"), rows)
        json.dump(log, open(logp, "w"), indent=1)
    rows = [r for d in days for r in read_events(dd / (d + ".tsv.gz"))]
    n = _write(D / ("gh/events/gh-%s_%s.tsv.gz" % (start, end)), rows)
    return {"rows": n, "failed_hours": sum(len(v) for v in log.values())}


LD_ENT = ("Forum_hasMember_Person", "Forum_containerOf_Post", "Post_hasCreator_Person", "Post")


def ldbc_extract(tar_zst, x):
    x = Path(x)
    x.mkdir(parents=True, exist_ok=True)
    p = subprocess.Popen(["zstd", "-dc", str(tar_zst)], stdout=subprocess.PIPE)
    with tarfile.open(fileobj=p.stdout, mode="r|") as tf:
        for m in tf:
            if m.isfile() and any("/dynamic/%s/" % e in m.name for e in LD_ENT): tf.extract(m, x)
    if p.wait(): raise RuntimeError("zstd failed")


def _ld(x, kind, ent):
    for p in sorted(glob.glob(str(Path(x) / "**" / kind / "dynamic" / ent / "**" / "*.csv.gz"), recursive=True)):
        with gzip.open(p, "rt", encoding="utf-8", newline="") as f:
            rd = csv.reader(f, delimiter="|")
            next(rd, None)
            for a in rd:
                if a: yield a


def ldbc_events(x, out_tsv):
    rows, forum, person = [], {}, {}
    for kind in ("initial_snapshot", "inserts"):
        for a in _ld(x, kind, "Forum_hasMember_Person"): rows.append((iso(a[0]), "member+", a[2], a[1]))
        for a in _ld(x, kind, "Forum_containerOf_Post"): forum[a[2]] = (iso(a[0]), a[1])
        for a in _ld(x, kind, "Post_hasCreator_Person"): person[a[1]] = a[2]
    for a in _ld(x, "deletes", "Forum_hasMember_Person"): rows.append((iso(a[0]), "member-", a[2], a[1]))
    npost = 0
    for pid, (ts, f) in forum.items():
        if pid in person:
            rows.append((ts, "post", pid, f, person[pid]))
            npost += 1
    for a in _ld(x, "deletes", "Post"): rows.append((iso(a[0]), "post-", a[1]))
    return {"rows": _write(out_tsv, rows), "posts": npost, "posts_without_creator": len(forum) - npost}


def ldbc(tar_zst=None, out=None):
    x = D / "ldbc/x"
    if not x.exists() or not any(x.rglob("*.csv.gz")): ldbc_extract(tar_zst or D / "ldbc/raw/bi-sf1-composite-projected-fk.tar.zst", x)
    return ldbc_events(x, out or D / "ldbc/events/ldbc-bi-sf1.tsv.gz")


def main():
    ap = argparse.ArgumentParser()
    s = ap.add_subparsers(dest="cmd", required=True)
    a = s.add_parser("se")
    a.add_argument("archive")
    a.add_argument("site")
    g = s.add_parser("gh")
    g.add_argument("start")
    g.add_argument("end")
    s.add_parser("ldbc")
    a = ap.parse_args()
    print(json.dumps(se(a.archive, a.site) if a.cmd == "se" else gh(a.start, a.end) if a.cmd == "gh" else ldbc()))


if __name__ == "__main__":
    main()
