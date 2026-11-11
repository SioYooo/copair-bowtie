#pragma once
#include <malloc.h>
#include <sys/resource.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../include/copair_core.hpp"

using namespace bt;
using Clock = std::chrono::steady_clock;

struct Op { char t; int rel; i32 u, v; };
struct Args {
  std::string algo, variant, mode = "check", trace, out, repeatTag;
  bool noPerOp = false, havePsi = false, haveTheta = false, haveMu = false, haveOpts = false, haveTau = false;
  double psi = 0, theta = 0, mu = 0, eps = 0, tau = 0;
  u64 hashSeed = 1; unsigned opts = 0xF;
  long audit_every = 1;
};

static const char* COMMON_USAGE =
    "                 [--mode check|perf] [--trace FILE | stdin] [--out FILE] [--no-per-op] [--repeat-tag STR]\n"
    "                 [--audit-every K] [--hash-seed S] [--help]\n"
    "  --no-per-op: no clock read per update (one per run of consecutive updates); JSON has update_total_ns and per_op=false.\n"
    "  --hash-seed S: master seed of the multiply-shift multiplier stream (default 1; logged in the perf record / on stderr).\n"
    "  trace lines: 'L <rel> <u> <v>' initial tuple (all before other ops); '+ <rel> <u> <v>'; '- <rel> <u> <v>';\n"
    "  'E' full enumeration; '#' comment. rel is R1..R6 or 1..6; tuples in own column order\n"
    "  R1(a,b) R2(b,x) R3(x,a) R4(x,d) R5(d,e) R6(e,x).\n"
    "  check mode: audit every K updates (default 1); per E prints {\"op_index\",\"count\",\"sum\",\"xor\"}\n"
    "  (op_index = 0-based index among non-L non-comment lines). perf mode prints one JSON object.\n";

static int parseArgs(int argc, char** argv, Args& ar, const char* usage) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto val = [&]() -> std::string { if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n%s%s", a.c_str(), usage, COMMON_USAGE); exit(2); } return argv[++i]; };
    if (a == "--help" || a == "-h") { fputs(usage, stdout); fputs(COMMON_USAGE, stdout); return 0; }
    else if (a == "--algo") ar.algo = val();
    else if (a == "--variant") ar.variant = val();
    else if (a == "--mode") ar.mode = val();
    else if (a == "--trace") ar.trace = val();
    else if (a == "--out") ar.out = val();
    else if (a == "--audit-every") ar.audit_every = std::max(1L, atol(val().c_str()));
    else if (a == "--psi") { ar.psi = atof(val().c_str()); ar.havePsi = true; }
    else if (a == "--theta") { ar.theta = atof(val().c_str()); ar.haveTheta = true; }
    else if (a == "--mu") { ar.mu = atof(val().c_str()); ar.haveMu = true; }
    else if (a == "--tau") { ar.tau = atof(val().c_str()); ar.haveTau = true; }
    else if (a == "--eps") ar.eps = atof(val().c_str());
    else if (a == "--hash-seed") ar.hashSeed = strtoull(val().c_str(), nullptr, 0);
    else if (a == "--opts") { ar.opts = (unsigned)strtoul(val().c_str(), nullptr, 0); ar.haveOpts = true; }
    else if (a == "--no-per-op") ar.noPerOp = true;
    else if (a == "--repeat-tag") ar.repeatTag = val();
    else { fprintf(stderr, "unknown argument %s\n%s%s", a.c_str(), usage, COMMON_USAGE); return 2; }
  }
  return -1;
}

template <class P> static auto setOpts(P& q, unsigned e, int) -> decltype(q.opts, void()) { q.opts = e; }
template <class P> static void setOpts(P&, unsigned, long) {}

template <class P> static void fillParams(P& q, const Args& ar) {
  q.psiExp = Params().psiExp;
  if (ar.havePsi) { q.psiFixed = true; q.psi = ar.psi; }
  if (ar.haveTheta) { q.thetaFixed = true; q.theta = ar.theta; }
  if (ar.haveMu) { q.muFixed = true; q.mu = ar.mu; }
  setOpts(q, ar.opts, 0);
}

