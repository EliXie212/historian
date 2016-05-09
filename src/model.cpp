#include <math.h>

#include <gsl/gsl_linalg.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_min.h>
#include <gsl/gsl_complex_math.h>

#include <algorithm>
#include <set>

#include "model.h"
#include "jsonutil.h"
#include "util.h"
#include "alignpath.h"
#include "logger.h"
#include "sumprod.h"

struct DistanceMatrixParams {
  const map<pair<AlphTok,AlphTok>,int>& pairCount;
  const RateModel& model;
  DistanceMatrixParams (const map<pair<AlphTok,AlphTok>,int>& pairCount, const RateModel& model)
    : pairCount(pairCount),
      model(model)
  { }
  double tML (int maxIterations) const;
  double negLogLike (double t) const;
};

void AlphabetOwner::readAlphabet (const JsonValue& json) {
  const JsonMap jm (json);
  Assert (jm.containsType("alphabet",JSON_STRING), "No alphabet");
  initAlphabet (jm["alphabet"].toString());
}

void AlphabetOwner::initAlphabet (const string& a) {
  alphabet = a;
  set<char> s;
  for (auto c : alphabet) {
    Assert (s.find(c) == s.end(), "Duplicate character %c in alphabet %s", c, alphabet.c_str());
    s.insert (c);
  }
}

UnvalidatedAlphTok AlphabetOwner::tokenize (char c) const {
  return ::tokenize (c, alphabet);
}

AlphTok AlphabetOwner::tokenizeOrDie (char c) const {
  UnvalidatedAlphTok tok = tokenize (c);
  if (tok >= 0 && tok < alphabetSize())
    return tok;
  Abort ("Character '%c' is not a member of alphabet '%s'", c, alphabet.c_str());
  return 0;
}

gsl_matrix* AlphabetOwner::newAlphabetMatrix() const {
  return gsl_matrix_alloc (alphabetSize(), alphabetSize());
}

gsl_vector* AlphabetOwner::newAlphabetVector() const {
  return gsl_vector_alloc (alphabetSize());
}

RateModel::RateModel()
  : subRate(NULL),
    insProb(NULL)
{ }

RateModel::RateModel (const RateModel& model)
  : subRate(NULL),
    insProb(NULL)
{
  *this = model;
}

RateModel::~RateModel()
{
  if (subRate)
    gsl_matrix_free (subRate);
  if (insProb)
    gsl_vector_free (insProb);
}

RateModel& RateModel::operator= (const RateModel& model) {
  if (subRate)
    gsl_matrix_free (subRate);
  if (insProb)
    gsl_vector_free (insProb);

  initAlphabet (model.alphabet);
  insRate = model.insRate;
  delRate = model.delRate;
  insExtProb = model.insExtProb;
  delExtProb = model.delExtProb;
  insProb = newAlphabetVector();
  subRate = newAlphabetMatrix();
  CheckGsl (gsl_vector_memcpy (insProb, model.insProb));
  CheckGsl (gsl_matrix_memcpy (subRate, model.subRate));
  return *this;
}

void RateModel::init (const string& alph) {
  if (subRate)
    gsl_matrix_free (subRate);
  if (insProb)
    gsl_vector_free (insProb);

  initAlphabet (alph);
  
  insProb = newAlphabetVector();
  subRate = newAlphabetMatrix();
}

