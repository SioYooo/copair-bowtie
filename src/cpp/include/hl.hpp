#pragma once
#include <map>

#include "copair_core.hpp"

namespace bt {
struct Frag {
  FMap set;
  MapOfSets<ISet> byU, byV;
  bool has(i32 u, i32 v) const { return set.get(pk(u, v)) != NONE; }
  void add(i32 u, i32 v) { set.set(pk(u, v), 0); byU.ins(k32(u), v); byV.ins(k32(v), u); }
  void del(i32 u, i32 v) { set.erase(pk(u, v)); byU.rem(k32(u), v); byV.rem(k32(v), u); }
};

#ifdef BT_STATS
#define HL_WK(r, i, n) do { (r)->wk[i] += (n); (r)->wtr[(r)->ctx] += (n); } while (0)
#else
#define HL_WK(r, i, n) ((void)0)
#endif

struct Roots {
  struct XRec { i32 x = 0; unsigned char m[2] = {0, 0}; u32 sid[2][8] = {}; };
  std::vector<XRec> X; std::vector<u32> xfree; FMap xmap; ISet XS;
  u64 wk[4] = {}, wtr[NCTX] = {}; int ctx = 0;
  void setMask(int s, int cfg, i32 x, bool on, u32 sid) {
    u32 xi = xmap.get(k32(x));
    if (xi == NONE) {
      if (!on) return;
      if (!xfree.empty()) { xi = xfree.back(); xfree.pop_back(); relax(xfree); } else { xi = (u32)X.size(); X.emplace_back(); }
      X[xi].x = x; X[xi].m[0] = X[xi].m[1] = 0; xmap.set(k32(x), xi);
    }
    XRec& r = X[xi]; unsigned char bit = (unsigned char)(1u << cfg);
    if (on) { r.m[s] |= bit; r.sid[s][cfg] = sid; } else r.m[s] &= (unsigned char)~bit;
    if (r.m[0] && r.m[1]) XS.insert((i32)xi); else XS.erase((i32)xi);
    if (!r.m[0] && !r.m[1]) { xmap.erase(k32(x)); xfree.push_back(xi); }
  }
  void clearRoots() { release(X); release(xfree); xmap.clear(); XS.clear(); }
};

struct Tri {
  Roots* o = nullptr; int side = 0, cfg = 0, S = 1, tree = 1;
  Frag *A = nullptr, *B = nullptr, *C = nullptr;
  MapOfSets<ISet> v1;

