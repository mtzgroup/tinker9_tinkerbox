#include "elec.h"
#include "echarge.h"
#include "empole.h"
#include "epolar.h"
#include "glob.chglj.h"
#include "glob.mplar.h"
#include "md.h"
#include "mod.vdwpot.h"
#include "nblist.h"
#include "pmestuf.h"
#include "potent.h"
#include "switch.h"
#include "tool/io_fort_str.h"
#include <tinker/detail/atoms.hh>
#include <tinker/detail/chgpot.hh>
#include <tinker/detail/couple.hh>
#include <tinker/detail/kchrge.hh>
#include <tinker/detail/limits.hh>
#include <tinker/detail/mplpot.hh>
#include <tinker/detail/mpole.hh>
#include <tinker/detail/polgrp.hh>
#include <tinker/detail/polpot.hh>


namespace tinker {
bool use_ewald()
{
   return limits::use_ewald;
}


//====================================================================//


void pchg_data(rc_op op)
{
   if (!use_potent(charge_term))
      return;


   if (op & rc_dealloc) {
      darray::deallocate(pchg);
   }


   if (op & rc_alloc) {
      darray::allocate(n, &pchg);
   }


   if (op & rc_init) {
      std::vector<real> pchgbuf(n);
      for (int i = 0; i < n; ++i) {
         int itype = atoms::type[i] - 1;
         pchgbuf[i] = kchrge::chg[itype];
      }
      darray::copyin(async_queue, n, pchg, pchgbuf.data());
      wait_for(async_queue);
   }
}


//====================================================================//


void pole_data(rc_op op)
{
   if (!use_potent(mpole_term) && !use_potent(polar_term) &&
       !use_potent(repuls_term))
      return;


   if (op & rc_dealloc) {
      darray::deallocate(zaxis, pole, rpole, udir, udirp, uind, uinp);
      darray::deallocate(trqx, trqy, trqz, vir_trq);
   }


   if (op & rc_alloc) {
      darray::allocate(n, &zaxis, &pole, &rpole);


      if (use_potent(polar_term)) {
         darray::allocate(n, &uind, &uinp, &udir, &udirp);
      } else {
         uind = nullptr;
         uinp = nullptr;
         udir = nullptr;
         udirp = nullptr;
      }


      if (rc_flag & calc::grad) {
         darray::allocate(n, &trqx, &trqy, &trqz);
      } else {
         trqx = nullptr;
         trqy = nullptr;
         trqz = nullptr;
      }
      if (rc_flag & calc::virial) {
         darray::allocate(buffer_size(), &vir_trq);
      } else {
         vir_trq = nullptr;
      }
   }


   if (op & rc_init) {
      // Regarding chkpole routine:
      // 1. The chiralities of the atoms are unlikely to change in MD; but
      // still possible in Monte Carlo;
      // 2. yaxis values are directly copied from Tinker, and are NOT
      // subtracted by 1 becasue of the checks in chkpole.
      static_assert(sizeof(LocalFrame) == 4 * sizeof(int), "");
      std::vector<LocalFrame> zaxisbuf(n);
      for (int i = 0; i < n; ++i) {
         zaxisbuf[i].zaxis = mpole::zaxis[i] - 1;
         zaxisbuf[i].xaxis = mpole::xaxis[i] - 1;
         zaxisbuf[i].yaxis = mpole::yaxis[i];
         fstr_view str = mpole::polaxe[i];
         int val;
         if (str == "Z-Only")
            val = pole_z_only;
         else if (str == "Z-then-X")
            val = pole_z_then_x;
         else if (str == "Bisector")
            val = pole_bisector;
         else if (str == "Z-Bisect")
            val = pole_z_bisect;
         else if (str == "3-Fold")
            val = pole_3_fold;
         else
            val = pole_none;
         zaxisbuf[i].polaxe = val;
      }
      darray::copyin(async_queue, n, zaxis, zaxisbuf.data());
      wait_for(async_queue);


      std::vector<double> polebuf(mpl_total * n);
      for (int i = 0; i < n; ++i) {
         int b1 = mpl_total * i;
         int b2 = mpole::maxpole * i;
         // Tinker c = 0, dx = 1, dy = 2, dz = 3
         // Tinker qxx = 4, qxy = 5, qxz = 6
         //        qyx    , qyy = 8, qyz = 9
         //        qzx    , qzy    , qzz = 12
         polebuf[b1 + mpl_pme_0] = mpole::pole[b2 + 0];
         polebuf[b1 + mpl_pme_x] = mpole::pole[b2 + 1];
         polebuf[b1 + mpl_pme_y] = mpole::pole[b2 + 2];
         polebuf[b1 + mpl_pme_z] = mpole::pole[b2 + 3];
         polebuf[b1 + mpl_pme_xx] = mpole::pole[b2 + 4];
         polebuf[b1 + mpl_pme_xy] = mpole::pole[b2 + 5];
         polebuf[b1 + mpl_pme_xz] = mpole::pole[b2 + 6];
         polebuf[b1 + mpl_pme_yy] = mpole::pole[b2 + 8];
         polebuf[b1 + mpl_pme_yz] = mpole::pole[b2 + 9];
         polebuf[b1 + mpl_pme_zz] = mpole::pole[b2 + 12];
      }
      darray::copyin(async_queue, n, pole, polebuf.data());
      wait_for(async_queue);
   }
}


void mdpuscale_data(rc_op op)
{
   if (not use_potent(mpole_term) and not use_potent(polar_term))
      return;


   if (op & rc_dealloc) {
      nmexclude = 0;
      darray::deallocate(mexclude, mexclude_scale);
      nmdpuexclude = 0;
      darray::deallocate(mdpuexclude, mdpuexclude_scale);
   }


   if (op & rc_alloc) {
      using key_t = std::pair<int, int>;
      struct m
      {
         real m;
      };
      std::map<key_t, m> ikm;
      auto insert_m = [](std::map<key_t, m>& a, int i, int k, real val,
                         char ch) {
         key_t key;
         key.first = i;
         key.second = k;
         auto it = a.find(key);
         if (it == a.end()) {
            m x;
            x.m = 1;
            if (ch == 'm')
               x.m = val;
            a[key] = x;
         } else {
            if (ch == 'm')
               it->second.m = val;
         }
      };
      struct mdpu
      {
         real m, d, p, u;
      };
      std::map<key_t, mdpu> ik_scale;
      auto insert_mdpu = [](std::map<key_t, mdpu>& a, int i, int k, real val,
                            char ch) {
         key_t key;
         key.first = i;
         key.second = k;
         auto it = a.find(key);
         if (it == a.end()) {
            mdpu x;
            x.m = 1;
            x.d = 1;
            x.p = 1;
            x.u = 1;
            if (ch == 'm')
               x.m = val;
            else if (ch == 'd')
               x.d = val;
            else if (ch == 'p')
               x.p = val;
            else if (ch == 'u')
               x.u = val;
            a[key] = x;
         } else {
            if (ch == 'm')
               it->second.m = val;
            else if (ch == 'd')
               it->second.d = val;
            else if (ch == 'p')
               it->second.p = val;
            else if (ch == 'u')
               it->second.u = val;
         }
      };


      // see also attach.f
      const int maxn12 = sizes::maxval;
      const int maxn13 = 3 * sizes::maxval;
      const int maxn14 = 9 * sizes::maxval;
      const int maxn15 = 27 * sizes::maxval;
      const int* couple_i12 = &couple::i12[0][0];
      const int* couple_i13 = couple::i13;
      const int* couple_i14 = couple::i14;
      const int* couple_i15 = couple::i15;
      const real m2scale = mplpot::m2scale;
      const real m3scale = mplpot::m3scale;
      const real m4scale = mplpot::m4scale;
      const real m5scale = mplpot::m5scale;


      const int maxp11 = polgrp::maxp11;
      const int maxp12 = polgrp::maxp12;
      const int maxp13 = polgrp::maxp13;
      const int maxp14 = polgrp::maxp14;
      const real d1scale = polpot::d1scale;
      const real d2scale = polpot::d2scale;
      const real d3scale = polpot::d3scale;
      const real d4scale = polpot::d4scale;
      const real p2scale = polpot::p2scale;
      const real p3scale = polpot::p3scale;
      const real p4scale = polpot::p4scale;
      const real p5scale = polpot::p5scale;
      const real p2iscale = polpot::p2iscale;
      const real p3iscale = polpot::p3iscale;
      const real p4iscale = polpot::p4iscale;
      const real p5iscale = polpot::p5iscale;
      const real u1scale = polpot::u1scale;
      const real u2scale = polpot::u2scale;
      const real u3scale = polpot::u3scale;
      const real u4scale = polpot::u4scale;


      int nn, bask;
      for (int i = 0; i < n; ++i) {
         // m
         if (m2scale != 1) {
            nn = couple::n12[i];
            bask = i * maxn12;
            for (int j = 0; j < nn; ++j) {
               int k = couple_i12[bask + j];
               k -= 1;
               if (k > i) {
                  insert_m(ikm, i, k, m2scale, 'm');
                  insert_mdpu(ik_scale, i, k, m2scale, 'm');
               }
            }
         }

         if (m3scale != 1) {
            nn = couple::n13[i];
            bask = i * maxn13;
            for (int j = 0; j < nn; ++j) {
               int k = couple_i13[bask + j];
               k -= 1;
               if (k > i) {
                  insert_m(ikm, i, k, m3scale, 'm');
                  insert_mdpu(ik_scale, i, k, m3scale, 'm');
               }
            }
         }

         if (m4scale != 1) {
            nn = couple::n14[i];
            bask = i * maxn14;
            for (int j = 0; j < nn; ++j) {
               int k = couple_i14[bask + j];
               k -= 1;
               if (k > i) {
                  insert_m(ikm, i, k, m4scale, 'm');
                  insert_mdpu(ik_scale, i, k, m4scale, 'm');
               }
            }
         }

         if (m5scale != 1) {
            nn = couple::n15[i];
            bask = i * maxn15;
            for (int j = 0; j < nn; ++j) {
               int k = couple_i15[bask + j];
               k -= 1;
               if (k > i) {
                  insert_m(ikm, i, k, m5scale, 'm');
                  insert_mdpu(ik_scale, i, k, m5scale, 'm');
               }
            }
         }
      }


      const bool usepolar = use_potent(polar_term);
      for (int i = 0; usepolar and i < n; ++i) {
         // p
         if (p2scale != 1 || p2iscale != 1) {
            nn = couple::n12[i];
            bask = i * maxn12;
            for (int j = 0; j < nn; ++j) {
               int k = couple_i12[bask + j];
               real val = p2scale;
               for (int jj = 0; jj < polgrp::np11[i]; ++jj) {
                  if (k == polgrp::ip11[i * maxp11 + jj])
                     val = p2iscale;
               }
               k -= 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, val, 'p');
               }
            }
         }

         if (p3scale != 1 || p3iscale != 1) {
            nn = couple::n13[i];
            bask = i * maxn13;
            for (int j = 0; j < nn; ++j) {
               int k = couple_i13[bask + j];
               real val = p3scale;
               for (int jj = 0; jj < polgrp::np11[i]; ++jj) {
                  if (k == polgrp::ip11[i * maxp11 + jj])
                     val = p3iscale;
               }
               k -= 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, val, 'p');
               }
            }
         }

         if (p4scale != 1 || p4iscale != 1) {
            nn = couple::n14[i];
            bask = i * maxn14;
            for (int j = 0; j < nn; ++j) {
               int k = couple_i14[bask + j];
               real val = p4scale;
               for (int jj = 0; jj < polgrp::np11[i]; ++jj) {
                  if (k == polgrp::ip11[i * maxp11 + jj])
                     val = p4iscale;
               }
               k -= 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, val, 'p');
               }
            }
         }

         if (p5scale != 1 || p5iscale != 1) {
            nn = couple::n15[i];
            bask = i * maxn15;
            for (int j = 0; j < nn; ++j) {
               int k = couple_i15[bask + j];
               real val = p5scale;
               for (int jj = 0; jj < polgrp::np11[i]; ++jj) {
                  if (k == polgrp::ip11[i * maxp11 + jj])
                     val = p5iscale;
               }
               k -= 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, val, 'p');
               }
            }
         }

         // d
         if (d1scale != 1) {
            nn = polgrp::np11[i];
            bask = i * maxp11;
            for (int j = 0; j < nn; ++j) {
               int k = polgrp::ip11[bask + j] - 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, d1scale, 'd');
               }
            }
         }

         if (d2scale != 1) {
            nn = polgrp::np12[i];
            bask = i * maxp12;
            for (int j = 0; j < nn; ++j) {
               int k = polgrp::ip12[bask + j] - 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, d2scale, 'd');
               }
            }
         }

         if (d3scale != 1) {
            nn = polgrp::np13[i];
            bask = i * maxp13;
            for (int j = 0; j < nn; ++j) {
               int k = polgrp::ip13[bask + j] - 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, d3scale, 'd');
               }
            }
         }

         if (d4scale != 1) {
            nn = polgrp::np14[i];
            bask = i * maxp14;
            for (int j = 0; j < nn; ++j) {
               int k = polgrp::ip14[bask + j] - 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, d4scale, 'd');
               }
            }
         }

         // u
         if (u1scale != 1) {
            nn = polgrp::np11[i];
            bask = i * maxp11;
            for (int j = 0; j < nn; ++j) {
               int k = polgrp::ip11[bask + j] - 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, u1scale, 'u');
               }
            }
         }

         if (u2scale != 1) {
            nn = polgrp::np12[i];
            bask = i * maxp12;
            for (int j = 0; j < nn; ++j) {
               int k = polgrp::ip12[bask + j] - 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, u2scale, 'u');
               }
            }
         }

         if (u3scale != 1) {
            nn = polgrp::np13[i];
            bask = i * maxp13;
            for (int j = 0; j < nn; ++j) {
               int k = polgrp::ip13[bask + j] - 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, u3scale, 'u');
               }
            }
         }

         if (u4scale != 1) {
            nn = polgrp::np14[i];
            bask = i * maxp14;
            for (int j = 0; j < nn; ++j) {
               int k = polgrp::ip14[bask + j] - 1;
               if (k > i) {
                  insert_mdpu(ik_scale, i, k, u4scale, 'u');
               }
            }
         }
      }


      std::vector<int> exclik;
      std::vector<real> excls;
      for (auto& it : ikm) {
         exclik.push_back(it.first.first);
         exclik.push_back(it.first.second);
         excls.push_back(it.second.m);
      }
      nmexclude = excls.size();
      darray::allocate(nmexclude, &mexclude, &mexclude_scale);
      darray::copyin(async_queue, nmexclude, mexclude, exclik.data());
      darray::copyin(async_queue, nmexclude, mexclude_scale, excls.data());
      wait_for(async_queue);


      std::vector<int> ik_vec;
      std::vector<real> scal_vec;
      for (auto& it : ik_scale) {
         ik_vec.push_back(it.first.first);
         ik_vec.push_back(it.first.second);
         scal_vec.push_back(it.second.m);
         scal_vec.push_back(it.second.d);
         scal_vec.push_back(it.second.p);
         scal_vec.push_back(it.second.u);
      }
      nmdpuexclude = ik_scale.size();
      darray::allocate(nmdpuexclude, &mdpuexclude, &mdpuexclude_scale);
      darray::copyin(async_queue, nmdpuexclude, mdpuexclude, ik_vec.data());
      darray::copyin(async_queue, nmdpuexclude, mdpuexclude_scale,
                     scal_vec.data());
      wait_for(async_queue);
   }


   if (op & rc_init) {
   }
}

