#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace current {
using i32 = int32_t;
using u32 = uint32_t;
using u64 = uint64_t;
using u8 = uint8_t;
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
#define CURRENT_CHECK(c, msg) do { if (!(c)) ::current::fail(std::string("audit: ") + (msg)); } while (0)

inline u64 g_hash_seed = 1, g_hash_state = 1, g_hash_draws = 0;
inline void hash_seed(u64 s) { g_hash_seed = g_hash_state = s; g_hash_draws = 0; }
inline u64 draw_multiplier() {
  u64 z = mix(g_hash_state); g_hash_state += 0x9e3779b97f4a7c15ULL; g_hash_draws++;
  return z | 1ULL;
}
inline u64 g_rehash_up = 0, g_rehash_down = 0;

inline unsigned g_opts = 0;
enum { OPT_CLASS_REUSE = 1, OPT_PROMOTE_FAST = 2, OPT_INLINE_WITNESS = 4, OPT_DEGREE_FIELD = 8, OPT_SKIP_NOOP = 16, OPT_KNOWN_STATE = 32, OPT_R_FROM_INDEX = 64, OPT_PREFETCH = 128 };

inline bool capok(size_t cap, size_t sz) { return cap <= std::max<size_t>(64, 4 * sz); }

template <class V> inline void relax(V& v) {
  size_t c = v.capacity();
  if (c <= 64) return;
  if (v.empty()) { V().swap(v); }
  else if (v.size() * 4 < c) {
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
  void prefetch_head(u64 k) const { if (!head.empty()) __builtin_prefetch(&head[slot(k)]); }
  u32 get(u64 k) const {
    if (ent.empty()) return NONE;
    for (u32 i = head[slot(k)]; i != NONE; i = ent[i].next) if (ent[i].key == k) return ent[i].val;
    return NONE;
  }
  void rebuild_heads(int l) {
    mul = draw_multiplier(); sh = 64 - l;
    if (head.capacity() >= ((size_t)1 << l) && head.capacity() <= 4 * ((size_t)1 << l)) head.assign((size_t)1 << l, NONE);
    else { std::vector<u32> h((size_t)1 << l, NONE); head.swap(h); }
    for (u32 i = 0; i < ent.size(); i++) { u32& hd = head[slot(ent[i].key)]; ent[i].next = hd; hd = i; }
  }
  int lg() const { return 64 - sh; }

  void set_absent(u64 k, u32 v) {
    if (head.empty()) rebuild_heads(4);
    ent.push_back({k, v, NONE});
    if (ent.size() > head.size()) { g_rehash_up++; rebuild_heads(lg() + 1); }
    else { u32 id = (u32)ent.size() - 1; u32& hd = head[slot(k)]; ent[id].next = hd; hd = id; }
  }
  bool set(u64 k, u32 v) {
    if (!head.empty()) { for (u32 i = head[slot(k)]; i != NONE; i = ent[i].next) if (ent[i].key == k) { ent[i].val = v; return false; } }
    set_absent(k, v);
    return true;
  }

  bool insert_if_absent(u64 k, u32 v) {
    if (!head.empty()) for (u32 i = head[slot(k)]; i != NONE; i = ent[i].next) if (ent[i].key == k) return false;
    set_absent(k, v);
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
    CURRENT_CHECK(ent.size() <= head.size() || ent.empty(), "dmap load");
    CURRENT_CHECK(head.size() <= std::max<size_t>(16, 4 * ent.size()) && (!ent.empty() || head.empty()), "dmap heads cap");
    CURRENT_CHECK(head.capacity() <= std::max<size_t>(128, 4 * head.size()), "dmap heads capacity");
    CURRENT_CHECK(capok(ent.capacity(), ent.size()), "dmap ent cap");
    if (head.empty()) { CURRENT_CHECK(ent.empty(), "dmap no heads"); return; }
    CURRENT_CHECK(head.size() == ((size_t)1 << lg()) && (mul & 1), "dmap shape");
    std::vector<unsigned char> seen(ent.size(), 0); size_t cnt = 0;
    for (u32 b = 0; b < head.size(); b++)
      for (u32 i = head[b]; i != NONE; i = ent[i].next) {
        CURRENT_CHECK(i < ent.size() && !seen[i] && slot(ent[i].key) == b, "dmap chain");
        seen[i] = 1; cnt++;
      }
    CURRENT_CHECK(cnt == ent.size(), "dmap reach");
    for (u32 i = 0; i < ent.size(); i++) CURRENT_CHECK(ent[i].val != NONE && get(ent[i].key) == ent[i].val, "dmap key");
  }
};
using FMap = DMap;

template <class T> struct ISetT {
  std::vector<T> el;
  std::unique_ptr<DMap> pos;
  bool hashed = false;
  ISetT() = default;
  ISetT(ISetT&&) noexcept = default;
  ISetT& operator=(ISetT&&) noexcept = default;
  ISetT(const ISetT&) = delete;
  ISetT& operator=(const ISetT&) = delete;
  static u64 K(T v) { return (u64)(std::make_unsigned_t<T>)v; }
  size_t size() const { return el.size(); }
  bool empty() const { return el.empty(); }
  long find(T v) const {
    if (!hashed) { for (size_t i = 0; i < el.size(); i++) if (el[i] == v) return (long)i; return -1; }

    u32 p = pos->get(K(v)); return p == NONE ? -1 : (long)p;
  }
  bool contains(T v) const { return find(v) >= 0; }
  void push_el(T v) { el.push_back(v); }
  void hashbuild() {
    if (!pos) pos = std::make_unique<DMap>();
    hashed = true;
    for (size_t i = 0; i < el.size(); i++) pos->set_absent(K(el[i]), (u32)i);
  }
  bool insert(T v) {
    if (find(v) >= 0) return false;
    push_el(v);
    if (hashed) pos->set_absent(K(v), (u32)el.size() - 1);
    else if (el.size() > 16) hashbuild();
    return true;
  }

  void insert_absent(T v) {
#ifndef NDEBUG
    CURRENT_CHECK(!contains(v), "insert_absent: element present");
#endif
    push_el(v);
    if (hashed) pos->set_absent(K(v), (u32)el.size() - 1);
    else if (el.size() > 16) hashbuild();
  }

  void bulk_finish() {
#ifndef NDEBUG
    { std::set<T> u(el.begin(), el.end()); CURRENT_CHECK(u.size() == el.size() && !hashed, "bulk_finish: duplicates"); }
#endif
    if (el.size() <= 16) return;
    if (!pos) pos = std::make_unique<DMap>();
    pos->ent.reserve(el.size());
    for (size_t i = 0; i < el.size(); i++) pos->ent.push_back({K(el[i]), (u32)i, NONE});
    int l = 5; while (((size_t)1 << l) < el.size()) l++;
    pos->rebuild_heads(l); hashed = true;
  }

  void retire() {
    if (el.capacity() > 64) release(el); else el.clear();
    hashed = false;
    if (pos) { if (pos->cap().bytes <= 64 * 8) pos->soft_clear(); else pos.reset(); }
  }
  bool erase(T v) {
    long i = find(v);
    if (i < 0) return false;
    erase_at((size_t)i);
    return true;
  }

  void erase_at(size_t i) {
    T v = el[i];
    if (hashed) pos->erase(K(v));
    if (i != el.size() - 1) { el[i] = el.back(); if (hashed) pos->set(K(el[i]), (u32)i); }
    el.pop_back(); relax(el);
    if (hashed && el.size() <= 8) { hashed = false; if (pos->cap().bytes <= 64 * 8) pos->soft_clear(); else pos.reset(); }
  }
  void clear() { release(el); pos.reset(); hashed = false; }

  size_t retain_bytes() const { return el.capacity() * sizeof(T) + (pos ? pos->cap().bytes : 0); }
  Cap cap() const {
    Cap c = vcap(el);
    if (pos) { Cap p = pos->cap(); c.cap += p.cap; c.bytes += p.bytes + sizeof(DMap); }
    return c;
  }
  void check() const {
    if (hashed) {
      CURRENT_CHECK(pos && pos->size() == el.size(), "iset pos size");
      for (size_t i = 0; i < el.size(); i++) CURRENT_CHECK(pos->get(K(el[i])) == i, "iset pos");
      pos->check();
    } else CURRENT_CHECK(!pos || (pos->size() == 0 && pos->head.empty()), "iset unhashed pos");
    CURRENT_CHECK(capok(el.capacity(), el.size()), "iset cap");
    std::set<T> u(el.begin(), el.end());
    CURRENT_CHECK(u.size() == el.size(), "iset dup");
  }
};
using ISet = ISetT<i32>;
using PSet = ISetT<u64>;

template <class S> inline bool deadok(const S& s, bool retain) {
  if (!retain) return s.cap().bytes == 0;
  return s.el.empty() && !s.hashed && s.el.capacity() <= 64 && (!s.pos || s.pos->cap().bytes <= 64 * 8);
}

template <class S> struct MapOfSets {
  DMap idx;
  std::vector<S> pool;
  std::vector<u32> fr;
  S* get(u64 k) { u32 i = idx.get(k); return i == NONE ? nullptr : &pool[i]; }
  const S* get(u64 k) const { u32 i = idx.get(k); return i == NONE ? nullptr : &pool[i]; }
  template <class T> bool ins(u64 k, T item) { size_t z; return ins_sz(k, item, z); }

  u32 slot_for(u64 k) {
    u32 i = idx.get(k);
    if (i == NONE) {
      if (!fr.empty()) { i = fr.back(); fr.pop_back(); relax(fr); } else { pool_limit(pool.size()); i = (u32)pool.size(); pool.emplace_back(); }
      idx.set_absent(k, i);
    }
    return i;
  }
  template <class T> bool ins_sz(u64 k, T item, size_t& sz) {
    u32 i = slot_for(k);
    bool r = pool[i].insert(item); sz = pool[i].size(); return r;
  }

  template <class T> void ins_absent_sz(u64 k, T item, size_t& sz, S** sp = nullptr) {
    u32 i = slot_for(k);
    pool[i].insert_absent(item); sz = pool[i].size(); if (sp) *sp = &pool[i];
  }

  template <class T> long probe(u64 k, T item, u32& sid) { sid = idx.get(k); return sid == NONE ? -1 : pool[sid].find(item); }

  template <class T> void ins_probed(u64 k, T item, u32 sid, size_t& sz, S** sp = nullptr) {
    if (sid != NONE) { pool[sid].insert_absent(item); sz = pool[sid].size(); if (sp) *sp = &pool[sid]; }
    else ins_absent_sz(k, item, sz, sp);
  }

  void erase_probed(u32 sid, u64 k, long pos, size_t& sz) {
    pool[sid].erase_at((size_t)pos); sz = pool[sid].size();
    if (pool[sid].empty()) { if (!(pool[sid].retain_bytes() <= 64 * 8)) pool[sid].clear(); idx.erase(k); fr.push_back(sid); relax(fr); }
  }
  template <class T> bool rem(u64 k, T item) { size_t z; return rem_sz(k, item, z); }
  template <class T> bool rem_sz(u64 k, T item, size_t& sz) {
    u32 i = idx.get(k);
    if (i == NONE) { sz = 0; return false; }
    bool r = pool[i].erase(item); sz = pool[i].size();
    if (pool[i].empty()) { if (!(pool[i].retain_bytes() <= 64 * 8)) pool[i].clear(); idx.erase(k); fr.push_back(i); relax(fr); }
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
    CURRENT_CHECK(idx.size() + fr.size() == pool.size(), "mos slots");
    std::vector<unsigned char> used(pool.size(), 0);
    for (auto& e : idx.ent) { CURRENT_CHECK(e.val < pool.size() && !used[e.val] && !pool[e.val].empty(), "mos idx"); used[e.val] = 1; }
    for (u32 f : fr) { CURRENT_CHECK(f < pool.size() && !used[f], "mos free"); used[f] = 2; CURRENT_CHECK(pool[f].empty() && pool[f].retain_bytes() <= 64 * 8, "mos free storage"); }
  }
};

enum { W_BASE, W_WIT, W_EDGE, W_VFLIP, W_FIX, W_SYNC, W_INC, W_XTR, W_ABTR, W_MU, W_GLB, NW };
inline const char* const WKN[NW] = {"base", "witness", "edge_events", "vertex_flip_fixes", "fix_edge", "hx_sync",
                                    "incidence", "x_trans", "ab_trans", "mu_rebuild", "global_rebuild"};
enum { CTX_NONE, CTX_X, CTX_AB, CTX_VF, CTX_MU, CTX_GLB, NCTX };
inline const char* const CTXN[NCTX] = {"outside", "x_transition", "ab_transition", "vertex_flip", "mu_rebuild",
                                       "global_rebuild"};
#ifdef BT_STATS
#define CURRENT_WK(i, n) do { wk[i] += (n); wtr[ctx] += (n); } while (0)
#else
#define CURRENT_WK(i, n) ((void)0)
#endif

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
  unsigned opts = 0;
};

