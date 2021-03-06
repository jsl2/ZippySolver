#include <math.h> // lrint()
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // strncmp()

#include <memory>
#include <string>
#include <vector>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_street_values.h"
#include "cfr_utils.h"
#include "files.h"
#include "game.h"
#include "io.h"
#include "split.h"

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;

// Compute probs from the current hand values using regret matching.  Normally the values we do this
// to are regrets, but they may also be sumprobs.
// May eventually want to take parameters for nonneg, explore, uniform, nonterminal succs,
// num nonterminal succs.
// Note: doesn't handle nodes with one succ


/*succ stands for successor -Brian*/
/*Calculates playing strategy by using regret matching
  1) sum all positive regrets
  2) each actions strategy is:
  if (regret < 0) 0
  else regret/sum of positive regrets
  
  inputs: vals = regrets
          num_succs = number of actions

          what is dsi?
          it looks like if it's the first iteration, it plays strategy at index dsi 100%, and other strategiees at 0%
          i've seen other cfr stuff use 1/numactions to play a random strategy
          i'm guessing dsi stands for Default Strategy Index

          probs is the outputted strategy

  The above comment by the author says this is used for regrets to find the current strategy, but can also be used for sumprobs
  to find the average strategy
  -Brian
*/
template <typename T> void RMProbs(const T *vals, int num_succs, int dsi, double *probs) {
  double sum = 0;
  for (int s = 0; s < num_succs; ++s) {
    T v = vals[s];
    if (v > 0) sum += v;
  }
  if (sum == 0) {
    for (int s = 0; s < num_succs; ++s) {
      probs[s] = s == dsi ? 1.0 : 0;
    }
  } else {
    for (int s = 0; s < num_succs; ++s) {
      T v = vals[s];
      if (v > 0) probs[s] = v / sum;
      else       probs[s] = 0;
    }
  }
}

template void RMProbs<double>(const double *vals, int num_succs, int dsi, double *probs);
template void RMProbs<int>(const int *vals, int num_succs, int dsi, double *probs);
template void RMProbs<unsigned short>(const unsigned short *vals, int num_succs, int dsi,
				      double *probs);
template void RMProbs<unsigned char>(const unsigned char *vals, int num_succs, int dsi,
				     double *probs);

// Uses the current strategy (from regrets or sumprobs) to compute the weighted average of
// the successor values.  This version for systems employing card abstraction.
template <typename T> void ComputeOurValsBucketed(const T *all_cs_vals, int num_hole_card_pairs,
						  int num_succs, int dsi,
						  shared_ptr<double []> *succ_vals,
						  int *street_buckets, shared_ptr<double []> vals) {
  unique_ptr<double []> current_probs(new double[num_succs]);
  for (int i = 0; i < num_hole_card_pairs; ++i) {
    int b = street_buckets[i];
    RMProbs(all_cs_vals + b * num_succs, num_succs, dsi, current_probs.get());
    for (int s = 0; s < num_succs; ++s) {
      vals[i] += succ_vals[s][i] * current_probs[s];
    }
  }
}

template void ComputeOurValsBucketed<double>(const double *all_cs_vals, int num_hole_card_pairs,
					     int num_succs, int dsi,
					     shared_ptr<double []> *succ_vals,
					     int *street_buckets, shared_ptr<double []> vals);
template void ComputeOurValsBucketed<int>(const int *all_cs_vals, int num_hole_card_pairs,
					  int num_succs, int dsi, shared_ptr<double []> *succ_vals,
					  int *street_buckets, shared_ptr<double []> vals);
template void ComputeOurValsBucketed<unsigned short>(const unsigned short *all_cs_vals, 
						     int num_hole_card_pairs, int num_succs,
						     int dsi, shared_ptr<double []> *succ_vals,
						     int *street_buckets,
						     shared_ptr<double []> vals);
template void ComputeOurValsBucketed<unsigned char>(const unsigned char *all_cs_vals,
						    int num_hole_card_pairs,
						    int num_succs, int dsi,
						    shared_ptr<double []> *succ_vals,
						    int *street_buckets,
						    shared_ptr<double []> vals);

// Uses the current strategy (from regrets or sumprobs) to compute the weighted average of
// the successor values.  This version for unabstracted systems.

