#pragma once
#include <memory>

#include "copair_fanout_base.hpp"

namespace bt {

struct USet {
  static constexpr u32 INL = 4;
  u32 n = 0, cap_ = INL;
  union { u32 in[INL]; u32* hp; };
  std::unique_ptr<DMap> pos;
  USet() { in[0] = in[1] = in[2] = in[3] = 0; }
  USet(USet&& o) noexcept : n(o.n), cap_(o.cap_), pos(std::move(o.pos)) { take(o); }
  USet& operator=(USet&& o) noexcept {
    if (this != &o) { release(); n = o.n; cap_ = o.cap_; pos = std::move(o.pos); take(o); }
    return *this;
  }
  USet(const USet&) = delete;
  USet& operator=(const USet&) = delete;
  ~USet() { if (cap_ > INL) delete[] hp; }
  void take(USet& o) {
    if (cap_ > INL) hp = o.hp; else for (u32 i = 0; i < n; i++) in[i] = o.in[i];
    o.n = 0; o.cap_ = INL;
  }
  void release() { if (cap_ > INL) delete[] hp; n = 0; cap_ = INL; pos.reset(); }
  u32* d() { return cap_ > INL ? hp : in; }
  const u32* d() const { return cap_ > INL ? hp : in; }
  u32 operator[](size_t i) const { return d()[i]; }
  u32 back() const { return d()[n - 1]; }
  size_t size() const { return n; }
  bool empty() const { return n == 0; }
  long find(u32 v) const {
    if (pos) { u32 p = pos->get(v); return p == NONE ? -1 : (long)p; }
    const u32* p = d();
    for (u32 i = 0; i < n; i++) if (p[i] == v) return (long)i;
    return -1;
  }
  bool contains(u32 v) const { return find(v) >= 0; }
  void grow() {
    u32 nc = cap_ * 2; u32* p = new u32[nc];
    const u32* s = d(); for (u32 i = 0; i < n; i++) p[i] = s[i];
    if (cap_ > INL) delete[] hp;
    hp = p; cap_ = nc;
  }
  bool insert(u32 v) {
    if (find(v) >= 0) return false;
    if (n == cap_) grow();
    d()[n++] = v;
    if (pos) pos->set(v, n - 1);
    else if (n > 16) { pos = std::make_unique<DMap>(); const u32* p = d(); for (u32 i = 0; i < n; i++) pos->set(p[i], i); }
    return true;
  }
  bool erase(u32 v) { long i = find(v); if (i < 0) return false; erase_at((u32)i); return true; }
  void erase_at(u32 i) {
    u32* p = d(); u32 v = p[i];
    if (pos) pos->erase(v);
    if (i != n - 1) { p[i] = p[n - 1]; if (pos) pos->set(p[i], i); }
    n--;
    if (pos && n <= 8) pos.reset();
    if (cap_ > INL && n <= cap_ / 4) {
      if (n <= INL) { u32 tmp[INL]; for (u32 k = 0; k < n; k++) tmp[k] = hp[k]; delete[] hp; cap_ = INL; for (u32 k = 0; k < n; k++) in[k] = tmp[k]; }
      else { u32 nc = cap_ / 2; u32* q = new u32[nc]; for (u32 k = 0; k < n; k++) q[k] = hp[k]; delete[] hp; hp = q; cap_ = nc; }
    }
  }
  Cap cap() const {
    Cap c; c.items = n; c.cap = cap_; c.bytes = cap_ > INL ? (u64)cap_ * 4 : 0;
    if (pos) { Cap q = pos->cap(); c.cap += q.cap; c.bytes += q.bytes + sizeof(DMap); }
    return c;
  }
  void check() const {
    BT_CHECK(n <= cap_ && (cap_ == INL || (cap_ & (cap_ - 1)) == 0), "uset cap");
    if (pos) {
      BT_CHECK(pos->size() == n, "uset pos size");
      for (u32 i = 0; i < n; i++) BT_CHECK(pos->get(d()[i]) == i, "uset pos");
      pos->check();
    } else BT_CHECK(n <= 16, "uset unhashed");
    std::set<u32> u(d(), d() + n);
    BT_CHECK(u.size() == n, "uset dup");
  }
};

struct PRec {
  USet adj;
  std::vector<u32> hadj;
  i32 a = 0, b = 0;
  u32 hpos = NONE, apos = NONE;
  bool high = false, act = false, alive = false;
};
struct WEdge { USet W; u32 p = 0, q = 0, pos[2] = {NONE, NONE}; bool alive = false; };
struct LRec { USet id[2], pl[2]; i32 x = 0; u32 xi = NONE, nh[2] = {0, 0}, xpos = NONE; bool alive = false; };
struct XRec { i32 x = 0; u32 deg = 0, h = NONE, lr = NONE; bool alive = false; };
struct HXr { PSet P[2]; };

struct CopairFanout : CopairBase {
  FanParams FP;
  double tau = 1;
  FanCtr f;
  std::vector<PRec> PV[2]; std::vector<u32> pfree[2]; FMap pmap[2];
  std::vector<WEdge> WE; std::vector<u32> wfree; FMap wmap;
  std::vector<LRec> LRv; std::vector<u32> lfree;
  std::vector<XRec> XV; std::vector<u32> xfree; FMap xmap;
  std::vector<HXr> HXv;
  std::vector<u32> xll, hi[2], anc[2];

  explicit CopairFanout(FanParams p) : CopairBase(p), FP(p) {}

  void thresh() {
    thresholds();
    tau = FP.tauFixed ? FP.tau : std::max(1.0, std::sqrt(psi * N0) / 8.0);
  }
  void clearLight() {
    for (int s = 0; s < 2; s++) { release(PV[s]); release(pfree[s]); pmap[s].clear(); release(hi[s]); release(anc[s]); }
    release(WE); release(wfree); wmap.clear(); release(LRv); release(lfree);
    release(XV); release(xfree); xmap.clear(); release(xll); release(HXv);
  }

  template <class F> void forSide(int s, i32 x, F fn) {
    const ISet* A = sd[s].A.get(k32(x)); const ISet* B = sd[s].B.get(k32(x));
    if (!A || !B) return;
    for (i32 a : A->el) for (i32 b : B->el) fn(a, b);
  }

