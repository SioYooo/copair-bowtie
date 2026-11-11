#pragma once
#include "copair_core.hpp"

namespace bt {

struct FanParams : Params { bool tauFixed = false; double tau = 0; };
struct LX { i32 x = 0; ISet PL, QL; bool alive = false; };
struct FanCtr {
  u64 pair_to_high[2] = {}, pair_to_low[2] = {}, r1_low = 0, r1_high = 0, r5_low = 0, r5_high = 0;
};

struct CopairFanoutBase : CopairBase {
  FanParams FP;
  double tau = 1;
  FanCtr f;
  std::vector<LX> LXv; std::vector<u32> lxfree; FMap lxmap;
  ISet xll, hi[2];
  std::vector<u32> scrO, scrL;

  explicit CopairFanoutBase(FanParams p) : CopairBase(p), FP(p) {}

  void thresh() {
    thresholds();
    tau = FP.tauFixed ? FP.tau : std::max(1.0, std::sqrt(psi * N0));
  }
  void clearLight() {
    release(LXv); release(lxfree); lxmap.clear(); xll.clear(); hi[0].clear(); hi[1].clear(); release(scrO); release(scrL);
  }

  template <class F> void forSide(int s, i32 x, F fn) {
    const ISet* A = sd[s].A.get(k32(x)); const ISet* B = sd[s].B.get(k32(x));
    if (!A || !B) return;
    for (i32 a : A->el) for (i32 b : B->el) fn(a, b);
  }
  void idsOf(int s, i32 x, std::vector<u32>& out) {
    out.clear();
    forSide(s, x, [&](i32 a, i32 b) { u32 v = vget(s, a, b); if (v == NONE) fail("light pair missing"); out.push_back(v); });
  }
  static u64 ekey(int s, u32 v, u32 o) { return s == 0 ? pk((i32)v, (i32)o) : pk((i32)o, (i32)v); }

  void anchor(int s, u32 v) {
    Vtx& V = sd[s].V[v];
    setMem(sd[s].anch1, (i32)v, V.high && V.act && !V.hadj.empty());
  }
  u32 lxMake(i32 x) {
    u32 i;
    if (!lxfree.empty()) { i = lxfree.back(); lxfree.pop_back(); relax(lxfree); } else { pool_limit(LXv.size()); i = (u32)LXv.size(); LXv.emplace_back(); }
    LXv[i].x = x; LXv[i].alive = true; lxmap.set(k32(x), i);
    return i;
  }
  void lowset(int s, i32 x, u32 v, bool in) {
    u32 i = lxmap.get(k32(x));
    if (i == NONE) { if (!in) return; i = lxMake(x); }
    LX& r = LXv[i];
    setMem(s == 0 ? r.PL : r.QL, (i32)v, in);
    setMem(xll, (i32)i, !r.PL.empty() && !r.QL.empty());
    if (r.PL.empty() && r.QL.empty()) { lxmap.erase(k32(x)); LXv[i] = LX(); lxfree.push_back(i); relax(lxfree); }
  }
  void fixedge(u32 e, bool ex) {
    Edge& Ed = E[e]; Vtx& Pv = sd[0].V[Ed.p]; Vtx& Qv = sd[1].V[Ed.q];
    setMem(Qv.hadj, (i32)e, ex && Qv.high && Pv.act);
    setMem(Pv.hadj, (i32)e, ex && Pv.high && !Qv.high && Qv.act);
    anchor(0, (u32)Ed.p); anchor(1, (u32)Ed.q);
  }
  void wadd(i32 x, u32 vp, u32 vq) {
    WK(W_WIT, 1);
    u64 ek = pk((i32)vp, (i32)vq); u32 e = emap.get(ek);
    if (e != NONE) { if (E[e].W.insert(x)) Wtot++; return; }
    WK(W_EDGE, 1);
    if (!efree.empty()) { e = efree.back(); efree.pop_back(); relax(efree); } else { pool_limit(E.size()); e = (u32)E.size(); E.emplace_back(); }
    Edge& Ed = E[e]; Ed.p = (i32)vp; Ed.q = (i32)vq; Ed.alive = true; Ed.W.insert(x); Wtot++;
    emap.set(ek, e); m++;
    fixedge(e, true);
  }
  void wdel(i32 x, u32 vp, u32 vq) {
    WK(W_WIT, 1);
    u64 ek = pk((i32)vp, (i32)vq); u32 e = emap.get(ek);
    if (e == NONE) fail("wdel: missing edge");
    if (!E[e].W.erase(x)) fail("wdel: missing witness");
    Wtot--;
    if (!E[e].W.empty()) return;
    WK(W_EDGE, 1);
    fixedge(e, false);
    emap.erase(ek); E[e] = Edge(); efree.push_back(e); m--;
  }

