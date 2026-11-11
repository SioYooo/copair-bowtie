#include "driver.hpp"
#include "../include/perx_tuned.hpp"
#include "../include/perx_tuned_index.hpp"
#include "../include/perx_layout.hpp"

static const char* USAGE =
    "usage: perx-<mode> --algo copair-base|perx [--variant base|tuned|tuned-index|layout] [--psi <v>] [--theta <v>] [--mu <v>] [--opts MASK]\n"
    "  --opts MASK: option bitmask; the perx variants fix their own mask (tuned 15, tuned-index 15, layout 7).\n"
    "  copair-base: psi=N0^(1/5), theta=N0^(1/2), mu=sqrt(m0); --psi/--theta/--mu fix the value (any subset; ignored by perx).\n";

int main(int argc, char** argv) {
  Args ar;
  ar.algo = "copair-base";
  int rc = parseArgs(argc, argv, ar, USAGE);
  if (rc >= 0) return rc;
  std::string name = ar.algo;
  if (name == "perx") {
    if (ar.variant.empty()) { fprintf(stderr, "--algo perx needs --variant\n%s", USAGE); return 2; }
    name = "perx-" + ar.variant;
  }
  ar.algo = name;
  static const struct { const char* n; unsigned mask; } FIXED[] = {{"perx-tuned", 15}, {"perx-tuned-index", 15}, {"perx-layout", 7}};
  for (auto& f : FIXED) if (name == f.n) { if (ar.haveOpts) { fprintf(stderr, "--opts is fixed for %s\n", name.c_str()); return 2; } ar.opts = f.mask; }
  if (name != "copair-base" && name != "perx-base" && name != "perx-tuned" && name != "perx-tuned-index" && name != "perx-layout") {
    fprintf(stderr, "unknown algo %s\n%s", ar.algo.c_str(), USAGE); return 2;
  }
  Params p; fillParams(p, ar);
  tuned::Params tunedParams; fillParams(tunedParams, ar);
  layout::Params layoutParams; fillParams(layoutParams, ar);
  hash_seed(ar.hashSeed); tuned::hash_seed(ar.hashSeed); layout::hash_seed(ar.hashSeed);
  if (ar.mode != "perf") fprintf(stderr, "#hash_seed %llu\n", (unsigned long long)ar.hashSeed);
  if (name == "perx-layout") { layout::PerxLayout alg(layoutParams); return execute(alg, ar, "perx"); }
  if (name == "perx-tuned") { tuned::PerxTuned alg(tunedParams); return execute(alg, ar, "perx"); }
  if (name == "perx-tuned-index") { tuned::PerxTunedIndex alg(tunedParams); return execute(alg, ar, "perx"); }
  if (name == "perx-base") { PerxBase alg(p); return execute(alg, ar, "perx"); }
  CopairBase alg(p);
  return execute(alg, ar, "perx");
}
