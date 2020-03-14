#include "add.h"
#include "echarge.h"
#include "gpu_card.h"
#include "mdegv.h"
#include "mdpq.h"
#include "named_struct.h"
#include "nblist.h"
#include "pmestuf.h"
#include "seq_bsplgen.h"
#include "seq_image.h"
#include "seq_pair_charge.h"
#include "seq_switch.h"
#include "switch.h"


TINKER_NAMESPACE_BEGIN
#define DEVICE_PTRS x, y, z, gx, gy, gz, pchg, nec, ec, vir_ec
template <class Ver, class ETYP>
void echarge_acc1()
{
   constexpr bool do_e = Ver::e;
   constexpr bool do_a = Ver::a;
   constexpr bool do_g = Ver::g;
   constexpr bool do_v = Ver::v;


   real f = electric / dielec;
   real cut, off, aewald;
   if CONSTEXPR (eq<ETYP, EWALD>()) {
      off = switch_off(switch_ewald);
      // cut = off; // not used
      const auto& st = *epme_unit;
      aewald = st.aewald;
   } else if CONSTEXPR (eq<ETYP, NON_EWALD_TAPER>()) {
      off = switch_off(switch_charge);
      cut = switch_cut(switch_charge);
   }
   const real off2 = off * off;
   const int maxnlist = clist_unit->maxnlst;
   const auto* nlst = clist_unit->nlst;
   const auto* lst = clist_unit->lst;
   auto bufsize = buffer_size();


   MAYBE_UNUSED int GRID_DIM = get_grid_size(BLOCK_DIM);
   #pragma acc parallel async num_gangs(GRID_DIM) vector_length(BLOCK_DIM)\
               deviceptr(DEVICE_PTRS,nlst,lst)
   #pragma acc loop gang independent
   for (int i = 0; i < n; ++i) {
      real xi = x[i];
      real yi = y[i];
      real zi = z[i];
      real ci = pchg[i];
      MAYBE_UNUSED real gxi = 0, gyi = 0, gzi = 0;


      int nlsti = nlst[i];
      #pragma acc loop vector independent reduction(+:gxi,gyi,gzi)
      for (int kk = 0; kk < nlsti; ++kk) {
         int offset = (kk + i * n) & (bufsize - 1);
         int k = lst[i * maxnlist + kk];
         real xr = xi - x[k];
         real yr = yi - y[k];
         real zr = zi - z[k];
         real ck = pchg[k];


         real r2 = image2(xr, yr, zr);
         if (r2 <= off2) {
            real r = REAL_SQRT(r2);


            int ctl;
            real e, frcx, frcy, frcz, vxx, vxy, vxz, vyy, vyz, vzz;
            if CONSTEXPR (do_a) {
               ctl = 0;
            }
            if CONSTEXPR (do_e) {
               e = 0;
            }
            if CONSTEXPR (do_g) {
               frcx = 0;
               frcy = 0;
               frcz = 0;
            }
            if CONSTEXPR (do_v) {
               vxx = 0;
               vxy = 0;
               vxz = 0;
               vyy = 0;
               vyz = 0;
               vzz = 0;
            }


            // EWALD           -> EWALD
            // NON_EWALD_TAPER -> NON_EWALD_TAPER
            pair_charge<Ver, ETYP>(
               r, xr, yr, zr, 1, ci, ck, ebuffer, f, aewald, cut, off, //
               frcx, frcy, frcz, ctl, e, vxx, vxy, vxz, vyy, vyz, vzz);


            if CONSTEXPR (do_a)
               atomic_add(ctl, nec, offset);
            if CONSTEXPR (do_e)
               atomic_add(e, ec, offset);
            if CONSTEXPR (do_g) {
               gxi += frcx;
               gyi += frcy;
               gzi += frcz;
               atomic_add(-frcx, gx, k);
               atomic_add(-frcy, gy, k);
               atomic_add(-frcz, gz, k);
            }
            if CONSTEXPR (do_v)
               atomic_add(vxx, vxy, vxz, vyy, vyz, vzz, vir_ec, offset);
         } // end if (include)
      }


      if CONSTEXPR (do_g) {
         atomic_add(gxi, gx, i);
         atomic_add(gyi, gy, i);
         atomic_add(gzi, gz, i);
      }
   } // end for (int i)


   #pragma acc parallel async deviceptr(DEVICE_PTRS, cexclude, cexclude_scale)
   #pragma acc loop independent
   for (int ii = 0; ii < ncexclude; ++ii) {
      int offset = ii & (bufsize - 1);


      int i = cexclude[ii][0];
      int k = cexclude[ii][1];
      real cscale = cexclude_scale[ii];


      real ci = pchg[i];
      real xi = x[i];
      real yi = y[i];
      real zi = z[i];


      real ck = pchg[k];
      real xr = xi - x[k];
      real yr = yi - y[k];
      real zr = zi - z[k];


      real r2 = image2(xr, yr, zr);
      if (r2 <= off2) {
         int ctl;
         real e, frcx, frcy, frcz, vxx, vxy, vxz, vyy, vyz, vzz;
         if CONSTEXPR (do_a) {
            ctl = 0;
         }
         if CONSTEXPR (do_e) {
            e = 0;
         }
         if CONSTEXPR (do_g) {
            frcx = 0;
            frcy = 0;
            frcz = 0;
         }
         if CONSTEXPR (do_v) {
            vxx = 0;
            vxy = 0;
            vxz = 0;
            vyy = 0;
            vyz = 0;
            vzz = 0;
         }


         // EWALD           -> NON_EWALD
         // NON_EWALD_TAPER -> NON_EWALD_TAPER
         real r = REAL_SQRT(r2);
         if CONSTEXPR (eq<ETYP, EWALD>()) {
            pair_charge<Ver, NON_EWALD>(
               r, xr, yr, zr, cscale, ci, ck, ebuffer, f, 0, 0, 0, //
               frcx, frcy, frcz, ctl, e, vxx, vxy, vxz, vyy, vyz, vzz);
         } else if CONSTEXPR (eq<ETYP, NON_EWALD_TAPER>()) {
            pair_charge<Ver, NON_EWALD_TAPER>(
               r, xr, yr, zr, cscale, ci, ck, ebuffer, f, 0, cut, off, //
               frcx, frcy, frcz, ctl, e, vxx, vxy, vxz, vyy, vyz, vzz);
         }


         if (e != 0) {
            if CONSTEXPR (do_a)
               atomic_add(ctl, nec, offset);
            if CONSTEXPR (do_e)
               atomic_add(e, ec, offset);
         }
         if CONSTEXPR (do_g) {
            atomic_add(frcx, gx, i);
            atomic_add(frcy, gy, i);
            atomic_add(frcz, gz, i);
            atomic_add(-frcx, gx, k);
            atomic_add(-frcy, gy, k);
            atomic_add(-frcz, gz, k);
         }
         if CONSTEXPR (do_v)
            atomic_add(vxx, vxy, vxz, vyy, vyz, vzz, vir_ec, offset);
      } // end if (include)
   }
}