  void up(int s, u32 v) {
    f.pair_to_high[s]++;
    sd[s].V[v].high = true; hi[s].insert((i32)v);
    for (i32 x : sd[s].V[v].adj.el) {
      lowset(s, x, v, false);
      forSide(1 - s, x, [&](i32 a, i32 b) {
        u32 o = vget(1 - s, a, b); if (o == NONE) return;
        if (!sd[1 - s].V[o].high) wadd(x, s == 0 ? v : o, s == 0 ? o : v);
      });
      if (s == 1) forSide(0, x, [&](i32 a, i32 b) {
        u32 o = vget(0, a, b); if (o == NONE || !sd[0].V[o].high) return;
        u32 e = emap.get(ekey(1, v, o)); if (e != NONE) fixedge(e, true);
      });
    }
    anchor(s, v);
  }
  void down(int s, u32 v) {
    f.pair_to_low[s]++;
    sd[s].V[v].high = false; hi[s].erase((i32)v);
    for (i32 x : sd[s].V[v].adj.el) {
      forSide(1 - s, x, [&](i32 a, i32 b) {
        u32 o = vget(1 - s, a, b); if (o == NONE) return;
        if (!sd[1 - s].V[o].high) wdel(x, s == 0 ? v : o, s == 0 ? o : v);
        else if (s == 1) { u32 e = emap.get(ekey(1, v, o)); if (e != NONE) fixedge(e, true); }
      });
      if (sd[s].V[v].act) lowset(s, x, v, true);
    }
    anchor(s, v);
  }
  void classTest(int s, u32 v) {
    Vtx& V = sd[s].V[v]; double fo = (double)V.adj.size();
    if (V.high) { if (fo < tau / 2 || V.adj.empty()) down(s, v); }
    else if (fo > 2 * tau) up(s, v);
  }
  void pairAdd(int s, i32 x, i32 a, i32 b, const std::vector<u32>& oth) {
    u32 v = vget(s, a, b); if (v == NONE) v = vnew(s, a, b);
    Vtx& V = sd[s].V[v];
    V.adj.insert(x);
    if (!V.high && V.act) lowset(s, x, v, true);
    for (u32 o : oth) if (V.high || sd[1 - s].V[o].high) wadd(x, s == 0 ? v : o, s == 0 ? o : v);
    classTest(s, v);
  }
  void pairRemove(int s, i32 x, i32 a, i32 b, const std::vector<u32>& oth) {
    u32 v = vget(s, a, b); if (v == NONE) fail("pairRemove: missing pair");
    Vtx& V = sd[s].V[v];
    if (!V.adj.erase(x)) fail("pairRemove: x not in fan-out");
    lowset(s, x, v, false);
    for (u32 o : oth) if (V.high || sd[1 - s].V[o].high) wdel(x, s == 0 ? v : o, s == 0 ? o : v);
    classTest(s, v);
    if (sd[s].V[v].adj.empty()) {
      if (!sd[s].V[v].hadj.empty()) fail("pairRemove: stale witnesses");
      vdel(s, v);
    }
  }

