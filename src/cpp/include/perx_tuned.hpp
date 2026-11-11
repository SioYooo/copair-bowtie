#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace tuned {
using i32 = int32_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i64 = int64_t;
constexpr u32 NONE = 0xFFFFFFFFu;

inline u64 mix(u64 z) {
  z += 0x9e3779b97f4a7c15ULL;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}
inline u64 pk(i32 a, i32 b) { return ((u64)(u32)a << 32) | (u32)b; }
inline u64 k32(i32 v) { return (u64)(u32)v; }
[[noreturn]] inline void fail(const std::string& m) { throw std::runtime_error(m); }
#define TUNED_CHECK(c, msg) do { if (!(c)) ::tuned::fail(std::string("audit: ") + (msg)); } while (0)

inline u64 g_hash_seed = 1, g_hash_state = 1, g_hash_draws = 0;
inline void hash_seed(u64 s) { g_hash_seed = g_hash_state = s; g_hash_draws = 0; }
inline u64 draw_multiplier() {
  u64 z = mix(g_hash_state); g_hash_state += 0x9e3779b97f4a7c15ULL; g_hash_draws++;
  return z | 1ULL;
}
inline u64 g_rehash_up = 0, g_rehash_down = 0;

inline unsigned g_opts = 0xF;
enum { OPT_CONTAINERS = 1 };

inline bool capok(size_t cap, size_t sz) {
  if (g_opts & OPT_CONTAINERS) return cap <= std::max<size_t>(64, 4 * sz);
  return cap <= std::max<size_t>(4, 4 * sz) && (sz || !cap);
}

template <class V> inline void relax(V& v) {
  size_t c = v.capacity();
  if ((g_opts & OPT_CONTAINERS) && c <= 64) return;
  if (v.empty()) { if (c) V().swap(v); }
  else if (c > 4 && v.size() < c / 4) {
    V w; w.reserve(c / 2);
    for (auto& e : v) w.push_back(std::move(e));
    v.swap(w);
  }
}
template <class V> inline void release(V& v) { V().swap(v); }
inline void pool_limit(size_t n) { if (n >= 0xFFFFFFFEull) throw std::runtime_error("pool id space exhausted (2^32-1 entries)"); }

struct Cap {
  u64 items = 0, cap = 0, bytes = 0, live = 0, total = 0;
  Cap& operator+=(const Cap& o) { items += o.items; cap += o.cap; bytes += o.bytes; live += o.live; total += o.total; return *this; }
};
template <class V> inline Cap vcap(const V& v) {
  Cap c; c.items = v.size(); c.cap = v.capacity(); c.bytes = v.capacity() * sizeof(typename V::value_type); return c;
}