/* Calculates ev for each hand
  ev = summation of (ev of taking an action * how often we take that action) 
      for all actions
  Calculates current strategy by using RMProbs function

    inputs: all_cs_vals = regrets
            succ_vals = ev of taking an action
            what is lbd? it looks like it used for finding a starting point in all_cs_values
            all_cs_vals then is larger than numhands*numactions, so i'm not exactly sure what that
            variable is either, I just know that it has regrets. I'll need to see how this function
            is implemented to really understand what all_cs_vals is.
    outputs: vals

An idea is that there is a betting game tree, that does not take into account chance
So the regrets at each node would need to contain regrets for each possible card.
On the initial street it would just be regrets for all possible hands, but on the turn it would need to be
regrets for all possible hands * 49 turns

Or all regrets are stored together somewhere 

that might be what all_cs_vals and lbd are used for

-Brian
*/
template <typename T> void ComputeOurVals(const T *all_cs_vals, int num_hole_card_pairs,
					  int num_succs, int dsi, shared_ptr<double []> *succ_vals,
					  int lbd, shared_ptr<double []> vals) {
  unique_ptr<double []> current_probs(new double[num_succs]);
  int base = lbd * num_hole_card_pairs * num_succs;
  for (int i = 0; i < num_hole_card_pairs; ++i) {
    int offset = base + i * num_succs;
    RMProbs(all_cs_vals + offset, num_succs, dsi, current_probs.get());
    for (int s = 0; s < num_succs; ++s) {
      vals[i] += succ_vals[s][i] * current_probs[s];
    }
  }
}

template void ComputeOurVals<double>(const double *all_cs_vals, int num_hole_card_pairs,
				     int num_succs, int dsi, shared_ptr<double []> *succ_vals,
				     int lbd, shared_ptr<double []> vals);
template void ComputeOurVals<int>(const int *all_cs_vals, int num_hole_card_pairs, int num_succs,
				  int dsi, shared_ptr<double []> *succ_vals, int lbd,
				  shared_ptr<double []> vals);
template void ComputeOurVals<unsigned short>(const unsigned short *all_cs_vals,
					     int num_hole_card_pairs, int num_succs, int dsi,
					     shared_ptr<double []> *succ_vals, int lbd,
					     shared_ptr<double []> vals);
template void ComputeOurVals<unsigned char>(const unsigned char *all_cs_vals,
					    int num_hole_card_pairs, int num_succs, int dsi,
					    shared_ptr<double []> *succ_vals, int lbd,
					    shared_ptr<double []> vals);

template <typename T> void SetCurrentAbstractedStrategy(const T *all_regrets, int num_buckets,
							int num_succs, int dsi,
							double *all_cs_probs) {
  unique_ptr<double []> current_probs(new double[num_succs]);
  for (int b = 0; b < num_buckets; ++b) {
    int offset = b * num_succs;
    RMProbs(all_regrets + offset, num_succs, dsi, current_probs.get());
    for (int s = 0; s < num_succs; ++s) {
      all_cs_probs[b * num_succs + s] = current_probs[s];
    }
  }
}

template void SetCurrentAbstractedStrategy<double>(const double *all_regrets, int num_buckets,
						   int num_succs, int dsi, double *all_cs_probs);
template void SetCurrentAbstractedStrategy<int>(const int *all_regrets, int num_buckets,
						int num_succs, int dsi, double *all_cs_probs);
template void SetCurrentAbstractedStrategy<unsigned short>(const unsigned short *all_regrets,
							   int num_buckets, int num_succs, int dsi,
							   double *all_cs_probs);
template void SetCurrentAbstractedStrategy<unsigned char>(const unsigned char *all_regrets,
							  int num_buckets, int num_succs, int dsi,
							  double *all_cs_probs);

/* Calculates ev of each hand at showdown
  The algorithm is not super simple and I won't comment here about how it works.
  This algorithm allows you to get ev for each hand with only ~100*1326 calculations (to account for blockers),
  instead of doing a 1326*1326 calculations. This is the reason vector based cfr (range vs range) is so much faster.

  inputs: node = gametree node
          hands = possible (unique?) hands, likely sorted by hand strength
          see folded function comment for better description of probs
          opp probs, sum opp probs, opp probs for each card
  outputs: ev for each hand
  -Brian
*/