void RateModel::read (const JsonValue& json) {
  Assert (subRate == NULL, "RateModel already initialized");

  readAlphabet (json);

  subRate = newAlphabetMatrix();
  gsl_matrix_set_zero (subRate);

  const JsonMap jm (json);
  const JsonMap rateMatrix = jm.getObject ("subrate");
  for (auto ci : alphabet) {
    AlphTok i = (AlphTok) tokenize (ci);
    const string si (1, ci);
    if (rateMatrix.contains (si)) {
      const JsonMap rateMatrixRow = rateMatrix.getObject (si);
      for (auto cj : alphabet)
	if (cj != ci) {
	  AlphTok j = (AlphTok) tokenize (cj);
	  const string sj (1, cj);
	  if (rateMatrixRow.contains (sj)) {
	    const double rate = rateMatrixRow.getNumber (string (1, cj));
	    *(gsl_matrix_ptr (subRate, i, j)) += rate;
	    *(gsl_matrix_ptr (subRate, i, i)) -= rate;
	  }
	}
    }
  }

  if (jm.contains("rootprob")) {
    const JsonMap insVec = jm.getObject ("rootprob");
    insProb = newAlphabetVector();
    gsl_vector_set_zero (insProb);

    for (auto ci : alphabet) {
      AlphTok i = (AlphTok) tokenize (ci);
      const string si (1, ci);
      if (insVec.contains (si))
	gsl_vector_set (insProb, i, insVec.getNumber(si));
    }
  } else
    insProb = getEqmProbVector();

  insRate = jm.getNumber("insrate");
  insExtProb = jm.getNumber("insextprob");
  delRate = jm.getNumber("delrate");
  delExtProb = jm.getNumber("delextprob");
}

void RateModel::write (ostream& out) const {
  out << "{" << endl;
  out << " \"alphabet\": \"" << alphabet << "\"," << endl;
  out << " \"insrate\": " << insRate << "," << endl;
  out << " \"insextprob\": " << insExtProb << "," << endl;
  out << " \"delrate\": " << delRate << "," << endl;
  out << " \"delextprob\": " << delExtProb << "," << endl;
  out << " \"rootprob\":" << endl;
  out << " {";
  for (AlphTok i = 0; i < alphabetSize(); ++i)
    out << (i>0 ? "," : "") << "\n  \"" << alphabet[i] << "\": " << gsl_vector_get(insProb,i);
  out << endl;
  out << " }," << endl;
  out << " \"subrate\":" << endl;
  out << " {" << endl;
  for (AlphTok i = 0; i < alphabetSize(); ++i) {
    out << "  \"" << alphabet[i] << "\": {";
    for (AlphTok j = 0; j < alphabetSize(); ++j)
      if (i != j)
	out << " \"" << alphabet[j] << "\": " << gsl_matrix_get(subRate,i,j) << (j < alphabetSize() - (i == alphabetSize() - 1 ? 2 : 1) ? "," : "");
    out << " }" << (i < alphabetSize() - 1 ? "," : "") << endl;
  }
  out << " }" << endl;
  out << "}" << endl;
}

gsl_vector* RateModel::getEqmProbVector() const {
  // find eqm via QR decomposition
  gsl_matrix* QR = gsl_matrix_alloc (alphabetSize() + 1, alphabetSize());
  for (AlphTok j = 0; j < alphabetSize(); ++j) {
    for (AlphTok i = 0; i < alphabetSize(); ++i)
      gsl_matrix_set (QR, i, j, gsl_matrix_get (subRate, j, i));
    gsl_matrix_set (QR, alphabetSize(), j, 1);
  }

  gsl_vector* tau = newAlphabetVector();

  CheckGsl (gsl_linalg_QR_decomp (QR, tau));

  gsl_vector* b = gsl_vector_alloc (alphabetSize() + 1);
  gsl_vector_set_zero (b);
  gsl_vector_set (b, alphabetSize(), 1);

  gsl_vector* residual = gsl_vector_alloc (alphabetSize() + 1);
  gsl_vector* eqm = newAlphabetVector();
  
  CheckGsl (gsl_linalg_QR_lssolve (QR, tau, b, eqm, residual));

  // make sure no "probabilities" have become negative & (re-)normalize
  double eqmNorm = 0;
  for (AlphTok i = 0; i < alphabetSize(); ++i) {
    const double eqm_i = max (0., gsl_vector_get (eqm, i));
    gsl_vector_set (eqm, i, eqm_i);
    eqmNorm += eqm_i;
  }
  CheckGsl (gsl_vector_scale (eqm, 1 / eqmNorm));

  gsl_vector_free (tau);
  gsl_vector_free (b);
  gsl_vector_free (residual);
  gsl_matrix_free (QR);
  
  return eqm;
}

