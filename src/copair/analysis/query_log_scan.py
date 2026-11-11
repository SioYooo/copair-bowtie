import argparse
import collections
import gzip
import itertools
import json
import logging
import os
import re
import sys
import time
from urllib.parse import unquote_plus
from rdflib import Literal, URIRef, Variable
from rdflib.plugins.sparql.parser import parseQuery
from rdflib.plugins.sparql.algebra import translateQuery
from rdflib.plugins.sparql.parserutils import CompValue

logging.disable(logging.CRITICAL)
CAP = 50
PFX = dict(
    wd="http://www.wikidata.org/entity/", wdt="http://www.wikidata.org/prop/direct/",
    wds="http://www.wikidata.org/entity/statement/", p="http://www.wikidata.org/prop/",
    ps="http://www.wikidata.org/prop/statement/", psv="http://www.wikidata.org/prop/statement/value/",
    pq="http://www.wikidata.org/prop/qualifier/", pqv="http://www.wikidata.org/prop/qualifier/value/",
    pr="http://www.wikidata.org/prop/reference/", prv="http://www.wikidata.org/prop/reference/value/",
    wdno="http://www.wikidata.org/prop/novalue/", wdv="http://www.wikidata.org/value/",
    wdref="http://www.wikidata.org/reference/", wikibase="http://wikiba.se/ontology#",
    bd="http://www.bigdata.com/rdf#", hint="http://www.bigdata.com/queryHints#",
    rdfs="http://www.w3.org/2000/01/rdf-schema#", rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#",
    xsd="http://www.w3.org/2001/XMLSchema#", schema="http://schema.org/",
    skos="http://www.w3.org/2004/02/skos/core#", owl="http://www.w3.org/2002/07/owl#",
    prov="http://www.w3.org/ns/prov#", geo="http://www.opengis.net/ont/geosparql#",
    geof="http://www.opengis.net/def/function/geosparql/", dct="http://purl.org/dc/terms/",
    gas="http://www.bigdata.com/rdf/gas#", wdtn="http://www.wikidata.org/prop/direct-normalized/")
PRELUDE = "".join("PREFIX %s: <%s>\n" % kv for kv in PFX.items())
MODSP = re.compile(r"(<[^<>\s]*>)\s+([*+])")
RELS = ("R1", "R2", "R3", "R4", "R5", "R6")
QID = re.compile(r"<http://www\.wikidata\.org/entity/(Q\d+)>")


def canon(q):
    m = {}
    return QID.sub(lambda mo: m.setdefault(mo.group(1), "<http://www.wikidata.org/entity/Q%d>" % (len(m) + 1)), q)


def kind(n):
    return "var" if isinstance(n, Variable) else "iri" if isinstance(n, URIRef) else \
        "literal" if isinstance(n, Literal) else "bnode"


def s_(n):
    return n.n3() if hasattr(n, "n3") else str(n)


def varsin(n, out):
    if isinstance(n, Variable):
        out.add(n)
    elif isinstance(n, CompValue):
        for k, v in n.items():
            if k != "_vars":
                varsin(v, out)
    elif isinstance(n, (list, tuple)):
        for c in n:
            varsin(c, out)
    return out


def walk(n, ctx, st):
    if isinstance(n, (list, tuple)):
        for c in n:
            walk(c, ctx, st)
        return
    if not isinstance(n, CompValue):
        return
    nm = n.name
    sub = lambda c: ctx if ctx != "core" else c
    if nm in ("BGP", "TriplesBlock"):
        for t in n.get("triples", []):
            st["tr"].append((t[0], t[1], t[2], ctx))
        return
    if nm == "Project":
        if st["top"] is None and ctx == "core":
            st["top"] = list(n.get("PV") or [])
            walk(n.get("p"), ctx, st)
        else:
            walk(n.get("p"), sub("subselect"), st)
        return
    if nm == "LeftJoin":
        walk(n.get("p1"), ctx, st)
        walk(n.get("p2"), sub("optional"), st)
        walk(n.get("expr"), sub("expr"), st)
        return
    if nm == "Minus":
        walk(n.get("p1"), ctx, st)
        walk(n.get("p2"), sub("minus"), st)
        return
    if nm in ("Union", "Graph", "Service"):
        for k, v in n.items():
            if k != "_vars":
                walk(v, sub(nm.lower()), st)
        return
    if nm in ("Builtin_EXISTS", "Builtin_NOTEXISTS"):
        walk(n.get("graph"), ctx if ctx not in ("core", "expr") else nm[8:].lower().replace("not", "not_"), st)
        return
    if ctx == "core":
        if nm in ("Distinct", "Reduced"):
            st["distinct"] = True
        elif nm == "OrderBy":
            st["order"] = True
        elif nm == "Group":
            st["group"] = True
        elif nm == "Slice" and n.get("length") is not None:
            st["limit"] = int(n.get("length"))
        elif nm == "Filter":
            st["filters"].append(varsin(n.get("expr"), set()))
    for k, v in n.items():
        if k != "_vars":
            walk(v, sub("expr") if k == "expr" else ctx, st)


