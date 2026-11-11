#include "driver.hpp"
#include "../include/perx_layout.hpp"
#include "../include/copair_final.hpp"
#include "../include/copair_fanout_base.hpp"
#include "../include/copair_fanout.hpp"

static const char* USAGE =
    "usage: copair-<mode> --algo copair|copair-simple|copair-deg|copair-base|perx [--variant base|layout|known-state|known-state-index] [--psi <v>] [--theta <v>] [--mu <v>] [--tau <v>] [--opts MASK]\n"
    "  paper names: CoPair = --algo copair; --algo copair-deg is the degree-partitioned variant (pairs classified by degree in the pair graph, a witness set at every edge); --algo copair-simple is a straightforward implementation of the same structure as copair, kept for answer comparison.\n"
    "  --opts MASK: option bitmask of copair-deg (default 159, <= 255); the perx variants fix their own mask (layout 7, known-state 32, known-state-index 96).\n"
    "  copair-deg, copair-base: psi=N0^(1/5), theta=N0^(1/2), mu=sqrt(m0); --psi/--theta/--mu fix the value (any subset; ignored by perx).\n"
    "  copair: psi=N0^(1/4), theta=N0^(1/2), tau=max(1,sqrt(psi*N0)/8) (copair-simple: tau=max(1,sqrt(psi*N0))); --psi/--theta/--tau fix the value; --mu is ignored.\n";

int main(int argc, char** argv) {
  Args ar;
  ar.algo = "copair";
  int rc = parseArgs(argc, argv, ar, USAGE);
  if (rc >= 0) return rc;
  std::string name = ar.algo;
  if (name == "perx") {
    if (ar.variant.empty()) { fprintf(stderr, "--algo perx needs --variant\n%s", USAGE); return 2; }
    name = "perx-" + ar.variant;
  }
  ar.algo = name;
  static const struct { const char* n; unsigned mask; } FIXED[] = {{"perx-layout", 7}, {"perx-known-state", 32}, {"perx-known-state-index", 96}};
  for (auto& f : FIXED) if (name == f.n) { if (ar.haveOpts) { fprintf(stderr, "--opts is fixed for %s\n", name.c_str()); return 2; } ar.opts = f.mask; }
  if (name == "copair-deg" && !ar.haveOpts) ar.opts = 159;
  if (name != "copair-deg" && name != "copair-base" && name != "copair" && name != "copair-simple" && name != "perx-base" && name != "perx-layout" && name != "perx-known-state" && name != "perx-known-state-index") {
    fprintf(stderr, "unknown algo %s\n%s", ar.algo.c_str(), USAGE); return 2;
  }
  if (name == "copair-deg" && ar.opts > 255) { fprintf(stderr, "--opts must be <= 255 for copair-deg\n"); return 2; }
  Params p; fillParams(p, ar);
  layout::Params layoutParams; fillParams(layoutParams, ar);
  current::Params currentParams; fillParams(currentParams, ar);
  hash_seed(ar.hashSeed); layout::hash_seed(ar.hashSeed); current::hash_seed(ar.hashSeed);
  if (ar.mode != "perf") fprintf(stderr, "#hash_seed %llu\n", (unsigned long long)ar.hashSeed);
  if (name == "copair" || name == "copair-simple") {
    FanParams fp; fillParams(fp, ar); fp.psiExp = 0.25;
    if (ar.haveTau) { fp.tauFixed = true; fp.tau = ar.tau; }
    if (name == "copair") { CopairFanout alg(fp); return execute(alg, ar, "copair"); }
    CopairFanoutBase alg(fp);
    return execute(alg, ar, "copair");
  }
  if (name == "copair-deg") { current::Copair alg(currentParams); return execute(alg, ar, "copair"); }
  if (name == "perx-known-state") { current::PerxKnownState alg(currentParams); return execute(alg, ar, "copair"); }
  if (name == "perx-known-state-index") { current::PerxKnownStateIndex alg(currentParams); return execute(alg, ar, "copair"); }
  if (name == "perx-layout") { layout::PerxLayout alg(layoutParams); return execute(alg, ar, "copair"); }
  if (name == "perx-base") { PerxBase alg(p); return execute(alg, ar, "copair"); }
  CopairBase alg(p);
  return execute(alg, ar, "copair");
}