gsl_matrix* RateModel::getSubProbMatrix (double t) const {
  gsl_matrix* m = newAlphabetMatrix();
  gsl_matrix* rt = newAlphabetMatrix();
  CheckGsl (gsl_matrix_memcpy (rt, subRate));
  CheckGsl (gsl_matrix_scale (rt, t));
  CheckGsl (gsl_linalg_exponential_ss (rt, m, GSL_PREC_DOUBLE));
  gsl_matrix_free (rt);
  return m;
}

ProbModel::ProbModel (const RateModel& model, double t)
  : AlphabetOwner (model),
    t (t),
    ins (1 - exp (-model.insRate * t)),
    del (1 - exp (-model.delRate * t)),
    insExt (model.insExtProb),
    delExt (model.delExtProb),
    insWait (IndelCounts::decayWaitTime (model.insRate, t)),
    delWait (IndelCounts::decayWaitTime (model.delRate, t)),
    insVec (model.newAlphabetVector()),
    subMat (model.getSubProbMatrix (t))
{
  CheckGsl (gsl_vector_memcpy (insVec, model.insProb));
}

ProbModel::~ProbModel() {
  gsl_matrix_free (subMat);
  gsl_vector_free (insVec);
}

double ProbModel::transProb (State src, State dest) const {
  switch (src) {
  case Match:
    switch (dest) {
    case Match:
      return (1 - ins) * (1 - del);
    case Insert:
      return ins;
    case Delete:
      return (1 - ins) * del;
    case End:
      return 1 - ins;
    default:
      break;
    }
    break;
  case Insert:
    switch (dest) {
    case Match:
      return (1 - insExt) * (1 - del);
    case Insert:
      return insExt;
    case Delete:
      return (1 - insExt) * del;
    case End:
      return 1 - insExt;
    default:
      break;
    }
    break;
  case Delete:
    switch (dest) {
    case Match:
    case End:
      return 1 - delExt;
    case Insert:
      return 0;
    case Delete:
      return delExt;
    default:
      break;
    }
    break;
  default:
    break;
  }
  return 0;
}

void ProbModel::write (ostream& out) const {
  out << "{" << endl;
  out << " \"alphabet\": \"" << alphabet << "\"," << endl;
  out << " \"insBegin\": " << ins << "," << endl;
  out << " \"insExtend\": " << insExt << "," << endl;
  out << " \"delBegin\": " << del << "," << endl;
  out << " \"delExtend\": " << delExt << "," << endl;
  out << " \"insVec\": {" << endl;
  for (AlphTok i = 0; i < alphabetSize(); ++i)
    out << "  \"" << alphabet[i] << "\": " << gsl_vector_get(insVec,i) << (i < alphabetSize() - 1 ? ",\n" : "");
  out << endl << " }," << endl;
  out << " \"subMat\": {" << endl;
  for (AlphTok i = 0; i < alphabetSize(); ++i) {
    out << "  \"" << alphabet[i] << "\": {" << endl;
    for (AlphTok j = 0; j < alphabetSize(); ++j)
      out << "   \"" << alphabet[j] << "\": " << gsl_matrix_get(subMat,i,j) << (j < alphabetSize() - 1 ? ",\n" : "");
    out << endl << "  }" << (i < alphabetSize() - 1 ? "," : "") << endl;
  }
  out << " }" << endl;
  out << "}" << endl;
}

ProbModel::State ProbModel::getState (bool parentUngapped, bool childUngapped) {
  if (parentUngapped)
    return childUngapped ? Match : Delete;
  return childUngapped ? Insert : End;
}

