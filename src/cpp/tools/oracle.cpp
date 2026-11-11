#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
using namespace std;
typedef pair<int32_t, int32_t> P;
typedef array<int32_t, 5> A5;

static uint64_t sm(uint64_t z) {
  z += 0x9e3779b97f4a7c15ULL;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

static set<P> R[7];

static void enumerate(long op, bool dump) {
  map<int32_t, vector<int32_t>> bs, as, ds, es;
  for (auto& t : R[2]) bs[t.second].push_back(t.first);
  for (auto& t : R[3]) as[t.first].push_back(t.second);
  for (auto& t : R[4]) ds[t.first].push_back(t.second);
  for (auto& t : R[6]) es[t.second].push_back(t.first);
  vector<A5> out;
  for (auto& kv : as) {
    int32_t x = kv.first;
    if (!bs.count(x) || !ds.count(x) || !es.count(x)) continue;
    for (int32_t a : kv.second)
      for (int32_t b : bs[x])
        if (R[1].count(P(a, b)))
          for (int32_t d : ds[x])
            for (int32_t e : es[x])
              if (R[5].count(P(d, e))) out.push_back({a, b, x, d, e});
  }
  sort(out.begin(), out.end());
  if (adjacent_find(out.begin(), out.end()) != out.end()) { fprintf(stderr, "ORACLE DUPLICATE at op %ld\n", op); exit(3); }
  uint64_t S = 0, X = 0;
  for (auto& t : out) {
    uint64_t h = sm((uint32_t)t[0]);
    for (int i = 1; i < 5; i++) h = sm(h ^ (uint64_t)(uint32_t)t[i]);
    S += h; X ^= h;
  }
  printf("{\"op_index\": %ld, \"count\": %zu, \"sum\": %llu, \"xor\": %llu}\n", op, out.size(), (unsigned long long)S, (unsigned long long)X);
  if (dump) {
    printf("BEGIN %ld\n", op);
    for (auto& t : out) printf("%d %d %d %d %d\n", t[0], t[1], t[2], t[3], t[4]);
    printf("END %ld\n", op);
  }
}

int main(int argc, char** argv) {
  bool dump = false; string path;
  for (int i = 1; i < argc; i++) { string a = argv[i]; if (a == "--dump") dump = true; else path = a; }
  ifstream f; if (!path.empty()) { f.open(path); if (!f) { perror("open"); return 2; } }
  istream& in = path.empty() ? cin : f;
  string line; long op = 0;
  while (getline(in, line)) {
    size_t h = line.find('#'); if (h != string::npos) line.resize(h);
    istringstream ss(line); string c; if (!(ss >> c)) continue;
    if (c == "E") { enumerate(op++, dump); continue; }
    string rs; long long u, v;
    if (!(ss >> rs >> u >> v) || (c != "L" && c != "+" && c != "-")) { fprintf(stderr, "bad line: %s\n", line.c_str()); return 2; }
    if (rs[0] == 'R') rs = rs.substr(1);
    int r = atoi(rs.c_str()); if (r < 1 || r > 6) { fprintf(stderr, "bad relation: %s\n", line.c_str()); return 2; }
    P t((int32_t)u, (int32_t)v);
    if (c == "-") R[r].erase(t); else R[r].insert(t);
    if (c != "L") op++;
  }
  return 0;
}