  static void dadd(std::vector<u32>& d, u32& pos, u32 id) { pos = (u32)d.size(); d.push_back(id); }
  void hiAdd(int s, u32 v) { dadd(hi[s], PV[s][v].hpos, v); }
  void hiDel(int s, u32 v) {
    u32 i = PV[s][v].hpos, last = hi[s].back();
    hi[s][i] = last; PV[s][last].hpos = i; hi[s].pop_back(); PV[s][v].hpos = NONE; relax(hi[s]);
  }
  void ancDel(int s, u32 v) {
    u32 i = PV[s][v].apos, last = anc[s].back();
    anc[s][i] = last; PV[s][last].apos = i; anc[s].pop_back(); PV[s][v].apos = NONE; relax(anc[s]);
  }
  void anchor(int s, u32 v) {
    PRec& V = PV[s][v];
    bool want = V.high && V.act && !V.hadj.empty();
    if (want == (V.apos != NONE)) return;
    if (want) dadd(anc[s], V.apos, v); else ancDel(s, v);
  }
  void setH(int s, u32 v, u32 e, bool in) {
    WEdge& Ed = WE[e]; PRec& V = PV[s][v]; u32& ps = Ed.pos[s];
    if (in) { if (ps == NONE) { ps = (u32)V.hadj.size(); V.hadj.push_back(e); } }
    else if (ps != NONE) {
      u32 last = V.hadj.back();
      V.hadj[ps] = last; WE[last].pos[s] = ps; V.hadj.pop_back(); WE[e].pos[s] = NONE; relax(V.hadj);
    }
  }
  void fixedge(u32 e, bool ex) {
    WEdge& Ed = WE[e]; PRec& Pv = PV[0][Ed.p]; PRec& Qv = PV[1][Ed.q];
    u32 p = Ed.p, q = Ed.q;
    setH(1, q, e, ex && Qv.high && Pv.act);
    setH(0, p, e, ex && Pv.high && !Qv.high && Qv.act);
    anchor(0, p); anchor(1, q);
  }
  void wadd(i32 x, u32 vp, u32 vq) {
    WK(W_WIT, 1);
    u64 ek = pk((i32)vp, (i32)vq); u32 e = wmap.get(ek);
    if (e != NONE) { if (WE[e].W.insert((u32)x)) Wtot++; return; }
    WK(W_EDGE, 1);
    if (!wfree.empty()) { e = wfree.back(); wfree.pop_back(); relax(wfree); } else { pool_limit(WE.size()); e = (u32)WE.size(); WE.emplace_back(); }
    WEdge& Ed = WE[e]; Ed.p = vp; Ed.q = vq; Ed.alive = true; Ed.W.insert((u32)x); Wtot++;
    wmap.set(ek, e); m++;
    fixedge(e, true);
  }
  void wdel(i32 x, u32 vp, u32 vq) {
    WK(W_WIT, 1);
    u64 ek = pk((i32)vp, (i32)vq); u32 e = wmap.get(ek);
    if (e == NONE) fail("wdel: missing edge");
    if (!WE[e].W.erase((u32)x)) fail("wdel: missing witness");
    Wtot--;
    if (!WE[e].W.empty()) return;
    WK(W_EDGE, 1);
    fixedge(e, false);
    wmap.erase(ek); WE[e] = WEdge(); wfree.push_back(e); relax(wfree); m--;
  }

  u32 pnew(int s, i32 a, i32 b) {
    u32 v;
    if (!pfree[s].empty()) { v = pfree[s].back(); pfree[s].pop_back(); relax(pfree[s]); } else { pool_limit(PV[s].size()); v = (u32)PV[s].size(); PV[s].emplace_back(); }
    PRec& r = PV[s][v];
    r.a = a; r.b = b; r.high = false; r.alive = true; r.act = sd[s].R.get(pk(a, b)) != NONE;
    pmap[s].set(pk(a, b), v);
    return v;
  }
  void pdel(int s, u32 v) {
    PRec& r = PV[s][v];
    pmap[s].erase(pk(r.a, r.b)); PV[s][v] = PRec(); pfree[s].push_back(v); relax(pfree[s]);
  }
  u32 xnew(i32 x) {
    u32 i;
    if (!xfree.empty()) { i = xfree.back(); xfree.pop_back(); relax(xfree); } else { pool_limit(XV.size()); i = (u32)XV.size(); XV.emplace_back(); }
    XRec& r = XV[i]; r.x = x; r.alive = true; r.deg = 0; r.h = NONE; r.lr = NONE;
    xmap.set(k32(x), i);
    return i;
  }
  void xdel(u32 i) { xmap.erase(k32(XV[i].x)); XV[i] = XRec(); xfree.push_back(i); relax(xfree); }
  u32 lmake(u32 xi) {
    u32 l;
    if (!lfree.empty()) { l = lfree.back(); lfree.pop_back(); relax(lfree); } else { pool_limit(LRv.size()); l = (u32)LRv.size(); LRv.emplace_back(); }
    LRec& r = LRv[l]; r.xi = xi; r.x = XV[xi].x; r.alive = true; XV[xi].lr = l;
    return l;
  }
  void xllDel(u32 l) {
    u32 i = LRv[l].xpos, last = xll.back();
    xll[i] = last; LRv[last].xpos = i; xll.pop_back(); LRv[l].xpos = NONE; relax(xll);
  }
  void xllRef(u32 l) {
    LRec& L = LRv[l];
    bool want = !L.pl[0].empty() && !L.pl[1].empty();
    if (want == (L.xpos != NONE)) return;
    if (want) dadd(xll, L.xpos, l); else xllDel(l);
  }
  void lfreeIf(u32 l) {
    LRec& L = LRv[l];
    if (!L.id[0].empty() || !L.id[1].empty()) return;
    if (L.xpos != NONE) xllDel(l);
    XV[L.xi].lr = NONE; LRv[l] = LRec(); lfree.push_back(l); relax(lfree);
  }
  void plset(int s, u32 l, u32 v, bool in) {
    LRec& L = LRv[l];
    if (in) L.pl[s].insert(v); else L.pl[s].erase(v);
    xllRef(l);
  }