LogProbModel::LogProbModel (const ProbModel& pm)
  : logInsProb (pm.alphabetSize()),
    logSubProb (pm.alphabetSize(), vguard<LogProb> (pm.alphabetSize()))
{
  logInsProb = log_gsl_vector (pm.insVec);
  for (AlphTok i = 0; i < pm.alphabetSize(); ++i)
    for (AlphTok j = 0; j < pm.alphabetSize(); ++j)
      logSubProb[i][j] = log (gsl_matrix_get (pm.subMat, i, j));
}

double RateModel::mlDistance (const FastSeq& x, const FastSeq& y, int maxIterations) const {
  LogThisAt(6,"Estimating distance from " << x.name << " to " << y.name << endl);
  map<pair<AlphTok,AlphTok>,int> pairCount;
  Assert (x.length() == y.length(), "Sequences %s and %s have different lengths (%u, %u)", x.name.c_str(), y.name.c_str(), x.length(), y.length());
  for (size_t col = 0; col < x.length(); ++col) {
    const char ci = x.seq[col];
    const char cj = y.seq[col];
    if (!Alignment::isGap(ci) && !Alignment::isGap(cj))
      ++pairCount[pair<AlphTok,AlphTok> (tokenizeOrDie(ci), tokenizeOrDie(cj))];
  }
  if (LoggingThisAt(6)) {
    LogThisAt(6,"Counts:");
    for (const auto& pc : pairCount)
      LogThisAt(6," " << pc.second << "*" << alphabet[pc.first.first] << alphabet[pc.first.second]);
    LogThisAt(6,endl);
  }
  const DistanceMatrixParams dmp (pairCount, *this);
  return dmp.tML (maxIterations);
}

vguard<vguard<double> > RateModel::distanceMatrix (const vguard<FastSeq>& gappedSeq, int maxIterations) const {
  vguard<vguard<double> > dist (gappedSeq.size(), vguard<double> (gappedSeq.size()));
  ProgressLog (plog, 4);
  plog.initProgress ("Distance matrix (%d rows)", gappedSeq.size());
  const size_t pairs = (gappedSeq.size() - 1) * gappedSeq.size() / 2;
  size_t n = 0;
  for (size_t i = 0; i < gappedSeq.size() - 1; ++i)
    for (size_t j = i + 1; j < gappedSeq.size(); ++j) {
      plog.logProgress (n / (double) pairs, "computing entry %d/%d", n + 1, pairs);
      ++n;
      dist[i][j] = dist[j][i] = mlDistance (gappedSeq[i], gappedSeq[j]);
    }
  if (LoggingThisAt(3)) {
    LogThisAt(3,"Distance matrix (" << dist.size() << " rows):" << endl);
    for (const auto& row : dist)
      LogThisAt(3,to_string_join(row) << endl);
  }
  return dist;
}

double distanceMatrixNegLogLike (double t, void *params) {
  const DistanceMatrixParams& dmp = *(const DistanceMatrixParams*) params;
  gsl_matrix* sub = dmp.model.getSubProbMatrix (t);
  double ll = 0;
  for (auto& pc : dmp.pairCount)
    ll += log (gsl_matrix_get(sub,pc.first.first,pc.first.second)) * (double) pc.second;
  gsl_matrix_free (sub);
  return -ll;
}

double DistanceMatrixParams::negLogLike (double t) const {
  return distanceMatrixNegLogLike (t, (void*) this);
}