struct Vtx {
  ISet adj, hadj, Alo, Ahi;
  i32 a = 0, b = 0;
  bool high = false, act = false, alive = false;
  bool inA1 = false, inA2 = false;
};

struct Edge {
  ISet W; i32 p = 0, q = 0, w1 = 0; bool alive = false; u8 nw = 0;
  size_t wsize() const { return nw == 1 ? 1 : W.size(); }
  i32 wget(size_t k) const { return nw == 1 ? w1 : W.el[k]; }
  void wbirth(i32 x) { if (g_opts & OPT_INLINE_WITNESS) { w1 = x; nw = 1; } else W.insert(x); }
  bool winsert(i32 x) {
    if (nw == 1) {
      if (w1 == x) return false;
      W.insert(w1); W.insert(x); nw = 2;
      return true;
    }
    return W.insert(x);
  }
  bool werase(i32 x) {
    if (nw == 1) { if (w1 != x) return false; nw = 0; return true; }
    return W.erase(x);
  }
  void wcheck() const {
    if (nw == 1) CURRENT_CHECK(W.empty(), "inline witness with non-empty W");
    else { CURRENT_CHECK(nw == 0 || nw == 2, "nw value"); W.check(); }
  }
};
static_assert(sizeof(Edge) == 56, "Edge must stay 56 bytes");
struct List { ISet items; i32 key = 0, posS = -1, anchSz = 0; };
inline constexpr int li(int k) { return k >> 1; }
struct HSide { FMap lm[2]; std::vector<u32> S[2]; PSet P2; i64 cnt = 0; };
struct HRec { i32 x = 0; bool alive = false, inXH = false; i32 deg = 0; HSide s[2]; };
struct Side {
  FMap R;
  MapOfSets<ISet> N1, N1r, A, B, R2H, R3H;
  ISet aH, bH;
  std::vector<Vtx> V; std::vector<u32> vfree; FMap vmap;
  ISet anch1, anch2;
  std::vector<List> pool[3]; std::vector<u32> lfree[3];
};
struct Core {
  Side sd[2];
  std::vector<Edge> E; std::vector<u32> efree; FMap emap;
  std::vector<HRec> H; std::vector<u32> hfree; FMap hmap;
  ISet heavyL, XH;
  i64 Wtot = 0, m = 0, m0 = 1, Nraw = 0;
  double N0 = 2, psi = 1, theta = 1, mu = 1;
};
struct Ctr {
  u64 x_to_heavy = 0, x_to_light = 0, a_to_heavy = 0, a_to_light = 0, b_to_heavy = 0, b_to_light = 0;
  u64 vertex_flips = 0, mu_rebuilds = 0, global_rebuilds = 0;
};

struct Copair : Core {
  static constexpr int NWK = NW;
  static const char* wkname(int i) { return WKN[i]; }
  Params P;
  Ctr c;
  u64 wk[NW] = {}, wtr[NCTX] = {};
  int ctx = 0;
  std::vector<u32> mark; u32 epoch = 0;
  std::vector<i32> scrH, scrN;
  std::vector<u32> pf_f, pf_o, pf_e, vo_scr;
  struct Ctx { int& r; int old; Ctx(int& r_, int n) : r(r_), old(r_) { r = n; } ~Ctx() { r = old; } };

  explicit Copair(Params p) : P(p) { g_opts = P.opts; reset(); }
  void reset() { static_cast<Core&>(*this) = Core(); }
  double Nt() const { return (double)std::max<i64>(2, Nraw); }
  i64 N() const { return Nraw; }
  void thresholds() {
    double n = Nt();
    psi = P.psiFixed ? P.psi : std::pow(n, P.psiExp);
    theta = P.thetaFixed ? P.theta : std::sqrt(n);
  }

  bool inR(Side& S, i32 a, i32 b) {
    if (g_opts & OPT_R_FROM_INDEX) { const ISet* q = S.N1.get(k32(a)); return q && q->contains(b); }
    return S.R.get(pk(a, b)) != NONE;
  }
  u64 rsize(const Side& S) const { return (g_opts & OPT_R_FROM_INDEX) ? S.N1.total() : (u64)S.R.size(); }
  u32 vget(int s, i32 a, i32 b) { return sd[s].vmap.get(pk(a, b)); }
  u32 vnew(int s, i32 a, i32 b) {
    Side& S = sd[s]; u32 v;
    if (!S.vfree.empty()) { v = S.vfree.back(); S.vfree.pop_back(); relax(S.vfree); } else { pool_limit(S.V.size()); v = (u32)S.V.size(); S.V.emplace_back(); }
    Vtx& x = S.V[v];

    x.a = a; x.b = b; x.high = false; x.alive = true; x.act = inR(S, a, b); x.inA1 = x.inA2 = false;
    S.vmap.set(pk(a, b), v);
    return v;
  }
  void vdel(int s, u32 v) {
    Side& S = sd[s]; Vtx& x = S.V[v];
#ifndef NDEBUG
    CURRENT_CHECK(!x.inA1 && !x.inA2, "vdel: vertex still flagged as anchor");
#endif
    S.vmap.erase(pk(x.a, x.b));
    retire(x);
    S.vfree.push_back(v);
  }

  static void retire(Vtx& x) {
    x.adj.retire(); x.hadj.retire(); x.Alo.retire(); x.Ahi.retire();
    x.a = x.b = 0; x.high = x.act = x.alive = false; x.inA1 = x.inA2 = false;
  }
  static void retire(Edge& e) { e.W.retire(); e.p = e.q = e.w1 = 0; e.alive = false; e.nw = 0; }
  static void freeList(List& L) {
    L.items.retire(); L.key = 0; L.posS = -1; L.anchSz = 0;
  }
  static bool setMem(ISet& s, i32 v, bool f) { return f ? s.insert(v) : s.erase(v); }

  static void setMemA(ISet& s, i32 v, bool want, bool& flag) {
    if (want == flag) return;
    setMem(s, v, want);
    flag = want;
  }
  void anch_p(u32 p) {
    Vtx& P_ = sd[0].V[p];
    setMemA(sd[0].anch1, (i32)p, P_.act && !P_.Alo.empty(), P_.inA1);
    setMemA(sd[0].anch2, (i32)p, P_.act && !P_.Ahi.empty(), P_.inA2);
  }
  void anch_q(u32 q) { Vtx& Q = sd[1].V[q]; setMemA(sd[1].anch1, (i32)q, Q.act && !Q.Alo.empty(), Q.inA1); }
  void fix_edge(u32 e, bool ex) {
    CURRENT_WK(W_FIX, 1);
    Edge& Ed = E[e]; Vtx& Pv = sd[0].V[Ed.p]; Vtx& Qv = sd[1].V[Ed.q];
    bool ph = Pv.high, qh = Qv.high, ap = Pv.act, aq = Qv.act;
    setMem(Pv.hadj, (i32)e, ex && qh);
    setMem(Qv.hadj, (i32)e, ex && ph);
    setMem(Pv.Alo, (i32)e, ex && aq && !qh);
    setMem(Qv.Alo, (i32)e, ex && qh && ap && !ph);
    setMem(Pv.Ahi, (i32)e, ex && ph && qh && aq);
    anch_p(Ed.p); anch_q(Ed.q);
  }
  void vcheck(int s, u32 v) {
    Vtx& X = sd[s].V[v];
    double deg = (double)X.adj.size();
    if (X.high) { if (!(deg < mu / 2)) return; X.high = false; }
    else { if (!(deg > 2 * mu)) return; X.high = true; }
    c.vertex_flips++;
    Ctx g(ctx, CTX_VF);
    CURRENT_WK(W_VFLIP, X.adj.size());
    for (i32 e : X.adj.el) fix_edge((u32)e, true);
  }
  u32 vres(int s, i32 a, i32 b) { u32 v = vget(s, a, b); return v == NONE ? vnew(s, a, b) : v; }
  void w_add_v(i32 x, u32 vp, u32 vq) {
    CURRENT_WK(W_WIT, 1);
    u64 ek = pk((i32)vp, (i32)vq);
    u32 e = emap.get(ek);
    if (e != NONE) { if (E[e].winsert(x)) Wtot++; return; }

    CURRENT_WK(W_EDGE, 1);
    if (!efree.empty()) { e = efree.back(); efree.pop_back(); relax(efree); } else { pool_limit(E.size()); e = (u32)E.size(); E.emplace_back(); }
    Edge* Edp = &E[e]; Edp->p = (i32)vp; Edp->q = (i32)vq; Edp->alive = true;
    Edp->wbirth(x); Wtot++;
    emap.set(ek, e);

    m++;
    Vtx& Pv = sd[0].V[vp]; Vtx& Qv = sd[1].V[vq];
    bool ph = Pv.high, qh = Qv.high, ap = Pv.act, aq = Qv.act;
    CURRENT_WK(W_FIX, 1);
    Pv.adj.insert_absent((i32)e); Qv.adj.insert_absent((i32)e);
    if (qh) Pv.hadj.insert_absent((i32)e);
    if (ph) Qv.hadj.insert_absent((i32)e);
    if (aq && !qh) Pv.Alo.insert_absent((i32)e);
    if (qh && ap && !ph) Qv.Alo.insert_absent((i32)e);
    if (ph && qh && aq) Pv.Ahi.insert_absent((i32)e);
    anch_p(vp); anch_q(vq);
    vcheck(0, vp); vcheck(1, vq);
  }
  void w_del_v(i32 x, u32 vp, u32 vq) {
    CURRENT_WK(W_WIT, 1);
    if (vp == NONE || vq == NONE) fail("w_del: missing vertex");
    u64 ek = pk((i32)vp, (i32)vq);
    u32 e = emap.get(ek);
    if (e == NONE) fail("w_del: missing edge");
    Edge& Ed = E[e];

    if (!Ed.werase(x)) fail("w_del: missing witness");
    Wtot--;
    if (Ed.wsize() != 0) { return; }

    CURRENT_WK(W_EDGE, 1);
    m--;
    Vtx& Pv = sd[0].V[vp]; Vtx& Qv = sd[1].V[vq];
    bool ph = Pv.high, qh = Qv.high, ap = Pv.act, aq = Qv.act;
    CURRENT_WK(W_FIX, 1);
#ifndef NDEBUG
#define EDGE_DEATH_CHECK(c) CURRENT_CHECK(c, "predicate-selected death: edge missing from a predicate-selected set")
#else
#define EDGE_DEATH_CHECK(c) ((void)(c))
#endif
    EDGE_DEATH_CHECK(Pv.adj.erase((i32)e)); EDGE_DEATH_CHECK(Qv.adj.erase((i32)e));
    if (qh) EDGE_DEATH_CHECK(Pv.hadj.erase((i32)e));
    if (ph) EDGE_DEATH_CHECK(Qv.hadj.erase((i32)e));
    if (aq && !qh) EDGE_DEATH_CHECK(Pv.Alo.erase((i32)e));
    if (qh && ap && !ph) EDGE_DEATH_CHECK(Qv.Alo.erase((i32)e));
    if (ph && qh && aq) EDGE_DEATH_CHECK(Pv.Ahi.erase((i32)e));
#undef EDGE_DEATH_CHECK
    anch_p(vp); anch_q(vq);
    vcheck(0, vp); vcheck(1, vq);
    emap.erase(ek);

    retire(E[e]);
    efree.push_back(e);
    if (sd[0].V[vp].adj.empty()) vdel(0, vp);
    if (sd[1].V[vq].adj.empty()) vdel(1, vq);
  }

