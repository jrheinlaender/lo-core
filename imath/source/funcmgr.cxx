/*************************************************************************
    funcmgr.cxx  -  class Functionmanager
                             -------------------
    begin                : Sat Oct 22 2022
    copyright            : (C) 2022 by Jan Rheinlaender
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

#include <sstream>

#ifdef INSIDE_SM
#include <imath/funcmgr.hxx>
#include <imath/func.hxx>
#else
#include "func.hxx"
#include "funcmgr.hxx"
#endif
#include "exderivative.hxx"

using namespace GiNaC;

/// A map containing the numbers assigned to different hints
const std::map<std::string, unsigned> funchint_map = {
  {"none",      FUNCHINT_NONE},
  {"lib",       FUNCHINT_LIB},
  {"trig",      FUNCHINT_TRIG},
  {"expand",    FUNCHINT_EXPAND},
  {"nobracket", FUNCHINT_NOBRACKET},
  {"defdiff",   FUNCHINT_DEFDIFF},
  {"print",     FUNCHINT_PRINT}
};

// Define static members------------------------------------------------------------------
std::map<const std::string, Functionmanager::funcrec> Functionmanager::hard_functions = std::map<const std::string, funcrec>();
std::map<const std::string, std::string> Functionmanager::hard_names = std::map<const std::string, std::string>();

std::string Functionmanager::get_hard_name(const std::string& ginac_name) {
  const auto& n_it = hard_names.find(ginac_name);
  if (n_it != hard_names.end())
    return n_it->second;
  return "";
}

expression Functionmanager::replace_function_by_func(const ex& e) {
  struct replace_function_by_func : public map_function {
    // The expression e must already be fully evaluated
    ex operator()(const ex &e) {
      MSG_INFO(3, "Replacing all hard-coded GiNaC functions by EQC funcs in " << e << endline);

      if (is_a<fderivative>(e)) {
        // Parameters which the function is being differentiated to
        const fderivative& f = ex_to<fderivative>(e);
        std::multiset<unsigned> parset = f.derivatives();
        if (parset.empty()) throw std::runtime_error("Internal error: fderivative with no parameters in paramset");
        if (parset.size() > 1) throw std::runtime_error("Internal error: Multiple parameters to fderivative not yet supported");

        // Name of the function being differentiated
        std::string fname = Functionmanager::get_hard_name(f.get_name());
        if (fname.empty()) throw std::runtime_error("Internal error: Unknown hard-coded function " + f.get_name() + " in expression");
        unsigned paridx = *parset.begin();
        if (msg::info().checkprio(2)) {
          msg::info() << "fderivative: index = " << paridx << "; function name: " << fname << "; arguments: ";
          for (const auto& i : f) msg::info() << i << ", ";
          msg::info() << endline;
        }

        const auto& fr = Functionmanager::hard_functions.find(fname)->second;
        ex the_function = dynallocate<func>(fname, exprseq(f.begin(), f.end()), fr.serial, fr.hard, fr.vars, fr.definition, fr.hints, fr.printname);
        if (paridx > ex_to<func>(the_function).get_numargs()) throw std::runtime_error("Error: Differentiation to non-existent variable");
        return dynallocate<exderivative>(dynallocate<differential>(the_function, false, 1),
                                         dynallocate<differential>(f.op(paridx), false, 1, the_function));
      } else if (is_a<function>(e)) {
        ex e_mapped = e.map(*this);
        if (!is_a<function>(e_mapped)) return e_mapped;

        std::string fname = Functionmanager::get_hard_name(ex_to<function>(e).get_name());
        if (fname.empty())
          throw std::runtime_error("Internal error: Unknown hard-coded function " + ex_to<function>(e).get_name() + " in expression");

        const auto& fr = Functionmanager::hard_functions.find(fname)->second;
        const function& f = ex_to<function>(e_mapped);
        return dynallocate<func>(fname, exprseq(f.begin(), f.end()), fr.serial, fr.hard, fr.vars, fr.definition, fr.hints, fr.printname).setflag(status_flags::evaluated); // Avoid infinite recursion in func::eval()
      }
      return e.map(*this);
    }
  };

  replace_function_by_func replace_func;
  return replace_func(e);
}

Functionmanager::Functionmanager() {
  static bool initialized = false;

  if (!initialized) {
    // Variables used for function arguments. These are just placeholders and not registered with the compiler
    // It is not allowed to create a hard-coded function without arguments
    ex x  = dynallocate<symbol>("x");
    ex m1 = dynallocate<symbol>("m_1");
    ex m2 = dynallocate<symbol>("m_2");
    ex n1 = dynallocate<symbol>("n_1");
    ex n2 = dynallocate<symbol>("n_2");
    ex e1 = dynallocate<symbol>("e_1");
    ex e2 = dynallocate<symbol>("e_2");
    ex v1 = dynallocate<symbol>("v_1");
    ex v2 = dynallocate<symbol>("v_2");
    expression empty;

    hard_functions = {
      // Hard-coded functions that come with GiNaC
      {"abs",    {function::find_function("abs",   1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_NOBRACKET, "abs"}},
      {"arccos", {function::find_function("acos",  1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "arccos"}},
      {"arcosh", {function::find_function("acosh", 1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "arcosh"}},
      {"arcsin", {function::find_function("asin",  1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "arcsin"}},
      {"arsinh", {function::find_function("asinh", 1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "arsinh"}},
      {"arctan", {function::find_function("atan",  1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "arctan"}},
      {"artanh", {function::find_function("atanh", 1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "artanh"}},
      {"cos",    {function::find_function("cos",   1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "cos"}},
      {"cosh",   {function::find_function("cosh",  1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "cosh"}},
      {"exp",    {function::find_function("exp",   1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_NOBRACKET, "exp"}},
      {"fact",   {function::find_function("factorial", 1), {x}, true, empty, FUNCHINT_LIB,                "fact"}},
      {"ln",     {function::find_function("log",   1), {x}, true, empty, FUNCHINT_LIB,                    "ln"}},
      {"sin",    {function::find_function("sin",   1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "sin"}},
      {"sinh",   {function::find_function("sinh",  1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "sinh"}},
      {"tan",    {function::find_function("tan",   1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "tan"}},
      {"tanh",   {function::find_function("tanh",  1), {x}, true, empty, FUNCHINT_LIB|FUNCHINT_TRIG,      "tanh"}},
      {"re",     {function::find_function("real_part", 1), {x}, true, empty, FUNCHINT_LIB,                "Re"}},
      {"Re",     {function::find_function("real_part", 1), {x}, true, empty, FUNCHINT_LIB,                "Re"}},
      {"im",     {function::find_function("imag_part", 1), {x}, true, empty, FUNCHINT_LIB,                "Im"}},
      {"Im",     {function::find_function("imag_part", 1), {x}, true, empty, FUNCHINT_LIB,                "Im"}},
      {"conjugate",{function::find_function("conjugate",1),{x}, true, empty, FUNCHINT_LIB,                "conjugate"}},
      // Hard-coded functions that come with iMath
      {"ceil",     {function::find_function("ceil",     2), {e1, e2},     true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "ceil"}},
      {"concat",   {function::find_function("concat",   2), {v1, v2},     true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "concat"}},
      {"floor",    {function::find_function("floor",    2), {e1, e2},     true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "floor"}},
      {"hadamard", {function::find_function("hadamard", 3), {m1, m2, n1}, true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "hadamard"}},
      {"ifelse",   {function::find_function("ifelse",   3), {x,  e1, e2}, true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "ifelse"}},
      {"mindex",   {function::find_function("mindex",   3), {m1, e1, e2}, true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "mindex"}},
      {"round",    {function::find_function("round",    2), {e1, e2},     true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "round"}},
      {"scalprod", {function::find_function("scalprod", 2), {v1, v2},     true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "scalprod"}},
      {"sum",      {function::find_function("sum",      3), {e1, e2, x},  true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "sum"}},
      {"transpose",{function::find_function("transpose",1), {m1},         true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "transpose"}},
      {"vecprod",  {function::find_function("vecprod",  2), {v1, v2},     true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "vecprod"}},
      {"vmax",     {function::find_function("vmax",     1), {v1},         true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "vmax"}},
      {"vmin",     {function::find_function("vmin",     1), {v2},         true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "vmin"}},
      // Hard-coded matrix functions from GiNaC matrix.h
      {"charpoly",     {function::find_function("charpoly",     2), {m1, v1},             true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "charpoly"}},
      {"det",          {function::find_function("determinant",  1), {m1},                 true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "det"}},
      {"diag",         {function::find_function("diagmatrix",   1), {m1},                 true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "diag"}},
      {"ident",        {function::find_function("identmatrix",  2), {e1, e2},             true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "ident"}},
      {"invertmatrix", {function::find_function("invertmatrix", 1), {m1},                 true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "invertmatrix"}},
      {"matrixcols",   {function::find_function("matrixcols",   1), {m1},                 true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "matrixcols"}},
      {"matrixrows",   {function::find_function("matrixrows",   1), {m1},                 true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "matrixrows"}},
      {"ones",         {function::find_function("onesmatrix",   2), {e1, e2},             true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "ones"}},
      {"rank",         {function::find_function("rank",         1), {m1},                 true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "rank"}},
      {"reducematrix", {function::find_function("reducematrix", 3), {m1, e1, e2},         true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "reducematrix"}},
      {"solvematrix",  {function::find_function("solvematrix",  3), {m1, v1, e1},         true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "solvematrix"}},
      {"submatrix",    {function::find_function("submatrix",    5), {m1, e1, n1, e2, n2}, true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "submatrix"}},
      {"tr",           {function::find_function("trace",        1), {m1},                 true, empty, FUNCHINT_LIB|FUNCHINT_PRINT, "tr"}}
      // TODO: complete this list both from the list of functions starmath recognizes and the list of functions GiNaC has built-in
      // TODO: atan2, arg, binom/binomial, csgn, deg, dim, gcd, hom, inf, ker, lg, lim, liminf, limsup, max (see vmax), min (see vmin), Pr, sup, step
    };

    for (const auto& fr : hard_functions) {
      MSG_INFO(3, "Hard-coded function " << fr.first << "(" << function(fr.second.serial).get_name() << ") with serial " << fr.second.serial << endline);
      hard_names[function(fr.second.serial).get_name()] = fr.first;
    }
  }

  initialized = true;
}

const Functionmanager::funcrec& Functionmanager::findAttributes(const std::string& fname, const unsigned nargs) const {
  MSG_INFO(3, "Finding attributes of " << fname << endline);
  std::map<const std::string, funcrec>::const_iterator it_frec = hard_functions.find(fname);
  if (it_frec != hard_functions.end() && nargs == 0)
    throw std::invalid_argument("Function " + fname + " cannot have zero arguments");

  if (it_frec == hard_functions.end()) {
    it_frec = user_functions.find(fname);
    if (it_frec == user_functions.end())
      throw std::runtime_error("Function " + fname + " has not been registered!");
  }

  MSG_INFO(2, "Found registered " << (it_frec->second.hard ? "hard-coded " : "") << "function with serial " << it_frec->second.serial << endline);
  MSG_INFO(3, "Checking variables" << endline);
  if (nargs > 0 && (nargs != it_frec->second.vars.size()))
    throw std::invalid_argument("Number of arguments does not match for " + fname + ". Expected " + std::to_string(it_frec->second.vars.size()) + " arguments, found " + std::to_string(nargs) + " arguments");

  return it_frec->second;
}

expression Functionmanager::create(const std::string& n, const exprseq &args) const {
  MSG_INFO(2, "Creating function " << n << "(" << args << ")" << endline);
  const funcrec& fr = findAttributes(n, args.nops());
  return expression(dynallocate<func>(n, args, fr.serial, fr.hard, fr.vars, fr.definition, fr.hints, fr.printname));
}

expression Functionmanager::create(const std::string& n, exprseq &&args) const {
  MSG_INFO(2, "Move-creating function " << n << "(" << args << ")" << endline);
  const funcrec& fr = findAttributes(n, args.nops());
  return dynallocate<func>(n, std::move(args), fr.serial, fr.hard, fr.vars, fr.definition, fr.hints, fr.printname);
}

expression Functionmanager::create_hard(const std::string& n, const exprseq &args) {
  MSG_INFO(2, "Creating hard-coded function " << n << "(" << args << ")" << endline);
  auto it_frec = hard_functions.find(n);
  if (it_frec == hard_functions.end())
      throw std::runtime_error("Hard-coded function " + n + " does not exist");
  if (args.nops() == 0)
    throw std::invalid_argument("Function " + n + " cannot have zero arguments");

  const funcrec& fr = it_frec->second;
  if (args.nops() != fr.vars.size())
    throw std::invalid_argument("Number of arguments does not match for " + n + ". Expected " + std::to_string(fr.vars.size()) + " arguments, found " + std::to_string(args.nops()) + " arguments");

  return expression(dynallocate<func>(n, args, fr.serial, true, fr.vars, fr.definition, fr.hints, fr.printname).hold());
}

expression Functionmanager::create_hard(const std::string& n, exprseq &&args) {
  MSG_INFO(2, "Creating hard-coded function " << n << "(" << args << ")" << endline);
  auto it_frec = hard_functions.find(n);
  if (it_frec == hard_functions.end())
      throw std::runtime_error("Hard-coded function " + n + " does not exist");
  if (args.nops() == 0)
    throw std::invalid_argument("Function " + n + " cannot have zero arguments");

  const funcrec& fr = it_frec->second;
  return expression(dynallocate<func>(n, std::move(args), fr.serial, true, fr.vars, fr.definition, fr.hints, fr.printname).hold());
}

expression Functionmanager::find_integral(const std::string& name, const exprseq& seq) {
  if (name == "cos")
    return create_hard("sin", seq);
  else if (name == "sin")
    return _ex_1 * create_hard("cos", seq);
  else if (name == "tan")
    return _ex_1 * create_hard("ln", exprseq({create_hard("abs", exprseq({create_hard("cos", seq)}))}));
  else if (name == "cot")
    return create_hard("ln", exprseq({create_hard("abs", exprseq({create_hard("sin", seq)}))}));
  else if (name == "sec")
    return create_hard("ln", exprseq({create_hard("abs", exprseq({(1 + create_hard("sin", seq)) / create_hard("cos", seq)}))}));
  else if (name == "csc")
    return create_hard("ln", exprseq({create_hard("abs", exprseq({create_hard("sin", seq) / (1 + create_hard("cos", seq))}))}));
  else if (name == "arcsin")
    return seq[0] * create_hard("arcsin", seq) + pow(_ex1 - pow(seq[0], _ex2), _ex1_2);
  else if (name == "arccos")
    return seq[0] * create_hard("arccos", seq) - pow(_ex1 - pow(seq[0], _ex2), _ex1_2);
  else if (name == "arctan")
    return seq[0] * create_hard("arctan", seq) - _ex1_2 * create_hard("ln", exprseq({_ex1 + pow(seq[0], _ex2)}));
  // arccot, arcsec, arccsc: See func.cxx
  else if (name == "cosh")
    return create_hard("sinh", seq);
  else if (name == "sinh")
    return create_hard("cosh", seq);
  else if (name == "tanh")
    return create_hard("ln", exprseq({create_hard("cosh", seq)}));
  else if (name == "coth")
    return create_hard("ln", exprseq({create_hard("abs", exprseq({create_hard("sinh", seq)}))}));
  else if (name == "sech")
    return create_hard("arctan", exprseq({create_hard("sinh", seq)}));
  else if (name == "csch")
    return create_hard("ln", exprseq({create_hard("abs", exprseq({create_hard("tanh", exprseq({seq[0]/_ex2}))}))}));
  else if (name == "arsinh")
    return (seq[0] * create_hard("arsinh", seq) - pow(_ex1 + pow(seq[0], _ex2), _ex1_2));
  else if (name == "arcosh")
    return (seq[0] * create_hard("arcosh", seq) - pow(pow(seq[0], _ex2) - _ex1, _ex1_2));
  else if (name == "artanh")
    return (seq[0] * create_hard("artanh", seq) + _ex1_2 * create_hard("ln", exprseq({_ex1 - pow(seq[0], _ex2)})));
  // arcoth, arsech, arcsch: See func.cxx
  else if (name == "exp")
    return create_hard("exp", seq);
  else if (name == "log")
    return _ex1 / (create_hard("ln", exprseq({numeric(10)}))) * (seq[0] * create_hard("ln", seq) - seq[0]);
  else if (name == "ln")
    return (seq[0] * create_hard("ln", seq) - seq[0]);

  return expression();
}

void Functionmanager::remove(const std::string& fname) {
  user_functions.erase(fname);
  // Note: hard-coded functions may not be erased
}

void Functionmanager::clear() {
  MSG_INFO(2, "Clearing functions..." << endline);
  if (msg::info().checkprio(3)) {
    msg::info() << "List of functions" << endline;
    for (const auto& it_func : hard_functions)
      msg::info() << it_func.first << " [hard-coded]" << endline;
    for (const auto& it_func : user_functions)
      msg::info() << it_func.first << " = " << it_func.second.definition << endline;
  }

  auto it_func = user_functions.begin();
  while (it_func != user_functions.end()) {
    if (!(it_func->second.hints & FUNCHINT_LIB)) {
      MSG_INFO(3, "Deleting " << it_func->first << endline);
      it_func = user_functions.erase(it_func);
    } else
      it_func++;
  }
}

void Functionmanager::clearall() {
  MSG_INFO(2, "Clearing all user-defined functions..." << endline);
  user_functions.clear();
}

unsigned Functionmanager::hint(const std::string &s) {
  std::string str = s;

  if (s == "no_bracket")  {
    MSG_WARN(0, "Warning: Replace deprecated function hint 'no_bracket' with 'nobracket'" << endline);
    str = "nobracket";
  }

  if (funchint_map.find(str) == funchint_map.end()) {
    MSG_WARN(0, "Warning: The hint " << str << " is not defined." << endline);
    return(0);
  } else {
    return funchint_map.at(str);
  }
}

std::vector<std::string> Functionmanager::get_hard_names() {
  std::vector<std::string> result;
  for (const auto& fr : hard_functions)
    result.emplace_back(fr.first);
  return result;
}

void Functionmanager::registr(const std::string &n, const exvector &args, const unsigned h, const std::string& printname) {
  static const unsigned max_unsigned = unsigned(1 << (sizeof(unsigned) * 8 - 1)); // *** TODO: 1 << ...*8 gives warning

  MSG_INFO(3, "Registering function " << n << " '" << printname << "' with arguments " << args << endline);
  if (hard_functions.find(n) != hard_functions.end() || user_functions.find(n) != user_functions.end())
    throw (std::invalid_argument("Function " + n + " already exists"));

  for (const auto& i : args) {
    if ( !(is_a<symbol>(i) || (is_a< func>(i) && ex_to< func>(i).is_pure())) )
      throw (std::invalid_argument("Argument of function " + n + " is no symbol! "));
  }

  unsigned serial = max_unsigned - (unsigned)user_functions.size();
  auto f = user_functions.emplace(n, funcrec(serial, std::move(args), false, expression(), h, (printname == "" ? n : printname))).first->second;

  MSG_INFO(2, "Registered function " << f.printname << "(" << f.vars << ") with serial " << f.serial << ((h & FUNCHINT_LIB) ? ", which is a library function." : ".")  << endline);
}

void Functionmanager::define(const std::string &n, const expression &def) {
  auto it_func = user_functions.find(n);
  if (it_func == user_functions.end())
   throw (std::invalid_argument("Function " + n + " does not exist! Please register it first. Or it is a hard-coded function and cannot be defined"));

  it_func->second.definition = def;
  MSG_INFO(2, "Defined function " << n << "(" << it_func->second.vars << ")  = " << def << endline);
}

bool Functionmanager::is_a_func(const std::string &fname) const {
  MSG_INFO(3, "Checking if " << fname << " is a function" << endline);
  return (hard_functions.find(fname) != hard_functions.end() || user_functions.find(fname) != user_functions.end());
}

bool Functionmanager::is_hard_func(const std::string& fname) {
  MSG_INFO(3, "Checking if " << fname << " is a hard-coded function" << endline);
  return (hard_functions.find(fname) != hard_functions.end());
}

bool Functionmanager::is_lib(const std::string& fname) const {
  if (hard_functions.find(fname) != hard_functions.end()) return true;
  const auto& it_func = user_functions.find(fname);
  if (it_func != user_functions.end())
    return it_func->second.hints & FUNCHINT_LIB;
  return false;
}