  void up(int s, u32 v) {
    f.pair_to_high[s]++;
    PRec& V = PV[s][v]; int o = 1 - s;
    V.high = true; hiAdd(s, v);
    for (u32 i = 0; i < V.adj.n; i++) {
      u32 l = V.adj[i]; LRec& L = LRv[l]; i32 x = L.x;
      if (V.act) plset(s, l, v, false);
      L.nh[s]++;
      for (u32 j = 0; j < L.id[o].n; j++) {
        u32 w = L.id[o][j];
        if (!PV[o][w].high) wadd(x, s == 0 ? v : w, s == 0 ? w : v);
        else if (s == 1) { u32 e = wmap.get(pk((i32)w, (i32)v)); if (e != NONE) fixedge(e, true); }
      }
    }
    anchor(s, v);
  }
  void down(int s, u32 v) {
    f.pair_to_low[s]++;
    PRec& V = PV[s][v]; int o = 1 - s;
    V.high = false; hiDel(s, v);
    for (u32 i = 0; i < V.adj.n; i++) {
      u32 l = V.adj[i]; LRec& L = LRv[l]; i32 x = L.x;
      L.nh[s]--;
      for (u32 j = 0; j < L.id[o].n; j++) {
        u32 w = L.id[o][j];
        if (!PV[o][w].high) wdel(x, s == 0 ? v : w, s == 0 ? w : v);
        else if (s == 1) { u32 e = wmap.get(pk((i32)w, (i32)v)); if (e != NONE) fixedge(e, true); }
      }
      if (V.act) plset(s, l, v, true);
    }
    anchor(s, v);
  }
  void classTest(int s, u32 v) {
    PRec& V = PV[s][v]; double fo = (double)V.adj.size();
    if (V.high) { if (fo < tau / 2 || V.adj.empty()) down(s, v); }
    else if (fo > 2 * tau) up(s, v);
  }
  void wloop(int s, u32 l, u32 v, bool add) {
    LRec& L = LRv[l]; const USet& oth = L.id[1 - s]; int o = 1 - s; i32 x = L.x;
    bool all = PV[s][v].high;
    if (!all && !L.nh[o]) return;
    for (u32 j = 0; j < oth.n; j++) {
      u32 w = oth[j];
      if (!all && !PV[o][w].high) continue;
      if (add) wadd(x, s == 0 ? v : w, s == 0 ? w : v); else wdel(x, s == 0 ? v : w, s == 0 ? w : v);
    }
  }
  void pairAdd(int s, u32 l, i32 a, i32 b) {
    u32 v = pmap[s].get(pk(a, b)); if (v == NONE) v = pnew(s, a, b);
    PRec& V = PV[s][v]; LRec& L = LRv[l];
    V.adj.insert(l); L.id[s].insert(v);
    if (V.high) L.nh[s]++; else if (V.act) plset(s, l, v, true);
    wloop(s, l, v, true);
    classTest(s, v);
  }
  void removeV(int s, u32 l, u32 v) {
    PRec& V = PV[s][v]; LRec& L = LRv[l];
    if (!V.adj.erase(l)) fail("pairRemove: x not in fan-out");
    if (!L.id[s].erase(v)) fail("pairRemove: pair not in x");
    if (V.high) L.nh[s]--; else if (V.act) plset(s, l, v, false);
    wloop(s, l, v, false);
    classTest(s, v);
    if (PV[s][v].adj.empty()) {
      if (!PV[s][v].hadj.empty()) fail("pairRemove: stale witnesses");
      pdel(s, v);
    }
  }
  void t3(int s, char kind, u32 xi, i32 x, i32 el, bool add) {
    Side& S = sd[s];
    const ISet* sib = (kind == 'B' ? S.A : S.B).get(k32(x));
    if (!sib || sib->empty()) return;
    u32 l = XV[xi].lr;
    if (l == NONE) { if (!add) fail("t3: missing light record"); l = lmake(xi); }
    for (i32 y : sib->el) {
      i32 a = kind == 'B' ? y : el, b = kind == 'B' ? el : y;
      if (add) pairAdd(s, l, a, b);
      else {
        u32 v = pmap[s].get(pk(a, b)); if (v == NONE) fail("pairRemove: missing pair");
        removeV(s, l, v);
      }
    }
    lfreeIf(l);
  }

  u32 newHeavy(i32 x) {
    u32 h = CopairBase::newHeavy(x);
    if (HXv.size() < H.size()) HXv.resize(H.size());
    return h;
  }
  void p2set(u32 h, int s, i32 a, i32 b, bool ok) {
    HSide& hs = H[h].s[s]; PSet& P = HXv[h].P[s]; size_t old = P.size();
    if (ok) P.insert(pk(a, b)); else P.erase(pk(a, b));
    if (P.size() != old) {
      bool was = hs.cnt > 0; hs.cnt += (i64)P.size() - (i64)old;
      if (was != (hs.cnt > 0)) upd_XH(h);
    }
  }

  void to_heavy(u32 xi) {
    c.x_to_heavy++; Ctx g(ctx, CTX_X); WK(W_XTR, 1);
    i32 x = XV[xi].x; u32 l = XV[xi].lr;
    if (l != NONE) {
      for (int s = 0; s < 2; s++) while (!LRv[l].id[s].empty()) removeV(s, l, LRv[l].id[s].back());
      lfreeIf(l);
    }
    u32 h = newHeavy(x); XV[xi].h = h; build_x(h);
  }
  void to_light(u32 xi) {
    c.x_to_light++; Ctx g(ctx, CTX_X); WK(W_XTR, 1);
    i32 x = XV[xi].x; u32 h = XV[xi].h;
    drop_x(h); HXv[h] = HXr();
    hmap.erase(k32(x)); heavyL.erase((i32)h); H[h] = HRec(); hfree.push_back(h); XV[xi].h = NONE;
    u32 l = NONE;
    for (int s = 0; s < 2; s++) {
      const ISet* A = sd[s].A.get(k32(x)); const ISet* B = sd[s].B.get(k32(x));
      if (!A || !B) continue;
      if (l == NONE) l = lmake(xi);
      for (i32 a : A->el) for (i32 b : B->el) pairAdd(s, l, a, b);
    }
  }
  void check_x(u32 xi) {
    XRec& X = XV[xi]; double d = (double)X.deg;
    if (X.h != NONE) { if (d < psi / 2 || X.deg == 0) to_light(xi); }
    else if (d > 2 * psi) to_heavy(xi);
    if (XV[xi].deg == 0 && XV[xi].h == NONE) xdel(xi);
  }