  int slotOf(Side& S, i32 a, i32 b, i32& key, i32& item) {
    if (S.aH.contains(a)) { key = a; item = b; return 0; }
    if (S.bH.contains(b)) { key = b; item = a; return 2; }
    key = a; item = b; return 1;
  }

  void upd_XH(u32 h) {
    HRec& r = H[h]; bool want = r.s[0].cnt > 0 && r.s[1].cnt > 0;
    if (want == r.inXH) return;
    setMem(XH, (i32)h, want);
    r.inXH = want;
  }
  u32 newList(Side& S, int k) {
    u32 lid;
    if (!S.lfree[k].empty()) { lid = S.lfree[k].back(); S.lfree[k].pop_back(); relax(S.lfree[k]); } else { pool_limit(S.pool[k].size()); lid = (u32)S.pool[k].size(); S.pool[k].emplace_back(); }
    List& L = S.pool[k][lid]; L.posS = -1; L.anchSz = 0; L.key = 0;
    return lid;
  }
  void touch(u32 h, int s, int k, u32 lid, int pre = -1) {
    Side& S = sd[s]; HRec& r = H[h]; HSide& hs = r.s[s]; List& L = S.pool[k][lid];
    bool inS = !L.items.empty();
    if (inS && pre >= 0) inS = pre != 0;
    else if (inS) {
      const ISet* q = (k == 0) ? S.A.get(k32(r.x)) : S.B.get(k32(r.x));
      inS = q && q->contains(L.key);
    }
    i32 nw = inS ? (i32)L.items.size() : 0, old = L.anchSz;
    auto& sv = hs.S[li(k)];
    if (nw > 0 && L.posS < 0) { L.posS = (i32)sv.size(); sv.push_back(lid); }
    else if (nw == 0 && L.posS >= 0) {
      u32 last = sv.back();
      sv[L.posS] = last; S.pool[k][last].posS = L.posS; sv.pop_back(); relax(sv); L.posS = -1;
    }
    L.anchSz = nw;
    if (nw != old) { hs.cnt += nw - old; upd_XH(h); }
    if (L.items.empty()) { hs.lm[li(k)].erase(k32(L.key)); freeList(S.pool[k][lid]); S.lfree[k].push_back(lid); }
  }

  void p2set(u32 h, int s, u64 p, bool ok) {
    HSide& hs = H[h].s[s]; size_t old = hs.P2.size();
    if (ok) hs.P2.insert(p); else hs.P2.erase(p);
    if (hs.P2.size() != old) { hs.cnt += (i64)hs.P2.size() - (i64)old; upd_XH(h); }
  }
  void refreshS(u32 h, int s, int k, i32 key, int pre = -1) {
    u32 lid = H[h].s[s].lm[li(k)].get(k32(key));
    if (lid != NONE) touch(h, s, k, lid, pre);
  }
  void setitem(u32 h, int s, int k, i32 key, i32 item, bool ok, int pre = -1) {
    CURRENT_WK(W_SYNC, 1);
    if (k == 1) { p2set(h, s, pk(key, item), ok); return; }
    Side& S = sd[s]; HSide& hs = H[h].s[s];
    u32 lid = hs.lm[li(k)].get(k32(key));
    if (ok) {
      if (lid == NONE) { lid = newList(S, k); S.pool[k][lid].key = key; hs.lm[li(k)].set(k32(key), lid); }
      S.pool[k][lid].items.insert(item);
    } else {
      if (lid == NONE) return;
      S.pool[k][lid].items.erase(item);
    }
    touch(h, s, k, lid, pre);
  }

