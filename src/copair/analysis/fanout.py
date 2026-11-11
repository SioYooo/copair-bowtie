import argparse
import json
import os
import random
import shutil
import tarfile
import time
import urllib.request
from pathlib import Path

from copair.analysis.fanout_stat import stat
from copair.datasets import stackexchange
from copair.datasets import native_deletions
from copair.datasets import adapters
from copair import paths

D = paths.DOWNLOADS
SW = D / "simplewiki/raw/download.tsv.link-dynamic-simplewiki.tar.bz2"
LIMIT_S = 7200
UA = "research-bot/1.0"


def _konect(w, path=None):
    return {"name": w, "kind": "konect", "path": Path(path) if path else D / w / "raw" / ("download.tsv.link-dynamic-%s.tar.bz2" % w),
            "url": "http://konect.cc/files/download.tsv.link-dynamic-%s.tar.bz2" % w, "member": "link-dynamic-%s/out.link-dynamic-%s" % (w, w)}


STREAMS = [_konect("simplewiki", SW)] + [_konect(w) for w in ("nlwiki", "plwiki", "itwiki")] + [
    {"name": "wiki-talk-temporal", "kind": "snap", "path": D / "wiki-talk-temporal/raw/wiki-talk-temporal.txt.gz",
     "url": "https://snap.stanford.edu/data/wiki-talk-temporal.txt.gz", "member": None}] + [_konect(w) for w in ("frwiki", "dewiki")]
SXD = paths.SX
STREAMS += [{"name": "higgs-activity", "kind": "snap", "path": D / "higgs/raw/higgs-activity_time.txt.gz",
             "url": "https://snap.stanford.edu/data/higgs-activity_time.txt.gz", "member": None}]
STREAMS += [dict(STREAMS[-1], name="higgs-activity-w1", window_days=1)]
STREAMS += [{"name": "sx3-" + n, "kind": "sx3", "sxname": n, "member": None, "url": "https://snap.stanford.edu/data/sx-%s-a2q.txt.gz" % n,
             "path": SXD / ("sx-%s-a2q.txt.gz" % n)}
            for n in ("mathoverflow", "askubuntu", "superuser", "stackoverflow")]
RD = D
STREAMS += [{"name": "reddit-hyperlinks", "kind": "reddit", "member": None, "url": "https://snap.stanford.edu/data/soc-redditHyperlinks-body.tsv",
             "path": RD / "reddit/raw/soc-redditHyperlinks-body.tsv",
             "paths": [RD / "reddit/raw/soc-redditHyperlinks-body.tsv", RD / "reddit/raw/soc-redditHyperlinks-title.tsv"]}]
STREAMS += [{"name": n, "kind": "tagging", "fmt": fmt, "url": u, "member": m, "path": RD / z}
            for n, fmt, u, m, z in (
                ("tag-movielens", "movielens", "https://files.grouplens.org/datasets/movielens/ml-latest.zip", "ml-latest/tags.csv", "movielens/raw/ml-latest.zip"),
                ("tag-delicious", "hetrec-delicious", "https://files.grouplens.org/datasets/hetrec2011/hetrec2011-delicious-2k.zip",
                 "user_taggedbookmarks-timestamps.dat", "hetrec/raw/hetrec2011-delicious-2k.zip"),
                ("tag-lastfm", "hetrec-lastfm", "https://files.grouplens.org/datasets/hetrec2011/hetrec2011-lastfm-2k.zip",
                 "user_taggedartists-timestamps.dat", "hetrec/raw/hetrec2011-lastfm-2k.zip"))]
EV = RD
STREAMS += [{"name": "se-badges-" + sh, "kind": "se", "member": None, "url": None, "path": EV / "se/events" / (site + ".tsv.gz")}
            for sh, site in (("mathoverflow", "mathoverflow.net"), ("stats", "stats.stackexchange.com"), ("askubuntu", "askubuntu.com"), ("superuser", "superuser.com"))]
STREAMS += [{"name": "gh-members-2015q1", "kind": "gh", "member": None, "url": None, "path": EV / "gh/events/gh-2015-01-01_2015-03-31.tsv.gz"},
            {"name": "ldbc-bi-sf1-members", "kind": "ldbc", "member": None, "url": None, "path": EV / "ldbc/events/ldbc-bi-sf1.tsv.gz"}]
STREAMS += [{"name": n, "kind": "snap", "member": None, "url": None, "path": D / n / "raw" / (n + ".txt.gz")} for n in ("CollegeMsg", "email-Eu-core-temporal")]
STREAMS += [{"name": n, "kind": "snapshots", "member": None, "url": "https://snap.stanford.edu/data/%s.tar.gz" % n, "path": D / n / "raw" / (n + ".tar.gz")}
            for n in ("as-733", "as-caida")]


class StreamError(Exception):
    def __init__(self, cls, msg):
        super().__init__(msg)
        self.cls = cls


def download(url, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".part")
    try:
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
            shutil.copyfileobj(r, f, 1 << 20)
        tmp.rename(path)
    except Exception as e:
        tmp.unlink(missing_ok=True)
        raise StreamError("DOWNLOAD_FAIL", "%s: %s" % (type(e).__name__, e))


def _check(t0, limit):
    if time.time() - t0 > limit:
        raise StreamError("TIMEOUT", "per-stream wall limit %ss exceeded" % limit)