shared_ptr<double []> Showdown(Node *node, const CanonicalCards *hands, double *opp_probs,
			       double sum_opp_probs, double *total_card_probs) {
  int max_card1 = Game::MaxCard() + 1;
  double cum_prob = 0;
  double cum_card_probs[52];
  for (Card c = 0; c < max_card1; ++c) cum_card_probs[c] = 0;
  int num_hole_card_pairs = hands->NumRaw();
  unique_ptr<double []> win_probs(new double[num_hole_card_pairs]);
  double half_pot = node->LastBetTo();
  shared_ptr<double []> vals(new double[num_hole_card_pairs]);

  int j = 0;
  while (j < num_hole_card_pairs) {
    int last_hand_val = hands->HandValue(j);
    int begin_range = j;
    // Make three passes through the range of equally strong hands
    // First pass computes win counts for each hand and finds end of range
    // Second pass updates cumulative counters
    // Third pass computes lose counts for each hand
    while (j < num_hole_card_pairs) {
      int hand_val = hands->HandValue(j);
      if (hand_val != last_hand_val) break;
      const Card *cards = hands->Cards(j);
      Card hi = cards[0];
      Card lo = cards[1];
      win_probs[j] = cum_prob - cum_card_probs[hi] - cum_card_probs[lo];
      ++j;
    }
    // Positions begin_range...j-1 (inclusive) all have the same hand value
    for (int k = begin_range; k < j; ++k) {
      const Card *cards = hands->Cards(k);
      Card hi = cards[0];
      Card lo = cards[1];
      int code = hi * max_card1 + lo;
      double prob = opp_probs[code];
      cum_card_probs[hi] += prob;
      cum_card_probs[lo] += prob;
      cum_prob += prob;
    }
    for (int k = begin_range; k < j; ++k) {
      const Card *cards = hands->Cards(k);
      Card hi = cards[0];
      Card lo = cards[1];
      double better_hi_prob = total_card_probs[hi] - cum_card_probs[hi];
      double better_lo_prob = total_card_probs[lo] - cum_card_probs[lo];
      double lose_prob = (sum_opp_probs - cum_prob) -
	better_hi_prob - better_lo_prob;
      vals[k] = (win_probs[k] - lose_prob) * half_pot;
      // fprintf(stderr, "vals[%u] %f sop %f\n", k, vals[k], sum_opp_probs);
    }
  }
  // fprintf(stderr, "showdown %i vals[0] %f\n", node->TerminalID(), vals[0]);

  return vals;
}


/* Calculates ev of each hand when someone folds
  ev = (halfpot or -halfpot depending on if hero or villain wins) *
        opponent reach probability
  opponent reach probability has to account for blockers, this is handled by 

  + opp_prob - (total_card_probs[hi] + total_card_probs[lo])

  opponent has a reach probability for each hand (opp probs)
  you can find the sum of these reach probabilities (sum_opp_probs)
  you can find the sum of reach probabilities for hands that contain a specific card (total_card_probs)
  
  to get blocker adjusted reach probability, take sum_opp_probs and subtract total_card_probs for both of our cards
  then you need to add back villains reach prob for our exact hand, because we subtracted the reach prob
  for that hand twice (as this hand is in both total_card_probs[card0] and total_card_probs[card1])
  ^worded kind of poorly, hope it makes sense

  inputs: node = gametree node, so we know who folded
          p = which is the active player this iteration? ip or oop?
          hands = possible (unique?) hands
          opp probs, sum opp probs, opp probs for each card

  outputs: ev for each hand
  -Brian
*/
shared_ptr<double []> Fold(Node *node, int p, const CanonicalCards *hands, double *opp_probs,
			   double sum_opp_probs, double *total_card_probs) {
  int max_card1 = Game::MaxCard() + 1;
  // Sign of half_pot reflects who wins the pot
  double half_pot;
  // Player acting encodes player remaining at fold nodes
  // LastBetTo() doesn't include the last called bet
  if (p == node->PlayerActing()) {
    half_pot = node->LastBetTo();
  } else {
    half_pot = -node->LastBetTo();
  }
  int num_hole_card_pairs = hands->NumRaw();
  shared_ptr<double []> vals(new double[num_hole_card_pairs]);

  for (int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    int enc = hi * max_card1 + lo;
    double opp_prob = opp_probs[enc];
    vals[i] = half_pot *
      (sum_opp_probs + opp_prob - (total_card_probs[hi] + total_card_probs[lo]));
  }

  return vals;
}