//====================================================================//


void elec_data(rc_op op)
{
   if (op & rc_init) {
      electric = chgpot::electric;
      dielec = chgpot::dielec;
   }
   rc_man pchg42{pchg_data, op};
   rc_man pole42{pole_data, op};
   rc_man mdpuscale42{mdpuscale_data, op};
}


//====================================================================//


void mpole_init(int vers)
{
   if (vers & calc::grad)
      darray::zero(async_queue, n, trqx, trqy, trqz);
   if (vers & calc::virial)
      darray::zero(async_queue, buffer_size(), vir_trq);


   chkpole();
   rotpole();


   if (use_ewald()) {
      rpole_to_cmp();
      if (vir_m)
         darray::zero(async_queue, buffer_size(), vir_m);
      if (pltfm_config & CU_PLTFM) {
         bool precompute_theta = (!TINKER_CU_THETA_ON_THE_FLY_GRID_MPOLE) ||
            (!TINKER_CU_THETA_ON_THE_FLY_GRID_UIND);
         if (epme_unit.valid()) {
            if (precompute_theta)
               bspline_fill(epme_unit, 3);
         }
         if (ppme_unit.valid() && (ppme_unit != epme_unit)) {
            if (precompute_theta)
               bspline_fill(ppme_unit, 2);
         }
         if (pvpme_unit.valid()) {
            if (precompute_theta)
               bspline_fill(pvpme_unit, 2);
         }
      }
   }
}


