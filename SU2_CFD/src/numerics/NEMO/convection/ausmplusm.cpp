/*!
 * \file ausmplusup2.cpp
 * \brief Implementations of the AUSM-family of schemes - AUSM+UP2.
 * \author W. Maier, A. Sachedeva, C. Garbacz
 * \version 7.0.8 "Blackbird"
 *
 * SU2 Project Website: https://su2code.github.io
 *
 * The SU2 Project is maintained by the SU2 Foundation
 * (http://su2foundation.org)
 *
 * Copyright 2012-2020, SU2 Contributors (cf. AUTHORS.md)
 *
 * SU2 is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SU2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SU2. If not, see <http://www.gnu.org/licenses/>.
 */

#include "../../../../include/numerics/NEMO/convection/ausmplusm.hpp"

CUpwAUSMPLUSM_NEMO::CUpwAUSMPLUSM_NEMO(unsigned short val_nDim, unsigned short val_nVar,
                                           unsigned short val_nPrimVar,
                                           unsigned short val_nPrimVarGrad,
                                           CConfig *config): CNEMONumerics (val_nDim, val_nVar, val_nPrimVar, val_nPrimVarGrad,
                                                          config){


  unsigned short iVar;

  /*--- Define useful constants ---*/
  Kp       = 0.25;
  sigma    = 1.0;

  /*--- Allocate data structures ---*/
  FcL    = new su2double [nVar];
  FcR    = new su2double [nVar];
  dmLP   = new su2double [nVar];
  dmRM   = new su2double [nVar];
  dpLP   = new su2double [nVar];
  dpRM   = new su2double [nVar];
  daL    = new su2double [nVar];
  daR    = new su2double [nVar];
  rhos_i = new su2double [nSpecies];
  rhos_j = new su2double [nSpecies];
  u_i    = new su2double [nDim];
  u_j    = new su2double [nDim];

  /*--- Allocate arrays ---*/
  Diff_U      = new su2double [nVar];
  RoeU        = new su2double[nVar];
  RoeV        = new su2double[nPrimVar];
  RoedPdU     = new su2double [nVar];
  RoeEve      = new su2double [nSpecies];
  Lambda      = new su2double [nVar];
  Epsilon     = new su2double [nVar];
  P_Tensor    = new su2double* [nVar];
  invP_Tensor = new su2double* [nVar];
  for (iVar = 0; iVar < nVar; iVar++) {
    P_Tensor[iVar] = new su2double [nVar];
    invP_Tensor[iVar] = new su2double [nVar];
  }

  Flux   = new su2double[nVar];
}

CUpwAUSMPLUSM_NEMO::~CUpwAUSMPLUSM_NEMO(void) {

  delete [] FcL;
  delete [] FcR;
  delete [] dmLP;
  delete [] dmRM;
  delete [] dpLP;
  delete [] dpRM;
  delete [] rhos_i;
  delete [] rhos_j;
  delete [] u_i;
  delete [] u_j;
  unsigned short iVar;

  delete [] Diff_U;
  delete [] RoeU;
  delete [] RoeV;
  delete [] RoedPdU;
  delete [] RoeEve;
  delete [] Lambda;
  delete [] Epsilon;
  for (iVar = 0; iVar < nVar; iVar++) {
    delete [] P_Tensor[iVar];
    delete [] invP_Tensor[iVar];
  }
  delete [] P_Tensor;
  delete [] invP_Tensor;
  delete [] Flux;
}

