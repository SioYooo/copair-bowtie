#pragma once
#include "perx_tuned.hpp"
namespace tuned {
struct PerxTunedIndex : public PerxTuned {
  struct Adj { MapOfSets<ISet> N1, N1r; };
  Adj ad[2];
  u64 wk[3] = {};
  static constexpr int NWK = 3;
  static const char* wkname(int i) { return i == 0 ? "lset_ops" : i == 1 ? "loop_iters" : "idx_ops"; }
  explicit PerxTunedIndex(Params p = Params()) : PerxTuned(p) {}
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
    RelInfo ri = relinfo(rel); int s = ri.s; bool add = sign > 0; SideB& S = sd[s]; Adj& D = ad[s];
    if (ri.kind == 'R') {
      if ((S.R.get(pk(u, v)) != NONE) == add) return true;
      if (add) { S.R.set(pk(u, v), 0); D.N1.ins(k32(u), v); D.N1r.ins(k32(v), u); }
      else { S.R.erase(pk(u, v)); D.N1.rem(k32(u), v); D.N1r.rem(k32(v), u); }
      TUNED_BWK(2); TUNED_BWK(2);
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
        const ISet* A = S.A.get(k32(x)); const ISet* nr = D.N1r.get(k32(el));
        if (A && nr) {
          if (nr->size() < A->size()) { for (i32 a : nr->el) { TUNED_BWK(1); if (A->contains(a)) lset(s, x, pk(a, el), add); } }
          else for (i32 a : A->el) { TUNED_BWK(1); if (S.R.get(pk(a, el)) != NONE) lset(s, x, pk(a, el), add); }
        }
      } else {
        const ISet* q = S.A.get(k32(x));
        if ((q && q->contains(el)) == add) return true;
        if (add) { S.A.ins(k32(x), el); S.Xa.ins(k32(el), x); } else { S.A.rem(k32(x), el); S.Xa.rem(k32(el), x); }
        const ISet* B = S.B.get(k32(x)); const ISet* nb = D.N1.get(k32(el));
        if (B && nb) {
          if (nb->size() < B->size()) { for (i32 b : nb->el) { TUNED_BWK(1); if (B->contains(b)) lset(s, x, pk(el, b), add); } }
          else for (i32 b : B->el) { TUNED_BWK(1); if (S.R.get(pk(el, b)) != NONE) lset(s, x, pk(el, b), add); }
        }
      }
    }
    Nraw += add ? 1 : -1;
    return false;
  }
  Census capacity() const {
    Census z = PerxTuned::capacity(); Cap c;
    for (int s = 0; s < 2; s++) { c += ad[s].N1.cap(); c += ad[s].N1r.cap(); }
    std::string p = "adj_idx";
    z.push_back({p + ".items", c.items}); z.push_back({p + ".cap", c.cap}); z.push_back({p + ".bytes", c.bytes});
    z.push_back({p + ".slots_live", c.live}); z.push_back({p + ".slots_total", c.total});
    for (auto& kv : z) if (kv.first == "total_bytes") kv.second += c.bytes;
    return z;
  }
  void audit() {
    PerxTuned::audit();
    for (int s = 0; s < 2; s++) {
      SideB& S = sd[s]; const Adj& D = ad[s]; u64 nR = S.R.size();
      D.N1.check(); D.N1r.check();
      TUNED_CHECK(D.N1.total() == nR && D.N1r.total() == nR, "adj totals");
      D.N1.each([&](u64 a, const ISet& st) { for (i32 b : st.el) { TUNED_CHECK(S.R.get(pk((i32)(u32)a, b)) != NONE, "N1 not in R"); auto* q = D.N1r.get(k32(b)); TUNED_CHECK(q && q->contains((i32)(u32)a), "N1r"); } });
      D.N1r.each([&](u64 b, const ISet& st) { for (i32 a : st.el) { TUNED_CHECK(S.R.get(pk(a, (i32)(u32)b)) != NONE, "N1r not in R"); auto* q = D.N1.get(k32(a)); TUNED_CHECK(q && q->contains((i32)(u32)b), "N1"); } });
    }
  }
};
}