static std::vector<Op> readTrace(const std::string& path) {
  std::istream* in = &std::cin; std::ifstream f;
  if (!path.empty()) { f.open(path); if (!f) fail("cannot open trace " + path); in = &f; }
  std::vector<Op> ops; std::string line; size_t ln = 0;
  while (std::getline(*in, line)) {
    ln++;
    size_t h = line.find('#'); if (h != std::string::npos) line.resize(h);
    std::istringstream ss(line); std::string t;
    if (!(ss >> t)) continue;
    Op o{t[0], 0, 0, 0};
    if (t == "E") { ops.push_back(o); continue; }
    if (t != "L" && t != "+" && t != "-") fail("trace line " + std::to_string(ln) + ": bad op");
    std::string r; long long u, v;
    if (!(ss >> r >> u >> v)) fail("trace line " + std::to_string(ln) + ": bad fields");
    if (r[0] == 'R' || r[0] == 'r') r = r.substr(1);
    o.rel = std::atoi(r.c_str());
    if (o.rel < 1 || o.rel > 6) fail("trace line " + std::to_string(ln) + ": bad relation");
    o.u = (i32)u; o.v = (i32)v; ops.push_back(o);
  }
  return ops;
}

static u64 ansHash(const i32* a) {
  u64 h = mix((u64)(u32)a[0]);
  for (int i = 1; i < 5; i++) h = mix(h ^ (u64)(u32)a[i]);
  return h;
}

template <class A> static int runCheck(A& alg, const std::vector<Op>& ops, const Args& ar, FILE* out) {
  std::vector<Tup> init; size_t i = 0;
  for (; i < ops.size() && ops[i].t == 'L'; i++) init.push_back({ops[i].rel, ops[i].u, ops[i].v});
  alg.load(init); alg.audit();
  long upd = 0, opidx = 0;
  for (; i < ops.size(); i++, opidx++) {
    const Op& o = ops[i];
    if (o.t == 'L') fail("L line after non-L op");
    if (o.t == '+' || o.t == '-') {
      alg.apply(o.t == '+' ? 1 : -1, o.rel, o.u, o.v);
      if (++upd % ar.audit_every == 0) {
        try { alg.audit(); } catch (std::exception& e) { fail(std::string(e.what()) + " (after op_index " + std::to_string(opidx) + ")"); }
      }
    } else {
      typename A::Enum en(alg);
      std::vector<std::array<i32, 5>> res; std::array<i32, 5> t{};
      u64 sum = 0, x = 0, ord = 0;
      for (bool ok = en.start(t.data()); ok; ok = en.next(t.data())) { res.push_back(t); u64 h = ansHash(t.data()); sum += h; x ^= h; ord = mix(ord ^ h); }
      std::sort(res.begin(), res.end());
      for (size_t k = 1; k < res.size(); k++) if (res[k] == res[k - 1]) fail("duplicate answer at op_index " + std::to_string(opidx));
      fprintf(out, "{\"op_index\": %ld, \"count\": %zu, \"sum\": %llu, \"xor\": %llu, \"ord\": %llu}\n", opidx, res.size(),
              (unsigned long long)sum, (unsigned long long)x, (unsigned long long)ord);
    }
  }
  alg.audit();
  return 0;
}