/* updates opp reach sum and card reach sum (see Fold function for information on card reach) after they take an action
  inputs: st = street (to get number of hands)
          canonicalcards *hands = all unique hands on this board
          oop_probs = probability that villain took this action for each hand

  outputs: ret_sum_opp_probs = opp reach sum
            total_card_prob = opp reach for each card

  I really do not understand this functions name
  -Brian
*/
void CommonBetResponseCalcs(int st, const CanonicalCards *hands, double *opp_probs,
			    double *ret_sum_opp_probs, double *total_card_probs) {
  double sum_opp_probs = 0;
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  Card max_card = Game::MaxCard();
  for (Card c = 0; c <= max_card; ++c) total_card_probs[c] = 0;

  for (int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    int enc = hi * (max_card + 1) + lo;
    double opp_prob = opp_probs[enc];
    sum_opp_probs += opp_prob;
    total_card_probs[hi] += opp_prob;
    total_card_probs[lo] += opp_prob;
  }
  *ret_sum_opp_probs = sum_opp_probs;
}

/* This function is called for every possible hand (by function ProcessOppProbs). It updates opponent reach for each hand after his action.
    It also updates his sumprobs (cumulative strategy, can be used to find average strategy at any point)
    warmup looks like an optional parameter for the solver to give less weight to strategies played in earlier iterations.

    inputs: enc = hand index
            num_succs = number of actions/number of successor nodes
            reach prob = current probability that villain holds this hand at this node
            current_probs = villains current strategy at this node (calculated by regret matching in the ProcessOppProbs function, before this function is called)
            it = iteration, used for the warmup stuff
            soft warmup, hardwarmup
            sumprob scaling is not even used in this function. It is used in a similar function below.
            sumprob scaling might have to do with preventing overflow? 
    outputs: succ_opp_probs = probability opponent reaches each successor node with current hand
            sumprobs = villains strategy sum (used for finding average strategy)
    -Brian
*/

static void UpdateSumprobsAndSuccOppProbs(int enc, int num_succs, double reach_prob,
					  double *current_probs,
					  shared_ptr<double []> *succ_opp_probs, int it,
					  int soft_warmup, int hard_warmup, double sumprob_scaling,
					  double *sumprobs) {
  for (int s = 0; s < num_succs; ++s) {
    double succ_opp_prob = reach_prob * current_probs[s];
    succ_opp_probs[s][enc] = succ_opp_prob;
    if (sumprobs) {
      if ((hard_warmup == 0 && soft_warmup == 0) ||
	  (soft_warmup > 0 && it <= soft_warmup)) {
	// Update sumprobs with weight of 1.  Do this when either:
	// a) There is no warmup (hard or soft), or
	// b) We are during the soft warmup period.
	sumprobs[s] += succ_opp_prob;
      } else if (hard_warmup > 0 && it > hard_warmup) {
	// Use a weight of (it - hard_warmup)
	sumprobs[s] += succ_opp_prob * (it - hard_warmup);
      } else if (soft_warmup > 0) {
	// Use a weight of (it - soft_warmup)
	sumprobs[s] += succ_opp_prob * (it - soft_warmup);
      }
    }
  }
}
/*similar as above function but int* sumprobs instead of double* sumprobs
  also uses sumprob scaling and downscaling to prevent overflow? -Brian*/