  MapOfSets<PSet> v3p;
  static u64 ent(i32 kept, u32 id) { return ((u64)(u32)kept << 32) | id; }
  FMap c4;
  void clear() { v1 = MapOfSets<ISet>(); v3p = MapOfSets<PSet>(); c4 = FMap(); }
  void adj4(i32 x, i64 d) {
    u32 c = c4.get(k32(x)); i64 n = (c == NONE ? 0 : (i64)c) + d;
    if (n < 0) fail("HeavyLight: negative V4 count");
    if (n == 0) c4.erase(k32(x)); else c4.set(k32(x), (u32)n);
  }
  void top(i32 x) { u32 id = v3p.idx.get(k32(x)); o->setMask(side, cfg, x, id != NONE, id); }
  bool thirdHas(i32 kept, i32 x) const { return S == 1 ? C->has(x, kept) : B->has(kept, x); }
  void chain(i32 kept, i32 x, i32 mid, int sg) {
    u64 k = pk(kept, x);
    const ISet* s = v1.get(k); size_t old = s ? s->size() : 0;
    bool th = thirdHas(kept, x);
    if (sg < 0 && th && old == 1) v3p.rem(k32(x), ent(kept, v1.idx.get(k)));
    if (sg > 0) v1.ins(k, mid); else v1.rem(k, mid);
    HL_WK(o, 1, 1);
    if (!th) return;
    adj4(x, sg);
    if (sg > 0 && old == 0) { v3p.ins(k32(x), ent(kept, v1.idx.get(k))); top(x); }
    else if (sg < 0 && old == 1) top(x);
  }
  void third(i32 kept, i32 x, int sg) {
    u32 id = v1.idx.get(pk(kept, x));
    HL_WK(o, 1, 1);
    if (id == NONE) return;
    adj4(x, sg * (i64)v1.pool[id].size());
    if (sg > 0) v3p.ins(k32(x), ent(kept, id)); else v3p.rem(k32(x), ent(kept, id));
    top(x);
  }
  void s3(i32 p, i32 q, i32 x, int sg) {
    u64 k = pk(p, q);
    if (sg > 0) v1.ins(k, x); else v1.rem(k, x);
    HL_WK(o, 1, 1);
    if (!A->has(p, q)) return;
    adj4(x, sg);
    if (sg > 0) v3p.ins(k32(x), k); else v3p.rem(k32(x), k);
    top(x);
  }
  void onA(i32 p, i32 q, int sg) {
    if (S == 1) { if (const ISet* s = B->byU.get(k32(q))) for (i32 x : s->el) { HL_WK(o, 2, 1); chain(p, x, q, sg); } }
    else if (S == 2) { if (const ISet* s = C->byV.get(k32(p))) for (i32 x : s->el) { HL_WK(o, 2, 1); chain(q, x, p, sg); } }
    else if (const ISet* s = v1.get(pk(p, q))) for (i32 x : s->el) {
      HL_WK(o, 2, 1); adj4(x, sg);
      if (sg > 0) v3p.ins(k32(x), pk(p, q)); else v3p.rem(k32(x), pk(p, q));
      top(x);
    }
  }
  void onB(i32 q, i32 x, int sg) {
    if (S == 1) { if (const ISet* s = A->byV.get(k32(q))) for (i32 p : s->el) { HL_WK(o, 2, 1); chain(p, x, q, sg); } }
    else if (S == 2) third(q, x, sg);
    else if (const ISet* s = C->byU.get(k32(x))) for (i32 p : s->el) { HL_WK(o, 2, 1); s3(p, q, x, sg); }
  }
  void onC(i32 x, i32 p, int sg) {
    if (S == 1) third(p, x, sg);
    else if (S == 2) { if (const ISet* s = A->byU.get(k32(p))) for (i32 q : s->el) { HL_WK(o, 2, 1); chain(q, x, p, sg); } }
    else if (const ISet* s = B->byV.get(k32(x))) for (i32 q : s->el) { HL_WK(o, 2, 1); s3(p, q, x, sg); }
  }
};

struct PIt {
  const Tri* t = nullptr; i32 x = 0; const PSet* ps = nullptr; const ISet* s2 = nullptr;
  size_t i = 0, j = 0;
  void begin(const Tri* tr, i32 xx, u32 id) { t = tr; x = xx; i = j = 0; s2 = nullptr; ps = &tr->v3p.pool[id]; }
  bool adv(i32& p, i32& q) {
    if (t->S == 3) {
      if (i >= ps->size()) return false;
      u64 k = ps->el[i++]; p = (i32)(k >> 32); q = (i32)(u32)k; return true;
    }
    for (;;) {
      if (i >= ps->size()) return false;
      if (!s2) { s2 = &t->v1.pool[(u32)ps->el[i]]; j = 0; }
      if (j >= s2->size()) { i++; s2 = nullptr; continue; }
      i32 kept = (i32)(ps->el[i] >> 32), mid = s2->el[j++];
      if (t->S == 1) { p = kept; q = mid; } else { q = kept; p = mid; }
      return true;
    }
  }
};

struct HeavyLight : Roots {
  static constexpr int NWK = 4;
  static const char* wkname(int i) {
    static const char* const n[4] = {"base", "view_updates", "join_iters", "tuples_moved"};
    return n[i];
  }

  static constexpr int VAR[7][2] = {{0, 0}, {0, 1}, {1, 2}, {2, 0}, {2, 3}, {3, 4}, {4, 2}};
  static constexpr int SIDE[7] = {0, 0, 0, 0, 1, 1, 1};
  static constexpr int ROLE[7] = {0, 0, 1, 2, 2, 0, 1};
  double eps;
  Frag fr[7][2][2];
  FMap deg[5], hv[5];
  i64 Nraw = 0, M = 1;
  std::array<std::array<Tri, 8>, 2> tri;
  u64 minor = 0, major = 0, moved = 0;

