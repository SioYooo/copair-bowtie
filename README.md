# Artifact for "Composite-Pair Partitioning: Sublinear Update Time for the Bow-Tie Join" (fan-out version)

CoPair maintains the bow-tie join

`Q(a,b,x,d,e) = R1(a,b) R2(b,x) R3(x,a) R4(x,d) R5(d,e) R6(e,x)`

under single-tuple insertions and deletions, with set semantics, and lets a client enumerate `Q` in full after any update. This repository holds the C++ implementations (CoPair, a degree-partitioned variant of it, the per-x baseline PerX in five versions, the heavy/light baseline HL), an independent oracle, the workload generators, the experiment runner and the analysis scripts of the paper. The Wikidata tables of the paper are included in `data/downloads/wikidata_sports/`; the other datasets are downloaded, and traces, binaries and results are produced locally.

## Table of Contents

- [Getting Started](#getting-started)
  - [Hardware Requirements](#hardware-requirements)
  - [Project Environment](#project-environment)
  - [Paths](#paths)
  - [Build](#build)
- [Directory Structure](#directory-structure)
- [Usage Guide](#usage-guide)
  - [Names used in the paper](#names-used-in-the-paper)
  - [Traces and generators](#traces-and-generators)
  - [Experiments](#experiments)
- [Tests](#tests)
- [License](#license)

## Getting Started

### Hardware Requirements

A Linux machine with one free CPU core; no GPU. Every timed run is a single-threaded process pinned to one core. Shards of an experiment can run on separate machines.

### Project Environment

Linux, Bash, GCC 13 with C++17 (`g++ -std=c++17 -Wall -Wextra -O2 -DNDEBUG`), Python 3.9 or newer (tested with 3.11), `taskset` and GNU `time` (`/usr/bin/time`) for the runner, and the `zstd` command for the LDBC stream. The core experiments use the Python standard library only. Optional packages:

```sh
python3 -m pip install matplotlib   # Fig. 2 and the regime heat map
python3 -m pip install rdflib       # query-log scan
python3 -m pip install py7zr        # Stack Exchange archives (datasets.prepare se)
```

### Paths

`src/copair/paths.py` defines three directories; edit them there or set environment variables (defaults in brackets):

```sh
export COPAIR_DATA=/path/to/data         # [<repo>/data]     downloaded datasets, below downloads/
export COPAIR_BUILD=/path/to/build       # [<repo>/build]    binaries and *.build.json records
export COPAIR_RESULTS=/path/to/results   # [<repo>/results]  traces/, runs/ and all experiment outputs
export PYTHONPATH="$PWD/src"             # run every command from the repository root
R="${COPAIR_RESULTS:-$PWD/results}"      # used by the commands below
mkdir -p "$R/traces" "$R/confirm"
```

### Build

```sh
bash scripts/build.sh            # copair-, perx-, hl-{perf,debug,stats}, oracle, into $COPAIR_BUILD
```

Each binary has a `*.build.json` record (compiler, flags, sha256 of every source). The drivers share one command line, for example

```sh
build/copair-perf --algo copair --mode perf --hash-seed 1 --trace TRACE --out OUT.json
```

`--mode check` prints one digest per enumeration instead; `--no-per-op` times without per-update clocks. CoPair (`--algo copair`) takes `--psi`, `--theta` and `--tau`; the defaults are psi = N0^(1/4), theta = N0^(1/2) and tau = max(1, sqrt(psi*N0)/8), where N0 is the number of tuples at load time. `--opts MASK` selects the engineering options of `--algo copair-deg` (default 159).

## Directory Structure

```
README.md
scripts/build.sh            builds every binary (perf, debug, stats) and the oracle into build/
scripts/fanout_digest.sh    answer-digest comparison of CoPair, its straightforward implementation, the degree-partitioned variant and PerX on small traces
scripts/fanout_timing.sh    small timing comparison of the same four arms
scripts/mirror_right.py     mirrors a trace to the other side of the join
src/cpp/include/            header-only implementations
src/cpp/tools/              command-line drivers: copair_cli.cpp, perx_cli.cpp, hl_cli.cpp, oracle.cpp (+ driver.hpp)
src/copair/paths.py         data, build and result directories (edit here or use environment variables)
src/copair/workloads/       synthetic trace generators (synthetic.py), the regime-grid generator (regime.py), the stress trace (fanout_stress.py)
src/copair/datasets/        public-data preparation: Stack Exchange, Wikidata sports (sparql/*.sparql), update streams
src/copair/experiments/     runner, experiment specs, confidence intervals, oracle confirmation, regime and badge-stream runs
src/copair/analysis/        fan-out statistic s_100, public-stream scan, query-log scan, Fig. 2
tests/                      offline tests
data/                       inputs; data/downloads/wikidata_sports/ holds the Wikidata tables of the paper (gzip)
results/                    empty directory (.gitkeep) for outputs
```

Headers in `src/cpp/include/`: `copair_fanout.hpp` (CoPair, `copair`), `copair_fanout_base.hpp` (straightforward implementation of CoPair, `copair-simple`), `copair_final.hpp` (degree-partitioned variant `copair-deg`, `perx-known-state`, `perx-known-state-index`), `copair_core.hpp` (`copair-base` and `perx-base`), `hl.hpp` (HL), `perx_tuned.hpp` and `perx_tuned_index.hpp` (`perx-tuned`, `perx-tuned-index`), `perx_layout.hpp` (`perx-layout`).

## Usage Guide

| Experiment | Input | Commands |
| --- | --- | --- |
| Build | sources | [Build](#build) |
| Agreement of CoPair with the oracle | generated traces | [Oracle and audit confirmation](#oracle-and-audit-confirmation) |
| Answer digests across implementations | small generated traces | [Digest comparison](#digest-comparison) |
| Update time on 15 workloads | generated traces; MathOverflow files (SNAP); Wikidata sports tables (included) | [Timing matrix](#timing-matrix) |
| Cost of a hub toggle against the fan-out (two scans) | generated traces | [Fan-out scans](#fan-out-scans) |
| Crossover on the hub generator, badge streams | generated traces; Stack Exchange data dumps | [Crossover sweep and badge streams](#crossover-sweep-and-badge-streams) |
| Share of hub-pair updates in 26 public streams | SNAP, KONECT, GH Archive, Stack Exchange | [Fan-out statistic](#fan-out-statistic-s_100-on-the-26-public-streams) |
| Bow-tie shapes in SPARQL query logs | Wikidata query logs | [Query-log scan](#query-log-scan) |

### Names used in the paper

| Paper | `--algo` / `--variant` | Arm | Binary |
| --- | --- | --- | --- |
| CoPair | `--algo copair` | `copair` | `copair-perf` |
| Straightforward implementation of the paper's structure (answer comparison) | `--algo copair-simple` | `copair-simple` | `copair-perf` |
| Degree-partitioned variant: pairs classified by their degree in the pair graph, a witness set at every edge (exponent 4/5 in its cost analysis) | `--algo copair-deg` | `copair-deg` | `copair-perf` |
| Degree-partitioned variant with the light side off | `--algo copair-deg --psi 0` | `copair-deg-nolight` | `copair-perf` |
| PerX, five versions | `--algo perx --variant tuned` | `perx-tuned` | `perx-perf` |
| | `--algo perx --variant tuned-index` | `perx-tuned-index` | `perx-perf` |
| | `--algo perx --variant layout` | `perx-layout` | `perx-perf` |
| | `--algo perx --variant known-state` | `perx-known-state` | `copair-perf` |
| | `--algo perx --variant known-state-index` | `perx-known-state-index` | `copair-perf` |
| HL, epsilon = 1/2 | `--algo hl --eps 0.5` | `hl-0.5` | `hl-perf` |
| Answer-check references | `--algo copair-base`, `--algo perx --variant base` | `copair-base`, `perx-base` | `perx-perf` |

The `--algo` names are those of the command line and of the result files; the paper's names are in the first column. Parameters of CoPair: psi = N0^(1/4), theta = N0^(1/2), tau = sqrt(psi*N0)/8 (at least 1). The straightforward implementation (`copair-simple`) uses tau = sqrt(psi*N0).

| Paper workload | Identifier | | Paper workload | Identifier |
| --- | --- | --- | --- | --- |
| Hub-distinct | `hub-distinct` | | Osc-x | `osc-x` |
| Hub-shared | `hub-shared` | | Osc-a | `osc-a` |
| Heavy-centre | `heavy-centre` | | Osc-ab | `osc-ab` |
| Mid-degree | `mid-degree` | | Grow-shrink | `grow-shrink` |
| Pool-heavy | `pool-heavy` | | MathOverflow | `mathoverflow` |
| Pool-light | `pool-light` | | Wikidata-hub | `wikidata-hub` |
| Zipf | `zipf` | | Wikidata-control | `wikidata-control` |
| | | | Wikidata-member | `wikidata-member` |

Generator parameter sets of the paper: `GEN` in `src/copair/experiments/specs.py`.

### Traces and generators

Trace lines: `L R1 1 2` load a tuple, `+ R1 1 2` insert, `- R1 1 2` delete, `E` enumerate; relations `R1` to `R6`.

```sh
python3 -m copair.workloads.synthetic hub --n 10000 --H 16 --variant d --ops 200000 --enum_every 20000 --seed 1001 \
  --out "$R/traces/hub-distinct.trace"
```

Families: `hub`, `heavy_centre`, `mid_degree`, `pool`, `zipf`, `churn`, `mathoverflow`, and the correctness families used by the oracle confirmation. Fixed traces (paths in `paths.FIXED_TRACES`):

* MathOverflow: needs `sx-mathoverflow-a2q.txt.gz`, `sx-mathoverflow-c2q.txt.gz`, `sx-mathoverflow-c2a.txt.gz` in `$COPAIR_DATA/downloads/sx/` (SNAP Stack Exchange temporal files; the a2q URL is `https://snap.stanford.edu/data/sx-mathoverflow-a2q.txt.gz`).
  ```sh
  python3 -m copair.workloads.synthetic mathoverflow --seed 0 --max_ops 100000 --enum_every 5000 --out "$COPAIR_RESULTS/traces/mathoverflow.trace"
  ```
* Wikidata sports: input is four tab-separated tables (header line, then `<entity IRI>` columns) from a Wikidata RDF snapshot, in `$COPAIR_DATA/downloads/wikidata_sports/`: `p54.tsv`, `p17_pairs.tsv`, `p27_full.tsv`, `sports_light_fanout_pairs.tsv`. The tables used in the paper are included, compressed, in `data/downloads/wikidata_sports/` (see the README there); decompress them with `gunzip -k data/downloads/wikidata_sports/*.tsv.gz`. They are the results of the queries in `src/copair/datasets/sparql/{p54,p17_pairs,p27_full,sports_light_fanout_pairs}.sparql` over the Wikidata dump of 2026-08-07, run on a SPARQL endpoint loaded with that dump. Then, for each trace:
  ```sh
  D="$COPAIR_DATA/downloads/wikidata_sports"; T="${COPAIR_RESULTS:-$PWD/results}/traces/wikidata_sports"
  python3 -m copair.datasets.wikidata_sports build --data "$D" --pairs "$D/sports_light_fanout_pairs.tsv" --variant full \
    --out "$T/hub" --topk 2298 --control-k 0 --member-k 0 --enum-every 1000000
  python3 -m copair.datasets.wikidata_sports build --data "$D" --pairs "$D/sports_light_fanout_pairs.tsv" --variant full \
    --out "$T/control" --topk 0 --control-k 2298 --member-k 0 --enum-every 1000000
  python3 -m copair.datasets.wikidata_sports build --data "$D" --pairs "$D/sports_light_fanout_pairs.tsv" --variant full \
    --out "$T/member" --topk 0 --control-k 0 --member-k 10000 --enum-every 1000000
  ```

### Experiments

Timings in the paper were taken with one single-threaded process per machine: the runner pins each timed run to one core and runs one run at a time within a shard. Shards are independent. The order of the arms inside a block is pseudo-random per (cell, unit, repeat) and does not reproduce the orders of the paper's runs.

#### Oracle and audit confirmation

```sh
B="${COPAIR_BUILD:-$PWD/build}"
python3 -B -m copair.experiments.fanout_confirm --seeds 0-1 --jobs 4 --out "$R/confirm/fanout.json"
python3 -B -m copair.experiments.fanout_confirm --stress --seeds 0-1 --jobs 4 --out "$R/confirm/fanout-stress.json"
python3 -B -m copair.experiments.fanout_coverage --seeds 0-1 --stats "$B/copair-stats"
```

`fanout_confirm` compares every enumeration of `copair-debug --algo copair` with the oracle, with the structure audit on after every update, over the correctness families and a set of parameter sets (`PARAMSETS` in the file). `--stress` runs the stress trace of `workloads/fanout_stress.py`, built to reach every code path of the structure with small parameters. `--seeds` takes a range such as `0-1` or `301-320`; the failure count is printed at the end. `fanout_coverage` prints how often each internal counter fired. The confirmation of the other arms is `python3 -B -m copair.experiments.confirm confirm --set dev --seeds 0-1 --candidate "$B/copair-debug" --perx-variant layout --out "$R/confirm/confirm.json"`.

#### Digest comparison

```sh
bash scripts/fanout_digest.sh [DIR]      # default results/digest
```

Generates six small traces (including a mirrored trace and the stress trace), runs `copair`, `copair-simple`, `copair-deg` and `perx-layout` in `--mode check` on each, and prints `EQUAL` when the enumeration digests (count, sum, xor) of the four agree. `bash scripts/fanout_timing.sh [DIR]` times the same four arms (three repeats) on nine traces and prints the median update time, spread, peak memory and enumeration time per answer.

#### Timing matrix

Needs `perx-perf`, `copair-perf`, `hl-perf` and the four fixed traces.

```sh
W="$R/matrix"
python3 -B -m copair.experiments.runner plan --spec matrix --out "$W"
for k in 0 1 2 3 4 5 6 7 8 9 10 11; do python3 -B -m copair.experiments.runner shard --manifest "$W/manifest.jsonl" --shard $k --out "$W"; done
python3 -B -m copair.experiments.runner merge --dir "$W"
python3 -B -m copair.experiments.ci --dir "$W" --exclude-invalid "$W/merge_report.json" --out "$W/ci.json" --md "$W/ci.md" \
  --pairs copair/perx-tuned,copair/perx-tuned-index,copair/perx-layout,copair/perx-known-state,copair/perx-known-state-index,copair/hl-0.5,copair/copair-deg,copair-deg-repeat/copair-deg
```

The matrix has 15 workloads and the arms `copair-deg`, `copair`, `copair-simple`, `hl-0.5` and the five PerX versions; `copair-deg-repeat` (a second copy of `copair-deg`) is added on six workloads. `merge` must print PASS; it fails if an answer-digest check between an arm and its reference (`copair-base`, `perx-base`) fails. `ci` computes within-block ratios, the median over repeat groups per unit, a 95% t-interval, the +-5% classes and the cost-ratio columns; the table is `ci.md`. `copair-deg-repeat/copair-deg` is the A/A check of the timing procedure.

#### Fan-out scans

```sh
for w in hub-distinct hub-shared; do
  W="$R/nscan-$w"
  python3 -B -m copair.experiments.runner plan --spec nscan-$w --out "$W"
  python3 -B -m copair.experiments.runner shard --manifest "$W/manifest.jsonl" --shard 0 --out "$W"
  python3 -B -m copair.experiments.runner merge --dir "$W"
done
```

Arms: `copair`, `copair-deg`, `copair-deg-nolight`, `perx-layout`, `hl-0.5`, for n = 10^2 ... 10^5 light x per hub. The two scans are the generator variants `hub-distinct` and `hub-shared`. `python3 -m copair.analysis.fig2` reads these runs and draws the figure for `copair`, `perx-layout` and `hl-0.5`.

#### Crossover sweep and badge streams

```sh
python3 -m copair.experiments.reruns regime --cell 100_0.01      # n in 10 100 1000 10000, q in 0.0001 0.001 0.01 0.1
python3 -m copair.experiments.reruns badges --stream se-badges-mathoverflow   # also se-badges-stats, -askubuntu, -superuser
python3 -m copair.experiments.reruns summarize regime
python3 -m copair.experiments.reruns summarize badges
```

`regime` generates five instances per cell, checks that all eight arms give equal answer digests, then times them; the arm under test is `copair`, and `copair-deg`, the five PerX versions and `hl-0.5` are the references (ratios `final/fastest_perx`, `final/perx-layout`, `final/hl-0.5` and `final/copair-deg`, where `final` is `copair`). Results are in `$R/regime/` and `$R/badges/`. `--ops N` shortens the traces (smoke tests). `badges` reads `$COPAIR_DATA/downloads/se/events/<site>.tsv.gz`, which is created from the Stack Exchange data-dump archive of the site (a `.7z` file with `Posts.xml` and `Badges.xml`):

```sh
python3 -m copair.datasets.prepare se /path/to/mathoverflow.net.7z mathoverflow.net   # also stats.stackexchange.com, askubuntu.com, superuser.com
```

#### Fan-out statistic s_100 on the 26 public streams

`s_100` is the share of all updates that are R1/R5 updates closing triangles with at least 100 light x. For a single trace: `python3 -m copair.analysis.fanout_stat TRACE --out STAT.json`. For the streams:

```sh
python3 -m copair.analysis.fanout --out "$R/fanout" [--only name1,name2] [--no-download]
```

Stream names are in `STREAMS` of `src/copair/analysis/fanout.py`. Files are downloaded when absent from the URLs in that file (KONECT `http://konect.cc/files/download.tsv.link-dynamic-<name>.tar.bz2`; SNAP; GroupLens), under `$COPAIR_DATA/downloads/<name>/raw/`. Four of the SNAP streams use archive or prefix settings: `as-733` and `as-caida` (`as-733.tar.gz`, `as-caida.tar.gz` from `https://snap.stanford.edu/data/<name>.tar.gz`), and `CollegeMsg`, `email-Eu-core-temporal` (the SNAP temporal edge lists `CollegeMsg.txt.gz`, `email-Eu-core-temporal.txt.gz`; no URL is built into the code, place the files in `$COPAIR_DATA/downloads/<name>/raw/`). The paper uses the `full` row of `as-733` and `as-caida` and the `first_1e5` row (first 10^5 updates) of `CollegeMsg` and `email-Eu-core-temporal`. Other prepared streams:

* `se-badges-*`: `datasets.prepare se` as above.
* `gh-members-2015q1`: `python3 -m copair.datasets.prepare gh 2015-01-01 2015-03-31` (downloads hourly files from `https://data.gharchive.org/<day>-<hour>.json.gz`).
* `ldbc-bi-sf1-members`: `python3 -m copair.datasets.prepare ldbc`, expecting `downloads/ldbc/raw/bi-sf1-composite-projected-fk.tar.zst`.

Output: one JSON per stream plus `summary.json`, `downloads.json` and `selection.json`.

#### Query-log scan

```sh
python3 -m copair.analysis.query_log_scan scan --format wdlogs --out "$R/sparql" LOGFILE [LOGFILE ...]
```

Input: the Wikidata SPARQL query logs (tab-separated, one header line, URL-encoded query in the first column; `.gz` accepted), published at `analytics.wikimedia.org/datasets/one-off/wikidata/sparql_query_logs/`, files `_organic` and `_status_500`. `--format wdbench` reads the WDBench query files (one query per line). Output: per-file `.occ.jsonl` and `.summary.json`. Requires `rdflib`.

## Tests

All run offline (the last one needs the build):

```sh
python3 -B tests/test_ci.py
python3 -B tests/test_runner.py
python3 -B tests/test_reruns.py
python3 -B tests/test_fanout.py
python3 -B tests/test_confirm.py
```

## License

Apache License 2.0; see [LICENSE](LICENSE).