struct DMap {
  struct Ent { u64 key; u32 val; u32 next; };
  std::vector<Ent> ent;
  std::vector<u32> head;
  u64 mul = 1;
  int sh = 61;
  size_t size() const { return ent.size(); }
  u32 slot(u64 k) const { return (u32)((mul * k) >> sh); }
  u32 get(u64 k) const {
    if (ent.empty()) return NONE;
    for (u32 i = head[slot(k)]; i != NONE; i = ent[i].next) if (ent[i].key == k) return ent[i].val;
    return NONE;
  }
  void rebuild_heads(int l) {
    mul = draw_multiplier(); sh = 64 - l;
    if ((g_opts & OPT_CONTAINERS) && head.capacity() >= ((size_t)1 << l) && head.capacity() <= 4 * ((size_t)1 << l)) head.assign((size_t)1 << l, NONE);
    else { std::vector<u32> h((size_t)1 << l, NONE); head.swap(h); }
    for (u32 i = 0; i < ent.size(); i++) { u32& hd = head[slot(ent[i].key)]; ent[i].next = hd; hd = i; }
  }
  int lg() const { return 64 - sh; }
  bool set(u64 k, u32 v) {
    if (!head.empty()) for (u32 i = head[slot(k)]; i != NONE; i = ent[i].next) if (ent[i].key == k) { ent[i].val = v; return false; }
    if (head.empty()) rebuild_heads(4);
    ent.push_back({k, v, NONE});
    if (ent.size() > head.size()) { g_rehash_up++; rebuild_heads(lg() + 1); }
    else { u32 id = (u32)ent.size() - 1; u32& hd = head[slot(k)]; ent[id].next = hd; hd = id; }
    return true;
  }
  bool erase(u64 k) {
    if (ent.empty()) return false;
    u32* pp = &head[slot(k)];
    while (*pp != NONE && ent[*pp].key != k) pp = &ent[*pp].next;
    if (*pp == NONE) return false;
    u32 i = *pp, last = (u32)ent.size() - 1;
    *pp = ent[i].next;
    if (i != last) {
      u32* q = &head[slot(ent[last].key)];
      while (*q != last) q = &ent[*q].next;
      *q = i; ent[i] = ent[last];
    }
    ent.pop_back(); relax(ent);
    if (ent.empty()) { clear(); return true; }
    if (ent.size() < head.size() / 4 && head.size() > 16) { g_rehash_down++; rebuild_heads(lg() - 1); }
    return true;
  }
  void clear() { release(ent); release(head); mul = 1; sh = 61; }
  void soft_clear() { ent.clear(); head.clear(); mul = 1; sh = 61; }
  Cap cap() const { Cap c = vcap(ent); c.bytes += head.capacity() * sizeof(u32); return c; }
  void check() const {
    TUNED_CHECK(ent.size() <= head.size() || ent.empty(), "dmap load");
    TUNED_CHECK(head.size() <= std::max<size_t>(16, 4 * ent.size()) && (!ent.empty() || head.empty()), "dmap heads cap");
    TUNED_CHECK(head.capacity() <= std::max<size_t>(128, 4 * head.size()), "dmap heads capacity");
    TUNED_CHECK(capok(ent.capacity(), ent.size()), "dmap ent cap");
    if (head.empty()) { TUNED_CHECK(ent.empty(), "dmap no heads"); return; }
    TUNED_CHECK(head.size() == ((size_t)1 << lg()) && (mul & 1), "dmap shape");
    std::vector<unsigned char> seen(ent.size(), 0); size_t cnt = 0;
    for (u32 b = 0; b < head.size(); b++)
      for (u32 i = head[b]; i != NONE; i = ent[i].next) {
        TUNED_CHECK(i < ent.size() && !seen[i] && slot(ent[i].key) == b, "dmap chain");
        seen[i] = 1; cnt++;
      }
    TUNED_CHECK(cnt == ent.size(), "dmap reach");
    for (u32 i = 0; i < ent.size(); i++) TUNED_CHECK(ent[i].val != NONE && get(ent[i].key) == ent[i].val, "dmap key");
  }
};
using FMap = DMap;

template <class T> struct ISetT {
  std::vector<T> el;
  DMap pos;
  bool hashed = false;
  static u64 K(T v) { return (u64)(std::make_unsigned_t<T>)v; }
  size_t size() const { return el.size(); }
  bool empty() const { return el.empty(); }
  long find(T v) const {
    if (!hashed) { for (size_t i = 0; i < el.size(); i++) if (el[i] == v) return (long)i; return -1; }
    u32 p = pos.get(K(v)); return p == NONE ? -1 : (long)p;
  }
  bool contains(T v) const { return find(v) >= 0; }
  bool insert(T v) {
    if (find(v) >= 0) return false;
    el.push_back(v);
    if (hashed) pos.set(K(v), (u32)el.size() - 1);
    else if (el.size() > 16) { hashed = true; for (size_t i = 0; i < el.size(); i++) pos.set(K(el[i]), (u32)i); }
    return true;
  }
  bool erase(T v) {
    long i = find(v);
    if (i < 0) return false;
    if (hashed) pos.erase(K(v));
    if ((size_t)i != el.size() - 1) { el[i] = el.back(); if (hashed) pos.set(K(el[i]), (u32)i); }
    el.pop_back(); relax(el);
    if (hashed && el.size() <= 8) { hashed = false; if ((g_opts & OPT_CONTAINERS) && pos.cap().bytes <= 64 * 8) pos.soft_clear(); else pos.clear(); }
    return true;
  }
  void clear() { release(el); pos.clear(); hashed = false; }
  Cap cap() const { Cap c = vcap(el); Cap p = pos.cap(); c.cap += p.cap; c.bytes += p.bytes; return c; }
  void check() const {
    if (hashed) {
      TUNED_CHECK(pos.size() == el.size(), "iset pos size");
      for (size_t i = 0; i < el.size(); i++) TUNED_CHECK(pos.get(K(el[i])) == i, "iset pos");
      pos.check();
    } else TUNED_CHECK(pos.size() == 0 && pos.head.empty(), "iset unhashed pos");
    TUNED_CHECK(capok(el.capacity(), el.size()), "iset cap");
    std::set<T> u(el.begin(), el.end());
    TUNED_CHECK(u.size() == el.size(), "iset dup");
  }
};
using ISet = ISetT<i32>;
using PSet = ISetT<u64>;