CNumerics::ResidualType<> CUpwAUSMPLUSM_NEMO::ComputeResidual(const CConfig *config) {

  unsigned short iDim, iVar, iSpecies;
  su2double rho_i, rho_j,
  e_ve_i, e_ve_j, mL, mR, mLP, mRM, mF, pLP, pRM, pF, Phi, sq_veli, sq_velj;

  /*--- Face area ---*/
  Area = 0.0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);

  /*-- Unit Normal ---*/
  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;

  Minf  = config->GetMach();

  /*--- Extracting primitive variables ---*/
  // Primitives: [rho1,...,rhoNs, T, Tve, u, v, w, P, rho, h, a, c]
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++){
    rhos_i[iSpecies] = V_i[RHOS_INDEX+iSpecies];
    rhos_j[iSpecies] = V_j[RHOS_INDEX+iSpecies];
  }

  sq_veli = 0.0; sq_velj = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    u_i[iDim]  = V_i[VEL_INDEX+iDim];
    u_j[iDim]  = V_j[VEL_INDEX+iDim];
    sq_veli   += u_i[iDim]*u_i[iDim];
    sq_velj   += u_j[iDim]*u_j[iDim];
  }

  P_i   = V_i[P_INDEX];   P_j   = V_j[P_INDEX];
  h_i   = V_i[H_INDEX];   h_j   = V_j[H_INDEX];
  a_i   = V_i[A_INDEX];   a_j   = V_j[A_INDEX];
  rho_i = V_i[RHO_INDEX]; rho_j = V_j[RHO_INDEX];

  rhoCvtr_i = V_i[RHOCVTR_INDEX]; rhoCvtr_j = V_j[RHOCVTR_INDEX];
  rhoCvve_i = V_i[RHOCVVE_INDEX]; rhoCvve_j = V_j[RHOCVVE_INDEX];

  e_ve_i = 0.0; e_ve_j = 0.0;
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    e_ve_i += (rhos_i[iSpecies]*eve_i[iSpecies])/rho_i;
    e_ve_j += (rhos_j[iSpecies]*eve_j[iSpecies])/rho_j;
  }

  /*--- Projected velocities ---*/
  ProjVel_i = 0.0; ProjVel_j = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    ProjVel_i += u_i[iDim]*UnitNormal[iDim];
    ProjVel_j += u_j[iDim]*UnitNormal[iDim];
  }

  su2double sqVi, sqVj, gam,Hnorm,gtl_i,gtl_j,atl,aij;

  sqVi = 0.0;   sqVj = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    sqVi += (u_i[iDim]-ProjVel_i*UnitNormal[iDim]) *
            (u_i[iDim]-ProjVel_i*UnitNormal[iDim]);
    sqVj += (u_j[iDim]-ProjVel_j*UnitNormal[iDim]) *
            (u_j[iDim]-ProjVel_j*UnitNormal[iDim]);
  }

  /*--- Calculate interface numerical gammas and speed of sound ---*/
  Hnorm = 0.5*(h_i-0.5*sqVi + h_j-0.5*sqVj);
  gtl_i = Gamma_i;
  gtl_j = Gamma_j;
  gam   = 0.5*(gtl_i+gtl_j);
  atl = sqrt(2.0*Hnorm*(gam-1.0)/(gam+1.0));

  // ATL has to account sound speed for reactive gases

 /* if (fabs(rho_i-rho_j)/(0.5*(rho_i+rho_j)) < 1E-3)   // Why this If condition ?
    atl = sqrt(2.0*Hnorm*(gam-1.0)/(gam+1.0));
  else {
    //Is the bottom equation correct ??
    atl = sqrt(2.0*Hnorm * (((gtl_i-1.0)/(gtl_i*rho_i) - (gtl_j-1.0)/(gtl_j*rho_j))/
                            ((gtl_j+1.0)/(gtl_j*rho_i) - (gtl_i+1.0)/(gtl_i*rho_j))));


    // Or should it be
    atl = sqrt(2.0*Hnorm * (((gtl_i-1.0)/(gtl_i)*rho_i - (gtl_j-1.0)/(gtl_j)*rho_j)/
                            ((gtl_j+1.0)/(gtl_j)*rho_i - (gtl_i+1.0)/(gtl_i)*rho_j)));
  } */


  if (0.5*(ProjVel_i+ProjVel_j) >= 0.0) aij = atl*atl/max(fabs(ProjVel_i),atl);
  else                                  aij = atl*atl/max(fabs(ProjVel_j),atl);

  aF=aij;

  mL  = ProjVel_i/aF;
  mR  = ProjVel_j/aF;

  rhoF = 0.5*(rho_i+rho_j);
  MFsq = 0.5*(sq_veli+sq_velj)/(aF*aF);

  param1 = max(MFsq, Minf*Minf);
  Mrefsq = (min(1.0, param1));
  fa = 2.0*sqrt(Mrefsq)-Mrefsq;

  alpha = 3.0/16.0*(-4.0+5.0*fa*fa);
  beta = 1.0/8.0;

  /*--- Pressure diffusion term ---*/


  su2double f,g,h_k,P_k,Point_aux;

  h_k=1.0;

  if (jPoint!=-1){
      for (auto Point_aux : nemo_geometry->nodes->GetPoints(jPoint)) {
         P_k = nemo_solution->GetPrimitive(Point_aux,P_INDEX)/nemo_solution->GetPrimitive(jPoint,P_INDEX);
         h_k = min(h_k,P_k);
         h_k = min(h_k,1/P_k);
      }
  }

  for (auto Point_aux : nemo_geometry->nodes->GetPoints(iPoint)) {
         P_k = nemo_solution->GetPrimitive(Point_aux,P_INDEX)/nemo_solution->GetPrimitive(iPoint,P_INDEX);
         h_k = min(h_k,P_k);
         h_k = min(h_k,1/P_k);
  }

  g=0.5*(1+cos(PI_NUMBER*h_k));
  f=0.5*(1-cos(PI_NUMBER*min(1.0,max(abs(mL),abs(mR)))));

  Mp = -0.5*(1-f)*(P_j-P_i)/(rhoF*aF*aF)*(1-g);

  if (fabs(mL) <= 1.0) mLP = 0.25*(mL+1.0)*(mL+1.0)+beta*(mL*mL-1.0)*(mL*mL-1.0);
  else                 mLP = 0.5*(mL+fabs(mL));

  if (fabs(mR) <= 1.0) mRM = -0.25*(mR-1.0)*(mR-1.0)-beta*(mR*mR-1.0)*(mR*mR-1.0);
  else                 mRM = 0.5*(mR-fabs(mR));

  mF = mLP + mRM + Mp;

  if (fabs(mL) <= 1.0) pLP = (0.25*(mL+1.0)*(mL+1.0)*(2.0-mL)+alpha*mL*(mL*mL-1.0)*(mL*mL-1.0));
  else                 pLP = 0.5*(mL+fabs(mL))/mL;

  if (fabs(mR) <= 1.0) pRM = (0.25*(mR-1.0)*(mR-1.0)*(2.0+mR)-alpha*mR*(mR*mR-1.0)*(mR*mR-1.0));
  else                 pRM = 0.5*(mR-fabs(mR))/mR;


  su2double f0;
  f0=min(1.0,max(f,Minf*Minf));

  // This is an alternative formulation already used by AUSMPLUSUP2 (Only multiplied by f0)
  pFi = f0*sqrt(0.5*(sq_veli+sq_velj))*(pLP+pRM-1.0)*0.5*(rho_j+rho_i)*aF;
 // pFi = f0*(pLP+pRM-1.0)*0.5*(P_i+P_j);

   pF  = 0.5*(P_j+P_i)+0.5*(pLP-pRM)*(P_i-P_j)+pFi;

  Phi = fabs(mF);

  mfP=0.5*(mF+Phi);
  mfM=0.5*(mF-Phi);

  /*--- Assign left & right covective fluxes ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    FcL[iSpecies] = rhos_i[iSpecies]*aF;
    FcR[iSpecies] = rhos_j[iSpecies]*aF;
  }
  for (iDim = 0; iDim < nDim; iDim++) {
    FcL[nSpecies+iDim] = rho_i*aF*u_i[iDim];
    FcR[nSpecies+iDim] = rho_j*aF*u_j[iDim];
  }
  FcL[nSpecies+nDim]   = rho_i*aF*h_i;
  FcR[nSpecies+nDim]   = rho_j*aF*h_j;
  FcL[nSpecies+nDim+1] = rho_i*aF*e_ve_i;
  FcR[nSpecies+nDim+1] = rho_j*aF*e_ve_j;

  /*--- Compute numerical flux ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    Flux[iVar] = (mfP*FcL[iVar]+mfM*FcR[iVar])*Area;

  for (iDim = 0; iDim < nDim; iDim++)
    Flux[nSpecies+iDim] += (pF*UnitNormal[iDim]-g*gam*0.5*(P_i+P_j)/aF*pLP*pRM*(u_j[iDim]-ProjVel_j*UnitNormal[iDim]-u_i[iDim]+ProjVel_i*UnitNormal[iDim]))*Area;

  return ResidualType<>(Flux, nullptr, nullptr);
}