  explicit HeavyLight(double e = 0) : eps(e) {
    static const int lt[8] = {1, 1, 2, 2, 1, 1, 3, 1};
    static const int rt[8] = {4, 4, 5, 6, 4, 4, 5, 4};
    static const int shape[7] = {0, 1, 2, 3, 2, 1, 3};
    for (int s = 0; s < 2; s++)
      for (int c = 0; c < 8; c++) {
        int dp = c >> 2, dq = (c >> 1) & 1, dx = c & 1;
        Tri& t = tri[s][c];
        t.o = this; t.side = s; t.cfg = c;
        t.tree = s == 0 ? lt[(dp << 2) | (dq << 1) | dx] : rt[(dx << 2) | (dp << 1) | dq];
        t.S = shape[t.tree];
        int ra = s ? 5 : 1, rb = s ? 6 : 2, rc = s ? 4 : 3;
        t.A = &fr[ra][dp][dq]; t.B = &fr[rb][dq][dx]; t.C = &fr[rc][dx][dp];
      }
  }
  i64 N() const { return Nraw; }

  i64 lmax() const { return (i64)std::floor(1.5 * std::pow((double)M, eps) + 1e-9); }
  i64 hmin() const { return (i64)std::floor(0.5 * std::pow((double)M, eps) + 1e-9); }
  i64 smax() const { return (i64)std::floor(std::pow((double)M, eps) + 1e-9); }
  int cls(int Y, i32 v) const { return hv[Y].get(k32(v)) != NONE; }

  void fragOp(int rel, int cu, int cv, i32 u, i32 v, int sg) {
    int s = SIDE[rel], role = ROLE[rel];
    for (int z = 0; z < 2; z++) {
      if (role == 0) tri[s][(cu << 2) | (cv << 1) | z].onA(u, v, sg);
      else if (role == 1) tri[s][(z << 2) | (cu << 1) | cv].onB(u, v, sg);
      else tri[s][(cv << 2) | (z << 1) | cu].onC(u, v, sg);
    }
    HL_WK(this, 0, 1);
    if (sg > 0) fr[rel][cu][cv].add(u, v); else fr[rel][cu][cv].del(u, v);
  }