static void UpdateSumprobsAndSuccOppProbs(int enc, int num_succs, double reach_prob,
					  double *current_probs,
					  shared_ptr<double []> *succ_opp_probs, int it,
					  int soft_warmup, int hard_warmup, double sumprob_scaling,
					  int *sumprobs) {
  bool downscale = false;
  for (int s = 0; s < num_succs; ++s) {
    double succ_opp_prob = reach_prob * current_probs[s];
    succ_opp_probs[s][enc] = succ_opp_prob;
    if (sumprobs) {
      if ((hard_warmup == 0 && soft_warmup == 0) ||
	  (soft_warmup > 0 && it <= soft_warmup)) {
	// Update sumprobs with weight of 1.  Do this when either:
	// a) There is no warmup (hard or soft), or
	// b) We are during the soft warmup period.
	sumprobs[s] += lrint(succ_opp_prob * sumprob_scaling);
      } else if (hard_warmup > 0) {
	// Use a weight of (it - hard_warmup)
	sumprobs[s] += lrint(succ_opp_prob * (it - hard_warmup) *
			     sumprob_scaling);
      } else {
	// Use a weight of (it - soft_warmup)
	sumprobs[s] += lrint(succ_opp_prob * (it - soft_warmup) *
			     sumprob_scaling);
      }
      if (sumprobs[s] > 2000000000) {
	downscale = true;
      }
    }
  }
  if (downscale) {
    for (int s = 0; s < num_succs; ++s) {
      sumprobs[s] /= 2;
    }
  }
}

// There are two versions of ProcessOppProbs().
// The first one is for use when running CFR+ with an abstraction.
// We have the current_probs already (from current_strategy_) so we use
// them directly.  The second version below uses regret matching to get the
// current probs.

/*This one is for cfr+ with abstraction, see below function for what we're interested in -Brian*/
template <typename T>
void ProcessOppProbs(Node *node, const CanonicalCards *hands, int *street_buckets,
		     double *opp_probs, shared_ptr<double []> *succ_opp_probs,
		     double *current_probs, int it, int soft_warmup, int hard_warmup,
		     double sumprob_scaling, CFRStreetValues<T> *sumprobs) {
  int st = node->Street();
  int num_succs = node->NumSuccs();
  int pa = node->PlayerActing();
  int nt = node->NonterminalID();
  int num_hole_cards = Game::NumCardsForStreet(0);
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  int max_card1 = Game::MaxCard() + 1;
  for (int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double opp_prob = opp_probs[enc];
    if (opp_prob == 0) {
      for (int s = 0; s < num_succs; ++s) {
	succ_opp_probs[s][enc] = 0;
      }
    } else {
      double *my_current_probs;
      T *my_sumprobs = nullptr;
      int b = street_buckets[i];
      int offset = b * num_succs;
      my_current_probs = current_probs + offset;
      if (sumprobs) my_sumprobs = sumprobs->AllValues(pa, nt) + offset;
      UpdateSumprobsAndSuccOppProbs(enc, num_succs, opp_prob, my_current_probs, succ_opp_probs, it,
				    soft_warmup, hard_warmup, sumprob_scaling, my_sumprobs);
    }
  }
}

// Instantiate
template void ProcessOppProbs<int>(Node *node, const CanonicalCards *hands, int *street_buckets,
				   double *opp_probs, shared_ptr<double []> *succ_opp_probs,
				   double *current_probs, int it, int soft_warmup,
				   int hard_warmup, double sumprob_scaling,
				   CFRStreetValues<int> *sumprobs);
template void ProcessOppProbs<double>(Node *node, const CanonicalCards *hands,
				      int *street_buckets, double *opp_probs,
				      shared_ptr<double []> *succ_opp_probs,
				      double *current_probs, int it, int soft_warmup,
				      int hard_warmup, double sumprob_scaling,
				      CFRStreetValues<double> *sumprobs);

/*For all possible hands-
  Calculates opponents strategy with reget matching
  Calls function UpdateSumprobsAndSuccOppProbs to update villains strategysum at this node and update 
  their reach probability for each successor node based on their current strategy for this node
  
  inputs: node = current node
          lbd = not sure exactly what it is, it's used to find an offset for pointer arithmetic
          hads = all possible hands
          bucketed = im not sure, the function above is used for running cfr+ with abstraction.
          this might be used for isomorphisms?
          street_buckets = only used if bucketed = true
          opp_probs = opponent reach probabilities for every hand for the current node
          cs_vals = all regret values
          dsi = default strategy index? used on the first iteration in RMProbs function when regrets = 0
          it = iteration, used for warmups when calculating strategy sum (placing less weight on earlier iterations)
          softwarmup,hardwarmup = solver parameters
          sumprob scaling not even used when using doubles


          
  outputs: succ_opp_probs = opponents reach probabilities for successor nodes = current reach probability * currentstrategy
            sumprobs = current node strategy sum (used for calculating average strategy at the end)

  -Brian
*/

