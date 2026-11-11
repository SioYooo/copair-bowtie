#include "driver.hpp"
#include "../include/hl.hpp"

static const char* USAGE = "usage: hl-<mode> --algo hl [--eps <v>]\n";

int main(int argc, char** argv) {
  Args ar;
  ar.algo = "hl";
  int rc = parseArgs(argc, argv, ar, USAGE);
  if (rc >= 0) return rc;
  if (ar.algo != "hl") { fprintf(stderr, "unknown algo %s\n%s", ar.algo.c_str(), USAGE); return 2; }
  hash_seed(ar.hashSeed);
  if (ar.mode != "perf") fprintf(stderr, "#hash_seed %llu\n", (unsigned long long)ar.hashSeed);
  HeavyLight alg(ar.eps);
  return execute(alg, ar, "hl");
}