  void sync_k(u32 h, int s, i32 a, i32 b, int k, i32 key, i32 item, bool rok) {
    Side& S = sd[s]; bool ok = rok; int pre = -1;
    if (ok && k == 1) { setitem(h, s, k, key, item, true); return; }
    if (ok) {
      i32 x = H[h].x;
      const ISet* Bx = S.B.get(k32(x)); const ISet* Ax = S.A.get(k32(x));
      bool inb = Bx && Bx->contains(b), ina = Ax && Ax->contains(a);
      ok = (k == 0) ? inb : (k == 2 ? ina : (ina && inb));
      pre = k == 0 ? (int)ina : (k == 2 ? (int)inb : -1);
    }
    setitem(h, s, k, key, item, ok, pre);
  }
  void sync(u32 h, int s, i32 a, i32 b) {
    Side& S = sd[s]; i32 key, item;
    int k = slotOf(S, a, b, key, item);
    bool ok = inR(S, a, b);
    if (ok) {
      i32 x = H[h].x;
      const ISet* Bx = S.B.get(k32(x)); const ISet* Ax = S.A.get(k32(x));
      bool inb = Bx && Bx->contains(b), ina = Ax && Ax->contains(a);
      ok = (k == 0) ? inb : (k == 2 ? ina : (ina && inb));
    }
    setitem(h, s, k, key, item, ok);
  }
  void discard_slot(u32 h, int s, i32 a, i32 b) {
    i32 key, item; int k = slotOf(sd[s], a, b, key, item);
    setitem(h, s, k, key, item, false);
  }
  u32 newHeavy(i32 x) {
    u32 h;
    if (!hfree.empty()) { h = hfree.back(); hfree.pop_back(); relax(hfree); } else { pool_limit(H.size()); h = (u32)H.size(); H.emplace_back(); }
    HRec& r = H[h]; r.x = x; r.alive = true; r.inXH = false;
    r.deg = (i32)deg(x);
    hmap.set(k32(x), h); heavyL.insert((i32)h);
    if (mark.size() < H.size()) mark.resize(H.size() * 2, 0);
    return h;
  }
  void build_x(u32 h) {
    i32 x = H[h].x;
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];
      const ISet* B = S.B.get(k32(x)); const ISet* A = S.A.get(k32(x));
      auto sb = [&](i32 a, i32 b) { sync(h, s, a, b); };
      if (B) for (i32 a : S.aH.el) {
        const ISet* n1 = S.N1.get(k32(a)); if (!n1) continue;
        if (B->size() <= n1->size()) { for (i32 b : B->el) if (n1->contains(b)) sb(a, b); }
        else { for (i32 b : n1->el) if (B->contains(b)) sb(a, b); }
      }
      HSide& hsP = H[h].s[s];
      if (A) for (i32 a : A->el) if (!S.aH.contains(a)) {
        const ISet* n1 = S.N1.get(k32(a));
        if (n1) for (i32 b : n1->el) {
          if (S.bH.contains(b)) sb(a, b);
          else if (B && B->contains(b)) hsP.P2.push_el(pk(a, b));
        }
      }
      hsP.P2.bulk_finish(); hsP.cnt += (i64)hsP.P2.size(); upd_XH(h);
      if (B) for (i32 b : B->el) { CURRENT_WK(W_INC, 1); S.R2H.ins(k32(b), (i32)h); }
      if (A) for (i32 a : A->el) { CURRENT_WK(W_INC, 1); S.R3H.ins(k32(a), (i32)h); }
    }
  }
  void drop_x(u32 h) {
    HRec& r = H[h]; i32 x = r.x;
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s]; HSide& hs = r.s[s];
      for (int k = 0; k < 3; k += 2) {
        for (auto& sl : hs.lm[li(k)].ent) { freeList(S.pool[k][sl.val]); S.lfree[k].push_back(sl.val); }
        hs.lm[li(k)].clear(); release(hs.S[li(k)]);
      }
      hs.P2.clear();
      hs.cnt = 0;
      const ISet* B = S.B.get(k32(x)); const ISet* A = S.A.get(k32(x));
      if (B) for (i32 b : B->el) { CURRENT_WK(W_INC, 1); S.R2H.rem(k32(b), (i32)h); }
      if (A) for (i32 a : A->el) { CURRENT_WK(W_INC, 1); S.R3H.rem(k32(a), (i32)h); }
    }
    XH.erase((i32)h); r.inXH = false;
  }
  template <class F> void forPairs(i32 x, F f) {
    const ISet *A0 = sd[0].A.get(k32(x)), *B0 = sd[0].B.get(k32(x)), *A1 = sd[1].A.get(k32(x)), *Bn = sd[1].B.get(k32(x));
    if (!A0 || !B0 || !A1 || !Bn) return;
    for (i32 a : A0->el) for (i32 b : B0->el) for (i32 d : A1->el) for (i32 e : Bn->el) f(a, b, d, e);
  }

  template <class F> void forPairsV(i32 x, bool add, F f) {
    const ISet *A0 = sd[0].A.get(k32(x)), *B0 = sd[0].B.get(k32(x)), *A1 = sd[1].A.get(k32(x)), *Bn = sd[1].B.get(k32(x));
    if (!A0 || !B0 || !A1 || !Bn) return;
    for (i32 a : A0->el) for (i32 b : B0->el) {
      u32 vp = add ? vres(0, a, b) : vget(0, a, b);
      for (i32 d : A1->el) for (i32 e : Bn->el) { u32 vq = add ? vres(1, d, e) : vget(1, d, e); f(vp, vq); }
    }
  }
  void to_heavy(i32 x) {
    c.x_to_heavy++; Ctx g(ctx, CTX_X); CURRENT_WK(W_XTR, 1);
    forPairsV(x, false, [&](u32 vp, u32 vq) { w_del_v(x, vp, vq); });
    u32 h = newHeavy(x); build_x(h);
  }

  void to_heavy_ins(i32 x, MapOfSets<ISet>& M, i32 el) {
    c.x_to_heavy++; Ctx g(ctx, CTX_X); CURRENT_WK(W_XTR, 1);
    forPairsV(x, false, [&](u32 vp, u32 vq) { w_del_v(x, vp, vq); });
    M.ins(k32(x), el);
    u32 h = newHeavy(x); build_x(h);
  }
  void to_light(i32 x) {
    c.x_to_light++; Ctx g(ctx, CTX_X); CURRENT_WK(W_XTR, 1);
    u32 h = hmap.get(k32(x));
    drop_x(h);
    hmap.erase(k32(x)); heavyL.erase((i32)h); H[h] = HRec(); hfree.push_back(h);
    forPairsV(x, true, [&](u32 vp, u32 vq) { w_add_v(x, vp, vq); });
  }
  size_t deg(i32 x) {
    size_t z = 0;
    for (int s = 0; s < 2; s++) {
      const ISet* A = sd[s].A.get(k32(x)); const ISet* B = sd[s].B.get(k32(x));
      z += (A ? A->size() : 0) + (B ? B->size() : 0);
    }
    return z;
  }
  void check_x(i32 x) {
    size_t d = deg(x);
    if (hmap.get(k32(x)) != NONE) { if ((double)d < psi / 2 || d == 0) to_light(x); }
    else if ((double)d > 2 * psi) to_heavy(x);
  }

  void check_x_known(i32 x, u32 h, size_t d) {
    if (h != NONE) { if ((double)d < psi / 2 || d == 0) to_light(x); }
    else if ((double)d > 2 * psi) to_heavy(x);
  }

  void check_x_hdeg(i32 x, size_t d) {
    if ((double)d < psi / 2 || d == 0) to_light(x);
  }
  void flip_class(int s, i32 v, bool isa) {
    Side& S = sd[s];
    const ISet* nb = isa ? S.N1.get(k32(v)) : S.N1r.get(k32(v));
    scrN.clear(); if (nb) scrN = nb->el;
    scrH.clear(); epoch++;
    auto addset = [&](const ISet* q) { if (q) for (i32 h : q->el) if (mark[h] != epoch) { mark[h] = epoch; scrH.push_back(h); } };
    if (isa) { addset(S.R3H.get(k32(v))); for (i32 n : scrN) addset(S.R2H.get(k32(n))); }
    else { addset(S.R2H.get(k32(v))); for (i32 n : scrN) addset(S.R3H.get(k32(n))); }
    CURRENT_WK(W_ABTR, scrH.size() * scrN.size() * 2);
    for (i32 h : scrH) for (i32 n : scrN) { if (isa) discard_slot((u32)h, s, v, n); else discard_slot((u32)h, s, n, v); }
    ISet& Hs = isa ? S.aH : S.bH;
    if (!Hs.erase(v)) Hs.insert(v);
    for (i32 h : scrH) for (i32 n : scrN) { if (isa) sync((u32)h, s, v, n); else sync((u32)h, s, n, v); }
    release(scrH); release(scrN);
  }

  void check_cls_known(int s, i32 v, bool isa, size_t sz, bool inH) {
    Ctx g(ctx, CTX_AB);
    double d = (double)sz;
    bool flip = inH ? d < theta / 2 : d > 2 * theta;
    if (!flip) return;
    if (isa) (inH ? c.a_to_light : c.a_to_heavy)++; else (inH ? c.b_to_light : c.b_to_heavy)++;
    flip_class(s, v, isa);
  }
  void check_cls(int s, i32 v, bool isa) {
    Side& S = sd[s]; Ctx g(ctx, CTX_AB);
    const ISet* q = isa ? S.N1.get(k32(v)) : S.N1r.get(k32(v));
    double d = q ? (double)q->size() : 0;
    bool inH = (isa ? S.aH : S.bH).contains(v);
    bool flip = inH ? d < theta / 2 : d > 2 * theta;
    if (!flip) return;
    if (isa) (inH ? c.a_to_light : c.a_to_heavy)++; else (inH ? c.b_to_light : c.b_to_heavy)++;
    flip_class(s, v, isa);
  }

  bool apply(int sign, int rel, i32 u, i32 v) {
    RelInfo ri = relinfo(rel); int s = ri.s; bool add = sign > 0; Side& S = sd[s];
    if (ri.kind == 'R') {
      size_t na = 0, nb = 0;
      if (!base_R(S, u, v, add, na, nb)) return true;
      upd_R(s, u, v, add, na, nb);
      Nraw += add ? 1 : -1;
      post();
      return false;
    }

    i32 x, el; xel(rel, u, v, x, el);
    MapOfSets<ISet>& M = ri.kind == 'A' ? S.A : S.B;
    u32 sid = NONE; long pos;
    pos = M.probe(k32(x), el, sid);
    if ((pos >= 0) == add) return true;
    upd_xel(s, ri.kind, x, el, add, sid, pos);
    Nraw += add ? 1 : -1;
    post();
    return false;
  }

  bool base_R(Side& S, i32 a, i32 b, bool add, size_t& na, size_t& nb) {
    const bool knownState = (g_opts & OPT_KNOWN_STATE) != 0, rFromIndex = (g_opts & OPT_R_FROM_INDEX) != 0;
    const u64 k = pk(a, b);
    if (rFromIndex) {
      if (!(add ? S.N1.ins_sz(k32(a), b, na) : S.N1.rem_sz(k32(a), b, na))) return false;
      if (add) { if (knownState) S.N1r.ins_absent_sz(k32(b), a, nb); else S.N1r.ins_sz(k32(b), a, nb); }
      else S.N1r.rem_sz(k32(b), a, nb);
      return true;
    }
    if (knownState) { if (!(add ? S.R.insert_if_absent(k, 0) : S.R.erase(k))) return false; }
    else if ((S.R.get(k) != NONE) == add) return false;
    if (!knownState) { if (add) S.R.set(k, 0); else S.R.erase(k); }

    if (add) {
      if (knownState) { S.N1.ins_absent_sz(k32(a), b, na); S.N1r.ins_absent_sz(k32(b), a, nb); }
      else { S.N1.ins_sz(k32(a), b, na); S.N1r.ins_sz(k32(b), a, nb); }
    } else { S.N1.rem_sz(k32(a), b, na); S.N1r.rem_sz(k32(b), a, nb); }
    return true;
  }
  void upd_R(int s, i32 a, i32 b, bool add, size_t na, size_t nb) {
    Side& S = sd[s]; CURRENT_WK(W_BASE, 3);
    u32 vt = vget(s, a, b);
    if (vt != NONE) {
      Vtx& X = S.V[vt]; X.act = add;
      if (s == 0) {
        if (!X.high) {
          for (i32 e : X.hadj.el) { u32 q = (u32)E[(u32)e].q; setMem(sd[1].V[q].Alo, e, add); anch_q(q); }
        }
        anch_p(vt);
      } else {
        bool hi = X.high;
        for (i32 e : (hi ? X.hadj : X.adj).el) { u32 p = (u32)E[(u32)e].p; setMem(hi ? sd[0].V[p].Ahi : sd[0].V[p].Alo, e, add); anch_p(p); }
        anch_q(vt);
      }
    }

    const bool classReuse = (g_opts & OPT_CLASS_REUSE) != 0;
    bool aHeavy = false, bIn = false;
    const ISet* r2 = S.R2H.get(k32(b)); const ISet* r3 = S.R3H.get(k32(a));
    aHeavy = S.aH.contains(a); bIn = (classReuse || !aHeavy) && S.bH.contains(b);
    bool bHeavy = !aHeavy && bIn;
    int k = aHeavy ? 0 : (bHeavy ? 2 : 1);
    i32 key = k == 2 ? b : a, item = k == 2 ? a : b;
    auto sy = [&](u32 h) { sync_k(h, s, a, b, k, key, item, add); };
    if (aHeavy) { if (r2) for (i32 h : r2->el) { CURRENT_WK(W_INC, 1); sy((u32)h); } }
    else if (bHeavy) { if (r3) for (i32 h : r3->el) { CURRENT_WK(W_INC, 1); sy((u32)h); } }
    else if (r2 && r3) {
      const ISet *sm = r2->size() <= r3->size() ? r2 : r3, *lg = sm == r2 ? r3 : r2;
      for (i32 h : sm->el) { CURRENT_WK(W_INC, 1); if (lg->contains(h)) sy((u32)h); }
    }
    if (classReuse) {
      check_cls_known(s, a, true, na, aHeavy); check_cls_known(s, b, false, nb, bIn);
    } else { check_cls(s, a, true); check_cls(s, b, false); }
  }

  void upd_xel(int s, char kind, i32 x, i32 el, bool add, u32 sid, long pos) {
    Side& S = sd[s]; CURRENT_WK(W_BASE, 1);
    MapOfSets<ISet>& M = kind == 'A' ? S.A : S.B;
    const bool knownState = (g_opts & OPT_KNOWN_STATE) != 0, rFromIndex = (g_opts & OPT_R_FROM_INDEX) != 0;
    u32 h = hmap.get(k32(x));

    if (add && h == NONE && (g_opts & OPT_PROMOTE_FAST) && (double)(deg(x) + 1) > 2 * psi) { to_heavy_ins(x, M, el); return; }
    const ISet* upd = nullptr;
    if (knownState) {
      size_t sz = 0;
      if (add) { ISet* q = nullptr; M.ins_probed(k32(x), el, sid, sz, &q); upd = q; }
      else { M.erase_probed(sid, k32(x), pos, sz); upd = sz ? &M.pool[sid] : nullptr; }
    } else if (add) M.ins(k32(x), el); else M.rem(k32(x), el);
    if (h != NONE) H[h].deg += add ? 1 : -1;
    size_t dg1 = 0;
    if (h != NONE) {
      if (kind == 'B') {
        i32 b = el; CURRENT_WK(W_INC, 1);
        if (add) S.R2H.ins(k32(b), (i32)h); else S.R2H.rem(k32(b), (i32)h);
        const ISet* nr = S.N1r.get(k32(b));
        const ISet* Ax = S.A.get(k32(x));
        auto rin = [&](i32 a) { return rFromIndex ? nr->contains(a) : S.R.get(pk(a, b)) != NONE; };
        if (nr) {
          auto s0 = [&](i32 a) { setitem(h, s, 0, a, b, add, (int)(Ax && Ax->contains(a))); };
          if (S.aH.size() <= nr->size()) { for (i32 a : S.aH.el) if (rin(a)) s0(a); }
          else { for (i32 a : nr->el) if (S.aH.contains(a)) s0(a); }
        }
        if (!S.bH.contains(b)) {
          if ((g_opts & OPT_SKIP_NOOP) && nr) {
            if (Ax && Ax->size() < nr->size()) {
              for (i32 a : Ax->el) { if (!S.aH.contains(a) && rin(a)) { setitem(h, s, 1, a, b, add); } }
            } else if (Ax) {
              for (i32 a : nr->el) { if (!S.aH.contains(a) && Ax->contains(a)) { setitem(h, s, 1, a, b, add); } }
            }
          } else
          if (nr) for (i32 a : nr->el) if (!S.aH.contains(a)) {
            setitem(h, s, 1, a, b, add && Ax && Ax->contains(a));
          }
        }
        else { refreshS(h, s, 2, b, (int)add); }
      } else {
        i32 a = el; CURRENT_WK(W_INC, 1);
        if (add) S.R3H.ins(k32(a), (i32)h); else S.R3H.rem(k32(a), (i32)h);
        if (S.aH.contains(a)) { refreshS(h, s, 0, a, (int)add); }
        else {
          const ISet* n1 = S.N1.get(k32(a));
          const ISet* Bx = S.B.get(k32(x));
          if ((g_opts & OPT_SKIP_NOOP) && n1) {
            auto k2 = [&](i32 b) { setitem(h, s, 2, b, a, add, (int)(Bx && Bx->contains(b))); };
            if (S.bH.size() < n1->size()) { for (i32 b : S.bH.el) { if (n1->contains(b)) k2(b); } }
            else { for (i32 b : n1->el) { if (S.bH.contains(b)) k2(b); } }
            if (Bx) {
              if (Bx->size() < n1->size()) { for (i32 b : Bx->el) { if (n1->contains(b) && !S.bH.contains(b)) { setitem(h, s, 1, a, b, add); } } }
              else { for (i32 b : n1->el) { if (!S.bH.contains(b) && Bx->contains(b)) { setitem(h, s, 1, a, b, add); } } }
            }
          } else
          if (n1) for (i32 b : n1->el) {
            bool inb = Bx && Bx->contains(b);
            if (S.bH.contains(b)) setitem(h, s, 2, b, a, add, (int)inb);
            else setitem(h, s, 1, a, b, add && inb);
          }
        }
      }
    } else {
      const ISet *A0, *B0, *A1, *Bn;
      A0 = (knownState && kind == 'A') ? upd : S.A.get(k32(x)); B0 = (knownState && kind == 'B') ? upd : S.B.get(k32(x));
      A1 = sd[1 - s].A.get(k32(x)); Bn = sd[1 - s].B.get(k32(x));
      dg1 = (A0 ? A0->size() : 0) + (B0 ? B0->size() : 0) + (A1 ? A1->size() : 0) + (Bn ? Bn->size() : 0);
      if (A1 && Bn) {
        const bool prefetch = (g_opts & OPT_PREFETCH) != 0;
        if (prefetch) {
          const int o = 1 - s;
          pf_f.clear(); pf_o.clear(); pf_e.clear();
          auto outer = [&](auto fn) { if (kind == 'B') { if (A0) for (i32 a : A0->el) fn(a, el); } else { if (B0) for (i32 b : B0->el) fn(el, b); } };
          outer([&](i32 a, i32 b) { sd[s].vmap.prefetch_head(pk(a, b)); });
          for (i32 oa : A1->el) for (i32 ob : Bn->el) sd[o].vmap.prefetch_head(pk(oa, ob));
          outer([&](i32 a, i32 b) { u32 v = vget(s, a, b); pf_f.push_back(v); if (v != NONE) __builtin_prefetch(&sd[s].V[v]); });
          for (i32 oa : A1->el) for (i32 ob : Bn->el) { u32 v = vget(o, oa, ob); pf_o.push_back(v); if (v != NONE) __builtin_prefetch(&sd[o].V[v]); }
          for (u32 vf : pf_f) if (vf != NONE) for (u32 vo : pf_o) if (vo != NONE) emap.prefetch_head(s == 0 ? pk((i32)vf, (i32)vo) : pk((i32)vo, (i32)vf));
          for (u32 vf : pf_f) for (u32 vo : pf_o) {
            u32 e = NONE;
            if (vf != NONE && vo != NONE) {
              e = emap.get(s == 0 ? pk((i32)vf, (i32)vo) : pk((i32)vo, (i32)vf));
              if (e != NONE) { __builtin_prefetch(&E[e]); __builtin_prefetch((const char*)&E[e] + 63); }
            }
            pf_e.push_back(e);
          }
          size_t ei = 0;
          for (u32 vf : pf_f) for (u32 vo : pf_o) {
            u32 e = pf_e[ei++];
            if (e == NONE) continue;
            const Edge& Ed = E[e];
            if (Ed.nw != 1 && !Ed.W.el.empty()) __builtin_prefetch(Ed.W.el.data());
            u32 vp = s == 0 ? vf : vo, vq = s == 0 ? vo : vf;
            __builtin_prefetch(sd[0].V[vp].adj.el.data()); __builtin_prefetch(sd[1].V[vq].adj.el.data());
          }
          relax(pf_f); relax(pf_o); relax(pf_e);
        }

        vo_scr.clear(); bool reuse = false; size_t oi = 0;
        auto go = [&](i32 cpa, i32 cpb) {
          u32 vf = add ? vres(s, cpa, cpb) : vget(s, cpa, cpb);
          oi = 0;
          for (i32 oa : A1->el) for (i32 ob : Bn->el) {
            u32 vo;
            if (reuse) { vo = vo_scr[oi++]; }
            else { vo = add ? vres(1 - s, oa, ob) : vget(1 - s, oa, ob); if (prefetch) vo_scr.push_back(vo); }
            u32 vp = s == 0 ? vf : vo, vq = s == 0 ? vo : vf;
            if (add) w_add_v(x, vp, vq); else w_del_v(x, vp, vq);
          }
          reuse = prefetch;
        };
        if (kind == 'B') { if (A0) for (i32 a : A0->el) go(a, el); }
        else { if (B0) for (i32 b : B0->el) go(el, b); }
        if (prefetch) relax(vo_scr);
      }
    }

    if (g_opts & OPT_CLASS_REUSE) {
      if (h != NONE) {
        if (g_opts & OPT_DEGREE_FIELD) dg1 = (size_t)H[h].deg; else dg1 = deg(x);
      }
      check_x_known(x, h, dg1);
    } else if ((g_opts & OPT_DEGREE_FIELD) && h != NONE) check_x_hdeg(x, (size_t)H[h].deg);
    else check_x(x);
  }
  void post() {
    if ((!P.psiFixed || !P.thetaFixed) && !(N0 / 4.0 <= Nt() && Nt() <= 2.0 * N0)) { rebuild(); return; }
    if (!P.muFixed && !(m >= m0 / 4 && m <= 2 * m0)) {
      c.mu_rebuilds++; Ctx g(ctx, CTX_MU); CURRENT_WK(W_MU, m);
      reclassG();
    }
  }
  void reclassG() {
    m0 = std::max<i64>(1, m);
    mu = P.muFixed ? P.mu : std::max(1.0, std::sqrt((double)m0));
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];

      for (auto& en : S.vmap.ent) { Vtx& v = S.V[en.val]; v.high = (double)v.adj.size() > mu; v.hadj.clear(); v.Alo.clear(); v.Ahi.clear(); v.inA1 = v.inA2 = false; }
      S.anch1.clear(); S.anch2.clear();
    }
    for (size_t i = 0; i < emap.ent.size(); i++) fix_edge(emap.ent[i].val, true);
  }
  void release_derived() {
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];
      release(S.V); release(S.vfree); S.vmap.clear();
      S.aH.clear(); S.bH.clear(); S.anch1.clear(); S.anch2.clear();
      S.R2H = MapOfSets<ISet>(); S.R3H = MapOfSets<ISet>();
      for (int k = 0; k < 3; k++) { release(S.pool[k]); release(S.lfree[k]); }
    }
    release(E); release(efree); emap.clear(); release(H); release(hfree); hmap.clear(); heavyL.clear(); XH.clear();
    release(mark); release(scrH); release(scrN);
  }
  void rebuild() {
    c.global_rebuilds++; Ctx g(ctx, CTX_GLB);
    release_derived();
    std::vector<Tup> t; t.reserve((size_t)Nraw);
    static const int relR[2] = {1, 5}, relB[2] = {2, 6}, relA[2] = {3, 4};
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];
      if (g_opts & OPT_R_FROM_INDEX) S.N1.each([&](u64 a, const ISet& st) { for (i32 b : st.el) t.push_back({relR[s], (i32)(u32)a, b}); });
      else for (auto& sl : S.R.ent) t.push_back({relR[s], (i32)(sl.key >> 32), (i32)(u32)sl.key});
      S.B.each([&](u64 x, const ISet& st) { for (i32 b : st.el) t.push_back({relB[s], b, (i32)(u32)x}); });
      S.A.each([&](u64 x, const ISet& st) { for (i32 a : st.el) t.push_back({relA[s], (i32)(u32)x, a}); });
    }
    CURRENT_WK(W_GLB, t.size());
    bulk(t);
  }
  void load(const std::vector<Tup>& t) { bulk(t); }
  void bulk(const std::vector<Tup>& t) {
    reset();
    std::vector<i32> xs; FMap seen;
    size_t nab = 0; for (auto& tp : t) nab += relinfo(tp[0]).kind != 'R'; xs.reserve(nab);

    for (auto& tp : t) {
      RelInfo ri = relinfo(tp[0]); Side& S = sd[ri.s];
      if (ri.kind == 'R') {
        bool fresh;
        if (g_opts & OPT_R_FROM_INDEX) fresh = S.N1.ins(k32(tp[1]), tp[2]);
        else { fresh = S.R.set(pk(tp[1], tp[2]), 0); if (fresh) S.N1.ins(k32(tp[1]), tp[2]); }
        if (fresh) { S.N1r.ins(k32(tp[2]), tp[1]); Nraw++; }
      } else {
        i32 x, el; xel(tp[0], tp[1], tp[2], x, el);
        if ((ri.kind == 'A' ? S.A : S.B).ins(k32(x), el)) { Nraw++; if (seen.set(k32(x), 0)) xs.push_back(x); }
      }
    }

    N0 = Nt(); thresholds();

    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];
      S.N1.each([&](u64 a, const ISet& st) { if ((double)st.size() > theta) S.aH.insert((i32)(u32)a); });
      S.N1r.each([&](u64 b, const ISet& st) { if ((double)st.size() > theta) S.bH.insert((i32)(u32)b); });
    }

    std::vector<u32> hv; hv.reserve(xs.size());

    for (i32 x : xs) {
      if ((double)deg(x) > psi) hv.push_back(newHeavy(x));
      else {
        auto pair2 = [&](u32 vp, u32 vq) {
          u64 ek = pk((i32)vp, (i32)vq); u32 ed = emap.get(ek);
          Wtot++;
          if (ed != NONE) { E[ed].winsert(x); return; }
          pool_limit(E.size()); ed = (u32)E.size(); E.emplace_back();
          Edge& Eg = E[ed]; Eg.p = (i32)vp; Eg.q = (i32)vq; Eg.alive = true; Eg.wbirth(x);
          emap.set(ek, ed); sd[0].V[vp].adj.insert((i32)ed); sd[1].V[vq].adj.insert((i32)ed); m++;
        };
        forPairsV(x, true, pair2);
      }
    }

    reclassG();

    for (u32 h : hv) build_x(h);
  }

  struct LIt {
    int k = 0; size_t si = 0, ii = 0;
    const u64* p2 = nullptr; size_t p2n = 0; const std::vector<u32>* sv[2] = {nullptr, nullptr}; const std::vector<List>* pl[2] = {nullptr, nullptr};
    void init(Copair* g, u32 h, int s) {
      k = 0; si = ii = 0;
      HSide& hs = g->H[h].s[s]; Side& S = g->sd[s];
      p2 = hs.P2.el.data(); p2n = hs.P2.el.size(); sv[0] = &hs.S[0]; sv[1] = &hs.S[1]; pl[0] = &S.pool[0]; pl[1] = &S.pool[2];
    }
    bool next(i32& u, i32& v) {
      while (k < 3) {
        if (k == 1) {
          if (ii < p2n) { u64 p = p2[ii++]; u = (i32)(p >> 32); v = (i32)(u32)p; return true; }
          k++; si = 0; ii = 0; continue;
        }
        const std::vector<u32>& Sv = *sv[k >> 1];
        if (si < Sv.size()) {
          const List& L = (*pl[k >> 1])[Sv[si]];
          if (ii < L.items.size()) {
            i32 it = L.items.el[ii++];
            if (k == 2) { u = it; v = L.key; } else { u = L.key; v = it; }
            return true;
          }
          si++; ii = 0;
        } else { k++; si = 0; ii = 0; }
      }
      return false;
    }
  };
  struct Enum {
    Copair* g; int ph = 0; size_t i = 0, j = 0, k = 0, xi = 0; u32 h = 0; LIt L, R; i32 la = 0, lb = 0; bool haveL = false;
    explicit Enum(Copair& c_) : g(&c_) {}
    bool start(i32* o) { ph = 0; i = j = k = xi = 0; haveL = false; return next(o); }
    bool next(i32* o) {
      while (ph < 3) {
        const ISet& an = ph == 2 ? g->sd[0].anch2 : (ph == 1 ? g->sd[1].anch1 : g->sd[0].anch1);
        if (i >= an.size()) { ph++; i = j = k = 0; continue; }
        Vtx& V = g->sd[ph == 1 ? 1 : 0].V[an.el[i]];
        const ISet& lst = ph == 2 ? V.Ahi : V.Alo;
        if (j >= lst.size()) { i++; j = 0; k = 0; continue; }
        Edge& Ed = g->E[lst.el[j]];
        if (k >= Ed.wsize()) { j++; k = 0; continue; }
        Vtx& Pv = g->sd[0].V[Ed.p]; Vtx& Qv = g->sd[1].V[Ed.q];
        o[0] = Pv.a; o[1] = Pv.b; o[2] = Ed.wget(k++); o[3] = Qv.a; o[4] = Qv.b;

        return true;
      }
      for (;;) {
        if (!haveL) {
          if (xi >= g->XH.size()) return false;
          h = (u32)g->XH.el[xi];
          L.init(g, h, 0); R.init(g, h, 1);
          if (!L.next(la, lb)) { xi++; continue; }
          haveL = true;
        }
        i32 d, e;
        if (R.next(d, e)) { o[0] = la; o[1] = lb; o[2] = g->H[h].x; o[3] = d; o[4] = e; return true; }
        if (L.next(la, lb)) { R.init(g, h, 1); continue; }
        xi++; haveL = false;
      }
    }
  };

  static void addset(Cap& c, const ISet& s) { Cap q = s.cap(); c.items += s.size(); c.cap += q.cap; c.bytes += q.bytes; }
  Census capacity() const {
    Cap cR, cN1, cN1r, cA, cB, cR2, cR3, cCls, cAnch, cV, cVm, cE, cEm, cH, cHm, cL, cHeavy;
    for (int s = 0; s < 2; s++) {
      const Side& S = sd[s];
      cR += S.R.cap(); cN1 += S.N1.cap(); cN1r += S.N1r.cap(); cA += S.A.cap(); cB += S.B.cap(); cR2 += S.R2H.cap(); cR3 += S.R3H.cap();
      cCls += S.aH.cap(); cCls += S.bH.cap(); cAnch += S.anch1.cap(); cAnch += S.anch2.cap();
      for (auto& v : S.V) { addset(cV, v.adj); addset(cV, v.hadj); addset(cV, v.Alo); addset(cV, v.Ahi); cV.live += v.alive; }
      cV.total += S.V.size(); cV.bytes += S.V.capacity() * sizeof(Vtx) + S.vfree.capacity() * 4; cVm += S.vmap.cap();
      for (int k = 0; k < 3; k++) {
        for (auto& L : S.pool[k]) addset(cL, L.items);
        cL.total += S.pool[k].size(); cL.bytes += S.pool[k].capacity() * sizeof(List) + S.lfree[k].capacity() * 4;
      }
    }
    for (auto& e : E) { addset(cE, e.W); cE.items += e.nw == 1; cE.live += e.alive; }
    cE.total += E.size(); cE.bytes += E.capacity() * sizeof(Edge) + efree.capacity() * 4; cEm += emap.cap();
    for (auto& r : H) {
      cH.live += r.alive;
      for (int s = 0; s < 2; s++) for (int i = 0; i < 2; i++) {
        Cap q = r.s[s].lm[i].cap(); cH.items += q.items; cH.cap += q.cap; cH.bytes += q.bytes;
        cH.items += r.s[s].S[i].size(); cH.cap += r.s[s].S[i].capacity(); cH.bytes += r.s[s].S[i].capacity() * 4;
      }
      for (int s = 0; s < 2; s++) { Cap q = r.s[s].P2.cap(); cH.items += r.s[s].P2.size(); cH.cap += q.cap; cH.bytes += q.bytes; }
    }
    cH.total += H.size(); cH.bytes += H.capacity() * sizeof(HRec) + hfree.capacity() * 4; cHm += hmap.cap();
    cHeavy += heavyL.cap(); cHeavy += XH.cap();
    Census z; u64 tot = 0;
    auto emit = [&](const char* n, const Cap& c) {
      std::string p = n;
      z.push_back({p + ".items", c.items}); z.push_back({p + ".cap", c.cap}); z.push_back({p + ".bytes", c.bytes});
      z.push_back({p + ".slots_live", c.live}); z.push_back({p + ".slots_total", c.total}); tot += c.bytes;
    };
    emit("R", cR); emit("N1", cN1); emit("N1r", cN1r); emit("A", cA); emit("B", cB); emit("R2H", cR2); emit("R3H", cR3);
    emit("abclass", cCls); emit("anchors", cAnch); emit("Gvtx", cV); emit("vmap", cVm); emit("Gedge", cE); emit("emap", cEm);
    emit("Hrec", cH); emit("hmap", cHm); emit("Hlists", cL); emit("heavy_sets", cHeavy);
    z.push_back({"total_bytes", tot}); z.push_back({"rehash_up", g_rehash_up}); z.push_back({"rehash_down", g_rehash_down});
    z.push_back({"hash_draws", g_hash_draws});
    return z;
  }
  Pairs params() const { return {{"psi", psi}, {"theta", theta}, {"mu", mu}, {"N0", N0}}; }
  Census counters() const {
    Census z = {{"x_to_heavy", c.x_to_heavy}, {"x_to_light", c.x_to_light}, {"a_to_heavy", c.a_to_heavy}, {"a_to_light", c.a_to_light},
                {"b_to_heavy", c.b_to_heavy}, {"b_to_light", c.b_to_light}, {"vertex_flips", c.vertex_flips},
                {"mu_rebuilds", c.mu_rebuilds}, {"global_rebuilds", c.global_rebuilds}};
    return z;
  }

  void audit() {
    using KS = std::set<u64>;
    std::set<i32> xs;
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];
      KS Rs, f1, f2;
      const bool rFromIndex = (g_opts & OPT_R_FROM_INDEX) != 0;
      if (rFromIndex) CURRENT_CHECK(S.R.size() == 0 && S.R.head.empty(), "R written under OPT_R_FROM_INDEX");
      else for (auto& sl : S.R.ent) Rs.insert(sl.key);
      S.N1.each([&](u64 a, const ISet& st) {
        CURRENT_CHECK(!st.empty(), "N1 empty"); st.check();
        for (i32 b : st.el) f1.insert(pk((i32)a, b));
        double d = (double)st.size(); bool h = S.aH.contains((i32)(u32)a);
        CURRENT_CHECK(h ? d >= theta / 2 : d <= 2 * theta, "a class");
      });
      S.N1r.each([&](u64 b, const ISet& st) {
        CURRENT_CHECK(!st.empty(), "N1r empty"); st.check();
        for (i32 a : st.el) f2.insert(pk(a, (i32)b));
        double d = (double)st.size(); bool h = S.bH.contains((i32)(u32)b);
        CURRENT_CHECK(h ? d >= theta / 2 : d <= 2 * theta, "b class");
      });
      CURRENT_CHECK(rFromIndex ? f1 == f2 : (Rs == f1 && Rs == f2), "R/N1/N1r");
      for (i32 a : S.aH.el) CURRENT_CHECK(S.N1.get(k32(a)), "aH not in N1");
      for (i32 b : S.bH.el) CURRENT_CHECK(S.N1r.get(k32(b)), "bH not in N1r");
      S.aH.check(); S.bH.check();
      S.R.check(); S.N1.check(); S.N1r.check(); S.A.check(); S.B.check(); S.R2H.check(); S.R3H.check(); S.vmap.check();
      for (auto& v : S.V) if (!v.alive) CURRENT_CHECK(deadok(v.adj, true) && deadok(v.hadj, true) && deadok(v.Alo, true) && deadok(v.Ahi, true) && !v.inA1 && !v.inA2, "dead vtx storage");
      for (int k = 0; k < 3; k++) for (u32 lid : S.lfree[k]) CURRENT_CHECK(deadok(S.pool[k][lid].items, true), "free list storage");
      for (auto* M : {&S.A, &S.B}) M->each([&](u64 x, const ISet& st) { CURRENT_CHECK(!st.empty(), "A/B empty"); st.check(); xs.insert((i32)(u32)x); });
    }
    i64 nsum = 0;
    for (int s = 0; s < 2; s++) nsum += (i64)rsize(sd[s]) + (i64)sd[s].A.total() + (i64)sd[s].B.total();
    CURRENT_CHECK(nsum == Nraw, "N");
    CURRENT_CHECK(hmap.size() == heavyL.size(), "hmap size"); heavyL.check();
    for (i32 h : heavyL.el) {
      CURRENT_CHECK(H[h].alive && hmap.get(k32(H[h].x)) == (u32)h, "hmap");
      CURRENT_CHECK(xs.count(H[h].x), "heavy x not in base");
    }
    for (i32 x : xs) {
      double d = (double)deg(x); bool hv = hmap.get(k32(x)) != NONE;
      if (hv) CURRENT_CHECK(d >= psi / 2 && d > 0, "heavy bound"); else CURRENT_CHECK(d <= 2 * psi, "light bound");
    }

    std::map<std::pair<u64, u64>, std::set<i32>> W;
    for (i32 x : xs) if (hmap.get(k32(x)) == NONE)
      forPairs(x, [&](i32 a, i32 b, i32 d, i32 e) { W[{pk(a, b), pk(d, e)}].insert(x); });
    std::map<std::pair<u64, u64>, std::set<i32>> Wa; i64 ne = 0, wt = 0;
    for (auto& Ed : E) if (Ed.alive) {
      ne++; Ed.wcheck(); wt += (i64)Ed.wsize();
      Vtx& P_ = sd[0].V[Ed.p]; Vtx& Q = sd[1].V[Ed.q];
      CURRENT_CHECK(P_.alive && Q.alive, "edge to dead vertex");
      CURRENT_CHECK(emap.get(pk(Ed.p, Ed.q)) != NONE && E[emap.get(pk(Ed.p, Ed.q))].alive, "emap");
      for (size_t k = 0; k < Ed.wsize(); k++) Wa[{pk(P_.a, P_.b), pk(Q.a, Q.b)}].insert(Ed.wget(k));
      CURRENT_CHECK(Ed.wsize() != 0, "empty W");
    }
    CURRENT_CHECK(W == Wa, "W"); CURRENT_CHECK(ne == m && (i64)W.size() == m && emap.size() == (size_t)m, "m");
    CURRENT_CHECK(wt == Wtot, "Wtot");

    std::map<u64, KS> adjL, adjR;
    for (auto& kv : W) { adjL[kv.first.first].insert(kv.first.second); adjR[kv.first.second].insert(kv.first.first); }
    auto vkey = [&](int s, u32 v) { return pk(sd[s].V[v].a, sd[s].V[v].b); };
    std::map<u64, u32> alive[2];
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s]; size_t na = 0;
      for (u32 v = 0; v < S.V.size(); v++) if (S.V[v].alive) { alive[s][vkey(s, v)] = v; na++; }
      auto& expAdj = s == 0 ? adjL : adjR;
      CURRENT_CHECK(na == expAdj.size() && S.vmap.size() == na, "vertex set");
      for (auto& kv : expAdj) {
        auto it = alive[s].find(kv.first); CURRENT_CHECK(it != alive[s].end(), "missing vertex");
        CURRENT_CHECK(S.vmap.get(kv.first) == it->second, "vmap");
      }
    }
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];
      for (auto& kv : alive[s]) {
        Vtx& X = S.V[kv.second]; u64 key = kv.first;
        X.adj.check(); X.hadj.check(); X.Alo.check(); X.Ahi.check();
        CURRENT_CHECK(X.act == inR(S, (i32)(key >> 32), (i32)(u32)key), "act");
        double d = (double)X.adj.size();
        CURRENT_CHECK(X.high ? d >= mu / 2 : d <= 2 * mu, "G class");
        KS ea, eh, el, ehi;
        for (u64 nk : (s == 0 ? adjL : adjR)[key]) {
          Vtx& Y = sd[1 - s].V[alive[1 - s].at(nk)];
          ea.insert(nk);
          if (Y.high) eh.insert(nk);
          if (s == 0) {
            if (Y.act && !Y.high) el.insert(nk);
            if (X.high && Y.high && Y.act) ehi.insert(nk);
          } else if (X.high && Y.act && !Y.high) el.insert(nk);
        }
        auto got = [&](const ISet& st) {
          KS r; for (i32 e : st.el) { Edge& Ed = E[e]; CURRENT_CHECK(Ed.alive, "dead edge in set");
            CURRENT_CHECK((s == 0 ? Ed.p : Ed.q) == (i32)kv.second, "edge owner");
            r.insert(s == 0 ? vkey(1, Ed.q) : vkey(0, Ed.p)); }
          return r; };
        CURRENT_CHECK(got(X.adj) == ea, "adj"); CURRENT_CHECK(got(X.hadj) == eh, "hadj");
        CURRENT_CHECK(got(X.Alo) == el, "AL/AR"); CURRENT_CHECK(got(X.Ahi) == ehi, "AH");
        bool a1 = X.act && !el.empty(), a2 = X.act && !ehi.empty();
        CURRENT_CHECK(S.anch1.contains((i32)kv.second) == a1, "anch1");
        CURRENT_CHECK(S.anch2.contains((i32)kv.second) == (s == 0 && a2), "anch2");
        CURRENT_CHECK(X.inA1 == S.anch1.contains((i32)kv.second) && X.inA2 == (s == 0 && S.anch2.contains((i32)kv.second)), "anchor flags");
      }
      S.anch1.check(); S.anch2.check();
      for (i32 v : S.anch1.el) CURRENT_CHECK(S.V[v].alive, "anch dead");
      for (i32 v : S.anch2.el) CURRENT_CHECK(S.V[v].alive, "anch dead");
    }

    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];
      std::map<u64, std::set<i32>> e2, e3;
      for (i32 h : heavyL.el) {
        i32 x = H[h].x;
        if (auto* B = S.B.get(k32(x))) for (i32 b : B->el) e2[k32(b)].insert(h);
        if (auto* A = S.A.get(k32(x))) for (i32 a : A->el) e3[k32(a)].insert(h);
      }
      std::map<u64, std::set<i32>> g2, g3;
      S.R2H.each([&](u64 k, const ISet& st) { st.check(); g2[k].insert(st.el.begin(), st.el.end()); });
      S.R3H.each([&](u64 k, const ISet& st) { st.check(); g3[k].insert(st.el.begin(), st.el.end()); });
      CURRENT_CHECK(e2 == g2, "R2H"); CURRENT_CHECK(e3 == g3, "R3H");
    }

    std::set<i32> xhExp;
    for (i32 h : heavyL.el) {
      HRec& r = H[h]; i32 x = r.x; i64 cnts[2];
      for (int s = 0; s < 2; s++) {
        Side& S = sd[s]; HSide& hs = r.s[s];
        std::set<i32> A, B;
        if (auto* q = S.A.get(k32(x))) A.insert(q->el.begin(), q->el.end());
        if (auto* q = S.B.get(k32(x))) B.insert(q->el.begin(), q->el.end());
        std::map<i32, std::set<i32>> l[3];
        for (i32 a : S.aH.el) { auto* n1 = S.N1.get(k32(a)); if (n1) for (i32 b : n1->el) if (B.count(b)) l[0][a].insert(b); }
        for (i32 a : A) if (!S.aH.contains(a)) { auto* n1 = S.N1.get(k32(a)); if (n1) for (i32 b : n1->el) if (B.count(b) && !S.bH.contains(b)) l[1][a].insert(b); }
        for (i32 b : S.bH.el) { auto* nr = S.N1r.get(k32(b)); if (nr) for (i32 a : nr->el) if (A.count(a) && !S.aH.contains(a)) l[2][b].insert(a); }
        i64 sum = 0;
        for (int k = 0; k < 3; k++) {
          if (k == 1) {
            std::set<u64> ex, gt;
            for (auto& kv : l[1]) for (i32 b : kv.second) ex.insert(pk(kv.first, b));
            hs.P2.check(); gt.insert(hs.P2.el.begin(), hs.P2.el.end());
            CURRENT_CHECK(ex == gt && gt.size() == hs.P2.size(), "P2");
            sum += (i64)hs.P2.size(); continue;
          }
          std::map<i32, std::set<i32>> got, exp;
          std::map<i32, i32> sg, se;
          for (auto& kv : l[k]) if (!kv.second.empty()) {
            exp[kv.first] = kv.second;
            bool in = k == 1 || (k == 0 && A.count(kv.first)) || (k == 2 && B.count(kv.first));
            if (in) se[kv.first] = (i32)kv.second.size();
          }
          for (auto& sl : hs.lm[li(k)].ent) {
            List& L = S.pool[k][sl.val]; L.items.check();
            CURRENT_CHECK(!L.items.empty() && k32(L.key) == sl.key, "list");
            got[L.key].insert(L.items.el.begin(), L.items.el.end());
          }
          CURRENT_CHECK(got == exp, "lists");
          for (size_t i = 0; i < hs.S[li(k)].size(); i++) {
            List& L = S.pool[k][hs.S[li(k)][i]];
            CURRENT_CHECK(L.posS == (i32)i && L.anchSz == (i32)L.items.size() && L.anchSz > 0, "anchor entry");
            sg[L.key] = L.anchSz;
          }
          CURRENT_CHECK(sg == se && sg.size() == hs.S[li(k)].size(), "S");
          for (auto& kv : sg) sum += kv.second;
        }
        i64 lx = 0; for (i32 a : A) for (i32 b : B) if (inR(S, a, b)) lx++;
        CURRENT_CHECK(hs.cnt == sum && sum == lx, "cnt");
        std::set<std::pair<i32, i32>> part; LIt it; it.init(this, h, s); i32 u, v; i64 nn = 0;
        while (it.next(u, v)) { part.insert({u, v}); nn++; CURRENT_CHECK(inR(S, u, v) && A.count(u) && B.count(v), "partition member"); }
        CURRENT_CHECK(nn == lx && (i64)part.size() == lx, "partition");
        cnts[s] = hs.cnt;
      }
      if (cnts[0] > 0 && cnts[1] > 0) xhExp.insert(h);
    }
    XH.check(); emap.check(); hmap.check();
    for (auto& Ed : E) if (!Ed.alive) CURRENT_CHECK(deadok(Ed.W, true) && Ed.nw == 0, "dead edge storage");
    for (auto& r : H) {
      if (!r.alive) for (int s = 0; s < 2; s++) for (int i = 0; i < 2; i++) CURRENT_CHECK(r.s[s].lm[i].cap().bytes == 0 && r.s[s].S[i].capacity() == 0 && r.s[s].P2.cap().bytes == 0, "dead heavy storage");
      else for (int s = 0; s < 2; s++) for (int i = 0; i < 2; i++) { r.s[s].lm[i].check(); CURRENT_CHECK(capok(r.s[s].S[i].capacity(), r.s[s].S[i].size()), "anchor array cap"); }
    }
    CURRENT_CHECK(std::set<i32>(XH.el.begin(), XH.el.end()) == xhExp, "XH");
    for (i32 h : heavyL.el) CURRENT_CHECK(H[h].inXH == XH.contains(h), "inXH flag");
    for (auto& r : H) if (!r.alive) CURRENT_CHECK(!r.inXH, "dead inXH");
    for (i32 h : heavyL.el) CURRENT_CHECK((size_t)H[h].deg == deg(H[h].x), "heavy deg");
  }
};