template <typename T1, typename T2>
void ProcessOppProbs(Node *node, int lbd, const CanonicalCards *hands, bool bucketed,
		     int *street_buckets, double *opp_probs, shared_ptr<double []> *succ_opp_probs,
		     const CFRStreetValues<T1> &cs_vals, int dsi, int it, int soft_warmup,
		     int hard_warmup, double sumprob_scaling, CFRStreetValues<T2> *sumprobs) {
  int st = node->Street();
  int num_succs = node->NumSuccs();
  int pa = node->PlayerActing();
  int nt = node->NonterminalID();
  unique_ptr<double []> current_probs(new double[num_succs]);
  int num_hole_cards = Game::NumCardsForStreet(0);
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  int max_card1 = Game::MaxCard() + 1;
  const T1 *all_cs_vals = cs_vals.AllValues(pa, nt);
  T2 *all_sumprobs = nullptr;
  if (sumprobs) all_sumprobs = sumprobs->AllValues(pa, nt);
  for (int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double opp_prob = opp_probs[enc];
    if (opp_prob == 0) {
      for (int s = 0; s < num_succs; ++s) {
	succ_opp_probs[s][enc] = 0;
      }
    } else {
      int offset;
      if (bucketed) {
	offset = street_buckets[i] * num_succs;
      } else {
	offset = lbd * num_hole_card_pairs * num_succs + i * num_succs;
      }
      // cs_vals.RMProbs(pa, nt, offset, num_succs, dsi, current_probs.get());
      RMProbs(all_cs_vals + offset, num_succs, dsi, current_probs.get());
      UpdateSumprobsAndSuccOppProbs(enc, num_succs, opp_prob, current_probs.get(), succ_opp_probs,
				    it, soft_warmup, hard_warmup, sumprob_scaling,
				    all_sumprobs ? all_sumprobs + offset : nullptr);
    }
  }
}

// Instantiate
template void
ProcessOppProbs<int, int>(Node *node, int lbd, const CanonicalCards *hands, bool bucketed,
			  int *street_buckets, double *opp_probs,
			  shared_ptr<double []> *succ_opp_probs,
			  const CFRStreetValues<int> &cs_vals, int dsi, int it,
			  int soft_warmup, int hard_warmup, double sumprob_scaling,
			  CFRStreetValues<int> *sumprobs);
template void
ProcessOppProbs<double, double>(Node *node, int lbd, const CanonicalCards *hands, bool bucketed,
				int *street_buckets, double *opp_probs,
				shared_ptr<double []> *succ_opp_probs,
				const CFRStreetValues<double> &cs_vals,
				int dsi, int it, int soft_warmup, int hard_warmup,
				double sumprob_scaling,	CFRStreetValues<double> *sumprobs);
template void
ProcessOppProbs<int, double>(Node *node, int lbd, const CanonicalCards *hands,
			     bool bucketed, int *street_buckets, double *opp_probs,
			     shared_ptr<double []> *succ_opp_probs,
			     const CFRStreetValues<int> &cs_vals, int dsi, int it,
			     int soft_warmup, int hard_warmup, double sumprob_scaling,
			     CFRStreetValues<double> *sumprobs);
template void
ProcessOppProbs<double, int>(Node *node, int lbd, const CanonicalCards *hands,
			     bool bucketed, int *street_buckets, double *opp_probs,
			     shared_ptr<double []> *succ_opp_probs,
			     const CFRStreetValues<double> &cs_vals, int dsi,
			     int it, int soft_warmup, int hard_warmup,
			     double sumprob_scaling,
			     CFRStreetValues<int> *sumprobs);
template void
ProcessOppProbs<unsigned char, int>(Node *node, int lbd, const CanonicalCards *hands, bool bucketed,
				    int *street_buckets, double *opp_probs,
				    shared_ptr<double []> *succ_opp_probs,
				    const CFRStreetValues<unsigned char> &cs_vals, int dsi, int it,
				    int soft_warmup, int hard_warmup, double sumprob_scaling,
				    CFRStreetValues<int> *sumprobs);