double DistanceMatrixParams::tML (int maxIterations) const {
  const gsl_min_fminimizer_type *T;
  gsl_min_fminimizer *s;

  gsl_function F;
  F.function = &distanceMatrixNegLogLike;
  F.params = (void*) this;

  T = gsl_min_fminimizer_goldensection; // gsl_min_fminimizer_brent;
  s = gsl_min_fminimizer_alloc (T);

  double t;
  const double tLower = .0001, tUpper = 10 + tLower;
  const double llLower = negLogLike(tLower), llUpper = negLogLike(tUpper);
  LogThisAt(8,"tML: f(" << tLower << ") = " << llLower << ", f(" << tUpper << ") = " << llUpper << endl);

  const double maxSteps = 128;
  double nSteps;
  bool foundGuess = false;
  for (double step = 4; step > 1.0001 && !foundGuess; step = sqrt(step))
    for (double x = tLower + step; x < tUpper && !foundGuess; x += step) {
      const double ll = negLogLike(x);
      LogThisAt(8,"tML: f(" << x << ") = " << ll << endl);
      if (ll < llLower && ll < llUpper) {
	foundGuess = true;
	t = x;
      }
    }
  if (!foundGuess)
    return llLower < llUpper ? tLower : tUpper;

  LogThisAt(8,"Initializing with t=" << t << ", tLower=" << tLower << ", tUpper=" << tUpper << endl);
  gsl_min_fminimizer_set (s, &F, t, tLower, tUpper);

  for (int iter = 0; iter < maxIterations; ++iter) {
    (void) gsl_min_fminimizer_iterate (s);

    t = gsl_min_fminimizer_x_minimum (s);
    const double a = gsl_min_fminimizer_x_lower (s);
    const double b = gsl_min_fminimizer_x_upper (s);

    LogThisAt(7,"Iteration #" << iter+1 << " tML: f(" << t << ") = " << negLogLike(t) << endl);
    
    const int status = gsl_min_test_interval (a, b, 0.01, 0.0);  // converge to 1%
    if (status == GSL_SUCCESS)
      break;
  }

  gsl_min_fminimizer_free (s);

  return t;
}


void AlphabetOwner::writeSubCounts (ostream& out, const vguard<double>& rootCounts, const vguard<vguard<double> >& subCountsAndWaitTimes, size_t indent) const {
  const string ind (indent, ' ');
  out << ind << "{" << endl;
  out << ind << " \"root\":" << endl;
  out << ind << "  {";
  for (AlphTok i = 0; i < alphabetSize(); ++i)
    out << (i == 0 ? "" : ",") << endl << ind << "   \"" << alphabet[i] << "\": " << rootCounts[i];
  out << endl << ind << "  }," << endl;
  out << ind << " \"sub\":" << endl;
  out << ind << "  {";
  for (AlphTok i = 0; i < alphabetSize(); ++i) {
    out << (i == 0 ? "" : ",") << endl << ind << "   \"" << alphabet[i] << "\": {";
    for (AlphTok j = 0; j < alphabetSize(); ++j)
      if (i != j)
	out << (j == (i == 0 ? 1 : 0) ? "" : ",") << " " << "\"" << alphabet[j] << "\": " << subCountsAndWaitTimes[i][j];
    out << " }";
  }
  out << endl;
  out << ind << "  }," << endl;
  out << ind << " \"wait\":" << endl;
  out << ind << "  {";
  for (AlphTok i = 0; i < alphabetSize(); ++i)
    out << (i == 0 ? "" : ",") << endl << ind << "   \"" << alphabet[i] << "\": " << subCountsAndWaitTimes[i][i];
  out << endl;
  out << ind << "  }" << endl;
  out << ind << "}";
}

IndelCounts::IndelCounts (double pseudocount, double pseudotime)
  : ins(pseudocount),
    del(pseudocount),
    insExt(pseudocount),
    delExt(pseudocount),
    insTime(pseudotime),
    delTime(pseudotime),
    lp(0)
{ }

IndelCounts& IndelCounts::operator+= (const IndelCounts& c) {
  ins += c.ins;
  del += c.del;
  insExt += c.insExt;
  delExt += c.delExt;
  insTime += c.insTime;
  delTime += c.delTime;
  lp += c.lp;
  return *this;
}

IndelCounts& IndelCounts::operator*= (double w) {
  ins *= w;
  del *= w;
  insExt *= w;
  delExt *= w;
  insTime *= w;
  delTime *= w;
  lp *= w;
  return *this;
}

IndelCounts IndelCounts::operator+ (const IndelCounts& c) const {
  IndelCounts result (*this);
  result += c;
  return result;
}

IndelCounts IndelCounts::operator* (double w) const {
  IndelCounts result (*this);
  result *= w;
  return result;
}