  void to_heavy(i32 x) {
    c.x_to_heavy++; Ctx g(ctx, CTX_X); WK(W_XTR, 1);
    idsOf(1, x, scrO);
    forSide(0, x, [&](i32 a, i32 b) { pairRemove(0, x, a, b, scrO); });
    scrO.clear();
    forSide(1, x, [&](i32 d, i32 e) { pairRemove(1, x, d, e, scrO); });
    u32 h = newHeavy(x); build_x(h);
  }
  void to_light(i32 x) {
    c.x_to_light++; Ctx g(ctx, CTX_X); WK(W_XTR, 1);
    u32 h = hmap.get(k32(x));
    drop_x(h);
    hmap.erase(k32(x)); heavyL.erase((i32)h); H[h] = HRec(); hfree.push_back(h);
    scrO.clear();
    forSide(0, x, [&](i32 a, i32 b) { pairAdd(0, x, a, b, scrO); });
    idsOf(0, x, scrO);
    forSide(1, x, [&](i32 d, i32 e) { pairAdd(1, x, d, e, scrO); });
  }
  void check_x(i32 x) {
    size_t d = deg(x);
    if (hmap.get(k32(x)) != NONE) { if ((double)d < psi / 2 || d == 0) to_light(x); }
    else if ((double)d > 2 * psi) to_heavy(x);
  }

  bool apply(int sign, int rel, i32 u, i32 v) {
    RelInfo ri = relinfo(rel); int s = ri.s; bool add = sign > 0; Side& S = sd[s];
    if (ri.kind == 'R') {
      if ((S.R.get(pk(u, v)) != NONE) == add) return true;
      upd_R(s, u, v, add);
    } else {
      i32 x, el; xel(rel, u, v, x, el);
      const ISet* q = (ri.kind == 'A' ? S.A : S.B).get(k32(x));
      if ((q && q->contains(el)) == add) return true;
      upd_xel(s, ri.kind, x, el, add);
    }
    Nraw += add ? 1 : -1;
    post();
    return false;
  }
  void upd_R(int s, i32 a, i32 b, bool add) {
    Side& S = sd[s]; WK(W_BASE, 3);
    if (add) { S.R.set(pk(a, b), 0); S.N1.ins(k32(a), b); S.N1r.ins(k32(b), a); }
    else { S.R.erase(pk(a, b)); S.N1.rem(k32(a), b); S.N1r.rem(k32(b), a); }
    u32 vt = vget(s, a, b);
    if (vt != NONE) {
      Vtx& X = S.V[vt]; X.act = add;
      (s == 0 ? (X.high ? f.r1_high : f.r1_low) : (X.high ? f.r5_high : f.r5_low))++;
      if (!X.high) for (i32 x : X.adj.el) lowset(s, x, vt, add);
      for (i32 o : hi[1 - s].el) { u32 e = emap.get(ekey(s, vt, (u32)o)); if (e != NONE) fixedge(e, true); }
      anchor(s, vt);
    }
    const ISet* r2 = S.R2H.get(k32(b)); const ISet* r3 = S.R3H.get(k32(a));
    if (S.aH.contains(a)) { if (r2) for (i32 h : r2->el) { WK(W_INC, 1); sync((u32)h, s, a, b); } }
    else if (S.bH.contains(b)) { if (r3) for (i32 h : r3->el) { WK(W_INC, 1); sync((u32)h, s, a, b); } }
    else if (r2 && r3) {
      const ISet *sm = r2->size() <= r3->size() ? r2 : r3, *lg = sm == r2 ? r3 : r2;
      for (i32 h : sm->el) { WK(W_INC, 1); if (lg->contains(h)) sync((u32)h, s, a, b); }
    }
    check_cls(s, a, true); check_cls(s, b, false);
  }
  void upd_xel(int s, char kind, i32 x, i32 el, bool add) {
    Side& S = sd[s]; WK(W_BASE, 1);
    MapOfSets<ISet>& M = kind == 'A' ? S.A : S.B;
    if (add) M.ins(k32(x), el); else M.rem(k32(x), el);
    u32 h = hmap.get(k32(x));
    if (h != NONE) {
      if (kind == 'B') {
        i32 b = el; WK(W_INC, 1);
        if (add) S.R2H.ins(k32(b), (i32)h); else S.R2H.rem(k32(b), (i32)h);
        const ISet* nr = S.N1r.get(k32(b));
        if (nr) {
          if (S.aH.size() <= nr->size()) { for (i32 a : S.aH.el) if (S.R.get(pk(a, b)) != NONE) sync(h, s, a, b); }
          else { for (i32 a : nr->el) if (S.aH.contains(a)) sync(h, s, a, b); }
        }
        if (!S.bH.contains(b)) { if (nr) for (i32 a : nr->el) if (!S.aH.contains(a)) sync(h, s, a, b); }
        else refreshS(h, s, 2, b);
      } else {
        i32 a = el; WK(W_INC, 1);
        if (add) S.R3H.ins(k32(a), (i32)h); else S.R3H.rem(k32(a), (i32)h);
        if (S.aH.contains(a)) refreshS(h, s, 0, a);
        else { const ISet* n1 = S.N1.get(k32(a)); if (n1) for (i32 b : n1->el) sync(h, s, a, b); }
      }
    } else {
      idsOf(1 - s, x, scrO);
      const ISet *A0 = S.A.get(k32(x)), *B0 = S.B.get(k32(x));
      if (kind == 'B') { if (A0) for (i32 a : A0->el) { if (add) pairAdd(s, x, a, el, scrO); else pairRemove(s, x, a, el, scrO); } }
      else { if (B0) for (i32 b : B0->el) { if (add) pairAdd(s, x, el, b, scrO); else pairRemove(s, x, el, b, scrO); } }
    }
    check_x(x);
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
    std::vector<u32> hv; std::vector<i32> lights;
    for (i32 x : xs) {
      if ((double)deg(x) > psi) hv.push_back(newHeavy(x));
      else {
        lights.push_back(x);
        for (int s = 0; s < 2; s++) forSide(s, x, [&](i32 a, i32 b) {
          u32 v = vget(s, a, b); if (v == NONE) v = vnew(s, a, b);
          sd[s].V[v].adj.insert(x);
        });
      }
    }
    for (int s = 0; s < 2; s++) for (auto& en : sd[s].vmap.ent) {
      Vtx& V = sd[s].V[en.val];
      if ((double)V.adj.size() > tau) { V.high = true; hi[s].insert((i32)en.val); }
    }
    std::vector<u32> L, R, Rh;
    for (i32 x : lights) {
      idsOf(0, x, L); idsOf(1, x, R); Rh.clear();
      for (u32 q : R) if (sd[1].V[q].high) Rh.push_back(q);
      for (u32 p : L) {
        Vtx& Vp = sd[0].V[p];
        if (!Vp.high && Vp.act) lowset(0, x, p, true);
        for (u32 q : Vp.high ? R : Rh) wadd(x, p, q);
      }
      for (u32 q : R) { Vtx& Vq = sd[1].V[q]; if (!Vq.high && Vq.act) lowset(1, x, q, true); }
    }
    for (u32 h : hv) build_x(h);
  }