  void touch(u32 h, int s, int k, u32 lid, int pre = -1) {
    Side& S = sd[s]; HRec& r = H[h]; HSide& hs = r.s[s]; List& L = S.pool[k][lid];
    bool inS = !L.items.empty();
    if (inS && k != 1) {
      if (pre >= 0) inS = pre != 0;
      else { const ISet* q = (k == 0) ? S.A.get(k32(r.x)) : S.B.get(k32(r.x)); inS = q && q->contains(L.key); }
    }
    i32 nw = inS ? (i32)L.items.size() : 0, old = L.anchSz;
    if (nw > 0 && L.posS < 0) { L.posS = (i32)hs.S[k].size(); hs.S[k].push_back(lid); }
    else if (nw == 0 && L.posS >= 0) {
      auto& v = hs.S[k]; u32 last = v.back();
      v[L.posS] = last; S.pool[k][last].posS = L.posS; v.pop_back(); relax(v); L.posS = -1;
    }
    L.anchSz = nw;
    if (nw != old) { bool was = hs.cnt > 0; hs.cnt += nw - old; if (was != (hs.cnt > 0)) upd_XH(h); }
    if (L.items.empty()) { hs.lm[k].erase(k32(L.key)); S.pool[k][lid] = List(); S.lfree[k].push_back(lid); }
  }
  void refreshS(u32 h, int s, int k, i32 key, int pre = -1) {
    u32 lid = H[h].s[s].lm[k].get(k32(key));
    if (lid != NONE) touch(h, s, k, lid, pre);
  }
  void setitem(u32 h, int s, int k, i32 key, i32 item, bool ok, int pre = -1) {
    WK(W_SYNC, 1);
    if (k == 1) { p2set(h, s, key, item, ok); return; }
    Side& S = sd[s]; HSide& hs = H[h].s[s];
    u32 lid = hs.lm[k].get(k32(key));
    if (ok) {
      if (lid == NONE) { lid = newList(S, k); S.pool[k][lid].key = key; hs.lm[k].set(k32(key), lid); }
      S.pool[k][lid].items.insert(item);
    } else {
      if (lid == NONE) return;
      S.pool[k][lid].items.erase(item);
    }
    touch(h, s, k, lid, pre);
  }
  void sync(u32 h, int s, i32 a, i32 b) {
    Side& S = sd[s]; i32 key, item;
    int k = slotOf(S, a, b, key, item);
    bool ok = S.R.get(pk(a, b)) != NONE; int pre = -1;
    if (ok) {
      i32 x = H[h].x;
      const ISet* Bx = S.B.get(k32(x)); const ISet* Ax = S.A.get(k32(x));
      bool inb = Bx && Bx->contains(b), ina = Ax && Ax->contains(a);
      ok = (k == 0) ? inb : (k == 2 ? ina : (ina && inb));
      pre = k == 0 ? (int)ina : (k == 2 ? (int)inb : -1);
    }
    setitem(h, s, k, key, item, ok, pre);
  }
  void discard_slot(u32 h, int s, i32 a, i32 b) {
    i32 key, item; int k = slotOf(sd[s], a, b, key, item);
    setitem(h, s, k, key, item, false);
  }
  void build_x(u32 h) {
    i32 x = H[h].x;
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];
      const ISet* B = S.B.get(k32(x)); const ISet* A = S.A.get(k32(x));
      if (B) for (i32 a : S.aH.el) {
        const ISet* n1 = S.N1.get(k32(a)); if (!n1) continue;
        if (B->size() <= n1->size()) { for (i32 b : B->el) if (n1->contains(b)) sync(h, s, a, b); }
        else { for (i32 b : n1->el) if (B->contains(b)) sync(h, s, a, b); }
      }
      if (A) for (i32 a : A->el) if (!S.aH.contains(a)) {
        const ISet* n1 = S.N1.get(k32(a));
        if (n1) for (i32 b : n1->el) sync(h, s, a, b);
      }
      if (B) for (i32 b : B->el) { WK(W_INC, 1); S.R2H.ins(k32(b), (i32)h); }
      if (A) for (i32 a : A->el) { WK(W_INC, 1); S.R3H.ins(k32(a), (i32)h); }
    }
  }
  void flip_class(int s, i32 v, bool isa) {
    Side& S = sd[s];
    const ISet* nb = isa ? S.N1.get(k32(v)) : S.N1r.get(k32(v));
    scrN.clear(); if (nb) scrN = nb->el;
    scrH.clear(); epoch++;
    auto addset = [&](const ISet* q) { if (q) for (i32 h : q->el) if (mark[h] != epoch) { mark[h] = epoch; scrH.push_back(h); } };
    if (isa) { addset(S.R3H.get(k32(v))); for (i32 n : scrN) addset(S.R2H.get(k32(n))); }
    else { addset(S.R2H.get(k32(v))); for (i32 n : scrN) addset(S.R3H.get(k32(n))); }
    WK(W_ABTR, scrH.size() * scrN.size() * 2);
    for (i32 h : scrH) for (i32 n : scrN) { if (isa) discard_slot((u32)h, s, v, n); else discard_slot((u32)h, s, n, v); }
    ISet& Hs = isa ? S.aH : S.bH;
    if (!Hs.erase(v)) Hs.insert(v);
    for (i32 h : scrH) for (i32 n : scrN) { if (isa) sync((u32)h, s, v, n); else sync((u32)h, s, n, v); }
    release(scrH); release(scrN);
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
      if ((S.R.get(pk(u, v)) != NONE) == add) return true;
      upd_R(s, u, v, add);
    } else {
      i32 x, el; xel(rel, u, v, x, el);
      MapOfSets<ISet>& M = ri.kind == 'A' ? S.A : S.B;
      u32 mi = M.idx.get(k32(x));
      if ((mi != NONE && M.pool[mi].contains(el)) == add) return true;
      upd_xel(s, ri.kind, x, el, add, mi);
    }
    Nraw += add ? 1 : -1;
    post();
    return false;
  }
  void upd_R(int s, i32 a, i32 b, bool add) {
    Side& S = sd[s]; WK(W_BASE, 3);
    if (add) { S.R.set(pk(a, b), 0); S.N1.ins(k32(a), b); S.N1r.ins(k32(b), a); }
    else { S.R.erase(pk(a, b)); S.N1.rem(k32(a), b); S.N1r.rem(k32(b), a); }
    u32 vt = pmap[s].get(pk(a, b));
    if (vt != NONE) {
      PRec& X = PV[s][vt]; X.act = add;
      (s == 0 ? (X.high ? f.r1_high : f.r1_low) : (X.high ? f.r5_high : f.r5_low))++;
      if (!X.high) for (u32 i = 0; i < X.adj.n; i++) plset(s, X.adj[i], vt, add);
      for (u32 o : hi[1 - s]) {
        u32 e = wmap.get(s == 0 ? pk((i32)vt, (i32)o) : pk((i32)o, (i32)vt));
        if (e != NONE) fixedge(e, true);
      }
      anchor(s, vt);
    }
    const ISet* r2 = S.R2H.get(k32(b)); const ISet* r3 = S.R3H.get(k32(a));
    if (S.aH.contains(a)) {
      if (r2) for (i32 h : r2->el) { WK(W_INC, 1); setitem((u32)h, s, 0, a, b, add, r3 && r3->contains(h) ? 1 : 0); }
    } else if (S.bH.contains(b)) {
      if (r3) for (i32 h : r3->el) { WK(W_INC, 1); setitem((u32)h, s, 2, b, a, add, r2 && r2->contains(h) ? 1 : 0); }
    } else if (r2 && r3) {
      const ISet *sm = r2->size() <= r3->size() ? r2 : r3, *lg = sm == r2 ? r3 : r2;
      for (i32 h : sm->el) { WK(W_INC, 1); if (lg->contains(h)) setitem((u32)h, s, 1, a, b, add); }
    }
    check_cls(s, a, true); check_cls(s, b, false);
  }
  void upd_xel(int s, char kind, i32 x, i32 el, bool add, u32 mi) {
    Side& S = sd[s]; WK(W_BASE, 1);
    MapOfSets<ISet>& M = kind == 'A' ? S.A : S.B;
    if (add) { if (mi == NONE) M.ins(k32(x), el); else M.pool[mi].insert(el); }
    else {
      M.pool[mi].erase(el);
      if (M.pool[mi].empty()) { M.pool[mi].clear(); M.idx.erase(k32(x)); M.fr.push_back(mi); relax(M.fr); }
    }
    u32 xi = xmap.get(k32(x));
    if (xi == NONE) { if (!add) fail("upd_xel: missing x record"); xi = xnew(x); }
    XV[xi].deg += add ? 1 : -1;
    u32 h = XV[xi].h;
    if (h != NONE) {
      if (kind == 'B') {
        i32 b = el; WK(W_INC, 1);
        if (add) S.R2H.ins(k32(b), (i32)h); else S.R2H.rem(k32(b), (i32)h);
        const ISet* nr = S.N1r.get(k32(b)); const ISet* Ax = S.A.get(k32(x));
        if (nr) {
          if (S.aH.size() <= nr->size()) { for (i32 a : S.aH.el) if (S.R.get(pk(a, b)) != NONE) setitem(h, s, 0, a, b, add, Ax && Ax->contains(a) ? 1 : 0); }
          else { for (i32 a : nr->el) if (S.aH.contains(a)) setitem(h, s, 0, a, b, add, Ax && Ax->contains(a) ? 1 : 0); }
        }
        if (!S.bH.contains(b)) { if (nr) for (i32 a : nr->el) if (!S.aH.contains(a)) setitem(h, s, 1, a, b, add && Ax && Ax->contains(a)); }
        else refreshS(h, s, 2, b, add ? 1 : 0);
      } else {
        i32 a = el; WK(W_INC, 1);
        if (add) S.R3H.ins(k32(a), (i32)h); else S.R3H.rem(k32(a), (i32)h);
        if (S.aH.contains(a)) refreshS(h, s, 0, a, add ? 1 : 0);
        else {
          const ISet* n1 = S.N1.get(k32(a)); const ISet* Bx = S.B.get(k32(x));
          if (n1) for (i32 b : n1->el) {
            bool inb = Bx && Bx->contains(b);
            if (S.bH.contains(b)) setitem(h, s, 2, b, a, add, inb ? 1 : 0);
            else setitem(h, s, 1, a, b, add && inb);
          }
        }
      }
    } else t3(s, kind, xi, x, el, add);
    check_x(xi);
  }
  void post() {
    if ((!P.psiFixed || !P.thetaFixed || !FP.tauFixed) && !(N0 / 4.0 <= Nt() && Nt() <= 2.0 * N0)) rebuild();
  }
  void release_all() {
    release_derived(); clearLight();
  }
  void rebuild() {
    c.global_rebuilds++; Ctx g(ctx, CTX_GLB);
    release_all();
    std::vector<Tup> t; t.reserve((size_t)Nraw);
    static const int relR[2] = {1, 5}, relB[2] = {2, 6}, relA[2] = {3, 4};
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];
      for (auto& sl : S.R.ent) t.push_back({relR[s], (i32)(sl.key >> 32), (i32)(u32)sl.key});
      S.B.each([&](u64 x, const ISet& st) { for (i32 b : st.el) t.push_back({relB[s], b, (i32)(u32)x}); });
      S.A.each([&](u64 x, const ISet& st) { for (i32 a : st.el) t.push_back({relA[s], (i32)(u32)x, a}); });
    }
    WK(W_GLB, t.size());
    bulk(t);
  }
  void load(const std::vector<Tup>& t) { bulk(t); }
  void bulk(const std::vector<Tup>& t) {
    reset(); clearLight();
    std::vector<i32> xs; FMap seen;
    for (auto& tp : t) {
      RelInfo ri = relinfo(tp[0]); Side& S = sd[ri.s];
      if (ri.kind == 'R') {
        if (S.R.set(pk(tp[1], tp[2]), 0)) { S.N1.ins(k32(tp[1]), tp[2]); S.N1r.ins(k32(tp[2]), tp[1]); Nraw++; }
      } else {
        i32 x, el; xel(tp[0], tp[1], tp[2], x, el);
        if ((ri.kind == 'A' ? S.A : S.B).ins(k32(x), el)) { Nraw++; if (seen.set(k32(x), 0)) xs.push_back(x); }
      }
    }
    N0 = Nt(); thresh();
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s];
      S.N1.each([&](u64 a, const ISet& st) { if ((double)st.size() > theta) S.aH.insert((i32)(u32)a); });
      S.N1r.each([&](u64 b, const ISet& st) { if ((double)st.size() > theta) S.bH.insert((i32)(u32)b); });
    }
    std::vector<u32> hv, lights;
    for (i32 x : xs) {
      u32 xi = xnew(x); size_t dg = deg(x); XV[xi].deg = (u32)dg;
      if ((double)dg > psi) { u32 h = newHeavy(x); XV[xi].h = h; hv.push_back(h); }
      else {
        lights.push_back(xi); u32 l = NONE;
        for (int s = 0; s < 2; s++) {
          const ISet* A = sd[s].A.get(k32(x)); const ISet* B = sd[s].B.get(k32(x));
          if (!A || !B) continue;
          if (l == NONE) l = lmake(xi);
          for (i32 a : A->el) for (i32 b : B->el) {
            u32 v = pmap[s].get(pk(a, b)); if (v == NONE) v = pnew(s, a, b);
            PV[s][v].adj.insert(l); LRv[l].id[s].insert(v);
          }
        }
      }
    }
    for (int s = 0; s < 2; s++) for (auto& en : pmap[s].ent)
      if ((double)PV[s][en.val].adj.size() > tau) { PV[s][en.val].high = true; hiAdd(s, en.val); }
    for (u32 xi : lights) {
      u32 l = XV[xi].lr; if (l == NONE) continue;
      LRec& L = LRv[l];
      for (int s = 0; s < 2; s++) for (u32 j = 0; j < L.id[s].n; j++) {
        u32 v = L.id[s][j];
        if (PV[s][v].high) L.nh[s]++; else if (PV[s][v].act) L.pl[s].insert(v);
      }
      xllRef(l);
      for (u32 i = 0; i < L.id[0].n; i++) {
        u32 p = L.id[0][i]; bool ph = PV[0][p].high;
        if (!ph && !L.nh[1]) continue;
        for (u32 j = 0; j < L.id[1].n; j++) { u32 q = L.id[1][j]; if (ph || PV[1][q].high) wadd(L.x, p, q); }
      }
    }
    for (u32 h : hv) build_x(h);
  }

  struct HIt {
    CopairFanout* g = nullptr; u32 h = 0; int s = 0, k = 0; size_t si = 0, ii = 0;
    void init(CopairFanout* g_, u32 h_, int s_) { g = g_; h = h_; s = s_; k = 0; si = ii = 0; }
    bool next(i32& u, i32& v) {
      HSide& hs = g->H[h].s[s]; Side& S = g->sd[s];
      while (k < 3) {
        if (k == 1) {
          const PSet& P = g->HXv[h].P[s];
          if (ii < P.size()) { u64 p = P.el[ii++]; u = (i32)(p >> 32); v = (i32)(u32)p; return true; }
          k++; si = ii = 0; continue;
        }
        auto& Sv = hs.S[k];
        if (si < Sv.size()) {
          List& L = S.pool[k][Sv[si]];
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
    CopairFanout* g; int ph = 0; size_t i = 0, j = 0, k = 0, xi = 0; u32 h = 0; HIt L, R; i32 la = 0, lb = 0; bool haveL = false;
    explicit Enum(CopairFanout& c_) : g(&c_) {}
    bool start(i32* o) { ph = 0; i = j = k = xi = 0; haveL = false; return next(o); }
    bool next(i32* o) {
      while (ph < 3) {
        if (ph == 0) {
          if (i >= g->xll.size()) { ph++; i = j = k = 0; continue; }
          LRec& r = g->LRv[g->xll[i]];
          if (j >= r.pl[0].size()) { i++; j = 0; k = 0; continue; }
          if (k >= r.pl[1].size()) { j++; k = 0; continue; }
          PRec& Pv = g->PV[0][r.pl[0][j]]; PRec& Qv = g->PV[1][r.pl[1][k++]];
          o[0] = Pv.a; o[1] = Pv.b; o[2] = r.x; o[3] = Qv.a; o[4] = Qv.b;
          return true;
        }
        int s = ph == 1 ? 1 : 0;
        const std::vector<u32>& an = g->anc[s];
        if (i >= an.size()) { ph++; i = j = k = 0; continue; }
        PRec& V = g->PV[s][an[i]];
        if (j >= V.hadj.size()) { i++; j = 0; k = 0; continue; }
        WEdge& Ed = g->WE[V.hadj[j]];
        if (k >= Ed.W.size()) { j++; k = 0; continue; }
        PRec& Pv = g->PV[0][Ed.p]; PRec& Qv = g->PV[1][Ed.q];
        o[0] = Pv.a; o[1] = Pv.b; o[2] = (i32)Ed.W[k++]; o[3] = Qv.a; o[4] = Qv.b;
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

  static void addu(Cap& c, const USet& s) { Cap q = s.cap(); c.items += s.size(); c.cap += q.cap; c.bytes += q.bytes; }
  static void addv(Cap& c, const std::vector<u32>& v) { c.items += v.size(); c.cap += v.capacity(); c.bytes += v.capacity() * 4; }
  Census capacity() const {
    Census z = CopairBase::capacity();
    Cap cp, cw, cl, cx, cs;
    for (int s = 0; s < 2; s++) {
      for (auto& r : PV[s]) { addu(cp, r.adj); addv(cp, r.hadj); cp.live += r.alive; }
      cp.total += PV[s].size(); cp.bytes += PV[s].capacity() * sizeof(PRec) + pfree[s].capacity() * 4 + pmap[s].cap().bytes;
      addv(cs, hi[s]); addv(cs, anc[s]);
    }
    for (auto& e : WE) { addu(cw, e.W); cw.live += e.alive; }
    cw.total = WE.size(); cw.bytes += WE.capacity() * sizeof(WEdge) + wfree.capacity() * 4 + wmap.cap().bytes;
    for (auto& r : LRv) { addu(cl, r.id[0]); addu(cl, r.id[1]); addu(cl, r.pl[0]); addu(cl, r.pl[1]); cl.live += r.alive; }
    cl.total = LRv.size(); cl.bytes += LRv.capacity() * sizeof(LRec) + lfree.capacity() * 4;
    for (auto& r : XV) cx.live += r.alive;
    cx.total = XV.size(); cx.bytes += XV.capacity() * sizeof(XRec) + xfree.capacity() * 4 + xmap.cap().bytes;
    addv(cs, xll);
    Cap ch; for (auto& r : HXv) for (int s = 0; s < 2; s++) { Cap q = r.P[s].cap(); ch.items += r.P[s].size(); ch.bytes += q.bytes; }
    ch.bytes += HXv.capacity() * sizeof(HXr);
    cs.bytes += ch.bytes; cs.items += ch.items;
    u64 add = cp.bytes + cw.bytes + cl.bytes + cx.bytes + cs.bytes;
    for (auto& kv : z) if (kv.first == "total_bytes") kv.second += add;
    z.push_back({"pairs.items", cp.items}); z.push_back({"pairs.bytes", cp.bytes}); z.push_back({"pairs.slots_live", cp.live}); z.push_back({"pairs.slots_total", cp.total});
    z.push_back({"witness_edges.items", cw.items}); z.push_back({"witness_edges.bytes", cw.bytes}); z.push_back({"witness_edges.slots_live", cw.live}); z.push_back({"witness_edges.slots_total", cw.total});
    z.push_back({"light_x.items", cl.items}); z.push_back({"light_x.bytes", cl.bytes}); z.push_back({"light_x.slots_live", cl.live}); z.push_back({"light_x.slots_total", cl.total});
    z.push_back({"x_records.bytes", cx.bytes}); z.push_back({"x_records.slots_live", cx.live});
    z.push_back({"fanout_sets.items", cs.items}); z.push_back({"fanout_sets.bytes", cs.bytes});
    return z;
  }
  Pairs params() const { return {{"psi", psi}, {"theta", theta}, {"tau", tau}, {"N0", N0}}; }
  Census counters() const {
    return {{"x_to_heavy", c.x_to_heavy}, {"x_to_light", c.x_to_light}, {"a_to_heavy", c.a_to_heavy}, {"a_to_light", c.a_to_light},
            {"b_to_heavy", c.b_to_heavy}, {"b_to_light", c.b_to_light}, {"global_rebuilds", c.global_rebuilds},
            {"left_to_high", f.pair_to_high[0]}, {"left_to_low", f.pair_to_low[0]}, {"right_to_high", f.pair_to_high[1]},
            {"right_to_low", f.pair_to_low[1]}, {"r1_toggle_low", f.r1_low}, {"r1_toggle_high", f.r1_high},
            {"r5_toggle_low", f.r5_low}, {"r5_toggle_high", f.r5_high}};
  }

  void audit_b() {
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
      BT_CHECK(e2 == g2, "R2H"); BT_CHECK(e3 == g3, "R3H");
    }
    BT_CHECK(HXv.size() >= H.size(), "heavy extra size");
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
        for (int k = 0; k < 3; k += 2) {
          std::map<i32, std::set<i32>> got, exp;
          std::map<i32, i32> sg, se;
          for (auto& kv : l[k]) if (!kv.second.empty()) {
            exp[kv.first] = kv.second;
            bool in = (k == 0 && A.count(kv.first)) || (k == 2 && B.count(kv.first));
            if (in) se[kv.first] = (i32)kv.second.size();
          }
          for (auto& sl : hs.lm[k].ent) {
            List& L = S.pool[k][sl.val]; L.items.check();
            BT_CHECK(!L.items.empty() && k32(L.key) == sl.key, "list");
            got[L.key].insert(L.items.el.begin(), L.items.el.end());
          }
          BT_CHECK(got == exp, "lists");
          for (size_t i = 0; i < hs.S[k].size(); i++) {
            List& L = S.pool[k][hs.S[k][i]];
            BT_CHECK(L.posS == (i32)i && L.anchSz == (i32)L.items.size() && L.anchSz > 0, "anchor entry");
            sg[L.key] = L.anchSz;
          }
          BT_CHECK(sg == se && sg.size() == hs.S[k].size(), "S");
          for (auto& kv : sg) sum += kv.second;
        }
        BT_CHECK(hs.lm[1].size() == 0 && hs.S[1].empty(), "unused middle lists");
        const PSet& P = HXv[h].P[s]; P.check();
        std::set<u64> pe, pg(P.el.begin(), P.el.end());
        for (auto& kv : l[1]) for (i32 b : kv.second) pe.insert(pk(kv.first, b));
        BT_CHECK(pe == pg && P.size() == pe.size(), "pair set");
        sum += (i64)P.size();
        i64 lx = 0; for (i32 a : A) for (i32 b : B) if (S.R.get(pk(a, b)) != NONE) lx++;
        BT_CHECK(hs.cnt == sum && sum == lx, "cnt");
        std::set<std::pair<i32, i32>> part; HIt it; it.init(this, h, s); i32 u, v; i64 nn = 0;
        while (it.next(u, v)) { part.insert({u, v}); nn++; BT_CHECK(S.R.get(pk(u, v)) != NONE && A.count(u) && B.count(v), "partition member"); }
        BT_CHECK(nn == lx && (i64)part.size() == lx, "partition");
        cnts[s] = hs.cnt;
      }
      if (cnts[0] > 0 && cnts[1] > 0) xhExp.insert(h);
    }
    XH.check(); hmap.check();
    for (auto& r : H) {
      if (!r.alive) for (int s = 0; s < 2; s++) for (int k = 0; k < 3; k++) BT_CHECK(r.s[s].lm[k].cap().bytes == 0 && r.s[s].S[k].capacity() == 0, "dead heavy storage");
      else for (int s = 0; s < 2; s++) for (int k = 0; k < 3; k++) { r.s[s].lm[k].check(); BT_CHECK(r.s[s].S[k].capacity() <= std::max<size_t>(4, 4 * r.s[s].S[k].size()), "anchor array cap"); }
    }
    for (u32 h = 0; h < H.size(); h++) if (!H[h].alive) BT_CHECK(HXv[h].P[0].cap().bytes + HXv[h].P[1].cap().bytes == 0, "dead heavy pair sets");
    BT_CHECK(std::set<i32>(XH.el.begin(), XH.el.end()) == xhExp, "XH");
  }

  void audit() { std::set<i32> xs = audit_a(); audit_light(xs); audit_b(); }
  void audit_light(const std::set<i32>& xs) {
    std::map<u64, std::set<i32>> Xp[2];
    std::set<i32> light;
    for (i32 x : xs) if (hmap.get(k32(x)) == NONE) {
      light.insert(x);
      for (int s = 0; s < 2; s++) forSide(s, x, [&](i32 a, i32 b) { Xp[s][pk(a, b)].insert(x); });
    }
    auto vkey = [&](int s, u32 v) { return pk(PV[s][v].a, PV[s][v].b); };
    BT_CHECK(xmap.size() == xs.size(), "xmap size"); xmap.check();
    std::map<i32, u32> lrOf;
    for (i32 x : xs) {
      u32 xi = xmap.get(k32(x));
      BT_CHECK(xi != NONE && XV[xi].alive && XV[xi].x == x, "xrec");
      XRec& X = XV[xi];
      BT_CHECK(X.deg == deg(x), "xrec deg");
      BT_CHECK(X.h == hmap.get(k32(x)), "xrec heavy");
      bool pairs = false;
      for (int s = 0; s < 2; s++) { const ISet* A = sd[s].A.get(k32(x)); const ISet* B = sd[s].B.get(k32(x)); if (A && B) pairs = true; }
      BT_CHECK((X.lr != NONE) == (X.h == NONE && pairs), "xrec light record");
      if (X.lr != NONE) { BT_CHECK(LRv[X.lr].alive && LRv[X.lr].xi == xi && LRv[X.lr].x == x, "lrec link"); lrOf[x] = X.lr; }
    }
    { size_t nxa = 0; for (auto& X : XV) nxa += X.alive; BT_CHECK(nxa == xs.size() && nxa + xfree.size() == XV.size(), "xrec slots"); }
    std::map<u64, u32> alive[2];
    for (int s = 0; s < 2; s++) {
      size_t na = 0;
      for (u32 v = 0; v < PV[s].size(); v++) {
        PRec& V = PV[s][v];
        if (V.alive) { alive[s][vkey(s, v)] = v; na++; }
        else BT_CHECK(V.adj.cap().bytes + V.hadj.capacity() == 0 && V.hpos == NONE && V.apos == NONE, "dead pair storage");
      }
      BT_CHECK(na == Xp[s].size() && pmap[s].size() == na && na + pfree[s].size() == PV[s].size(), "pair vertex set");
      std::set<u32> hiExp;
      for (auto& kv : Xp[s]) {
        auto it = alive[s].find(kv.first); BT_CHECK(it != alive[s].end(), "missing pair vertex");
        u32 v = it->second; PRec& V = PV[s][v];
        BT_CHECK(pmap[s].get(kv.first) == v, "pmap");
        V.adj.check();
        std::set<u32> expl; for (i32 x : kv.second) expl.insert(lrOf.at(x));
        BT_CHECK(std::set<u32>(V.adj.d(), V.adj.d() + V.adj.n) == expl && V.adj.size() == expl.size(), "fan-out set");
        BT_CHECK(V.act == (sd[s].R.get(kv.first) != NONE), "act");
        double fo = (double)V.adj.size();
        BT_CHECK(V.high ? fo >= tau / 2 : fo <= 2 * tau, "pair class hysteresis");
        if (V.high) hiExp.insert(v);
        BT_CHECK((V.hpos != NONE) == V.high && (!V.high || hi[s][V.hpos] == v), "hpos");
      }
      BT_CHECK(std::set<u32>(hi[s].begin(), hi[s].end()) == hiExp && hi[s].size() == hiExp.size(), "high set");
      pmap[s].check();
    }
    std::set<i32> xllExp, xllGot; size_t nl = 0;
    for (u32 l = 0; l < LRv.size(); l++) {
      LRec& L = LRv[l];
      if (!L.alive) { BT_CHECK(L.id[0].cap().bytes + L.id[1].cap().bytes + L.pl[0].cap().bytes + L.pl[1].cap().bytes == 0 && L.xpos == NONE, "dead light storage"); continue; }
      nl++;
      BT_CHECK(lrOf.count(L.x) && lrOf[L.x] == l, "light record owner");
      for (int s = 0; s < 2; s++) {
        L.id[s].check(); L.pl[s].check();
        std::set<u32> ids, pls; u32 nh = 0;
        forSide(s, L.x, [&](i32 a, i32 b) {
          u32 v = alive[s].at(pk(a, b)); ids.insert(v);
          if (PV[s][v].high) nh++; else if (PV[s][v].act) pls.insert(v);
        });
        BT_CHECK(std::set<u32>(L.id[s].d(), L.id[s].d() + L.id[s].n) == ids && L.id[s].size() == ids.size(), "pair list");
        BT_CHECK(L.nh[s] == nh, "high count");
        BT_CHECK(std::set<u32>(L.pl[s].d(), L.pl[s].d() + L.pl[s].n) == pls && L.pl[s].size() == pls.size(), "PL/QL");
      }
      BT_CHECK(!L.id[0].empty() || !L.id[1].empty(), "empty light record");
      bool both = !L.pl[0].empty() && !L.pl[1].empty();
      BT_CHECK((L.xpos != NONE) == both && (!both || xll[L.xpos] == l), "xll pos");
      if (both) xllExp.insert(L.x);
    }
    for (u32 l : xll) { BT_CHECK(LRv[l].alive, "xll dead"); xllGot.insert(LRv[l].x); }
    BT_CHECK(xllGot == xllExp && xll.size() == xllExp.size(), "XLL");
    BT_CHECK(nl == lrOf.size() && nl + lfree.size() == LRv.size(), "light slots");
    std::map<std::pair<u64, u64>, std::set<i32>> W, Wa;
    for (i32 x : light) forSide(0, x, [&](i32 a, i32 b) {
      u32 p = alive[0].at(pk(a, b)); bool ph = PV[0][p].high;
      forSide(1, x, [&](i32 d, i32 e) {
        u32 q = alive[1].at(pk(d, e));
        if (ph || PV[1][q].high) W[{pk(a, b), pk(d, e)}].insert(x);
      });
    });
    i64 ne = 0, wt = 0;
    for (u32 e = 0; e < WE.size(); e++) {
      WEdge& Ed = WE[e];
      if (!Ed.alive) { BT_CHECK(Ed.W.cap().bytes == 0 && Ed.pos[0] == NONE && Ed.pos[1] == NONE, "dead edge storage"); continue; }
      ne++; Ed.W.check(); wt += (i64)Ed.W.size();
      BT_CHECK(PV[0][Ed.p].alive && PV[1][Ed.q].alive, "edge to dead vertex");
      BT_CHECK(!Ed.W.empty(), "empty W");
      BT_CHECK(wmap.get(pk((i32)Ed.p, (i32)Ed.q)) == e, "wmap");
      std::set<i32> ws; for (u32 i = 0; i < Ed.W.n; i++) ws.insert((i32)Ed.W[i]);
      Wa[{vkey(0, Ed.p), vkey(1, Ed.q)}].insert(ws.begin(), ws.end());
    }
    BT_CHECK(W == Wa, "W");
    BT_CHECK(ne == m && (i64)W.size() == m && wmap.size() == (size_t)m && wt == Wtot && ne + (i64)wfree.size() == (i64)WE.size(), "W counts");
    wmap.check();
    std::map<u32, std::set<u64>> hxe[2];
    for (auto& Ed : WE) if (Ed.alive) {
      PRec& Pv = PV[0][Ed.p]; PRec& Qv = PV[1][Ed.q];
      if (Qv.high && Pv.act) hxe[1][Ed.q].insert(vkey(0, Ed.p));
      if (Pv.high && !Qv.high && Qv.act) hxe[0][Ed.p].insert(vkey(1, Ed.q));
    }
    for (int s = 0; s < 2; s++) {
      std::set<u32> anExp;
      for (auto& kv : alive[s]) {
        u32 v = kv.second; PRec& V = PV[s][v];
        std::set<u64> hx = hxe[s][v], hg;
        for (u32 i = 0; i < V.hadj.size(); i++) {
          u32 e = V.hadj[i]; WEdge& Ed = WE[e];
          BT_CHECK(Ed.alive && (s == 0 ? Ed.p : Ed.q) == v && Ed.pos[s] == i, "hadj owner");
          hg.insert(s == 0 ? vkey(1, Ed.q) : vkey(0, Ed.p));
        }
        BT_CHECK(hx == hg && hg.size() == V.hadj.size(), "HP/HQ");
        bool an = V.high && V.act && !hx.empty();
        if (an) anExp.insert(v);
        BT_CHECK((V.apos != NONE) == an && (!an || anc[s][V.apos] == v), "apos");
      }
      BT_CHECK(std::set<u32>(anc[s].begin(), anc[s].end()) == anExp && anc[s].size() == anExp.size(), "SP/SQ");
    }
    for (auto& Ed : WE) if (Ed.alive) {
      for (int s = 0; s < 2; s++) {
        u32 pos = Ed.pos[s]; u32 v = s == 0 ? Ed.p : Ed.q;
        if (pos != NONE) BT_CHECK(pos < PV[s][v].hadj.size() && WE[PV[s][v].hadj[pos]].alive, "edge pos");
      }
    }
  }
};
}