void echarge_nonewald_acc(int vers)
{
   if (vers == calc::v0)
      echarge_acc1<calc::V0, NON_EWALD_TAPER>();
   else if (vers == calc::v1)
      echarge_acc1<calc::V1, NON_EWALD_TAPER>();
   else if (vers == calc::v3)
      echarge_acc1<calc::V3, NON_EWALD_TAPER>();
   else if (vers == calc::v4)
      echarge_acc1<calc::V4, NON_EWALD_TAPER>();
   else if (vers == calc::v5)
      echarge_acc1<calc::V5, NON_EWALD_TAPER>();
   else if (vers == calc::v6)
      echarge_acc1<calc::V6, NON_EWALD_TAPER>();
}


void echarge_ewald_real_acc(int vers)
{
   if (vers == calc::v0)
      echarge_acc1<calc::V0, EWALD>();
   else if (vers == calc::v1)
      echarge_acc1<calc::V1, EWALD>();
   else if (vers == calc::v3)
      echarge_acc1<calc::V3, EWALD>();
   else if (vers == calc::v4)
      echarge_acc1<calc::V4, EWALD>();
   else if (vers == calc::v5)
      echarge_acc1<calc::V5, EWALD>();
   else if (vers == calc::v6)
      echarge_acc1<calc::V6, EWALD>();
}


//====================================================================//