def find_bowties(text):
    res = {"status": "parsed", "uncovered": [], "core_edges": 0, "noncore_edges": 0, "occurrences": [],
           "capped": False}
    try:
        pq = parseQuery(MODSP.sub(r"\1\2", text))
        alg = translateQuery(pq)
    except Exception as e:
        res.update(status="unparsed", error=type(e).__name__)
        return res
    star = pq[1].get("projection") is None
    st = dict(tr=[], top=None, select=pq[1].name == "SelectQuery", distinct=False, order=False,
              group=False, limit=None, filters=[], form=pq[1].name)
    try:
        walk(alg.algebra, "core", st)
    except Exception as e:
        res.update(status="unparsed", error="walk-" + type(e).__name__)
        return res
    unc, pairs, noncore = set(), collections.defaultdict(list), []
    for s, p, o, ctx in st["tr"]:
        if not isinstance(p, URIRef):
            unc.add("variable-predicate" if isinstance(p, Variable) else "property-path")
        if ctx != "core":
            noncore.append((s, p, o, ctx))
        elif isinstance(p, URIRef):
            res["core_edges"] += 1
            if s != o:
                pairs[frozenset((s, o))].append((s, p, o))
    res["noncore_edges"] = len(noncore)
    res["uncovered"] = sorted(unc)
    adj = collections.defaultdict(set)
    for pr in pairs:
        u, v = tuple(pr)
        adj[u].add(v)
        adj[v].add(u)
    projected = None if (star or st["top"] is None) else set(st["top"])
    for x in sorted(adj, key=s_):
        nb = sorted(adj[x], key=s_)
        if len(nb) < 4:
            continue
        es = [(u, v) for u, v in itertools.combinations(nb, 2) if frozenset((u, v)) in pairs]
        for (a, b), (d, e) in itertools.combinations(es, 2):
            if len({a, b, d, e}) < 4:
                continue
            if len(res["occurrences"]) >= CAP:
                res["capped"] = True
                return res
            res["occurrences"].append(mk_occ(x, a, b, d, e, pairs, adj, noncore, st, projected))
    return res


def mk_occ(x, a, b, d, e, pairs, adj, noncore, st, projected):
    five = {x, a, b, d, e}
    edges = {}
    for r, (f, s) in zip(RELS, [(a, b), (b, x), (x, a), (x, d), (d, e), (e, x)]):
        edges[r] = [dict(s=s_(t[0]), p=s_(t[1]), o=s_(t[2]), pred=s_(t[1]),
                         dir="fwd" if (t[0], t[2]) == (f, s) else "rev") for t in pairs[frozenset((f, s))]]
    used = {frozenset(p) for p in [(a, b), (b, x), (x, a), (x, d), (d, e), (e, x)]}
    extra_e = sum(1 for p in pairs if p <= five and p not in used)
    extra_v = {m for n in five for m in adj[n] if m not in five and isinstance(m, Variable)}
    fv = {n for n in five if isinstance(n, Variable)}
    return dict(
        roles={k: s_(v) for k, v in zip("xabde", (x, a, b, d, e))}, edges=edges,
        n_const=sum(1 for n in five if not isinstance(n, Variable)),
        extra_core_edges_among_five=extra_e, extra_core_vars=len(extra_v),
        projects_all_five=projected is None or fv <= projected,
        distinct=st["distinct"], limit=st["limit"], order_by=st["order"], group_by=st["group"],
        filters_on_five=sum(1 for f in st["filters"] if f & fv), form=st["form"],
        noncore_blocks_on_five=sorted({c for s, p, o, c in noncore if s in five or o in five}),
        candidate=not (five - fv) and (projected is None or fv <= projected) and not st["distinct"]
        and st["form"] == "SelectQuery")


