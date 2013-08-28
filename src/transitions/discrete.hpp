#ifndef DISCRETE_HPP
#define DISCRETE_HPP

#include <cmath>
#include <limits>
#include "../base_classes.hpp"
#include "../em_base.hpp"

class Discrete : public TransitionFunction {
public:
  Discrete(int n_states, int stateID, int n_targets, int * targets) : TransitionFunction(n_states, stateID, n_targets, targets) {

    _log_probs = new double[n_states];

    // default equi-probable
    // set all to -Inf
    for (int i = 0; i < _n_states; ++i)
      _log_probs[i] = -std::numeric_limits<double>::infinity();

    double log_prob = -log(_n_states);
    for (int i = 0; i < _n_targets; ++i)
      _log_probs[_targets[i]] = log_prob;
  }

  ~Discrete() {
    delete[] _log_probs;
  }

  virtual bool validParams(Params const & params) const {
    double sum = 0.0;

    for (int i = 0; i < params.length(); ++i)
      sum = sum + params[i];

    return params.length() == _n_targets && same_probability(sum, 1.0);
  }
  
  virtual Params * getParams() const {
    double * probs = new double[_n_targets];

    for (int i = 0; i < _n_targets; ++i)
      probs[i] = exp(_log_probs[_targets[i]]);

    Params * result = new Params(_n_targets, probs);

    delete probs;
    return result;
  }

  virtual void setParams(Params const & params) {
    for (int i = 0; i < _n_targets; ++i)
      _log_probs[_targets[i]] = log(params[i]);
  }
  
  virtual double log_probability(int target) const {
    return _log_probs[target];
  }
    
  virtual double log_probability(Iter const & iter, int target) const {
    return log_probability(target);
  }

  virtual void updateParams(EMSequences * sequences, std::vector<TransitionFunction*> * group) {
    // sufficient statistics are the per target expected counts
    double expected_counts[_n_targets];

    EMSequences::PosteriorTransitionIterators * siter = sequences->transition_iterators(*group);

    // initialize
    for (int i = 0; i < _n_targets; ++i)
      expected_counts[i] = 0;

    // sum expected counts
    do {
      PostIter & piter = siter->iter();

      piter.reset();
      do {
	for (unsigned int gidx = 0; gidx < group->size(); ++gidx)
	  for (int tgt_idx = 0; tgt_idx < _n_targets; ++tgt_idx)
	    expected_counts[tgt_idx] += piter.posterior(gidx, tgt_idx);
      } while (piter.next());
    } while (siter->next());

    // estimate parameters
    double normalization = 0;
    for (int i = 0; i < _n_targets; ++i)
      normalization += expected_counts[i];
    for (int i = 0; i < _n_targets; ++i)
      _log_probs[_targets[i]] = log(expected_counts[i] / normalization);

    // propagate to other elements in the group
    std::vector<TransitionFunction*>::iterator tf_it;
    for (tf_it = group->begin(); tf_it != group->end(); ++tf_it) {
      Discrete * tf = (Discrete*) *tf_it;

      if (tf != this) {
	for (int i = 0; i < _n_targets; ++i)
	  tf->_log_probs[tf->_targets[i]] = _log_probs[_targets[i]];
      }
    }

    delete siter;
  }

private:
  double * _log_probs;
};

#endif