#ifdef BT_STATS
#define CURRENT_BWK(i) (++wk[i])
#else
#define CURRENT_BWK(i) ((void)0)
#endif
struct PerxKnownState {
  struct XRec { i32 x = 0; PSet L[2]; bool inXs = false; };
  struct SideB { FMap R; MapOfSets<ISet> A, B, Xa, Xb; };
  SideB sd[2];
  std::vector<XRec> X; std::vector<u32> xfree; FMap xmap; ISet Xs;
  i64 Nraw = 0;
  u64 wk[2] = {}, wtr[NCTX] = {};
  static constexpr int NWK = 2;
  static const char* wkname(int i) { return i ? "loop_iters" : "lset_ops"; }
  explicit PerxKnownState(Params p = Params()) { g_opts = p.opts; }
  i64 N() const { return Nraw; }
  void load(const std::vector<Tup>& t) { for (auto& tp : t) apply(+1, tp[0], tp[1], tp[2]); }

  u32 xres(i32 x, bool add) {
    u32 xi = xmap.get(k32(x));
    if (xi == NONE) {
      if (!add) return NONE;
      if (!xfree.empty()) { xi = xfree.back(); xfree.pop_back(); relax(xfree); } else { pool_limit(X.size()); xi = (u32)X.size(); X.emplace_back(); }
      X[xi].x = x; xmap.set(k32(x), xi);
    }
    return xi;
  }
  void lset_xi(int s, u32 xi, u64 pair, bool add) {
    XRec& r = X[xi];
    if (add) r.L[s].insert(pair); else r.L[s].erase(pair);
    bool both = !r.L[0].empty() && !r.L[1].empty();
    if (both == r.inXs) return;
    both ? Xs.insert((i32)xi) : Xs.erase((i32)xi);
    r.inXs = both;
  }