template <class S> struct MapOfSets {
  DMap idx;
  std::vector<S> pool;
  std::vector<u32> fr;
  S* get(u64 k) { u32 i = idx.get(k); return i == NONE ? nullptr : &pool[i]; }
  const S* get(u64 k) const { u32 i = idx.get(k); return i == NONE ? nullptr : &pool[i]; }
  template <class T> bool ins(u64 k, T item) {
    u32 i = idx.get(k);
    if (i == NONE) {
      if (!fr.empty()) { i = fr.back(); fr.pop_back(); relax(fr); } else { pool_limit(pool.size()); i = (u32)pool.size(); pool.emplace_back(); }
      idx.set(k, i);
    }
    return pool[i].insert(item);
  }
  template <class T> bool rem(u64 k, T item) {
    u32 i = idx.get(k);
    if (i == NONE) return false;
    bool r = pool[i].erase(item);
    if (pool[i].empty()) { if (!((g_opts & OPT_CONTAINERS) && pool[i].cap().bytes <= 64 * 8)) pool[i].clear(); idx.erase(k); fr.push_back(i); relax(fr); }
    return r;
  }
  template <class F> void each(F f) const {
    for (auto& e : idx.ent) f(e.key, pool[e.val]);
  }
  u64 total() const { u64 z = 0; each([&](u64, const S& s) { z += s.size(); }); return z; }
  Cap cap() const {
    Cap c = idx.cap(); c.items = total();
    for (auto& s : pool) { Cap q = s.cap(); c.cap += q.cap; c.bytes += q.bytes; }
    Cap pv = vcap(pool), fv = vcap(fr);
    c.bytes += pv.bytes + fv.bytes; c.live = idx.size(); c.total = pool.size();
    return c;
  }
  void check() const {
    idx.check();
    TUNED_CHECK(idx.size() + fr.size() == pool.size(), "mos slots");
    std::vector<unsigned char> used(pool.size(), 0);
    for (auto& e : idx.ent) { TUNED_CHECK(e.val < pool.size() && !used[e.val] && !pool[e.val].empty(), "mos idx"); used[e.val] = 1; }
    for (u32 f : fr) { TUNED_CHECK(f < pool.size() && !used[f], "mos free"); used[f] = 2; TUNED_CHECK(pool[f].empty() && pool[f].cap().bytes <= ((g_opts & OPT_CONTAINERS) ? 64 * 8 : 0), "mos free storage"); }
  }
};

using Tup = std::array<i32, 3>;
using Census = std::vector<std::pair<std::string, u64>>;
using Pairs = std::vector<std::pair<std::string, double>>;

struct RelInfo { int s; char kind; };
inline RelInfo relinfo(int rel) {
  static const RelInfo t[7] = {{0, 0}, {0, 'R'}, {0, 'B'}, {0, 'A'}, {1, 'A'}, {1, 'R'}, {1, 'B'}};
  return t[rel];
}

inline void xel(int rel, i32 u, i32 v, i32& x, i32& el) {
  if (relinfo(rel).kind == 'B') { el = u; x = v; } else { x = u; el = v; }
}

struct Params {
  double psiExp = 0.2;
  bool psiFixed = false, thetaFixed = false, muFixed = false;
  double psi = 0, theta = 0, mu = 0;
  unsigned opts = 0xF;
};

