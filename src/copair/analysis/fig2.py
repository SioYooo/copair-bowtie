import argparse
import hashlib
import json
import statistics as st
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from copair import paths
from copair.experiments.reruns import r15_mean

CELLS = {"hub-distinct": "Hub-distinct", "hub-shared": "Hub-shared"}
NS = [100, 1000, 10000, 100000]
FIGA = (3.45, 2.05)
PDFMETA = {"CreationDate": None, "Creator": None, "Producer": None}


def style():
    plt.rcParams.update({"font.family": "STIXGeneral", "mathtext.fontset": "stix", "pdf.fonttype": 42, "font.size": 8,
                         "xtick.labelsize": 8, "ytick.labelsize": 8, "axes.labelsize": 8.5, "legend.fontsize": 8.5,
                         "axes.linewidth": 0.6, "xtick.major.width": 0.6, "ytick.major.width": 0.6})


ARMS = {"hl-0.5": r"HL $\epsilon=1/2$", "perx-layout": "PerX", "copair": "CoPair"}
COLOR = {"hl-0.5": "#CC79A7", "perx-layout": "#D55E00", "copair": "#0072B2"}
MARK = {"hl-0.5": "D", "perx-layout": "^", "copair": "o"}


def sha16(p): return hashlib.sha256(open(p, "rb").read()).hexdigest()[:16]


def collect(rows):
    d = {}
    for r in rows:
        if r["status"] == "OK" and r["arm"] in ARMS:
            d.setdefault((int(r["cell"].split("_n")[1]), r["arm"]), []).append(r15_mean(r["updates"]))
    return d


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(paths.RESULTS / "figures"))
    ap.add_argument("--png")
    a = ap.parse_args(argv)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    style()
    fig, axs = plt.subplots(1, 2, figsize=FIGA, sharey=True)
    side = {"panels": {}, "sources": {}, "figsize_in": list(FIGA)}
    for ax, v in zip(axs, ["hub-shared", "hub-distinct"]):
        p = paths.RESULTS / ("nscan-%s/runs.jsonl" % v)
        side["sources"][str(p.relative_to(paths.RESULTS))] = sha16(p)
        d = collect([json.loads(l) for l in open(p)])
        P = side["panels"][v] = {"title": CELLS[v], "arms": {}}
        for arm in ARMS:
            ps = [d[(n, arm)] for n in NS]
            assert all(len(x) == 5 for x in ps), (v, arm, [len(x) for x in ps])
            md, lo, hi = [st.median(x) for x in ps], [min(x) for x in ps], [max(x) for x in ps]
            P["arms"][arm] = {"name": ARMS[arm], "n": NS, "median": md, "min": lo, "max": hi}
            top = arm == "copair"
            ax.fill_between(NS, lo, hi, color=COLOR[arm], alpha=0.2, lw=0)
            ax.plot(NS, md, color=COLOR[arm], marker=MARK[arm], ms=4.5 if top else 3.5, lw=1.8 if top else 1.0,
                    mfc="none" if arm == "copair-deg-nolight" else None, zorder=5 if top else 3)
            if top:
                g = md[-1] / md[0]
                P["copair_growth_n100_to_n100000"] = g
                ax.annotate(f"×{g:.1f}", (NS[-1], md[-1]), xytext=(-2, 5), textcoords="offset points", ha="right", va="bottom", fontsize=7, color=COLOR[arm])
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_ylim(bottom=2)
        ax.set_xticks(NS)
        ax.set_title(CELLS[v], fontsize=8.5, pad=2)
        ax.tick_params(length=2.5, pad=1.5)
        ax.minorticks_off()
    axs[0].set_ylabel(r"Mean $R_1/R_5$ update (ns)")
    fig.supxlabel(r"Light $x$ per hub, $n$", fontsize=8.5, y=0.01)
    hs = [Line2D([], [], color=COLOR[k], marker=MARK[k], ms=4.5 if k == "copair" else 3.5, lw=1.8 if k == "copair" else 1.0,
                 mfc="none" if k == "copair-deg-nolight" else None) for k in ARMS]
    fig.legend(hs, list(ARMS.values()), loc="upper center", ncol=2, frameon=False, handlelength=1.6, columnspacing=1.0,
               labelspacing=0.2, borderpad=0.1, bbox_to_anchor=(0.5, 1.0))
    fig.subplots_adjust(left=0.15, right=0.985, top=0.74, bottom=0.2, wspace=0.08)
    fig.savefig(out / "fig-mechanism.pdf", format="pdf", metadata=PDFMETA)
    if a.png: fig.savefig(a.png, dpi=200)
    json.dump(side, open(out / "fig2.json", "w"), indent=1, sort_keys=True)
    print({v: side["panels"][v]["copair_growth_n100_to_n100000"] for v in side["panels"]})


if __name__ == "__main__":
    main()