def scan_file(path, fmt, outdir, limit, min_iri):
    t0 = time.time()
    base = os.path.basename(path)
    texts = collections.OrderedDict()
    lines = pre_out = 0
    op = gzip.open if path.endswith(".gz") else open
    with op(path, "rt", encoding="utf-8", errors="replace") as f:
        if fmt == "wdlogs":
            next(f, None)
        for i, ln in enumerate(f, start=2 if fmt == "wdlogs" else 1):
            if limit is not None and lines >= limit:
                break
            ln = ln.rstrip("\n")
            if not ln:
                continue
            lines += 1
            if fmt == "wdbench":
                q, ts, ag, sc = ln.split(",", 1)[-1], None, None, None
            else:
                c = ln.split("\t")
                q, ts = unquote_plus(c[0]), c[1] if len(c) > 1 else None
                sc = c[2] if len(c) > 2 else None
                ag = (c[3].replace("/", " ").split() or [""])[0] if len(c) > 3 else None
                if q.count("<") < min_iri:
                    pre_out += 1
                    continue
            key = canon(q) if fmt == "wdlogs" else q
            r = texts.get(key)
            if r is None:
                r = texts[key] = dict(line_no=i, count=0, ts=[], agents=collections.Counter(),
                                      cats=collections.Counter(), q=q, raw=set())
            r["count"] += 1
            r["raw"].add(q)
            if ts:
                r["ts"].append(ts)
            if ag:
                r["agents"][ag] += 1
            if sc:
                r["cats"][sc] += 1
    sm = dict(file=base, format=fmt, limit=limit, min_iri=min_iri if fmt == "wdlogs" else None, lines=lines,
              prefiltered_out=pre_out, parsed=0, unparsed=0, partial=0, parsed_lines=0, unparsed_lines=0,
              partial_lines=0, uncovered_counts=collections.Counter(), queries_with_occurrence=0,
              occurrences=0, candidates=0, capped_queries=0, distinct_canonical_texts=len(texts),
              distinct_query_texts=sum(len(r["raw"]) for r in texts.values()))
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, base + ".occ.jsonl"), "w") as out:
        for r in texts.values():
            q = r["q"]
            res = find_bowties(PRELUDE + q if fmt == "wdlogs" else "SELECT * WHERE { %s }" % q)
            n = r["count"]
            if res["status"] == "unparsed":
                sm["unparsed"] += 1
                sm["unparsed_lines"] += n
                continue
            sm["parsed"] += 1
            sm["parsed_lines"] += n
            for u in res["uncovered"]:
                sm["uncovered_counts"][u] += 1
            if res["uncovered"]:
                sm["partial"] += 1
                sm["partial_lines"] += n
            sm["capped_queries"] += res["capped"]
            if res["occurrences"]:
                sm["queries_with_occurrence"] += 1
            for o in res["occurrences"]:
                sm["occurrences"] += 1
                sm["candidates"] += o["candidate"]
                out.write(json.dumps(dict(
                    file=base, line_no=r["line_no"], query_count=n, ts_first=min(r["ts"], default=None),
                    ts_last=max(r["ts"], default=None), agent_classes=dict(r["agents"]),
                    distinct_raw_texts=len(r["raw"]),
                    source_categories=dict(r["cats"]), partial=bool(res["uncovered"]),
                    uncovered=res["uncovered"], capped=res["capped"], **o, query=q)) + "\n")
    sm["wall_seconds"] = round(time.time() - t0, 2)
    with open(os.path.join(outdir, base + ".summary.json"), "w") as f:
        json.dump(sm, f, indent=1)
    print(json.dumps(sm), flush=True)


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True).add_parser("scan")
    sp.add_argument("--format", choices=["wdbench", "wdlogs"], required=True)
    sp.add_argument("--out", required=True)
    sp.add_argument("--limit", type=int)
    sp.add_argument("--min-iri", type=int, default=6)
    sp.add_argument("files", nargs="+")
    a = ap.parse_args()
    sys.setrecursionlimit(5000)
    for p in a.files:
        scan_file(p, a.format, a.out, a.limit, a.min_iri)


if __name__ == "__main__":
    main()