static std::string jcensus(const Census& c) {
  std::string s = "{"; bool f = true;
  for (auto& kv : c) { s += (f ? "" : ", ") + std::string("\"") + kv.first + "\": " + std::to_string(kv.second); f = false; }
  return s + "}";
}
static std::string jnum(double v) { char b[64]; snprintf(b, sizeof b, "%.10g", v); return b; }
static std::string jstr(const std::string& s) {
  std::string r = "\"";
  for (char c : s) { if (c == '"' || c == '\\') r += '\\'; r += c; }
  return r + "\"";
}
static u64 pct(const std::vector<u64>& v, double p) {
  size_t n = v.size(); if (!n) return 0;
  size_t k = (size_t)std::ceil(p * (double)n); if (k < 1) k = 1;
  return v[k - 1];
}
static long curRssKiB() {
  FILE* f = fopen("/proc/self/statm", "r"); long a = 0, b = 0; if (f) { if (fscanf(f, "%ld %ld", &a, &b) != 2) b = 0; fclose(f); }
  return b * (sysconf(_SC_PAGESIZE) / 1024);
}
static u64 nowNs() { return (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }

template <class A> static int runPerf(A& alg, const std::vector<Op>& ops, const Args& ar, FILE* out, const std::string& buildRec, const char* driver) {
  std::vector<Tup> init; size_t i = 0;
  for (; i < ops.size() && ops[i].t == 'L'; i++) init.push_back({ops[i].rel, ops[i].u, ops[i].v});
  size_t nupd = 0; for (size_t k = i; k < ops.size(); k++) nupd += ops[k].t != 'E';
  std::vector<u64> ns(nupd); std::vector<unsigned char> rs(nupd), noop(nupd);
  const bool perOp = !ar.noPerOp; u64 segTotal = 0, seg0 = 0; bool inSeg = false;
  struct EnumRec { u64 first, total, count; };
  std::vector<EnumRec> enums;
  u64 t0 = nowNs(); alg.load(init); u64 pre = nowNs() - t0;
  i64 N0 = alg.N();
  size_t u = 0;
  for (; i < ops.size(); i++) {
    const Op& o = ops[i];
    if (o.t == 'E') {
      if (inSeg) { segTotal += nowNs() - seg0; inSeg = false; }
      typename A::Enum en(alg); i32 t[5]; EnumRec r{0, 0, 0};
      u64 s0 = nowNs(); bool ok = en.start(t); u64 s1 = nowNs(); r.first = s1 - s0;
      while (ok) { r.count++; ok = en.next(t); }
      r.total = nowNs() - s0; enums.push_back(r);
      continue;
    }
    int idx = (o.rel - 1) * 2 + (o.t == '+' ? 0 : 1);
    bool np;
    if (perOp) {
      u64 a0 = nowNs();
      np = alg.apply(o.t == '+' ? 1 : -1, o.rel, o.u, o.v);
      u64 a1 = nowNs();
      ns[u] = a1 - a0;
    } else {
      if (!inSeg) { seg0 = nowNs(); inSeg = true; }
      np = alg.apply(o.t == '+' ? 1 : -1, o.rel, o.u, o.v);
      ns[u] = 0;
    }
    rs[u] = (unsigned char)idx; noop[u] = np; u++;
  }
  if (inSeg) { segTotal += nowNs() - seg0; inSeg = false; }
  std::string r = "{\n";
  char exe[4096]; ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1); if (el < 0) el = 0; exe[el] = 0;
  r += "  \"algo\": " + jstr(ar.algo) + ",\n  \"params\": {";
  { bool f = true; for (auto& kv : alg.params()) { r += (f ? "" : ", ") + jstr(kv.first) + ": " + jnum(kv.second); f = false; } }
  r += "},\n  \"build_record\": " + jstr(buildRec) + ",\n  \"binary\": " + jstr(exe) + ",\n";
  r += "  \"driver\": \"" + std::string(driver) + "\", \"per_op\": " + std::string(perOp ? "true" : "false") + ", \"repeat_tag\": " + jstr(ar.repeatTag) + ", \"opts\": " + std::to_string(ar.opts) + ",\n";
  r += "  \"hash_seed\": " + std::to_string(ar.hashSeed) + ",\n";
  r += "  \"N0\": " + std::to_string(N0) + ", \"N_final\": " + std::to_string(alg.N()) + ",\n";
  r += "  \"preprocessing_ns\": " + std::to_string(pre) + ",\n  \"updates\": {\n";
  u64 tot = 0, nops = 0, nreal = 0; bool first = true;
  for (int idx = 0; idx < 12; idx++) {
    std::vector<u64> v; u64 sum = 0, nn = 0, cnt = 0;
    for (size_t k = 0; k < nupd; k++) if (rs[k] == idx) { if (noop[k]) nn++; else { cnt++; if (perOp) { v.push_back(ns[k]); sum += ns[k]; } } }
    nops += nn; tot += sum; nreal += cnt;
    if (!cnt && !nn) continue;
    if (!perOp) {
      r += std::string(first ? "" : ",\n") + "    \"R" + std::to_string(idx / 2 + 1) + (idx % 2 ? "-" : "+") + "\": {\"count\": " + std::to_string(cnt) + ", \"noop\": " + std::to_string(nn) + "}";
      first = false; continue;
    }
    std::sort(v.begin(), v.end());
    r += std::string(first ? "" : ",\n") + "    \"R" + std::to_string(idx / 2 + 1) + (idx % 2 ? "-" : "+") + "\": {\"count\": " + std::to_string(v.size()) +
         ", \"noop\": " + std::to_string(nn) + ", \"mean_ns\": " + jnum(v.empty() ? 0.0 : (double)sum / (double)v.size()) +
         ", \"p50_ns\": " + std::to_string(pct(v, 0.5)) + ", \"p95_ns\": " + std::to_string(pct(v, 0.95)) +
         ", \"p99_ns\": " + std::to_string(pct(v, 0.99)) + ", \"max_ns\": " + std::to_string(v.empty() ? 0 : v.back()) +
         ", \"total_ns\": " + std::to_string(sum) + "}";
    first = false;
  }
  r += "\n  },\n";
  if (!perOp) {
    tot = segTotal;
    r += "  \"overall\": {\"count\": " + std::to_string(nreal) + ", \"mean_ns\": " + jnum(nreal ? (double)tot / (double)nreal : 0.0) + "},\n";
  } else {
    std::vector<u64> all; all.reserve(nupd);
    for (size_t k = 0; k < nupd; k++) if (!noop[k]) all.push_back(ns[k]);
    std::sort(all.begin(), all.end());
    r += "  \"overall\": {\"count\": " + std::to_string(all.size()) + ", \"mean_ns\": " + jnum(all.empty() ? 0.0 : (double)tot / (double)all.size()) +
         ", \"p50_ns\": " + std::to_string(pct(all, 0.5)) + ", \"p95_ns\": " + std::to_string(pct(all, 0.95)) + ", \"p99_ns\": " + std::to_string(pct(all, 0.99)) +
         ", \"p999_ns\": " + std::to_string(pct(all, 0.999)) + ", \"max_ns\": " + std::to_string(all.empty() ? 0 : all.back()) + "},\n";
  }
  r += "  \"noop_count\": " + std::to_string(nops) + ", \"update_total_ns\": " + std::to_string(tot) + ",\n";
  r += "  \"enumerations\": [";
  for (size_t k = 0; k < enums.size(); k++)
    r += std::string(k ? ", " : "") + "{\"start_to_first_ns\": " + std::to_string(enums[k].first) + ", \"total_ns\": " + std::to_string(enums[k].total) +
         ", \"count\": " + std::to_string(enums[k].count) + "}";
  r += "],\n  \"capacity\": " + jcensus(alg.capacity()) + ",\n  \"counters\": " + jcensus(alg.counters()) + ",\n";
  struct rusage ru; getrusage(RUSAGE_SELF, &ru);
  r += "  \"peak_rss_kib\": " + std::to_string(ru.ru_maxrss) + ", \"current_rss_kib\": " + std::to_string(curRssKiB());
  r += "\n}\n";
  fputs(r.c_str(), out);
  return 0;
}

template <class A> static int execute(A& alg, const Args& ar, const char* driver) {
  FILE* out = stdout;
  if (!ar.out.empty()) { out = fopen(ar.out.c_str(), "w"); if (!out) { perror("open --out"); return 2; } }
  try {
    std::vector<Op> ops = readTrace(ar.trace);
    char exe[4096]; ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1); if (el < 0) el = 0; exe[el] = 0;
    std::string rec = std::string(exe) + ".build.json";
    int rc = ar.mode == "perf" ? runPerf(alg, ops, ar, out, rec, driver) : runCheck(alg, ops, ar, out);
    if (out != stdout) fclose(out);
    return rc;
  } catch (std::exception& e) {
    fflush(out);
    fprintf(stderr, "FAIL: %s\n", e.what());
    return 3;
  }
}