  struct Enum {
    CopairFanoutBase* g; int ph = 0; size_t i = 0, j = 0, k = 0, xi = 0; u32 h = 0; LIt L, R; i32 la = 0, lb = 0; bool haveL = false;
    explicit Enum(CopairFanoutBase& c_) : g(&c_) {}
    bool start(i32* o) { ph = 0; i = j = k = xi = 0; haveL = false; return next(o); }
    bool next(i32* o) {
      while (ph < 3) {
        if (ph == 0) {
          if (i >= g->xll.size()) { ph++; i = j = k = 0; continue; }
          LX& r = g->LXv[g->xll.el[i]];
          if (j >= r.PL.size()) { i++; j = 0; k = 0; continue; }
          if (k >= r.QL.size()) { j++; k = 0; continue; }
          Vtx& Pv = g->sd[0].V[r.PL.el[j]]; Vtx& Qv = g->sd[1].V[r.QL.el[k++]];
          o[0] = Pv.a; o[1] = Pv.b; o[2] = r.x; o[3] = Qv.a; o[4] = Qv.b;
          return true;
        }
        const ISet& an = g->sd[ph == 1 ? 1 : 0].anch1;
        if (i >= an.size()) { ph++; i = j = k = 0; continue; }
        Vtx& V = g->sd[ph == 1 ? 1 : 0].V[an.el[i]];
        if (j >= V.hadj.size()) { i++; j = 0; k = 0; continue; }
        Edge& Ed = g->E[V.hadj.el[j]];
        if (k >= Ed.W.size()) { j++; k = 0; continue; }
        Vtx& Pv = g->sd[0].V[Ed.p]; Vtx& Qv = g->sd[1].V[Ed.q];
        o[0] = Pv.a; o[1] = Pv.b; o[2] = Ed.W.el[k++]; o[3] = Qv.a; o[4] = Qv.b;
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

  Census capacity() const {
    Census z = CopairBase::capacity();
    Cap cl, cs;
    for (auto& r : LXv) { addset(cl, r.PL); addset(cl, r.QL); cl.live += r.alive; }
    cl.total = LXv.size(); cl.bytes += LXv.capacity() * sizeof(LX) + lxfree.capacity() * 4; cl.bytes += lxmap.cap().bytes;
    addset(cs, xll); addset(cs, hi[0]); addset(cs, hi[1]);
    u64 add = cl.bytes + cs.bytes;
    for (auto& kv : z) if (kv.first == "total_bytes") kv.second += add;
    z.push_back({"LX.items", cl.items}); z.push_back({"LX.bytes", cl.bytes}); z.push_back({"LX.slots_live", cl.live}); z.push_back({"LX.slots_total", cl.total});
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

  void audit() { std::set<i32> xs = audit_a(); audit_light(xs); audit_b(); }
  void audit_light(const std::set<i32>& xs) {
    std::map<u64, std::set<i32>> Xp[2];
    std::set<i32> light;
    for (i32 x : xs) if (hmap.get(k32(x)) == NONE) {
      light.insert(x);
      for (int s = 0; s < 2; s++) forSide(s, x, [&](i32 a, i32 b) { Xp[s][pk(a, b)].insert(x); });
    }
    auto vkey = [&](int s, u32 v) { return pk(sd[s].V[v].a, sd[s].V[v].b); };
    std::map<u64, u32> alive[2];
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s]; size_t na = 0;
      for (u32 v = 0; v < S.V.size(); v++) {
        Vtx& V = S.V[v];
        if (V.alive) { alive[s][vkey(s, v)] = v; na++; }
        else BT_CHECK(V.adj.cap().bytes + V.hadj.cap().bytes + V.Alo.cap().bytes + V.Ahi.cap().bytes == 0, "dead vtx storage");
      }
      BT_CHECK(na == Xp[s].size() && S.vmap.size() == na, "pair vertex set");
      std::set<i32> hiExp, anExp;
      for (auto& kv : Xp[s]) {
        auto it = alive[s].find(kv.first); BT_CHECK(it != alive[s].end(), "missing pair vertex");
        u32 v = it->second; Vtx& V = S.V[v];
        BT_CHECK(S.vmap.get(kv.first) == v, "vmap");
        V.adj.check(); V.hadj.check();
        BT_CHECK(std::set<i32>(V.adj.el.begin(), V.adj.el.end()) == kv.second && V.adj.size() == kv.second.size(), "fan-out set");
        BT_CHECK(V.act == (S.R.get(kv.first) != NONE), "act");
        double fo = (double)V.adj.size();
        BT_CHECK(V.high ? fo >= tau / 2 : fo <= 2 * tau, "pair class hysteresis");
        BT_CHECK(V.Alo.empty() && V.Ahi.empty(), "unused sets");
        if (V.high) hiExp.insert((i32)v);
      }
      BT_CHECK(std::set<i32>(hi[s].el.begin(), hi[s].el.end()) == hiExp && hi[s].size() == hiExp.size(), "high set");
      hi[s].check();
    }
    std::map<i32, std::set<u32>> pl[2], plg[2];
    for (i32 x : light) for (int s = 0; s < 2; s++) forSide(s, x, [&](i32 a, i32 b) {
      u32 v = alive[s].at(pk(a, b)); Vtx& V = sd[s].V[v];
      if (!V.high && V.act) pl[s][x].insert(v);
    });
    std::set<i32> xllExp, xllGot;
    for (u32 i = 0; i < LXv.size(); i++) {
      LX& r = LXv[i];
      if (!r.alive) { BT_CHECK(r.PL.cap().bytes + r.QL.cap().bytes == 0, "dead lx storage"); continue; }
      BT_CHECK(lxmap.get(k32(r.x)) == i, "lxmap");
      BT_CHECK(!r.PL.empty() || !r.QL.empty(), "empty lx");
      r.PL.check(); r.QL.check();
      if (!r.PL.empty()) plg[0][r.x].insert(r.PL.el.begin(), r.PL.el.end());
      if (!r.QL.empty()) plg[1][r.x].insert(r.QL.el.begin(), r.QL.el.end());
      if (!r.PL.empty() && !r.QL.empty()) xllExp.insert(r.x);
    }
    for (int s = 0; s < 2; s++) BT_CHECK(pl[s] == plg[s], "PL/QL");
    for (i32 i : xll.el) { BT_CHECK(LXv[i].alive, "xll dead"); xllGot.insert(LXv[i].x); }
    BT_CHECK(xllGot == xllExp && xll.size() == xllExp.size(), "XLL");
    xll.check(); lxmap.check();
    size_t nl = 0; for (auto& r : LXv) nl += r.alive;
    BT_CHECK(nl == lxmap.size() && nl + lxfree.size() == LXv.size(), "lx slots");
    std::map<std::pair<u64, u64>, std::set<i32>> W, Wa;
    for (i32 x : light) forSide(0, x, [&](i32 a, i32 b) {
      u32 p = alive[0].at(pk(a, b)); bool ph = sd[0].V[p].high;
      forSide(1, x, [&](i32 d, i32 e) {
        u32 q = alive[1].at(pk(d, e));
        if (ph || sd[1].V[q].high) W[{pk(a, b), pk(d, e)}].insert(x);
      });
    });
    i64 ne = 0, wt = 0;
    for (auto& Ed : E) if (Ed.alive) {
      ne++; Ed.W.check(); wt += (i64)Ed.W.size();
      BT_CHECK(sd[0].V[Ed.p].alive && sd[1].V[Ed.q].alive, "edge to dead vertex");
      BT_CHECK(!Ed.W.empty(), "empty W");
      BT_CHECK(emap.get(pk(Ed.p, Ed.q)) != NONE && E[emap.get(pk(Ed.p, Ed.q))].alive, "emap");
      Wa[{vkey(0, (u32)Ed.p), vkey(1, (u32)Ed.q)}].insert(Ed.W.el.begin(), Ed.W.el.end());
    } else BT_CHECK(Ed.W.cap().bytes == 0, "dead edge storage");
    BT_CHECK(W == Wa, "W");
    BT_CHECK(ne == m && (i64)W.size() == m && emap.size() == (size_t)m && wt == Wtot, "W counts");
    emap.check();
    std::map<u32, std::set<u64>> hxe[2];
    for (auto& Ed : E) if (Ed.alive) {
      Vtx& Pv = sd[0].V[Ed.p]; Vtx& Qv = sd[1].V[Ed.q];
      if (Qv.high && Pv.act) hxe[1][(u32)Ed.q].insert(vkey(0, (u32)Ed.p));
      if (Pv.high && !Qv.high && Qv.act) hxe[0][(u32)Ed.p].insert(vkey(1, (u32)Ed.q));
    }
    for (int s = 0; s < 2; s++) {
      Side& S = sd[s]; std::set<i32> anExp;
      for (auto& kv : alive[s]) {
        u32 v = kv.second; Vtx& V = S.V[v];
        std::set<u64> hx = hxe[s][v], hg;
        for (i32 e : V.hadj.el) {
          Edge& Ed = E[e]; BT_CHECK(Ed.alive && (s == 0 ? Ed.p : Ed.q) == (i32)v, "hadj owner");
          hg.insert(s == 0 ? vkey(1, (u32)Ed.q) : vkey(0, (u32)Ed.p));
        }
        BT_CHECK(hx == hg && hg.size() == V.hadj.size(), "HP/HQ");
        if (V.high && V.act && !hx.empty()) anExp.insert((i32)v);
      }
      BT_CHECK(std::set<i32>(S.anch1.el.begin(), S.anch1.el.end()) == anExp && S.anch1.size() == anExp.size(), "SP/SQ");
      BT_CHECK(S.anch2.empty(), "anch2");
      S.anch1.check();
    }
  }
};
}
