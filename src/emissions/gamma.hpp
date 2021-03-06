#ifndef GAMMA_HPP
#define GAMMA_HPP

#include <base_classes.hpp>
#include <em_base.hpp>

#include "../src/math.hpp"


class Gamma : public EmissionFunction {
public:
  Gamma(int stateID, int slotID, double shape = 1.0, double scale = 2.0) : EmissionFunction(stateID, slotID), _shape(shape), _scale(scale), _fixedParams(false), _offset(0), _tolerance(1e-6), _maxIter(100) {
    update_constants();
  }

  ~Gamma() {
  }
  
  virtual bool validParams(Params const & params) const {
    // supports two parameters: shape, scale
    // all three must be > 0
    if (params.length() != 2)
      return false;
    
    for (int i = 0; i < params.length(); ++i)
      if (params[i] <= 0)
        return false;

    return true;
  }
  
  virtual Params * getParams() const {
    Params * params;
    double pvals[2] = {_shape, _scale };
    params = new Params(2, pvals);

    if (_fixedParams) {
      params->setFixed(0, true);
      params->setFixed(1, true);
    }
    return params;
  }
  
  virtual void setParams(Params const & params) {
    _fixedParams = params.isFixed(0) || params.isFixed(1);
    update_shape_scale(params[0], params[1]);
  }
  
  virtual bool getOption(const char * name, double * out_value) {
    if (!strcmp(name, "offset")) {
      *out_value = (double) _offset;
      return true;
    } else if(!strcmp(name, "maxIter")) {
      *out_value = _maxIter;
      return true;
    } else if (!strcmp(name, "tolerance")) {
      *out_value = _tolerance;
      return true;
    }
    return false;
  }
  
  virtual bool setOption(const char * name, double value) {
    if (!strcmp(name, "offset")) {
      _offset = value;
      return true;
    } else if (!strcmp(name, "maxIter")) {
      int maxIter = (int) value;
      if (maxIter <= 0) {
        log_msg("maxIter must be > 0: %d\n", maxIter);
        return false;
      }
      _maxIter = maxIter;
      return true;
    } else if (!strcmp(name, "tolerance")) {
      if (value < 0) {
        log_msg("tolerance must be >= 0: %g\n", value);
        return false;
      }
      _tolerance = value;
      return true;
    }
    return false;
  }

  virtual double log_probability(Iter const & iter) const {
    double x = (iter.emission(_slotID) + _offset);
    
    assert(x >= 0);
    
    return _A + (_shape - 1) * log(x) - x / _scale;
  }
  
  virtual void updateParams(EMSequences * sequences, std::vector<EmissionFunction*> * group) {
    if (_fixedParams)
      return;
    
    // sufficient statistics
    double sum_Pzi = 0;
    double sum_Pzi_xi = 0;
    double sum_Pzi_log_xi = 0;
    
    std::vector<EmissionFunction*>::iterator ef_it;
    
    for (ef_it = group->begin(); ef_it != group->end(); ++ef_it) {
      Gamma * ef = (Gamma*) (*ef_it)->inner();
      PosteriorIterator * post_it = sequences->iterator(ef->_stateID, ef->_slotID);
      
      do {
        const double * post_j = post_it->posterior();
        Iter & iter = post_it->iter();
        iter.resetFirst();
        
        for (int j = 0; j < iter.length(); iter.next(), ++j) {
          double x = (iter.emission(ef->_slotID) + _offset);
          
          sum_Pzi += post_j[j];
          sum_Pzi_xi += post_j[j] * x;
          sum_Pzi_log_xi += post_j[j] * log(x);
        }
      } while (post_it->next());
      
      delete post_it;
    }
    
    // update parameter
    // 1. estimate shape
    double mean = sum_Pzi_xi / sum_Pzi;
    double s = log(mean) - sum_Pzi_log_xi / sum_Pzi;

    //log_state_slot_msg(_stateID, _slotID, " U: mean = %g, sum_Pzi_xi = %g, sum_Pzi = %g sum_Pzi_log_xi = %g\n", mean, sum_Pzi_xi, sum_Pzi, sum_Pzi_log_xi);

    // 1.1 Initial guess
    double shape = (3 - s + sqrt(pow(s - 3, 2) + 24 * s)) / (12 * s);
    
    if (QHMM_isinf(shape) || QHMM_isnan(shape) || shape <= 0) {
      log_state_slot_msg(_stateID, _slotID, "initial shape guess failed: %g (starting with old value: %g)\n", shape, _shape);
      shape = _shape;
    }
    
    // 1.2 Apply Newton's method to refine estimate
    double ki;
    double knext = shape;
    int i = 0;
    do {
      ++i;
      ki = shape;
      /*log_state_slot_msg(_stateID, _slotID, " U[%d] s = %g, ki = %g\n",
			 i, s, ki);
      */
      
      /* update shape */
      //knext = ki - (log(ki) - digamma(ki) - s) / (1.0/ki - trigamma(ki));
      knext = ki - (log(ki) - QHMM_digamma(ki) - s) / (1.0 / ki - QHMM_trigamma(ki));
      
      /* test boundary conditions */
      if (QHMM_isinf(shape) || QHMM_isnan(shape) || shape <= 0) {
        log_state_slot_msg(_stateID, _slotID, "shape update failed: %g (keeping old value: %g)\n", knext, _shape);

        knext = shape;
        break;
      }
    } while (fabs(ki - shape) > _tolerance && i < _maxIter);

    shape = knext;
    
    // check for weirdness
    if (shape > 1000 || QHMM_isnan(shape) || QHMM_isinf(shape)) {
      log_state_slot_msg(_stateID, _slotID, "shape update failed: %g (keeping old value: %g)\n", shape, _shape);
      return;
    }
    
    // 1.3 Accept update
    _shape = shape;
    
    // 2. Update scale
    _scale = mean / _shape;
    
    update_constants();

    // propagate to other elements in the group
    for (ef_it = group->begin(); ef_it != group->end(); ++ef_it) {
      Gamma * ef = (Gamma*) (*ef_it)->inner();
      
      if (ef != this) {
        ef->_shape = _shape;
        ef->_scale = _scale;
        ef->update_constants();
      }
    }
  }
  
private:
  double _shape;
  double _scale;
  bool _fixedParams;
  double _offset;
  double _tolerance;
  int _maxIter;

  double _A; // -log Gamma(_shape) -_shape * log(_scale) 

  void update_constants() {
    _A = -QHMM_log_gamma(_shape) - _shape * log(_scale);
  }

  void update_shape_scale(double shape, double scale) {
    _shape = shape;
    _scale = scale;
    update_constants();
  }
};

#endif