template <class Ver, int bsorder>
void echarge_acc3()
{
   constexpr bool do_e = Ver::e;
   constexpr bool do_a = Ver::a;
   constexpr bool do_g = Ver::g;


   real f = electric / dielec;
   const auto& st = *epme_unit;
   real aewald = st.aewald;
   const auto* qgrid = st.qgrid;
   int nfft1 = st.nfft1;
   int nfft2 = st.nfft2;
   int nfft3 = st.nfft3;
   auto bufsize = buffer_size();


   #pragma acc parallel async deviceptr(pchg,qgrid,nec,ec,x,y,z,gx,gy,gz)
   #pragma acc loop independent
   for (int ii = 0; ii < n; ++ii) {
      real chgi = pchg[ii];
      if (chgi == 0)
         continue;


      // self energy, tinfoil
      if CONSTEXPR (do_e) {
         int offset = ii & (bufsize - 1);
         real fs = -f * aewald * REAL_RECIP(sqrtpi);
         real e = fs * chgi * chgi;
         atomic_add(e, ec, offset);
         if (do_a) {
            atomic_add(1, nec, offset);
         }
      }


      // recip gradient
      if CONSTEXPR (do_g) {
         real xi = x[ii];
         real yi = y[ii];
         real zi = z[ii];


         real w1 = xi * recipa.x + yi * recipa.y + zi * recipa.z;
         w1 = w1 + 0.5f - REAL_FLOOR(w1 + 0.5f);
         real fr1 = nfft1 * w1;
         int igrid1 = REAL_FLOOR(fr1);
         w1 = fr1 - igrid1;


         real w2 = xi * recipb.x + yi * recipb.y + zi * recipb.z;
         w2 = w2 + 0.5f - REAL_FLOOR(w2 + 0.5f);
         real fr2 = nfft2 * w2;
         int igrid2 = REAL_FLOOR(fr2);
         w2 = fr2 - igrid2;


         real w3 = xi * recipc.x + yi * recipc.y + zi * recipc.z;
         w3 = w3 + 0.5f - REAL_FLOOR(w3 + 0.5f);
         real fr3 = nfft3 * w3;
         int igrid3 = REAL_FLOOR(fr3);
         w3 = fr3 - igrid3;


         igrid1 = igrid1 - bsorder + 1;
         igrid2 = igrid2 - bsorder + 1;
         igrid3 = igrid3 - bsorder + 1;
         igrid1 += (igrid1 < 0 ? nfft1 : 0);
         igrid2 += (igrid2 < 0 ? nfft2 : 0);
         igrid3 += (igrid3 < 0 ? nfft3 : 0);


         real thetai1[4 * 5];
         real thetai2[4 * 5];
         real thetai3[4 * 5];
         bsplgen<2>(w1, thetai1, bsorder);
         bsplgen<2>(w2, thetai2, bsorder);
         bsplgen<2>(w3, thetai3, bsorder);


         real fi = f * chgi;
         real de1 = 0, de2 = 0, de3 = 0;
         #pragma acc loop seq
         for (int iz = 0; iz < bsorder; ++iz) {
            int zbase = igrid3 + iz;
            zbase -= (zbase >= nfft3 ? nfft3 : 0);
            zbase *= (nfft1 * nfft2);
            real t3 = thetai3[4 * iz];
            real dt3 = nfft3 * thetai3[1 + 4 * iz];
            #pragma acc loop seq
            for (int iy = 0; iy < bsorder; ++iy) {
               int ybase = igrid2 + iy;
               ybase -= (ybase >= nfft2 ? nfft2 : 0);
               ybase *= nfft1;
               real t2 = thetai2[4 * iy];
               real dt2 = nfft2 * thetai2[1 + 4 * iy];
               #pragma acc loop seq
               for (int ix = 0; ix < bsorder; ++ix) {
                  int xbase = igrid1 + ix;
                  xbase -= (xbase >= nfft1 ? nfft1 : 0);
                  int index = xbase + ybase + zbase;
                  real term = qgrid[2 * index];
                  real t1 = thetai1[4 * ix];
                  real dt1 = nfft1 * thetai1[1 + 4 * ix];
                  de1 += term * dt1 * t2 * t3;
                  de2 += term * dt2 * t1 * t3;
                  de3 += term * dt3 * t1 * t2;
               }
            }
         } // end for (iz)


         real frcx = fi * (recipa.x * de1 + recipb.x * de2 + recipc.x * de3);
         real frcy = fi * (recipa.y * de1 + recipb.y * de2 + recipc.y * de3);
         real frcz = fi * (recipa.z * de1 + recipb.z * de2 + recipc.z * de3);
         atomic_add(frcx, gx, ii);
         atomic_add(frcy, gy, ii);
         atomic_add(frcz, gz, ii);
      }
   }
}


void echarge_ewald_fphi_self_acc(int vers)
{
   if (vers == calc::v0)
      echarge_acc3<calc::V0, 5>();
   else if (vers == calc::v1)
      echarge_acc3<calc::V1, 5>();
   else if (vers == calc::v3)
      echarge_acc3<calc::V3, 5>();
   else if (vers == calc::v4)
      echarge_acc3<calc::V4, 5>();
   else if (vers == calc::v5)
      echarge_acc3<calc::V5, 5>();
   else if (vers == calc::v6)
      echarge_acc3<calc::V6, 5>();
}
TINKER_NAMESPACE_END