def measure(spec, scratch, t0, limit):
    path = spec["path"]
    res = {}
    if spec["kind"] == "konect":
        try:
            with tarfile.open(path, "r:*") as tf:
                names = tf.getnames()
        except Exception as e:
            raise StreamError("PARSE_FAIL", "cannot list tar: %s: %s" % (type(e).__name__, e))
        if spec["member"] not in names:
            raise StreamError("PARSE_FAIL", "member %s not in tar (members: %s)" % (spec["member"], names[:10]))
    for label, mo in (("full", None), ("first_1e5", 100000)):
        _check(t0, limit)
        tp = scratch / "t.trace"
        try:
            with open(tp, "w") as f:
                if spec["kind"] == "konect":
                    info = native_deletions.events(f, str(path), spec["member"], max_ops=mo)
                elif spec["kind"] == "sx3":
                    if str(SXD) not in stackexchange.DIRS: stackexchange.DIRS.insert(0, str(SXD))
                    stackexchange.sx(f, random.Random(0), name=spec["sxname"], window_days=30, max_ops=10**12 if mo is None else mo)
                    info = {"mapping": "a2q->R1,R4; c2q->R2,R5; c2a->R3,R6", "window_days": 30, "max_ops": mo}
                elif spec["kind"] == "snapshots":
                    info = native_deletions.snapshots(f, str(path), max_ops=mo)
                elif spec["kind"] == "reddit":
                    info = adapters.reddit(f, [str(p) for p in spec["paths"]], window_days=30, max_ops=mo)
                elif spec["kind"] in ("se", "gh", "ldbc"):
                    info = {"se": adapters.se_badges, "gh": adapters.gh_members, "ldbc": adapters.ldbc_members}[spec["kind"]](f, path, max_ops=mo)
                elif spec["kind"] == "tagging":
                    info = adapters.tagging(f, "%s::%s" % (path, spec["member"]), spec["fmt"], window_days=30, max_ops=mo)
                else:
                    stackexchange.onetype(f, random.Random(0), str(path), window_days=spec.get("window_days", 30), max_ops=10**12 if mo is None else mo)
                    info = {"window_days": spec.get("window_days", 30), "max_ops": mo}
        except MemoryError:
            raise
        except Exception as e:
            raise StreamError("PARSE_FAIL", "%s: %s" % (type(e).__name__, e))
        _check(t0, limit)
        n0 = sum(1 for l in open(tp) if l[0] == "L")
        s = stat(str(tp))
        _check(t0, limit)
        fs = stat(str(tp), ret_f=True)["fs"]
        nu = s["n_updates"]
        big = lambda t: sum(1 for f in fs if f >= t)
        s10 = big(10) / nu if nu else None
        s100 = big(100) / nu if nu else None
        assert s100 == s["s_100"], (s100, s["s_100"])
        res[label] = {"gen_info": info, "N0": n0, **s, "s_10": s10, "s_100_recomputed": s100,
                      "n_R1R5_f_ge": {str(t): big(t) for t in (1, 10, 100, 1000)},
                      "share_R1R5_f_ge100": big(100) / len(fs) if fs else None, "top20_f": sorted(fs, reverse=True)[:20]}
        print("%-18s %-9s upd=%s s100=%s s10=%s max=%s rt=%ss" % (spec["name"], label, nu, s["s_100"], s10, s["max_f"], s["runtime_s"]), flush=True)
        tp.unlink()
    _check(t0, limit)
    return res


def run_stream(spec, out, scratch, do_download=True, limit=LIMIT_S):
    t0 = time.time()
    path = spec["path"]
    r = {"name": spec["name"], "status": "OK"}
    try:
        if not path.exists():
            if not spec.get("url"):
                raise StreamError("DOWNLOAD_FAIL", "input file absent (see README): %s" % path)
            if not do_download:
                raise StreamError("DOWNLOAD_FAIL", "file absent and downloads disabled: %s" % path)
            download(spec["url"], path)
        r["download"] = {"path": str(path), "bytes": path.stat().st_size, "sha256": native_deletions._sha(path), "url": spec["url"]}
        r["rows"] = measure(spec, scratch, t0, limit)
    except StreamError as e:
        r.update(status=e.cls, error=str(e))
    except MemoryError:
        r.update(status="MEM_FAIL", error="MemoryError")
    except Exception as e:
        r.update(status="PARSE_FAIL", error="%s: %s" % (type(e).__name__, e))
    r["wall_s"] = round(time.time() - t0, 1)
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    json.dump(r, open(out / (spec["name"] + ".json"), "w"), indent=1)
    rebuild(out)
    return r


def rebuild(out):
    rs = [json.load(open(out / (s["name"] + ".json"))) for s in STREAMS if (out / (s["name"] + ".json")).exists()]
    summary = {r["name"]: (r["rows"] if r["status"] == "OK" else {"error_class": r["status"], "error": r["error"]}) for r in rs}
    sel = {r["name"]: r["rows"]["full"]["s_100"] for r in rs if r["status"] == "OK"}
    json.dump(summary, open(out / "summary.json", "w"), indent=1)
    json.dump({r["name"]: r["download"] for r in rs if "download" in r}, open(out / "downloads.json", "w"), indent=1)
    json.dump({"rule": "s_100 >= 0.01 on the full stream", "s_100_full": sel,
               "selected": sorted((n for n, v in sel.items() if v is not None and v >= 0.01), key=lambda n: -sel[n])},
              open(out / "selection.json", "w"), indent=1)


def run_all(specs, out, do_download=True, limit=LIMIT_S):
    out.mkdir(parents=True, exist_ok=True)
    scratch = Path(os.environ.get("TMPDIR", "/tmp")) / ("fanout-%d" % os.getpid())
    scratch.mkdir()
    try:
        return [run_stream(s, out, scratch, do_download, limit) for s in specs]
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--only")
    ap.add_argument("--no-download", action="store_true")
    a = ap.parse_args()
    only = a.only.split(",") if a.only else None
    run_all([s for s in STREAMS if only is None or s["name"] in only], Path(a.out), not a.no_download)


if __name__ == "__main__":
    main()