EigenCounts::EigenCounts (size_t alphabetSize)
  : indelCounts(),
    rootCount (alphabetSize, 0),
    eigenCount (alphabetSize, vguard<gsl_complex> (alphabetSize, gsl_complex_rect(0,0)))
{ }

EigenCounts& EigenCounts::operator+= (const EigenCounts& c) {
  indelCounts += c.indelCounts;

  if (c.rootCount.size()) {
    if (rootCount.size())
      for (size_t n = 0; n < rootCount.size(); ++n)
	rootCount[n] += c.rootCount.at(n);
    else
      rootCount = c.rootCount;
  }

  if (c.eigenCount.size()) {
    if (eigenCount.size())
      for (size_t i = 0; i < eigenCount.size(); ++i)
	for (size_t j = 0; j < eigenCount[i].size(); ++j)
	  eigenCount[i][j] = gsl_complex_add (eigenCount[i][j], c.eigenCount.at(i).at(j));
    else
      eigenCount = c.eigenCount;
  }

  return *this;
}

EigenCounts& EigenCounts::operator*= (double w) {
  indelCounts *= w;
  for (auto& c : rootCount)
    c *= w;
  for (auto& sc : eigenCount)
    for (auto& c : sc)
      c = gsl_complex_mul_real (c, w);
  return *this;
}

EigenCounts EigenCounts::operator+ (const EigenCounts& c) const {
  EigenCounts result (*this);
  result += c;
  return result;
}

EigenCounts EigenCounts::operator* (double w) const {
  EigenCounts result (*this);
  result *= w;
  return result;
}

EventCounts::EventCounts (const AlphabetOwner& alph, double pseudo)
  : AlphabetOwner (alph),
    indelCounts (pseudo, pseudo),
    rootCount (alph.alphabetSize(), pseudo),
    subCount (alph.alphabetSize(), vguard<double> (alph.alphabetSize(), pseudo))
{ }

EventCounts& EventCounts::operator+= (const EventCounts& c) {
  Assert (alphabet == c.alphabet, "Alphabets don't match");

  indelCounts += c.indelCounts;

  for (size_t n = 0; n < rootCount.size(); ++n)
    rootCount[n] += c.rootCount.at(n);

  for (size_t i = 0; i < subCount.size(); ++i)
    for (size_t j = 0; j < subCount[i].size(); ++j)
      subCount[i][j] += c.subCount.at(i).at(j);

  return *this;
}

EventCounts& EventCounts::operator*= (double w) {
  indelCounts *= w;
  for (auto& c : rootCount)
    c *= w;
  for (auto& sc : subCount)
    for (auto& c : sc)
      c *= w;
  return *this;
}

EventCounts EventCounts::operator+ (const EventCounts& c) const {
  EventCounts result (*this);
  result += c;
  return result;
}

EventCounts EventCounts::operator* (double w) const {
  EventCounts result (*this);
  result *= w;
  return result;
}

void IndelCounts::accumulateIndelCounts (const RateModel& model, double time, const AlignRowPath& parent, const AlignRowPath& child, double weight) {
  const double insWait = decayWaitTime (model.insRate, time), delWait = decayWaitTime (model.delRate, time);
  ProbModel pm (model, time);
  ProbModel::State state, next;
  state = ProbModel::Match;
  for (size_t col = 0; col < parent.size(); ++col) {
    if (parent[col] && child[col])
      next = ProbModel::Match;
    else if (parent[col])
      next = ProbModel::Delete;
    else if (child[col])
      next = ProbModel::Insert;
    else
      continue;
    switch (next) {
    case ProbModel::Match:
      if (state == next) {
	insTime += weight * time;
	delTime += weight * time;
      }
      break;
    case ProbModel::Insert:
      if (state == next)
	insExt += weight;
      else {
	ins += weight;
	insTime += weight * insWait;
      }
      break;
    case ProbModel::Delete:
      if (state == next)
	delExt += weight;
      else {
	del += weight;
	delTime += weight * delWait;
      }
      break;
    default:
      break;
    }
    lp += log (pm.transProb (state, next)) * weight;
    state = next;
  }
  lp += log (pm.transProb (state, ProbModel::End)) * weight;
}