  void xfin(i32 x, u32 xi) {
    XRec& r = X[xi];
    if (r.L[0].empty() && r.L[1].empty()) {
      xmap.erase(k32(x));
      r.L[0].retire(); r.L[1].retire(); r.x = 0; r.inXs = false;
      xfree.push_back(xi);
    }
  }
  void lset_nc(int s, i32 x, u64 pair, bool add) {
    u32 xi = xres(x, add);
    if (xi == NONE) return;
    lset_xi(s, xi, pair, add); xfin(x, xi);
  }
  void lset(int s, i32 x, u64 pair, bool add) { CURRENT_BWK(0); lset_nc(s, x, pair, add); }

  struct XCur {
    PerxKnownState& g; i32 x; u32 xi = NONE; bool res = false;
    XCur(PerxKnownState& g_, i32 x_) : g(g_), x(x_) {}
    XCur(const XCur&) = delete; XCur& operator=(const XCur&) = delete;
    void ls(int s, u64 pair, bool add) {
      if (!res) { res = true; xi = g.xres(x, add); }
      if (xi != NONE) g.lset_xi(s, xi, pair, add);
    }
    ~XCur() { if (xi != NONE) g.xfin(x, xi); }
  };

  void base_M(MapOfSets<ISet>& M, MapOfSets<ISet>& Inv, i32 x, i32 el, bool add, u32 sid, long pos) {
    if (g_opts & OPT_KNOWN_STATE) {
      size_t sz = 0;
      if (add) { M.ins_probed(k32(x), el, sid, sz); Inv.ins_absent_sz(k32(el), x, sz); }
      else { M.erase_probed(sid, k32(x), pos, sz); Inv.rem_sz(k32(el), x, sz); }
    } else if (add) { M.ins(k32(x), el); Inv.ins(k32(el), x); }
    else { M.rem(k32(x), el); Inv.rem(k32(el), x); }
  }
  bool apply(int sign, int rel, i32 u, i32 v) {
    RelInfo ri = relinfo(rel); int s = ri.s; bool add = sign > 0; SideB& S = sd[s];
    const bool knownState = (g_opts & OPT_KNOWN_STATE) != 0;
    if (ri.kind == 'R') {
      if (knownState) { if (!(add ? S.R.insert_if_absent(pk(u, v), 0) : S.R.erase(pk(u, v)))) return true; }
      else { if ((S.R.get(pk(u, v)) != NONE) == add) return true; if (add) S.R.set(pk(u, v), 0); else S.R.erase(pk(u, v)); }
      const ISet *xa = S.Xa.get(k32(u)), *xb = S.Xb.get(k32(v));
      if (xa && xb) {
        const ISet *it = xa->size() <= xb->size() ? xa : xb, *oth = it == xa ? xb : xa;
        for (i32 x : it->el) { CURRENT_BWK(1); if (oth->contains(x)) lset(s, x, pk(u, v), add); }
      }
    } else {
      i32 x, el; xel(rel, u, v, x, el);

      const bool isB = ri.kind == 'B';
      MapOfSets<ISet>& M = isB ? S.B : S.A; MapOfSets<ISet>& Inv = isB ? S.Xb : S.Xa;
      u32 sid; long pos;
      pos = M.probe(k32(x), el, sid); if ((pos >= 0) == add) return true;
      base_M(M, Inv, x, el, add, sid, pos);
      XCur cur(*this, x);
      if (isB) { if (const ISet* A = S.A.get(k32(x))) for (i32 a : A->el) { CURRENT_BWK(1); if (S.R.get(pk(a, el)) != NONE) { CURRENT_BWK(0); cur.ls(s, pk(a, el), add); } } }
      else { if (const ISet* B = S.B.get(k32(x))) for (i32 b : B->el) { CURRENT_BWK(1); if (S.R.get(pk(el, b)) != NONE) { CURRENT_BWK(0); cur.ls(s, pk(el, b), add); } } }
    }
    Nraw += add ? 1 : -1;
    return false;
  }
  struct Enum {
    PerxKnownState* g; size_t xi = 0, i = 0, j = 0; bool have = false; u32 cur = 0;
    explicit Enum(PerxKnownState& b) : g(&b) {}
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
  void audit() { audit_impl([&](int s, i32 a, i32 b) { return sd[s].R.get(pk(a, b)) != NONE; }); }
  template <class F> void audit_impl(F inR) {
    std::set<i32> xs;
    for (int s = 0; s < 2; s++) {
      SideB& S = sd[s];
      S.A.each([&](u64 x, const ISet& st) { st.check(); xs.insert((i32)(u32)x);
        for (i32 a : st.el) { auto* q = S.Xa.get(k32(a)); CURRENT_CHECK(q && q->contains((i32)(u32)x), "Xa"); } });
      S.B.each([&](u64 x, const ISet& st) { st.check(); xs.insert((i32)(u32)x);
        for (i32 b : st.el) { auto* q = S.Xb.get(k32(b)); CURRENT_CHECK(q && q->contains((i32)(u32)x), "Xb"); } });
      CURRENT_CHECK(S.Xa.total() == S.A.total() && S.Xb.total() == S.B.total(), "inverse totals");
    }
    std::map<i32, std::set<u64>> exp[2];
    for (int s = 0; s < 2; s++) {
      SideB& S = sd[s];
      for (i32 x : xs) {
        auto *A = S.A.get(k32(x)); auto *B = S.B.get(k32(x));
        if (A && B) for (i32 a : A->el) for (i32 b : B->el) if (inR(s, a, b)) exp[s][x].insert(pk(a, b));
      }
    }
    std::map<i32, std::set<u64>> got[2]; std::set<i32> xsExp, xsGot;
    for (u32 xi = 0; xi < X.size(); xi++) if (xmap.get(k32(X[xi].x)) == xi) {
      for (int s = 0; s < 2; s++) { X[xi].L[s].check(); if (!X[xi].L[s].empty()) got[s][X[xi].x].insert(X[xi].L[s].el.begin(), X[xi].L[s].el.end()); }
      CURRENT_CHECK(!X[xi].L[0].empty() || !X[xi].L[1].empty(), "empty xrec");
      if (!X[xi].L[0].empty() && !X[xi].L[1].empty()) xsGot.insert(X[xi].x);
    }
    for (int s = 0; s < 2; s++) CURRENT_CHECK(got[s] == exp[s], "L");
    for (u32 xi = 0; xi < X.size(); xi++) {
      if (xmap.get(k32(X[xi].x)) == xi) CURRENT_CHECK(X[xi].inXs == Xs.contains((i32)xi), "inXs flag");
    }
    for (u32 f : xfree) CURRENT_CHECK(deadok(X[f].L[0], true) && deadok(X[f].L[1], true) && !X[f].inXs, "free xrec storage");
    for (auto& kv : exp[0]) if (exp[1].count(kv.first)) xsExp.insert(kv.first);
    std::set<i32> xsActual; for (i32 xi : Xs.el) xsActual.insert(X[xi].x);
    CURRENT_CHECK(xsActual == xsExp && xsGot == xsExp && xsActual.size() == Xs.size(), "X*");
    Xs.check();
  }
};

struct PerxKnownStateIndex : public PerxKnownState {
  struct Adj { MapOfSets<ISet> N1, N1r; };
  Adj ad[2];
  u64 wk[3] = {};
  static constexpr int NWK = 3;
  static const char* wkname(int i) { return i == 0 ? "lset_ops" : i == 1 ? "loop_iters" : "idx_ops"; }
  explicit PerxKnownStateIndex(Params p = Params()) : PerxKnownState(p) {}
  void load(const std::vector<Tup>& t) { for (auto& tp : t) apply(+1, tp[0], tp[1], tp[2]); }

