# Wikidata sports relations (snapshot of 2026-08-07)

Input tables of the three Wikidata workloads (Wikidata-hub, Wikidata-control, Wikidata-member)
in the paper "Composite-Pair Partitioning: Sublinear Update Time for the Bow-Tie Join".
The code that turns them into update streams is in the `copair-bowtie` repository
(`src/copair/datasets/wikidata_sports.py`).

## Files

| File | Columns | Data rows | Uncompressed |
|---|---|---|---|
| `p54.tsv.gz` | `?x` person, `?a` sports team (`wdt:P54`, member of sports team) | 1,658,111 | 139 MB |
| `p27_full.tsv.gz` | `?x` person, `?b` country (`wdt:P27`, country of citizenship) | 6,468,274 | 524 MB |
| `p17_pairs.tsv.gz` | `?a` team, `?b` country (`wdt:P17`), teams with at least one member | 50,040 | 4.0 MB |
| `sports_light_fanout_pairs.tsv.gz` | `?a` team, `?b` country, `?f` number of low-degree persons that are members of `?a` and citizens of `?b` | 42,482 | 3.5 MB |

Each file is tab-separated with one header line; entities are full IRIs in angle brackets
(`<http://www.wikidata.org/entity/Q...>`). Row counts exclude the header and are the line counts
of the files as exported.

`SHA256SUMS` lists the checksums of the `.gz` files, `SHA256SUMS.uncompressed` those of the
decompressed `.tsv` files.

## Provenance

The tables are the results of four SPARQL queries over the Wikidata RDF dump of 2026-08-07
(truthy `wdt:` statements), run on a public SPARQL endpoint loaded with that dump. The queries
are in the code repository under `src/copair/datasets/sparql/`:

```sparql
PREFIX wdt: <http://www.wikidata.org/prop/direct/>
# p54.tsv
SELECT ?x ?a WHERE { ?x wdt:P54 ?a }
# p27_full.tsv
SELECT ?x ?b WHERE { ?x wdt:P27 ?b }
# p17_pairs.tsv
SELECT ?a ?b WHERE { ?a wdt:P17 ?b . FILTER EXISTS { ?x wdt:P54 ?a } }
# sports_light_fanout_pairs.tsv
SELECT ?a ?b (COUNT(?x) AS ?f) WHERE { ?a wdt:P17 ?b . ?x wdt:P54 ?a . ?x wdt:P27 ?b .
  { SELECT ?x (COUNT(?t) AS ?dt) WHERE { ?x wdt:P54 ?t } GROUP BY ?x }
  { SELECT ?x (COUNT(?c) AS ?dc) WHERE { ?x wdt:P27 ?c } GROUP BY ?x }
  FILTER(?dt + ?dc <= 13) } GROUP BY ?a ?b ORDER BY DESC(?f)
```

Running the queries on a later dump gives different tables, which is why this snapshot is
archived. The data are a static snapshot; the update streams of the paper are constructed from
them by the code and are not a record of real Wikidata edits.

## Use

This directory is where the code expects the tables (`$COPAIR_DATA/downloads/wikidata_sports/`,
with `COPAIR_DATA` defaulting to `<repo>/data`). Decompress them in place:

```sh
cd data/downloads/wikidata_sports
sha256sum -c SHA256SUMS
gunzip -k *.tsv.gz
sha256sum -c SHA256SUMS.uncompressed
```

Then follow the "Wikidata sports" steps of the code repository's README.

## License

Wikidata structured data is released under CC0 1.0
(<https://creativecommons.org/publicdomain/zero/1.0/>). These tables are unmodified query
results and are distributed under the same dedication.
