import os
from pathlib import Path

PACKAGE_ROOT = Path(__file__).resolve().parents[2]

DATA = Path(os.environ.get("COPAIR_DATA") or PACKAGE_ROOT / "data").resolve()
BUILD = Path(os.environ.get("COPAIR_BUILD") or PACKAGE_ROOT / "build").resolve()
RESULTS = Path(os.environ.get("COPAIR_RESULTS") or PACKAGE_ROOT / "results").resolve()

TRACES = RESULTS / "traces"
RUNS = RESULTS / "runs"
DOWNLOADS = DATA / "downloads"
SX = DOWNLOADS / "sx"

FIXED_TRACES = {
    "mathoverflow": TRACES / "mathoverflow.trace",
    "wikidata-hub": TRACES / "wikidata_sports/hub/init_and_toggles.trace",
    "wikidata-control": TRACES / "wikidata_sports/control/init_and_toggles.trace",
    "wikidata-member": TRACES / "wikidata_sports/member/init_and_toggles.trace",
}
