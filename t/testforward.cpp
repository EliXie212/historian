#include <iostream>
#include <fstream>
#include <iomanip>
#include "../src/forward.h"
#include "../src/util.h"
#include "../src/jsonutil.h"

int main (int argc, char **argv) {
  if (argc != 6 && argc != 7) {
    cout << "Usage: " << argv[0] << " [-all|-hubs|-absorbers] [-best|-matrix|<numberofpaths>] <sequences> <modelfile> <xtime> [<ytime>]\n";
    exit (EXIT_FAILURE);
  }

  const string strat (argv[1]);
  ForwardMatrix::EliminationStrategy strategy;
  if (strat == "-all") strategy = ForwardMatrix::KeepAll;
  else if (strat == "-hubs") strategy = ForwardMatrix::KeepHubsAndAbsorbers;
  else if (strat == "-absorbers") strategy = ForwardMatrix::KeepAbsorbers;
  else
    Abort ("Unknown strategy: %s", argv[1]);

  const string whatCells (argv[2]);
  bool useBest = false, useMatrix = false;
  int nPaths = 0;
  if (whatCells == "-best") useBest = true;
  else if (whatCells == "-matrix") useMatrix = true;
  else nPaths = atoi (argv[2]);

  vguard<FastSeq> seqs = readFastSeqs (argv[3]);
  Assert (seqs.size() == 2, "Expected two sequences in file %s", argv[3]);

  RateModel rates;
  ifstream in (argv[4]);
  ParsedJson pj (in);
  rates.read (pj.value);

  ProbModel xprobs (rates, atof (argv[5]));
  ProbModel yprobs (rates, atof (argv[argc > 6 ? 6 : 5]));
  gsl_vector* eqm = rates.getEqmProb();
  PairHMM hmm (xprobs, yprobs, eqm);

  Profile xprof (rates.alphabet, seqs[0], 1);
  Profile yprof (rates.alphabet, seqs[1], 2);
  ForwardMatrix forward (xprof, yprof, hmm, 0);

  Profile prof(0);
  if (useMatrix) {
    set<ForwardMatrix::CellCoords> allCells;
    allCells.insert (forward.startCell);
    allCells.insert (forward.endCell);
    for (ProfileStateIndex xpos = 0; xpos < xprof.size() - 1; ++xpos)
      for (ProfileStateIndex ypos = 0; ypos < yprof.size() - 1; ++ypos)
	for (PairHMM::State s : hmm.states())
	  if (xpos > 0 || ypos > 0)
	    allCells.insert (ForwardMatrix::CellCoords (xpos, ypos, s));
    prof = forward.makeProfile (allCells, strategy);

  } else if (useBest)
    prof = forward.bestProfile (strategy);

  else {
    auto generator = forward.newRNG();
    prof = forward.sampleProfile (generator, nPaths, 0, strategy);
  }
    
  prof.calcSumPathAbsorbProbs (hmm.logRoot);
  prof.writeJson (cout);

  exit (EXIT_SUCCESS);
}