  void buildFrom(const std::vector<Tup>& ts) {
    Nraw = (i64)ts.size();
    for (auto& t : ts) for (int c = 0; c < 2; c++) {
      i32 val = c ? t[2] : t[1]; int Y = VAR[t[0]][c]; u32 d = deg[Y].get(k32(val));
      deg[Y].set(k32(val), d == NONE ? 1 : d + 1);
    }
    i64 sm = smax();
    for (int Y = 0; Y < 5; Y++)
      for (auto& sl : deg[Y].ent) if (sl.val != NONE && (i64)sl.val > sm) hv[Y].set(sl.key, 0);
    for (auto& t : ts) fragOp(t[0], cls(VAR[t[0]][0], t[1]), cls(VAR[t[0]][1], t[2]), t[1], t[2], +1);
  }
  void load(const std::vector<Tup>& init) {
    std::vector<Tup> ts; FMap seen[7];
    for (auto& t : init) if (seen[t[0]].set(pk(t[1], t[2]), 0)) ts.push_back(t);
    M = 2 * (i64)ts.size() + 1;
    buildFrom(ts);
  }
  void rebuild() {
    Ctx g(ctx, CTX_GLB);
    std::vector<Tup> ts;
    for (int r = 1; r <= 6; r++) for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++)
      for (auto& sl : fr[r][a][b].set.ent) if (sl.val != NONE) ts.push_back({r, (i32)(sl.key >> 32), (i32)(u32)sl.key});
    for (int r = 1; r <= 6; r++) for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) fr[r][a][b] = Frag();
    for (int Y = 0; Y < 5; Y++) { deg[Y] = FMap(); hv[Y] = FMap(); }
    for (int s = 0; s < 2; s++) for (int c = 0; c < 8; c++) tri[s][c].clear();
    clearRoots();
    HL_WK(this, 3, ts.size());
    buildFrom(ts);
  }
  struct Ctx { int& r; int old; Ctx(int& r_, int n) : r(r_), old(r_) { r = n; } ~Ctx() { r = old; } };

  void move(int Y, i32 val, bool toHeavy) {
    Ctx g(ctx, CTX_VF);
    minor++;
    int old = toHeavy ? 0 : 1;
    std::vector<Tup> ts;
    for (int r = 1; r <= 6; r++) for (int c = 0; c < 2; c++) {
      if (VAR[r][c] != Y) continue;
      for (int oc = 0; oc < 2; oc++) {
        Frag& F = c == 0 ? fr[r][old][oc] : fr[r][oc][old];
        const ISet* s = (c == 0 ? F.byU : F.byV).get(k32(val));
        if (s) for (i32 w : s->el) ts.push_back(c == 0 ? Tup{r, val, w} : Tup{r, w, val});
      }
    }
    for (auto& t : ts) fragOp(t[0], cls(VAR[t[0]][0], t[1]), cls(VAR[t[0]][1], t[2]), t[1], t[2], -1);
    if (toHeavy) hv[Y].set(k32(val), 0); else hv[Y].erase(k32(val));
    for (auto& t : ts) fragOp(t[0], cls(VAR[t[0]][0], t[1]), cls(VAR[t[0]][1], t[2]), t[1], t[2], +1);
    moved += ts.size(); HL_WK(this, 3, ts.size());
  }
  void bump(int Y, i32 val, int sg) {
    u32 d = deg[Y].get(k32(val)); i64 n = (d == NONE ? 0 : (i64)d) + sg;
    if (n < 0) fail("HeavyLight: negative degree");
    if (n == 0) { deg[Y].erase(k32(val)); hv[Y].erase(k32(val)); } else deg[Y].set(k32(val), (u32)n);
  }
  void minorCheck(int Y, i32 val) {
    u32 d = deg[Y].get(k32(val)); i64 D = d == NONE ? 0 : d;
    bool h = cls(Y, val);
    if (!h && D > lmax()) move(Y, val, true);
    else if (h && D <= hmin()) move(Y, val, false);
  }
  bool apply(int sign, int rel, i32 u, i32 v) {
    int cu = cls(VAR[rel][0], u), cv = cls(VAR[rel][1], v);
    if (fr[rel][cu][cv].has(u, v) == (sign > 0)) return true;
    Nraw += sign;
    fragOp(rel, cu, cv, u, v, sign);
    bump(VAR[rel][0], u, sign); bump(VAR[rel][1], v, sign);
    minorCheck(VAR[rel][0], u); minorCheck(VAR[rel][1], v);
    if (Nraw < M / 4) M = std::max<i64>(1, M / 2 - 1);
    else if (Nraw >= M) M = 2 * M;
    else return false;
    major++; rebuild();
    return false;
  }

  struct Enum {
    HeavyLight* g; size_t xi = 0; int state = 0, lc = 0, rc = 0; PIt L, R; i32 x = 0, a = 0, b = 0; unsigned char mA = 0, mB = 0; const XRec* rec = nullptr;
    explicit Enum(HeavyLight& p) : g(&p) {}
    static int nextCfg(unsigned char m, int from) { for (int c = from; c < 8; c++) if (m >> c & 1) return c; return 8; }
    bool start(i32* o) { xi = 0; state = 0; return next(o); }
    bool next(i32* o) {
      for (;;) {
        if (state == 0) {
          if (xi >= g->XS.size()) return false;
          const XRec& r = g->X[(u32)g->XS.el[xi]];
          rec = &r; x = r.x; mA = r.m[0]; mB = r.m[1];
          lc = nextCfg(mA, 0); L.begin(&g->tri[0][lc], x, rec->sid[0][lc]); state = 1;
        }
        if (state == 1) {
          if (L.adv(a, b)) { rc = nextCfg(mB, 0); R.begin(&g->tri[1][rc], x, rec->sid[1][rc]); state = 2; }
          else {
            lc = nextCfg(mA, lc + 1);
            if (lc < 8) L.begin(&g->tri[0][lc], x, rec->sid[0][lc]); else { xi++; state = 0; }
            continue;
          }
        }
        i32 d, e;
        if (R.adv(d, e)) { o[0] = a; o[1] = b; o[2] = x; o[3] = d; o[4] = e; return true; }
        rc = nextCfg(mB, rc + 1);
        if (rc < 8) R.begin(&g->tri[1][rc], x, rec->sid[1][rc]); else state = 1;
      }
    }
  };

  Census capacity() const {
    Cap c;
    auto addm = [&](const FMap& m) { c += m.cap(); };
    auto addmos = [&](const auto& m) { c += m.cap(); };
    for (auto& a : fr) for (auto& b : a) for (auto& f : b) { addm(f.set); addmos(f.byU); addmos(f.byV); }
    for (int Y = 0; Y < 5; Y++) { addm(deg[Y]); addm(hv[Y]); }
    for (auto& s : tri) for (auto& t : s) { addmos(t.v1); addmos(t.v3p); addm(t.c4); }
    c += vcap(X); c += vcap(xfree); addm(xmap); c += XS.cap();
    return {{"total_bytes", c.bytes}, {"rehash_up", g_rehash_up}, {"rehash_down", g_rehash_down}, {"hash_draws", g_hash_draws}};
  }
  Pairs params() const { return {{"eps", eps}}; }
  Census counters() const { return {{"minor_rebalances", minor}, {"major_rebalances", major}, {"tuples_moved", moved}}; }

  template <class S>
  static void cmp(const MapOfSets<S>& m, const std::map<u64, std::set<u64>>& e, const char* what) {
    BT_CHECK(m.idx.size() == e.size(), std::string(what) + " key count");
    for (auto& kv : e) {
      const S* s = m.get(kv.first);
      BT_CHECK(s && s->size() == kv.second.size(), std::string(what) + " set size");
      for (auto& el : s->el) BT_CHECK(kv.second.count(S::K(el)), std::string(what) + " element");
    }
    m.each([&](u64, const S& s) { s.check(); });
  }
  void audit() {
    std::set<std::pair<i32, i32>> base[7]; i64 tot = 0;
    for (int r = 1; r <= 6; r++) for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
      Frag& F = fr[r][a][b];
      BT_CHECK(F.byU.total() == F.set.size() && F.byV.total() == F.set.size(), "frag index size");
      for (auto& sl : F.set.ent) if (sl.val != NONE) {
        i32 u = (i32)(sl.key >> 32), v = (i32)(u32)sl.key;
        BT_CHECK(cls(VAR[r][0], u) == a && cls(VAR[r][1], v) == b, "tuple in wrong fragment");
        BT_CHECK(F.byU.get(k32(u)) && F.byU.get(k32(u))->contains(v) && F.byV.get(k32(v)) && F.byV.get(k32(v))->contains(u), "frag index entry");
        base[r].insert({u, v}); tot++;
      }
      F.byU.each([](u64, const ISet& s) { s.check(); }); F.byV.each([](u64, const ISet& s) { s.check(); });
    }
    BT_CHECK(tot == Nraw, "N");
    BT_CHECK(M >= 1 && Nraw < M && Nraw >= M / 4, "M range");
    std::map<i32, i64> dg[5];
    for (int r = 1; r <= 6; r++) for (auto& t : base[r]) { dg[VAR[r][0]][t.first]++; dg[VAR[r][1]][t.second]++; }
    for (int Y = 0; Y < 5; Y++) {
      BT_CHECK(deg[Y].size() == dg[Y].size(), "degree map size");
      size_t nh = 0;
      for (auto& kv : dg[Y]) {
        BT_CHECK(deg[Y].get(k32(kv.first)) == (u32)kv.second, "degree");
        if (cls(Y, kv.first)) { nh++; BT_CHECK(kv.second > hmin(), "heavy value below 1/2 M^eps"); }
        else BT_CHECK(kv.second <= lmax(), "light value above 3/2 M^eps");
      }
      BT_CHECK(hv[Y].size() == nh, "heavy set has stray values");
    }
    std::map<i32, unsigned char> em[2];
    for (int s = 0; s < 2; s++) for (int c = 0; c < 8; c++) {
      Tri& t = tri[s][c]; int dp = c >> 2, dq = (c >> 1) & 1, dx = c & 1;
      int ra = s ? 5 : 1, rb = s ? 6 : 2, rc = s ? 4 : 3;
      std::set<std::pair<i32, i32>> RA, RB, RC;
      for (auto& q : base[ra]) if (cls(VAR[ra][0], q.first) == dp && cls(VAR[ra][1], q.second) == dq) RA.insert(q);
      for (auto& q : base[rb]) if (cls(VAR[rb][0], q.first) == dq && cls(VAR[rb][1], q.second) == dx) RB.insert(q);
      for (auto& q : base[rc]) if (cls(VAR[rc][0], q.first) == dx && cls(VAR[rc][1], q.second) == dp) RC.insert(q);
      std::map<u64, std::set<u64>> e1, e3; std::map<i32, i64> e4;
      if (t.S == 3) {
        for (auto& b : RB) for (auto& cc : RC) if (b.second == cc.first)
          e1[pk(cc.second, b.first)].insert(k32(b.second));
        for (auto& kv : e1) {
          i32 p = (i32)(kv.first >> 32), q = (i32)(u32)kv.first;
          if (!RA.count({p, q})) continue;
          for (u64 x : kv.second) { e3[x].insert(kv.first); e4[(i32)(u32)x]++; }
        }
        cmp(t.v1, e1, "S3 V1"); cmp(t.v3p, e3, "S3 V3");
      } else {
        const auto& J = t.S == 1 ? RB : RC;
        for (auto& a : RA) for (auto& j : J) {
          if (t.S == 1 && a.second == j.first) e1[pk(a.first, j.second)].insert(k32(a.second));
          if (t.S == 2 && a.first == j.second) e1[pk(a.second, j.first)].insert(k32(a.first));
        }
        for (auto& kv : e1) {
          i32 kept = (i32)(kv.first >> 32), x = (i32)(u32)kv.first;
          bool th = t.S == 1 ? RC.count({x, kept}) > 0 : RB.count({kept, x}) > 0;
          if (!th) continue;
          e3[k32(x)].insert(k32(kept)); e4[x] += (i64)kv.second.size();
        }
        cmp(t.v1, e1, "S1/2 V1"); std::map<u64, std::set<u64>> d3;
        t.v3p.each([&](u64 xk, const PSet& ss) {
          for (u64 el : ss.el) {
            BT_CHECK(t.v1.idx.get(pk((i32)(el >> 32), (i32)xk)) == (u32)el, "V3 entry stale V1 id");
            d3[xk].insert(el >> 32);
          }
        });
        BT_CHECK(d3 == e3, "S1/2 V3"); cmp(t.v3p, [&] { std::map<u64, std::set<u64>> m; t.v3p.each([&](u64 xk, const PSet& ss) { for (u64 el : ss.el) m[xk].insert(el); }); return m; }(), "S1/2 V3 raw");
      }
      BT_CHECK(t.c4.size() == e4.size(), "V4 key count");
      for (auto& kv : e4) BT_CHECK(t.c4.get(k32(kv.first)) == (u32)kv.second, "V4 count");
      for (auto& kv : e3) {
        em[s][(i32)(u32)kv.first] |= (unsigned char)(1u << c);
      }
    }

    size_t live = 0, both = 0;
    for (u32 i = 0; i < X.size(); i++) {
      const XRec& r = X[i];
      if (xmap.get(k32(r.x)) != i) continue;
      live++; BT_CHECK(r.m[0] || r.m[1], "empty x record");
      for (int s = 0; s < 2; s++) { auto it = em[s].find(r.x); BT_CHECK((it == em[s].end() ? 0 : it->second) == r.m[s], "root mask"); }
      for (int s = 0; s < 2; s++) for (int c = 0; c < 8; c++) if (r.m[s] >> c & 1)
        BT_CHECK(r.sid[s][c] == tri[s][c].v3p.idx.get(k32(r.x)), "root V3 set id");
      bool b = r.m[0] && r.m[1]; both += b;
      BT_CHECK(XS.contains((i32)i) == b, "X* membership");
    }
    std::set<i32> xs; for (int s = 0; s < 2; s++) for (auto& kv : em[s]) xs.insert(kv.first);
    BT_CHECK(live == xs.size() && xmap.size() == xs.size(), "x record count");
    BT_CHECK(XS.size() == both, "X* size"); XS.check();
  }
};
constexpr int HeavyLight::VAR[7][2];
constexpr int HeavyLight::SIDE[7];
constexpr int HeavyLight::ROLE[7];
}