#ifdef BT_STATS
#define TUNED_BWK(i) (++wk[i])
#else
#define TUNED_BWK(i) ((void)0)
#endif
struct PerxTuned {
  struct XRec { i32 x = 0; PSet L[2]; };
  struct SideB { FMap R; MapOfSets<ISet> A, B, Xa, Xb; };
  SideB sd[2];
  std::vector<XRec> X; std::vector<u32> xfree; FMap xmap; ISet Xs;
  i64 Nraw = 0;
  u64 wk[2] = {};
  static constexpr int NWK = 2;
  static const char* wkname(int i) { return i ? "loop_iters" : "lset_ops"; }
  explicit PerxTuned(Params p = Params()) { g_opts = p.opts; }
  i64 N() const { return Nraw; }
  void load(const std::vector<Tup>& t) { for (auto& tp : t) apply(+1, tp[0], tp[1], tp[2]); }

  void lset(int s, i32 x, u64 pair, bool add) {
    TUNED_BWK(0);
    u32 xi = xmap.get(k32(x));
    if (xi == NONE) {
      if (!add) return;
      if (!xfree.empty()) { xi = xfree.back(); xfree.pop_back(); relax(xfree); } else { pool_limit(X.size()); xi = (u32)X.size(); X.emplace_back(); }
      X[xi].x = x; xmap.set(k32(x), xi);
    }
    XRec& r = X[xi];
    if (add) r.L[s].insert(pair); else r.L[s].erase(pair);
    bool both = !r.L[0].empty() && !r.L[1].empty();
    if (both) Xs.insert((i32)xi); else Xs.erase((i32)xi);
    if (r.L[0].empty() && r.L[1].empty()) { xmap.erase(k32(x)); X[xi] = XRec(); xfree.push_back(xi); }
  }
  bool apply(int sign, int rel, i32 u, i32 v) {
    RelInfo ri = relinfo(rel); int s = ri.s; bool add = sign > 0; SideB& S = sd[s];
    if (ri.kind == 'R') {
      if ((S.R.get(pk(u, v)) != NONE) == add) return true;
      if (add) S.R.set(pk(u, v), 0); else S.R.erase(pk(u, v));
      const ISet *xa = S.Xa.get(k32(u)), *xb = S.Xb.get(k32(v));
      if (xa && xb) {
        const ISet *it = xa->size() <= xb->size() ? xa : xb, *oth = it == xa ? xb : xa;
        for (i32 x : it->el) { TUNED_BWK(1); if (oth->contains(x)) lset(s, x, pk(u, v), add); }
      }
    } else {
      i32 x, el; xel(rel, u, v, x, el);
      if (ri.kind == 'B') {
        const ISet* q = S.B.get(k32(x));
        if ((q && q->contains(el)) == add) return true;
        if (add) { S.B.ins(k32(x), el); S.Xb.ins(k32(el), x); } else { S.B.rem(k32(x), el); S.Xb.rem(k32(el), x); }
        if (const ISet* A = S.A.get(k32(x))) for (i32 a : A->el) { TUNED_BWK(1); if (S.R.get(pk(a, el)) != NONE) lset(s, x, pk(a, el), add); }
      } else {
        const ISet* q = S.A.get(k32(x));
        if ((q && q->contains(el)) == add) return true;
        if (add) { S.A.ins(k32(x), el); S.Xa.ins(k32(el), x); } else { S.A.rem(k32(x), el); S.Xa.rem(k32(el), x); }
        if (const ISet* B = S.B.get(k32(x))) for (i32 b : B->el) { TUNED_BWK(1); if (S.R.get(pk(el, b)) != NONE) lset(s, x, pk(el, b), add); }
      }
    }
    Nraw += add ? 1 : -1;
    return false;
  }
  struct Enum {
    PerxTuned* g; size_t xi = 0, i = 0, j = 0; bool have = false; u32 cur = 0;
    explicit Enum(PerxTuned& b) : g(&b) {}
    bool start(i32* o) { xi = i = j = 0; have = false; return next(o); }
    bool next(i32* o) {
      for (;;) {
        if (!have) { if (xi >= g->Xs.size()) return false; cur = (u32)g->Xs.el[xi]; i = j = 0; have = true; }
        XRec& r = g->X[cur];
        if (i >= r.L[0].size()) { xi++; have = false; continue; }
        if (j >= r.L[1].size()) { i++; j = 0; continue; }
        u64 p = r.L[0].el[i], q = r.L[1].el[j++];
        o[0] = (i32)(p >> 32); o[1] = (i32)(u32)p; o[2] = r.x; o[3] = (i32)(q >> 32); o[4] = (i32)(u32)q;
        return true;
      }
    }
  };
  Census capacity() const {
    Cap cB, cX, cI, cM; Census z; u64 tot = 0;
    for (int s = 0; s < 2; s++) {
      cB += sd[s].R.cap(); cB += sd[s].A.cap(); cB += sd[s].B.cap(); cI += sd[s].Xa.cap(); cI += sd[s].Xb.cap();
    }
    for (auto& r : X) for (int s = 0; s < 2; s++) { Cap q = r.L[s].cap(); cX.items += r.L[s].size(); cX.cap += q.cap; cX.bytes += q.bytes; }
    cX.total = X.size(); cX.live = xmap.size(); cX.bytes += X.capacity() * sizeof(XRec); cM += xmap.cap(); cM += Xs.cap();
    auto emit = [&](const char* n, const Cap& c) {
      std::string p = n;
      z.push_back({p + ".items", c.items}); z.push_back({p + ".cap", c.cap}); z.push_back({p + ".bytes", c.bytes});
      z.push_back({p + ".slots_live", c.live}); z.push_back({p + ".slots_total", c.total}); tot += c.bytes;
    };
    emit("base", cB); emit("Lsets", cX); emit("inverse_idx", cI); emit("xmap_Xs", cM);
    z.push_back({"total_bytes", tot}); z.push_back({"rehash_up", g_rehash_up}); z.push_back({"rehash_down", g_rehash_down});
    z.push_back({"hash_draws", g_hash_draws});
    return z;
  }
  Pairs params() const { return {}; }
  Census counters() const { return {}; }
  void audit() {
    std::set<i32> xs;
    for (int s = 0; s < 2; s++) {
      SideB& S = sd[s];
      S.A.each([&](u64 x, const ISet& st) { st.check(); xs.insert((i32)(u32)x);
        for (i32 a : st.el) { auto* q = S.Xa.get(k32(a)); TUNED_CHECK(q && q->contains((i32)(u32)x), "Xa"); } });
      S.B.each([&](u64 x, const ISet& st) { st.check(); xs.insert((i32)(u32)x);
        for (i32 b : st.el) { auto* q = S.Xb.get(k32(b)); TUNED_CHECK(q && q->contains((i32)(u32)x), "Xb"); } });
      TUNED_CHECK(S.Xa.total() == S.A.total() && S.Xb.total() == S.B.total(), "inverse totals");
    }
    std::map<i32, std::set<u64>> exp[2];
    for (int s = 0; s < 2; s++) {
      SideB& S = sd[s];
      for (i32 x : xs) {
        auto *A = S.A.get(k32(x)); auto *B = S.B.get(k32(x));
        if (A && B) for (i32 a : A->el) for (i32 b : B->el) if (S.R.get(pk(a, b)) != NONE) exp[s][x].insert(pk(a, b));
      }
    }
    std::map<i32, std::set<u64>> got[2]; std::set<i32> xsExp, xsGot;
    for (u32 xi = 0; xi < X.size(); xi++) if (xmap.get(k32(X[xi].x)) == xi) {
      for (int s = 0; s < 2; s++) { X[xi].L[s].check(); if (!X[xi].L[s].empty()) got[s][X[xi].x].insert(X[xi].L[s].el.begin(), X[xi].L[s].el.end()); }
      TUNED_CHECK(!X[xi].L[0].empty() || !X[xi].L[1].empty(), "empty xrec");
      if (!X[xi].L[0].empty() && !X[xi].L[1].empty()) xsGot.insert(X[xi].x);
    }
    for (int s = 0; s < 2; s++) TUNED_CHECK(got[s] == exp[s], "L");
    for (auto& kv : exp[0]) if (exp[1].count(kv.first)) xsExp.insert(kv.first);
    std::set<i32> xsActual; for (i32 xi : Xs.el) xsActual.insert(X[xi].x);
    TUNED_CHECK(xsActual == xsExp && xsGot == xsExp && xsActual.size() == Xs.size(), "X*");
    Xs.check();
  }
};
}
