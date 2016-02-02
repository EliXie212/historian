#ifndef SUMPROD_INCLUDED
#define SUMPROD_INCLUDED

#include <gsl/gsl_matrix_complex_double.h>
#include <gsl/gsl_vector_complex_double.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>

#include "model.h"
#include "tree.h"
#include "fastseq.h"
#include "alignpath.h"

class EigenModel {
public:
  const RateModel& model;

  gsl_vector_complex *eval;
  gsl_matrix_complex *evec;  // right eigenvectors
  gsl_matrix_complex *evecInv;  // left eigenvectors

  EigenModel (const RateModel& model);
  ~EigenModel();

  gsl_matrix* getSubProb (double t) const;

  gsl_matrix_complex* eigenSubCount (double t) const;
  void accumSubCount (gsl_matrix* count, AlphTok a, AlphTok b, double weight, const gsl_matrix* sub, const gsl_matrix_complex* eSubCount);
  
private:
  vguard<gsl_complex> ev, ev_t, exp_ev_t;
  void compute_exp_ev_t (double t);
  
  EigenModel (const EigenModel&) = delete;
  EigenModel& operator= (const EigenModel&) = delete;
};

class AlignColSumProduct {
public:
  const RateModel& model;
  const Tree& tree;
  const vguard<FastSeq>& gapped;  // tree node index must match alignment row index

  vguard<LogProb> logInsProb;
  vguard<vguard<vguard<LogProb> > > branchLogSubProb;  // branchLogSubProb[node][parentState][nodeState]

  EigenModel eigen;
  vguard<gsl_matrix_complex*> branchEigenSubCount;

  AlignColIndex col;
  vguard<AlignRowIndex> ungappedRows;

  // F_n(x_n): variable->function, tip->root messages
  // E_n(x_p): function->variable, tip->root messages
  // G_n(x_n): function->variable, root->tip messages
  // G_p(x_p)*E_s(x_p): variable->function, root->tip messages
  vguard<vguard<LogProb> > logE, logF, logG;
  LogProb colLogLike;  // marginal likelihood, all unobserved states summed out
  
  AlignColSumProduct (const RateModel& model, const Tree& tree, const vguard<FastSeq>& gapped);
  ~AlignColSumProduct();

  bool alignmentDone() const;
  void nextColumn();

  inline bool isGap (AlignRowIndex row) const { return Alignment::isGap (gapped[row].seq[col]); }
  inline bool isWild (AlignRowIndex row) const { return Alignment::isWildcard (gapped[row].seq[col]); }
  inline bool columnEmpty() const { return ungappedRows.empty(); }
  inline AlignRowIndex root() const { return ungappedRows.back(); }

  void fillUp();  // E, F
  void fillDown();  // G

  vguard<LogProb> logNodePostProb (AlignRowIndex node) const;
  LogProb logBranchPostProb (AlignRowIndex node, AlphTok parentState, AlphTok nodeState) const;
  AlphTok maxPostState (AlignRowIndex node) const;  // maximum a posteriori reconstruction

  void accumulateCounts (gsl_vector* rootCounts, gsl_matrix_complex* eigenCounts) const;
  gsl_matrix* getSubCounts (gsl_matrix_complex* eigenCounts) const;  // wait times on diagonal

private:
  void initColumn();  // populates ungappedRows

  AlignColSumProduct (const AlignColSumProduct&) = delete;
  AlignColSumProduct& operator= (const AlignColSumProduct&) = delete;
};

#endif /* SUMPROD_INCLUDED */