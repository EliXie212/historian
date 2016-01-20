#include <cmath>
#include "pairhmm.h"

PairHMM::PairHMM (const ProbModel& l, const ProbModel& r, gsl_vector* root)
  : AlphabetOwner (l),
    l (l),
    r (r),
    root (root)
{
  imm_imi = log (rIns());
  imm_iiw = log (lIns() * rNoIns());
  imm_imm = log (lNoIns() * rNoIns() * lNoDel() * rNoDel());
  imm_imd = log (lNoIns() * rNoIns() * lNoDel() * rDel());
  imm_idm = log (lNoIns() * rNoIns() * lDel() * rNoDel());
  imm_eee = log (lNoIns() * rNoIns());

  imd_iix = log (lIns());
  imd_imm = log (lNoIns() * lNoDel() * rNoDelExt());
  imd_imd = log (lNoIns() * lNoDel() * rDelExt());
  imd_idm = log (lNoIns() * lDel() * rNoDelExt());
  imd_eee = log (lNoIns());

  idm_idi = log (rIns());
  idm_imm = log (rNoIns() * lNoDelExt() * rNoDel());
  idm_imd = log (rNoIns() * lNoDelExt() * rDel());
  idm_idm = log (rNoIns() * lDelExt() * rNoDel());
  idm_eee = log (rNoIns());

  imi_imi = log (rInsExt());
  imi_iiw = log (lIns() * rNoInsExt());
  imi_imm = log (lNoIns() * rNoInsExt() * lNoDel() * rNoDel());
  imi_imd = log (lNoIns() * rNoInsExt() * lNoDel() * rDel());
  imi_idm = log (lNoIns() * rNoInsExt() * lDel() * rNoDel());
  imi_eee = log (lNoIns() * rNoInsExt());

  iiw_iiw = log (lInsExt());
  iiw_imm = log (lNoInsExt() * lNoDel() * rNoDel());
  iiw_imd = log (lNoInsExt() * lNoDel() * rDel());
  iiw_idm = log (lNoInsExt() * lDel() * rNoDel());
  iiw_eee = log (lNoInsExt());

  idi_idi = log (rInsExt());
  idi_imm = log (rNoInsExt() * lNoDelExt() * rNoDel());
  idi_imd = log (rNoInsExt() * lNoDelExt() * rDel());
  idi_idm = log (rNoInsExt() * lDelExt() * rNoDel());
  idi_eee = log (rNoInsExt());

  iix_iix = log (lInsExt());
  iix_imm = log (lNoInsExt() * lNoDel() * rNoDelExt());
  iix_imd = log (lNoInsExt() * lNoDel() * rDelExt());
  iix_idm = log (lNoInsExt() * lDel() * rNoDelExt());
  iix_eee = log (lNoInsExt());
}

LogProb PairHMM::lpTrans (State src, State dest) const {
  switch (src) {
  case IMM:
    switch (dest) {
    case IMM: return imm_imm;
    case IMD: return imm_imd;
    case IDM: return imm_idm;
    case IMI: return imm_imi;
    case IIW: return imm_iiw;
    case EEE: return imm_eee;
    default:
      break;
    }
    break;

  case IMD:
    switch (dest) {
    case IMM: return imd_imm;
    case IMD: return imd_imd;
    case IDM: return imd_idm;
    case IIX: return imd_iix;
    case EEE: return imd_eee;
    default:
      break;
    }
    break;

  case IDM:
    switch (dest) {
    case IMM: return idm_imm;
    case IMD: return idm_imd;
    case IDM: return idm_idm;
    case IDI: return idm_idi;
    case EEE: return idm_eee;
    default:
      break;
    }
    break;

  case IMI:
    switch (dest) {
    case IMM: return imi_imm;
    case IMD: return imi_imd;
    case IDM: return imi_idm;
    case IMI: return imi_imi;
    case IIW: return imi_iiw;
    case EEE: return imi_eee;
    default:
      break;
    }
    break;

  case IDI:
    switch (dest) {
    case IMM: return idi_imm;
    case IMD: return idi_imd;
    case IDM: return idi_idm;
    case IDI: return idi_idi;
    case EEE: return idi_eee;
    default:
      break;
    }
    break;

  case IIW:
    switch (dest) {
    case IMM: return iiw_imm;
    case IMD: return iiw_imd;
    case IDM: return iiw_idm;
    case IIW: return iiw_iiw;
    case EEE: return iiw_eee;
    default:
      break;
    }
    break;

  case IIX:
    switch (dest) {
    case IMM: return iix_imm;
    case IMD: return iix_imd;
    case IDM: return iix_idm;
    case IIX: return iix_iix;
    case EEE: return iix_eee;
    default:
      break;
    }
    break;

  default:
    break;
  }
  return -numeric_limits<double>::infinity();
}

vector<PairHMM::State> PairHMM::states() {
  vector<PairHMM::State> s = { IMM, IMD, IDM, IMI, IDI, IIW, IIX };
  return s;
}