void IndelCounts::accumulateIndelCounts (const RateModel& model, const Tree& tree, const AlignPath& align, double weight) {
  for (TreeNodeIndex node = 0; node < tree.nodes() - 1; ++node)
    accumulateIndelCounts (model, tree.branchLength(node), align.at(tree.parentNode(node)), align.at(node), weight);
}

void EigenCounts::accumulateSubstitutionCounts (const RateModel& model, const Tree& tree, const vguard<FastSeq>& gapped, double weight) {
  AlignColSumProduct colSumProd (model, tree, gapped);

  EigenCounts c (model.alphabetSize());

  while (!colSumProd.alignmentDone()) {
    colSumProd.fillUp();
    colSumProd.fillDown();
    colSumProd.accumulateEigenCounts (c.rootCount, c.eigenCount);
    c.indelCounts.lp += colSumProd.columnLogLikelihood();
    colSumProd.nextColumn();
  }

  c *= weight;
  *this += c;
}

void EigenCounts::accumulateCounts (const RateModel& model, const Alignment& align, const Tree& tree, bool updateIndelCounts, bool updateSubstCounts, double weight) {
  if (updateIndelCounts)
    indelCounts.accumulateIndelCounts (model, tree, align.path, weight);
  if (updateSubstCounts)
    accumulateSubstitutionCounts (model, tree, align.gapped(), weight);
}

EventCounts EigenCounts::transform (const RateModel& model) const {
  EventCounts c (model);
  EigenModel eigen (model);
  c.indelCounts = indelCounts;
  c.rootCount = rootCount;
  c.subCount = eigen.getSubCounts (eigenCount);
  return c;
}

void EventCounts::writeJson (ostream& out) const {
  out << "{" << endl;
  out << " \"alphabet\": \"" << alphabet << "\"," << endl;
  out << " \"indel\":" << endl;
  indelCounts.writeJson (out, 2);
  out << "," << endl;
  out << " \"sub\":" << endl;
  writeSubCounts (out, rootCount, subCount, 2);
  out << "," << endl;
  out << " \"logLikelihood\": " << indelCounts.lp << endl;
  out << "}" << endl;
}

void IndelCounts::writeJson (ostream& out, const size_t indent) const {
  const string ind (indent, ' ');
  out << ind << "{" << endl;
  out << ind << " \"ins\": " << ins << "," << endl;
  out << ind << " \"del\": " << del << "," << endl;
  out << ind << " \"insExt\": " << insExt << "," << endl;
  out << ind << " \"delExt\": " << delExt << "," << endl;
  out << ind << " \"insTime\": " << insTime << endl;
  out << ind << " \"delTime\": " << delTime << endl;
  out << ind << "}";
}

void IndelCounts::read (const JsonValue& json) {
  JsonMap jm (json);
  ins = jm.getNumber("ins");
  del = jm.getNumber("del");
  insExt = jm.getNumber("insExt");
  delExt = jm.getNumber("delExt");
  insTime = jm.getNumber("insTime");
  delTime = jm.getNumber("delTime");
}

void EventCounts::read (const JsonValue& json) {
  AlphabetOwner alph;
  alph.readAlphabet (json);
  *this = EventCounts (alph);
  JsonMap jm (json);
  indelCounts.read (jm.getType("indel",JSON_OBJECT));
  indelCounts.lp = jm.getNumber("logLikelihood");
  JsonMap subBlock = jm.getObject("sub");
  JsonMap root = subBlock.getObject("root");
  JsonMap sub = subBlock.getObject("sub");
  JsonMap wait = subBlock.getObject("wait");
  for (AlphTok i = 0; i < alphabetSize(); ++i) {
    const string si (1, alphabet[i]);
    rootCount[i] = root.getNumber(si);
    subCount[i][i] = wait.getNumber(si);
    JsonMap sub_i = sub.getObject(si);
    for (AlphTok j = 0; j < alphabetSize(); ++j)
      if (i != j) {
	const string sj (1, alphabet[j]);
	subCount[i][j] = sub_i.getNumber(sj);
      }
  }
}