#if 0
// Abstracted, integer regrets
// No flooring here.  Will be done later.
void VCFR::UpdateRegretsBucketed(Node *node, int **street_buckets, double *vals, double **succ_vals,
				 int *regrets) {
  int st = node->Street();
  int num_succs = node->NumSuccs();
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);

  int ceiling = regret_ceilings_[st];
  if (nn_regrets_) {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      int b = street_buckets[st][i];
      int *my_regrets = regrets + b * num_succs;
      for (int s = 0; s < num_succs; ++s) {
	double d = succ_vals[s][i] - vals[i];
	// Need different implementation for doubles
	int di = lrint(d * regret_scaling_[st]);
	int ri = my_regrets[s] + di;
	if (ri > ceiling) {
	  my_regrets[s] = ceiling;
	} else {
	  my_regrets[s] = ri;
	}
      }
    }
  } else {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      int b = street_buckets[st][i];
      int *my_regrets = regrets + b * num_succs;
      bool overflow = false;
      for (int s = 0; s < num_succs; ++s) {
	double d = succ_vals[s][i] - vals[i];
	my_regrets[s] += lrint(d * regret_scaling_[st]);
	if (my_regrets[s] < -2000000000 || my_regrets[s] > 2000000000) {
	  overflow = true;
	}
      }
      if (overflow) {
	for (int s = 0; s < num_succs; ++s) {
	  my_regrets[s] /= 2;
	}
      }
    }
  }
}

// Abstracted, double regrets
// This implementation does not round regrets to ints, nor do scaling.
// No flooring here.  Will be done later.
void VCFR::UpdateRegretsBucketed(Node *node, int **street_buckets, double *vals, double **succ_vals,
				 double *regrets) {
  int st = node->Street();
  int num_succs = node->NumSuccs();
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  
  double ceiling = regret_ceilings_[st];
  if (nn_regrets_) {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      int b = street_buckets[st][i];
      double *my_regrets = regrets + b * num_succs;
      for (int s = 0; s < num_succs; ++s) {
	double newr = my_regrets[s] + succ_vals[s][i] - vals[i];
	if (newr > ceiling) {
	  my_regrets[s] = ceiling;
	} else {
	  my_regrets[s] = newr;
	}
      }
    }
  } else {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      int b = street_buckets[st][i];
      double *my_regrets = regrets + b * num_succs;
      for (int s = 0; s < num_succs; ++s) {
	my_regrets[s] += succ_vals[s][i] - vals[i];
      }
    }
  }
}
#endif

/*
    I think every iteration is saved in the cfr folder. This function is for deleting the old files,
     as we're just interested in the last iteration. - Brian
 */

void DeleteOldFiles(const CardAbstraction &ca, const string &betting_abstraction_name,
		    const CFRConfig &cc, int it) {
  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::NewCFRBase(), Game::GameName().c_str(),
	  Game::NumPlayers(), ca.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(), betting_abstraction_name.c_str(),
	  cc.CFRConfigName().c_str());

  if (! FileExists(dir)) return;
  
  vector<string> listing;
  GetDirectoryListing(dir, &listing);
  int num_listing = listing.size();
  int num_deleted = 0;
  for (int i = 0; i < num_listing; ++i) {
    string full_path = listing[i];
    int full_path_len = full_path.size();
    int j = full_path_len - 1;
    while (j > 0 && full_path[j] != '/') --j;
    if (strncmp(full_path.c_str() + j + 1, "sumprobs", 8) == 0 ||
	strncmp(full_path.c_str() + j + 1, "regrets", 7) == 0) {
      string filename(full_path, j + 1, full_path_len - (j + 1));
      vector<string> comps;
      Split(filename.c_str(), '.', false, &comps);
      if (comps.size() != 8) {
	fprintf(stderr, "File \"%s\" has wrong number of components\n",
		full_path.c_str());
	exit(-1);
      }
      int file_it;
      if (sscanf(comps[5].c_str(), "%u", &file_it) != 1) {
	fprintf(stderr, "Couldn't extract iteration from file \"%s\"\n",
		full_path.c_str());
	exit(-1);
      }
      if (file_it == it) {
	RemoveFile(full_path.c_str());
	++num_deleted;
      }
    }
  }
  fprintf(stderr, "%u files deleted\n", num_deleted);
}