  void lset(int s, i32 x, u64 pair, bool add) { CURRENT_BWK(0); lset_nc(s, x, pair, add); }
  bool apply(int sign, int rel, i32 u, i32 v) {
    RelInfo ri = relinfo(rel); int s = ri.s; bool add = sign > 0; SideB& S = sd[s]; Adj& D = ad[s];
    const bool knownState = (g_opts & OPT_KNOWN_STATE) != 0, rFromIndex = (g_opts & OPT_R_FROM_INDEX) != 0;
    if (ri.kind == 'R') {
      size_t sz = 0;
      if (rFromIndex) {
        if (!(add ? D.N1.ins_sz(k32(u), v, sz) : D.N1.rem_sz(k32(u), v, sz))) return true;
        if (add) { if (knownState) D.N1r.ins_absent_sz(k32(v), u, sz); else D.N1r.ins_sz(k32(v), u, sz); }
        else D.N1r.rem_sz(k32(v), u, sz);
      } else {
        if (knownState) { if (!(add ? S.R.insert_if_absent(pk(u, v), 0) : S.R.erase(pk(u, v)))) return true; }
        else { if ((S.R.get(pk(u, v)) != NONE) == add) return true; if (add) S.R.set(pk(u, v), 0); else S.R.erase(pk(u, v)); }
        if (add) {
          if (knownState) { D.N1.ins_absent_sz(k32(u), v, sz); D.N1r.ins_absent_sz(k32(v), u, sz); }
          else { D.N1.ins(k32(u), v); D.N1r.ins(k32(v), u); }
        } else { D.N1.rem(k32(u), v); D.N1r.rem(k32(v), u); }
      }
      CURRENT_BWK(2); CURRENT_BWK(2);
      const ISet *xa = S.Xa.get(k32(u)), *xb = S.Xb.get(k32(v));
      if (xa && xb) {
        const ISet *it = xa->size() <= xb->size() ? xa : xb, *oth = it == xa ? xb : xa;
        for (i32 x : it->el) { CURRENT_BWK(1); if (oth->contains(x)) lset(s, x, pk(u, v), add); }
      }
    } else {
      i32 x, el; xel(rel, u, v, x, el);

      const bool isB = ri.kind == 'B';
      MapOfSets<ISet>& M = isB ? S.B : S.A; MapOfSets<ISet>& Inv = isB ? S.Xb : S.Xa;
      u32 sid; long pos;
      pos = M.probe(k32(x), el, sid); if ((pos >= 0) == add) return true;
      base_M(M, Inv, x, el, add, sid, pos);
      if (isB) {
        const ISet* A = S.A.get(k32(x)); const ISet* nr = D.N1r.get(k32(el));
        XCur cur(*this, x);
        if (A && nr) {
          if (nr->size() < A->size()) { for (i32 a : nr->el) { CURRENT_BWK(1); if (A->contains(a)) { CURRENT_BWK(0); cur.ls(s, pk(a, el), add); } } }
          else for (i32 a : A->el) { CURRENT_BWK(1); if (rFromIndex ? nr->contains(a) : S.R.get(pk(a, el)) != NONE) { CURRENT_BWK(0); cur.ls(s, pk(a, el), add); } }
        }
      } else {
        const ISet* B = S.B.get(k32(x)); const ISet* nb = D.N1.get(k32(el));
        XCur cur(*this, x);
        if (B && nb) {
          if (nb->size() < B->size()) { for (i32 b : nb->el) { CURRENT_BWK(1); if (B->contains(b)) { CURRENT_BWK(0); cur.ls(s, pk(el, b), add); } } }
          else for (i32 b : B->el) { CURRENT_BWK(1); if (rFromIndex ? nb->contains(b) : S.R.get(pk(el, b)) != NONE) { CURRENT_BWK(0); cur.ls(s, pk(el, b), add); } }
        }
      }
    }
    Nraw += add ? 1 : -1;
    return false;
  }
  Census capacity() const {
    Census z = PerxKnownState::capacity(); Cap c;
    for (int s = 0; s < 2; s++) { c += ad[s].N1.cap(); c += ad[s].N1r.cap(); }
    std::string p = "adj_idx";
    z.push_back({p + ".items", c.items}); z.push_back({p + ".cap", c.cap}); z.push_back({p + ".bytes", c.bytes});
    z.push_back({p + ".slots_live", c.live}); z.push_back({p + ".slots_total", c.total});
    for (auto& kv : z) if (kv.first == "total_bytes") kv.second += c.bytes;
    return z;
  }
  bool inR2(int s, i32 a, i32 b) const {
    if (g_opts & OPT_R_FROM_INDEX) { const ISet* q = ad[s].N1.get(k32(a)); return q && q->contains(b); }
    return sd[s].R.get(pk(a, b)) != NONE;
  }
  void audit() {
    PerxKnownState::audit_impl([&](int s, i32 a, i32 b) { return inR2(s, a, b); });
    const bool rFromIndex = (g_opts & OPT_R_FROM_INDEX) != 0;
    for (int s = 0; s < 2; s++) {
      SideB& S = sd[s]; const Adj& D = ad[s]; u64 nR = rFromIndex ? D.N1.total() : (u64)S.R.size();
      D.N1.check(); D.N1r.check();
      if (rFromIndex) CURRENT_CHECK(S.R.size() == 0 && S.R.head.empty(), "R written under OPT_R_FROM_INDEX");
      CURRENT_CHECK(D.N1.total() == nR && D.N1r.total() == nR, "adj totals");
      D.N1.each([&](u64 a, const ISet& st) { for (i32 b : st.el) { CURRENT_CHECK(inR2(s, (i32)(u32)a, b), "N1 not in R"); auto* q = D.N1r.get(k32(b)); CURRENT_CHECK(q && q->contains((i32)(u32)a), "N1r"); } });
      D.N1r.each([&](u64 b, const ISet& st) { for (i32 a : st.el) { CURRENT_CHECK(inR2(s, a, (i32)(u32)b), "N1r not in R"); auto* q = D.N1.get(k32(a)); CURRENT_CHECK(q && q->contains((i32)(u32)b), "N1"); } });
    }
  }
};
}