void EventCounts::optimize (RateModel& model, bool fitIndelRates, bool fitSubstRates) const {
  if (model.alphabet != alphabet)
    model.init (alphabet);

  if (fitSubstRates) {
    const double insNorm = accumulate (rootCount.begin(), rootCount.end(), 0.);
    for (AlphTok i = 0; i < alphabetSize(); ++i)
      gsl_vector_set (model.insProb, i, rootCount[i] / insNorm);

    for (AlphTok i = 0; i < alphabetSize(); ++i) {
      double r_ii = 0;
      for (AlphTok j = 0; j < alphabetSize(); ++j)
	if (j != i) {
	  const double r_ij = subCount[i][j] / subCount[i][i];
	  gsl_matrix_set (model.subRate, i, j, r_ij);
	  r_ii -= r_ij;
	}
      gsl_matrix_set (model.subRate, i, i, r_ii);
    }
  }

  if (fitIndelRates) {
    model.insRate = indelCounts.ins / indelCounts.insTime;
    model.delRate = indelCounts.del / indelCounts.delTime;
    model.insExtProb = indelCounts.insExt / (indelCounts.insExt + indelCounts.ins);
    model.delExtProb = indelCounts.delExt / (indelCounts.delExt + indelCounts.del);
  }
}

double EventCounts::logPrior (const RateModel& model, bool includeIndelRates, bool includeSubstRates) const {
  double lp = 0;
  if (includeIndelRates)
    lp += logGammaPdf (model.insRate, indelCounts.ins, indelCounts.insTime)
      + logGammaPdf (model.delRate, indelCounts.del, indelCounts.delTime)
      + logBetaPdf (model.insExtProb, indelCounts.insExt, indelCounts.ins)
      + logBetaPdf (model.delExtProb, indelCounts.delExt, indelCounts.del);
  if (includeSubstRates) {
    lp += logDirichletPdf (gsl_vector_to_stl(model.insProb), rootCount);
    for (AlphTok i = 0; i < alphabetSize(); ++i)
      for (AlphTok j = 0; j < alphabetSize(); ++j)
	if (i != j)
	  lp += logGammaPdf (gsl_matrix_get (model.subRate, i, j), subCount[i][j], subCount[i][i]);
  }
  return lp;
}

double xlogy (double x, double y) {
  return x > 0 && y > 0 ? x*log(y) : 0;
}

double EventCounts::expectedLogLikelihood (const RateModel& model) const {
  double lp = -model.insRate * indelCounts.insTime
    + xlogy (indelCounts.ins, model.insRate)
    -model.delRate * indelCounts.delTime
    + xlogy (indelCounts.del, model.delRate)
    + xlogy (indelCounts.insExt, model.insExtProb)
    + xlogy (indelCounts.ins, 1 - model.insExtProb)
    + xlogy (indelCounts.delExt, model.delExtProb)
    + xlogy (indelCounts.del, 1 - model.delExtProb);
  for (AlphTok i = 0; i < alphabetSize(); ++i) {
    const double exit_i = -gsl_matrix_get (model.subRate, i, i);
    lp += xlogy (rootCount[i], gsl_vector_get (model.insProb, i));
    lp -= exit_i * subCount[i][i];
    for (AlphTok j = 0; j < alphabetSize(); ++j)
      if (i != j)
	lp += xlogy (subCount[i][j], gsl_matrix_get (model.subRate, i, j));
  }
  return lp;
}

double IndelCounts::decayWaitTime (double decayRate, double timeInterval) {
  return 1 / decayRate - timeInterval / (exp (decayRate*timeInterval) - 1);
}