void chkpole()
{
   chkpole_acc();
}


void rotpole()
{
   rotpole_acc();
}


void torque(int vers, grad_prec* dx, grad_prec* dy, grad_prec* dz)
{
   // #if TINKER_CUDART
   //    if (pltfm_config & CU_PLTFM)
   //    else
   // #endif
   torque_acc(vers, dx, dy, dz);
}


bool amoeba_emplar(int vers)
{
   if (mplpot::use_chgpen)
      return false;
   if (rc_flag & calc::analyz)
      return false;
   if (vers & calc::analyz)
      return false;


   return use_potent(mpole_term) && use_potent(polar_term) &&
      (mlist_version() & NBL_SPATIAL);
}


bool amoeba_empole(int vers)
{
   if (mplpot::use_chgpen)
      return false;


   if (amoeba_emplar(vers))
      return false;
   return use_potent(mpole_term);
}


bool amoeba_epolar(int vers)
{
   if (mplpot::use_chgpen)
      return false;


   if (amoeba_emplar(vers))
      return false;
   return use_potent(polar_term);
}


bool amoeba_echglj(int vers)
{
   if (rc_flag & calc::analyz)
      return false;
   if (vers & calc::analyz)
      return false;
   if (!use_potent(charge_term) || !use_potent(vdw_term))
      return false;
   if (!(clist_version() & NBL_SPATIAL))
      return false;
   if (ebuffer != 0)
      return false;
   if (vdwtyp != evdw_t::lj)
      return false;
   if (vdwpr_in_use)
      return false;
   return true;
}


bool amoeba_echarge(int vers)
{
   if (amoeba_echglj(vers))
      return false;
   return use_potent(charge_term);
}


bool amoeba_evdw(int vers)
{
   if (amoeba_echglj(vers))
      return false;
   return use_potent(vdw_term);
}
}
