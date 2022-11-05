/*************************************************************************
    func.cpp  -  class func, extending the GiNaC function class
                             -------------------
    begin                : Fri Feb 13 2004
    copyright            : (C) 2004 by Jan Rheinlaender
    email                : jrheinlaender@users.sourceforge.net
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifdef INSIDE_SM
#include <imath/func.hxx>
#include <imath/funcmgr.hxx>
#include <imath/msgdriver.hxx>
#include <imath/unit.hxx>
#else
#include "func.hxx"
#include "funcmgr.hxx"
#include "msgdriver.hxx"
#include "unit.hxx"
#endif
#include "exderivative.hxx"

namespace GiNaC {
class round_SERIAL { public: static unsigned serial; };

GINAC_IMPLEMENT_REGISTERED_CLASS_OPT(func, exprseq, print_func<print_context>(&func::do_print).print_func<imathprint>(&func::do_print_imath));

// Required constructors and destructors and other GiNaC-specific methods-----------------
// Default constructor etc.
func::func() : name("") {
  MSG_INFO(3, "Constructing empty func" << endline);
}

#ifdef DEBUG_CONSTR_DESTR
  func::func(const func& other) : exprseq(other.seq), name(other.name), serial(other.serial), vars(other.vars), definition(other.definition), hard(other.hard), hints(other.hints) {
    MSG_INFO(3, "Copying func from " << other.name << endline);
  }
  func& func::operator=(const func& other) {
    MSG_INFO(3, "Assigning func from " << other.name << endline);
    seq = other.seq;
    name = other.name;
    serial = other.serial;
    vars = other.vars;
    definition = other.definition;
    hard = other.hard;
    hints = other.hints;
    return *this;
  }
  func::~func() {
    MSG_INFO(3, "Destructing func " << name << endline);
  }
#endif

func::func(const std::string &n, const exprseq &args, const unsigned s, const bool hc, const exvector& v, const expression& d, const unsigned h, const std::string& pn)
    : exprseq(args), name(n), serial(s), hard(hc), vars(v), definition(d), hints(h), printname(pn) {
  MSG_INFO(2, "Constructing " << (hard ? "hard-coded " : "") << "func from exprseq with name " << n << " and arguments " << seq << endline);
}
func::func(const std::string &n, exprseq &&args, const unsigned s, const bool hc, const exvector& v, const expression& d, const unsigned h, const std::string& pn)
    : exprseq(std::move(args)), name(n), serial(s), hard(hc), vars(v), definition(d), hints(h), printname(pn) {
  MSG_INFO(2, "Moving " << (hard ? "hard-coded " : "") << "func from exprseq with name " << n << " and arguments " << seq << endline);
}

expression func::makepure() const {
  func result = *this;
  result.seq.clear();
  return result;
}

void func::printseq(const print_context & c, const std::string &openbracket, const std::string& delim,
      const std::string &closebracket) const {
  MSG_INFO(5, "func::printseq() with print_context called" << endline);
  c.s << openbracket;

  if (!seq.empty()) {
    exvector::const_iterator it = seq.begin();
    while (it != seq.end()) {
      it->print(c, 0);
      if (it < seq.end() - 1) c.s << delim;
      ++it;
    }
  }

  c.s << closebracket;
}

void func::do_print(const print_context &c, unsigned level) const {
  (void)level;
  c.s << name;
  if (seq.empty()) c.s << vars;
  exprseq::printseq(c, '(', ',', ')', exprseq::precedence(), func::precedence());
}

void func::do_print_imath(const imathprint &c, unsigned level) const {
  print_imath(c, 1, level);
}

void func::print_imath(const imathprint&c, const ex& p, unsigned level) const {
  MSG_INFO(2, "func::print_imath() called for " << name << endline);

  // Set the argument delimiter. The spaces are required because otherwise in (x_1, x_2) the comma moves into the subscript
  // Note that this depends on the locale being set to "C" (default)
  std::string delim = " , ";
  if (imathprint::decimalpoint != ".")
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning (disable: 4129)
#endif
     delim = " \%fc ";
#ifdef _MSC_VER
#pragma warning(pop)
#endif

  if (hard) {
    if ((hints & FUNCHINT_PRINT) && !seq.empty()) {
      // Case 1: Hardcoded functions that come with GiNaC and should use their own print function)
      // Case 2: Hardcoded functions that come with iMath (always use their own print function)
      MSG_INFO(3, "Printing hardcoded function through its own print function" << endline);
      function(serial, seq).print(c, level+1); // Fall through to the hard-coded print function
      return;
    } else {
      // Case 3: Hardcoded functions that come with GiNaC and require special handling
      MSG_INFO(3, "Printing hardcoded function with special handling" << endline);
      // TODO: Is it possible to register a print context handler on functions?
      if (name == "fact") {
        c.s << "fact ";
        if (seq.empty()) { // Avoid crash
          c.s << "()";
          return;
        }
        if (is_a<numeric>(op(0)) || is_a<symbol>(op(0)) || is_a<power>(op(0)) ||
            is_a<Unit>(op(0)) || (is_a<func>(op(0)) && !ex_to<func>(op(0)).is_lib())) // do not print brackets
          seq[0].print(c);
        else
          printseq(c, "(", delim, ")");
        return;
      } else if (name == "conjugate") {
        // The hardcoded function prints conjugate() which is not nice
        c.s << "overline";
        printseq(c, "{", delim, "}");
        return;
      } else if (name == "ln") {
        // Note: The hardcoded print function for ln prints log!!
        c.s << "ln";
        printseq(c, "(", delim, ")");
        return;
      }
      // All other hardcoded functions are handled like soft-coded functions
    }
  }

  // Case 4: All other hardcoded functions and user-defined functions
  MSG_INFO(3, "Printing user-defined function or other hardcoded function" << endline);
  if (p != 1) { // Print the exponent of a trigonometric function directly after the function name
    c.s << printname << "^";
    p.print(c, level+1);
    if (seq.empty()) return;
  } else {
    if (!hard && is_lib() && name[0] != '%') // func does not work in combination with e.g. %alpha
      c.s << "func "; // so that sec() etc. are not printed in italics like user-defined functions
    c.s << printname;
    if (seq.empty()) return;
  }

  if (is_nobracket()) { // do not print brackets
    printseq(c, "{", delim, "}");
  } else if (is_trig() && (seq.size() == 1) &&
             (is_a<numeric>(seq[0]) || is_a<symbol>(seq[0]) || is_a<power>(seq[0]) || is_a<Unit>(seq[0]) || (is_a<func>(seq[0]) && !ex_to<func>(seq[0]).is_lib()))) {
    c.s << "{";
    seq[0].print(c); // do not print brackets for trigonometric functions of a simple argument
    c.s << "}";
  } else {
    printseq(c, "(", delim, ")");
  }
}

void func::print_diff_line(const ex& g, const int gr, const print_context& c) const {
  // TODO: All this is mostly copied from func::print()
  if (hard && (hints & FUNCHINT_PRINT))
    MSG_WARN(0, "Warning: Cannot use hardcoded print function for difftype line" << endline);

  // Concatenate the lines
  std::string lines = "";
  std::string diffchar = "%d1"; // Special character from Math catalog 'iMath', see iMathFormatting.xcu
  if (gr > 0)
    for (int i = 0; i < gr; i++) lines += diffchar;
  lines = "^{" + lines + "}";

  // Set the argument delimiter
  std::string delim = " , ";
  if (imathprint::decimalpoint != ".")
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning (disable: 4129)
#endif
     delim = " \%fc ";
#ifdef _MSC_VER
#pragma warning(pop)
#endif

  std::string fname = printname;
  if (!hard && is_lib())
    fname = "func " + fname; // so that sec() etc. are not printed in italics like user-defined functions

  if (!is_lib() || seq.empty()) {
    // Print e.g. as f'(x)
    c.s << fname;
    if ((gr > 0) && (gr < 4))
      c.s << lines;
    else
      c.s << "^(" << g << ")";
  } else
    // Print e.g. as (sin x)'
    c.s << "(" << fname;

  if (seq.empty()) return;

  if (is_nobracket()) { // do not print brackets
    printseq(c, "{", delim, "}");
  } else if (is_trig() && (seq.size() == 1) &&
             (is_a<numeric>(seq[0]) || is_a<symbol>(seq[0]) || is_a<power>(seq[0]) || is_a<Unit>(seq[0]) || (is_a<func>(seq[0]) && !ex_to<func>(seq[0]).is_lib()))) {
    c.s << "{";
    seq[0].print(c); // do not print brackets for trigonometric functions of a simple argument
    c.s << "}";
  } else {
    printseq(c, "(", delim, ")");
  }

  if (is_lib()) {
    c.s << ")";
    if ((gr > 0) && (gr < 4))
      c.s << lines;
    else
      c.s << "^(" << g << ")";
  }
}

expression func::expand_definition() const {
  if (definition.is_empty() && !hard)
    throw(std::logic_error("Warning: Function " + name + " has no definition! Not expanded."));

  if (seq.empty()) { // There is nothing to expand
    if (hard)
      return this->setflag(status_flags::expanded);
    else
      return definition;
  }

  if (hard)
    return this->setflag(status_flags::expanded);

  // Substitute the arguments in the definition
  exmap subst;
  for (unsigned i = 0; i < seq.size(); i++)
    subst.emplace(vars[i], seq[i]);
  expression result = definition.subs(subst);
  MSG_INFO(2, "Expanding user function " << name << "(" << subst << ") = "
                     << definition << endline
                     << "Expansion result: " << result << endline);
  return(result);
}

ex func::expand(unsigned options) const {
  MSG_INFO(2, "Expanding " << name << endline);
  func result;

  if (options & expand_options::expand_function_args) { // Only expand arguments when asked to do so
    if (name == "mindex") { // Special treatment, because we don't want the indices to became floats...
      MSG_INFO(3,  "Expanding mindex: " << *this << endline);
      result = *this;
    } else {
      result = ex_to<func>(inherited::expand(options));
    }
  } else {
    result = *this;
  }

  if (!definition.is_empty() && !(options & no_expand_function_definition)) { // is there a function definition that can be expanded?
    try {
      return (result.expand_definition());//TODO: should we do another expand() on this result,
        // since the definition itself has not been expanded yet?
        // Should this depend on expand_options::expand_function_args ?
    } catch (std::runtime_error &e) {
      MSG_ERROR(0, e.what() << endline);
    } catch (std::logic_error &e) {
      MSG_ERROR(0, e.what() << endline);
    }
    return result;
  } else {
    return (options == 0) ? result.setflag(status_flags::expanded) : ex(result);
  }
} // func::expand()

ex func::conjugate() const {
  if (hard) { // Fall through to GiNaC::function::conjugate()
    ex result = function(serial, seq).setflag(status_flags::evaluated).conjugate(); // Note: Omitting setting the flag results in an infinite loop!
    // TODO: Compare if something changed, e.g. if (are_ex_trivially_equal(funct, result)) return *this;
    return Functionmanager::replace_function_by_func(result).eval(); // get rid of GiNaC functions that might have been introduced
  }

  return Functionmanager::create_hard("conjugate", {ex(*this)});
}

ex func::real_part() const {
  if (hard) { // Fall through to GiNaC::function::real_part()
    ex result = function(serial, seq).setflag(status_flags::evaluated).real_part(); // Note: Omitting setting the flag results in an infinite loop!
    return Functionmanager::replace_function_by_func(result).eval(); // get rid of GiNaC functions that might have been introduced
  }

  return Functionmanager::create_hard("Re", {ex(*this)});
}

ex func::imag_part() const {
  if (hard) { // Fall through to GiNaC::function::imag_part()
    ex result = function(serial, seq).setflag(status_flags::evaluated).imag_part(); // Note: Omitting setting the flag results in an infinite loop!
    return Functionmanager::replace_function_by_func(result).eval(); // get rid of GiNaC functions that might have been introduced
  }

  return Functionmanager::create_hard("Im", {ex(*this)});
}

bool func::has(const ex & other, unsigned options) const {
  MSG_INFO(3, "Checking if " << *this << " has " << other << endline);
  if (is_a<func>(other) && this->is_equal_same_type(ex_to<func>(other)))
    return true;
  else if (seq.empty()) {
    for (const auto& v : vars) {
      MSG_INFO(3, "Checking if " << v << " has " << other << endline);
      if (v.has(other, options)) return true;
    }
    return false;
  } else
    return basic::has(other, options);
}

 ex func::eval() const {
  // If the function is hardcoded, drop through to the GiNaC::function::eval() method
  MSG_INFO(3, "Doing eval of " << name << "(" << seq << ")" << endline);
  if (hard) {
    MSG_INFO(3, "Function is hard-coded with serial " << serial << endline);
    if (seq.size() == 1) {
      if (is_exactly_a<func>(seq[0])) {
        const func& f0 = ex_to<func>(seq[0]);

        if (f0.hard) {
          // Take advantage of the hard-coded GiNac eval rules, e.g. tan(atan(x)) = x
          ex func_arg = function(f0.serial, f0.seq);
          ex result = function(serial, func_arg).eval();
  #ifndef _MSC_VER
          // Note: MSVC does not guarantee the order of initialization so this test sometimes fails
          if (serial >= round_SERIAL::serial) return result;
  #endif
          if (is_a<function>(result) && ex_to<function>(result).get_serial() == serial)
            return this->hold(); // Nothing appears to have happened

          // Don't introduce any GiNaC::function into the system!
          return Functionmanager::replace_function_by_func(result);
        }
      } else if (name =="ln") {
        if (is_a<constant>(seq[0]) && seq[0].is_equal(Euler_number)) { // handle ln e = 1
          return _ex1;
        } else if (is_a<power>(seq[0])) {
          const power& p = ex_to<power>(seq[0]);
          if (get_basis(p).is_equal(Euler_number)) // handle ln e^x = x
            return get_exp(p);
        }
      }
    }
    if (!seq.empty()) {
      MSG_INFO(3, "Drop through to GinaC eval rules" << endline);
      // Take advantage of the hard-coded GiNaC eval rules, e.g. sin(-2) = -sin(2)
      ex result = function(serial, seq).eval();
#ifndef _MSC_VER
      if (serial >= round_SERIAL::serial) return result;
#endif
      return Functionmanager::replace_function_by_func(result);
    } else {
      return (this->hold());
    }
  }

  if (is_expand() && !definition.is_empty())
    return this->expand_definition(); // no full expansion! Just the function, not the arguments
  else
    return this->hold();
}

ex func::subs(const exmap & m, unsigned options) const {
  MSG_INFO(2, "Substituting exmap " << m << " in " << *this << endline);

  if (!seq.empty()) {
    return exprseq::subs(m, options);
  } else {
    // Substitute into the vars. Required for exderivate::derivative() of a pure function
    exvector newvars;
    bool changed = false;

    for (auto& v : vars) {
      newvars.push_back(v.subs(m, options));
      if (!are_ex_trivially_equal(v, newvars.back())) changed = true;
    }

    if (changed) {
      // We must only return a new object if something has actually changed, otherwise cancelling in muls will fail
      func f(*this);
      f.vars = newvars;
      return f.subs_one_level(m, options);
    } else {
      return subs_one_level(m, options);
    }
  }
}

ex func::map(map_function& f) const {
  MSG_INFO(2, "Func: Mapping " << *this << endline);

  if (!seq.empty()) {
    // Avoid infinite loop that might occur when simply returning exprseq::map(f). This code duplicates basic::map()
    // The loop occurs e.g. when constructing conjugate(f(x)) where conjugate() is hard-coded and f() is an iMath function
    func* copy = nullptr;
    for (size_t i = 0; i < seq.size(); i++) {
      const ex& o = seq[i];
      const ex& n = f(o);
      if (!are_ex_trivially_equal(o, n)) {
        if (copy == nullptr)
          copy = duplicate();
        copy->seq[i] = n;
      }
    }

    if (copy) {
      copy->clearflag(status_flags::hash_calculated | status_flags::expanded);
      return *copy;
    } else
      return *this;
  } else {
    // Map into the vars. Required for partial derivatives of a pure function in expression::pdiff()
    exvector newvars;
    bool changed = false;

    for (const auto& v : vars) {
      newvars.push_back(f(v));
      if (!are_ex_trivially_equal(v, newvars.back())) changed = true;
    }

    if (changed) {
      // We only return a new object if something has actually changed
      func newf(*this);
      newf.vars = newvars;
      return newf;
    } else {
      return *this;
    }
  }
}

expression func::find_integral(const ex& sym) const {
  MSG_INFO(1, "Finding integral for " << *this << endline);
  expression result;
  if (seq.size() > 1) return result;
  if (seq.empty()) return result;

  // Check if the argument of the function is linear in the symbol
  ex factor;
  if (!is_linear(seq[0], sym, factor))
    return result;

  // These functions are not hard-coded and the integral is self-referencing, thus they cannot be handled in the Functionmanager;
  if (name == "arccot")
    result =  seq[0] * *this + _ex1_2 * Functionmanager::create_hard("ln", exprseq({_ex1 + pow(seq[0], _ex2)}));
  else if (name == "arcoth")
    result = (seq[0] * *this + _ex1_2 * Functionmanager::create_hard("ln", exprseq({pow(seq[0], _ex2) - _ex1})));
  else if (name == "arcsec")
    result = seq[0] * *this - Functionmanager::create_hard("arcosh", exprseq({Functionmanager::create_hard("abs", seq)}));
  else if (name == "arsech")
    result = seq[0] * *this - Functionmanager::create_hard("arctan", exprseq({pow(_ex1/pow(seq[0], _ex2) - _ex1, _ex1_2)}));
  else if (name == "arccsc")
    result = seq[0] * *this + Functionmanager::create_hard("arcosh", exprseq({Functionmanager::create_hard("abs", seq)}));
  else if (name == "arcsch")
    result = seq[0] * *this + Functionmanager::create_hard("ln", exprseq({seq[0] + seq[0] * pow(_ex1 + _ex1/pow(seq[0], _ex2), _ex1_2)}));
  else
    result = Functionmanager::find_integral(name, seq);

  if (!result.is_empty()) return result / factor;

  return result;
}

ex func::evalf() const {
  MSG_INFO(3, "Evaluating " << *this << endline);
  if (seq.size() != get_numargs()) // Avoid crash for illegal syntax sin^2(x)
    throw std::runtime_error("Function with insufficient number of arguments cannot be evaluated");

  // If the function is hardcoded, drop through to the GiNaC::function::evalf() method
  // TODO: expression::evalf() will not be called here on the arguments
  if (hard) {
    ex result = function(serial, seq).evalf();
    return Functionmanager::replace_function_by_func(result).eval();
  }

  // Evaluate children first
  exvector eseq;
  eseq.reserve(seq.size());
  for (auto & it : seq) {
    eseq.emplace_back(expression(it).evalf());
  }

  if (!definition.is_empty()) {
    // Note: The code in GiNaC::functions.cpp::evalf() seems to drop eseq here!
    try {
      return dynallocate<func>(name, eseq, serial, hard, vars, definition, hints, printname).expand_definition().evalf();
    } catch (std::exception &e) { // The function cannot be expanded
      MSG_ERROR(0, e.what() << endline);
    }
  }

  return dynallocate<func>(name, eseq, serial, hard, vars, definition, hints, printname).hold();
}

ex func::evalm() const {
  // Evaluate children first
  exvector eseq;
  eseq.reserve(seq.size());

  for (const auto& it : seq)
    eseq.emplace_back(expression(it).evalm());

  return dynallocate<func>(name, eseq, serial, hard, vars, definition, hints, printname).hold();
}

unsigned func::calchash(void) const {
  unsigned v = golden_ratio_hash(make_hash_seed(typeid(*this)) ^ serial);
  for (size_t i=0; i<nops(); i++) {
    v = rotate_left(v);
    v ^= this->op(i).gethash();
  }

  if (flags & status_flags::evaluated) {
    setflag(status_flags::hash_calculated);
    hashvalue = v;
  }
  MSG_INFO(3, "Hash value of " << name << " (serial " << serial << "): " << v << endline);
  return v;
}

ex func::series(const relational &r, int order, unsigned options) const {
  // Not implemented yet. Returns basic::series
  return basic::series(r, order, options);
}

ex func::thiscontainer(const exvector &v) const {
  return dynallocate<func>(name, v, serial, hard, vars, definition, hints, printname);
}

ex func::thiscontainer(exvector &&v) const {
  return dynallocate<func>(name, std::move(v), serial, hard, vars, definition, hints, printname);
}

ex func::derivative(const symbol & s) const {
  MSG_INFO(2, "Calculating derivative of " << *this << " to " << s << endline);

  if (hard) { // Fall through to GiNaC::function::diff(). derivative() is protected!
    // Special handling of sum function - hard-coded derivative function is unusable because differentiation symbol s cannot be passed to it
    if ((name == "sum") && (seq.size() == 3))
      return Functionmanager::create_hard(name, exprseq{seq[0], seq[1], expression(seq[2]).diff(s)});

    ex result = function(serial, seq).setflag(status_flags::evaluated).diff(s); // Note: Omitting setting the flag results in an infinite loop!
    return Functionmanager::replace_function_by_func(result).eval(); // get rid of GiNaC functions that might have been introduced
  }

  ex result(_ex0);

  if (seq.empty()) {
    if (name == s.get_name()) return _ex1; // Handle case dr() / dr()
    for (unsigned i = 0; i < get_numargs(); i++) {
      ex arg_diff = expression(vars[i]).diff(s);
      if (!arg_diff.is_zero()) result += pderivative(i)*arg_diff;
      MSG_INFO(2, "Derivative of " << *this << " stage " << i << ": " << result << endline);
    }
  } else {
    for (unsigned i = 0; i < seq.size(); i++) {
      ex arg_diff = expression(seq[i]).diff(s);
      if (!arg_diff.is_zero()) result += pderivative(i)*arg_diff;
      MSG_INFO(2, "Derivative of " << *this << " stage " << i << ": " << result << endline);
    }
  }
  return result;
}

int func::compare_same_type(const basic &other) const {
  // This function is important because it is used to simplify expressions!
  // If (*this == other) then the expression "*this / other" will be 1.
  // TODO: What is the point of comparing > and < ?
  const func &o = static_cast<const func &>(other);
  MSG_INFO(3, "Comparing " << *this << " and " << o << endline);
  if (name == o.name) {
    if (seq.empty())
      // This is required to make substitution into vars last - otherwise it is discarded
      return (std::equal(vars.begin(), vars.end(), o.vars.begin()) ? 0 : 1);
    else
      return exprseq::compare_same_type(o);
  }
  else if (name < o.name)
    return -1;
  else
    return 1;
}

bool func::is_equal_same_type(const basic & other) const {
  // This function seems to be used for substitution: If a.is_equal_same_type(b),
  // then b==c will be substituted
  const func & o = static_cast<const func &>(other);
  MSG_INFO(3, "Checking equality of " << *this << " and " << o << endline);

  if (name != o.name)
    return false;
  else
    return (exprseq::is_equal_same_type(o));
}

bool func::match_same_type(const basic &other) const {
  const func & o = static_cast<const func &>(other);
  MSG_INFO(3, "Checking match of " << *this << " and " << o << endline);
  return name == o.name;
}

unsigned func::return_type(void) const {
  MSG_INFO(4, "Return type of " << *this << " requested." << endline);
  if (seq.empty())
    return return_types::commutative;
  else
    return seq.begin()->return_type();
}

ex func::pderivative(unsigned diff_param) const {
  MSG_INFO(2, "Calculating partial derivative of " << *this <<
                 " to parameter " << diff_param << endline);

  ex result;
  bool partial = get_numargs() > 1;

  // We assume that no hardcoded functions are called with this method!
  if (definition.is_empty() || !(hints & FUNCHINT_DEFDIFF)) {
    if (seq.size() > diff_param)
      result = dynallocate<exderivative>(dynallocate<differential>(*this, partial),
                                         dynallocate<differential>(seq[diff_param], partial, 1, *this));
    else if (get_numargs() > diff_param)
      result = dynallocate<exderivative>(dynallocate<differential>(*this, partial),
                                         dynallocate<differential>(vars[diff_param], partial, 1, *this));
    else
      throw std::logic_error("The requested dependant variable does not exist in " + name + "()");
  } else {
    if (diff_param >= get_numargs())
      throw std::logic_error("The requested dependant variable does not exist in " + name + "()");

    ex diffvar = vars[diff_param];
    result = definition.diff(diffvar); // pdiff is not necessary here, because we are differentiating directly to the functions' variables

    if (!seq.empty()) { // Substitute the function arguments in the result
      exmap subs_map;
      for (unsigned i = 0; i < get_numargs(); i++) subs_map.emplace(vars[i], seq[i]);
      result = result.subs(subs_map);
    }
  }

  MSG_INFO(2, "Partial derivative #" << diff_param << " of " << *this << ": " << result << endline);
  return result;
}

exvector func::get_args() const {
  if (seq.empty())
    return vars;
  else
    return seq;
}

func_unarchiver::func_unarchiver() {}
func_unarchiver::~func_unarchiver() {}
}
