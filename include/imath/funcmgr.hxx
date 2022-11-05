/***************************************************************************
  funcmgr.hxx  -  header file for class function manager
                             -------------------
    begin                : Fri Oct 21 2022
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

#ifndef FUNCMGR_H
#define FUNCMGR_H

/**
 * @author Jan Rheinlaender
 * @short Manage GiNaC functions for eqc
 */

#include <string>
#include "msgdriver.hxx"
#include "expression.hxx"

// Element-wise operations on vectors and matrices
enum hadamard_operation {
  h_product = 0,
  h_division = 1,
  h_power = 2
};

class IMATH_DLLPUBLIC Functionmanager {
public:
  /// Initialize the Functionmanager with the GiNaC and iMath hard-coded functions
  Functionmanager();

  /**
   * Register a user-defined function so that the scanner will be able to look it up.
   * @param n    The Latex name of the function which the scanner finds in the input file
   * @param args The arguments of the function. This defines number and name of the default arguments.
   *             The arguments are moved into the funcrec, therefore args will become an empty exvector
   * @param h A flag giving hints for printing and processing the function
   * @param printname An optional name to be printed, instead of the function name
   * @exception invalid_argument(Function already exists, argument is no symbol)
   **/
  void registr(const std::string &n, const GiNaC::exvector &args, const unsigned h = 0, const std::string& printname = "");

  /**
   * Define a user-defined function
   * @param n    The name of the function
   * @param def  An expression defining how to evaluate the function
   * @exception invalid_argument (Function does not exist)
   **/
  void define(const std::string &n, const GiNaC::expression &def);

  /// Return a new function object for the given name and arguments
  GiNaC::expression create(const std::string& n, const GiNaC::exprseq &args = GiNaC::exprseq()) const;
  GiNaC::expression create(const std::string& n, GiNaC::exprseq &&args) const;
  /// Return a new function object (hard-coded version)
  static GiNaC::expression create_hard(const std::string& n, const GiNaC::exprseq &args = GiNaC::exprseq());
  static GiNaC::expression create_hard(const std::string& n, GiNaC::exprseq &&args);

  /// Find an integral for a hard-coded function. Returns an empty expression if not possible
  // Note: It is assumed that the function has only one argument and that this argument is linear in the integration variable
  // The factor introduced by the linear argument must be handled by the caller
  static GiNaC::expression find_integral(const std::string& name, const GiNaC::exprseq& seq);

  /// Delete a function
  void remove(const std::string& fname);

  /// Return the function table to the state after ???
  void clear();

  /// Return the function table to the state after init()
  void clearall();

  /**
   * Check whether fname is a function
   * @param fname A string containing the name of the function
   * @returns True if the function exists
   **/
  bool is_a_func(const std::string &fname) const;

  /// Return true if this function name refers to a library function
  bool is_lib(const std::string& fname) const;

  /// Replace all GiNaC functions by EQC funcs
  static GiNaC::expression replace_function_by_func(const GiNaC::ex& e);

  /// Return the number assigned to this hint
  static unsigned hint(const std::string &s);

  /// Return a list of all hard-coded function names
  static std::vector<std::string> get_hard_names();

private:
  /// This structure stores all the information about a user defined function
  struct funcrec {
    /// The serial number of the function, if it is hard-coded into GiNaC
    unsigned serial;

    /// Function is hard-coded
    bool hard;

    /// dependant variables of the function (e.g. (x; y; z)
    GiNaC::exvector vars;

    /// The definition of the function (e.g. for cubic: x^3)
    GiNaC::expression definition;

    /// Contains hints for handling and printing of the function
    unsigned hints;

    /// Optional print name of the function
    std::string printname;

    funcrec(const unsigned s, const GiNaC::exvector& v, const bool hc, const GiNaC::expression& d, const unsigned h, const std::string& n) : serial(s), hard(hc), vars(v), definition(d), hints(h), printname(n) {
      MSG_INFO(3, "Constructing funcrec for serial " << s << endline);
    }

#ifdef DEBUG_CONSTR_DESTR
    funcrec() {
      MSG_INFO(3, "Constructing empty funcrec" << endline);
    }
    funcrec(const funcrec& other) : serial(other.serial), hard(other.hard), vars(other.vars), definition(other.definition), hints(other.hints) {
      MSG_INFO(3, "Copying funcrec from serial " << other.serial << endline);
    }
    funcrec& operator=(const funcrec& other) {
      MSG_INFO(3, "Assigning funcrec from serial " << other.serial << endline);
      serial = other.serial;
      hard = other.hard;
      vars = exvector(other.vars.begin(), other.vars.end());
      definition = other.definition;
      hints = other.hints;
      return *this;
    }
    ~funcrec() {
      MSG_INFO(3, "Destructing funcrec with serial " << serial << endline);
    }
#endif
  };

  /// Helper function for the constructors
  const funcrec& findAttributes(const std::string& fname, const unsigned nargs) const;
  /// Return the eqc name of the given GiNaC hard-coded function. If the function is not hard-coded return an empty string
  static std::string get_hard_name(const std::string& ginac_name);

  /**
   * Maps containing a list of function names and information about the functions
   **/
  static std::map<const std::string, funcrec> hard_functions; // Hard-coded functions that are the same for every instance of the Functionmanager
  std::map<const std::string, funcrec> user_functions; // User-defined functions

  /// A map containing the iMath names of the functions hard-coded in GiNaC
  static std::map<const std::string, std::string> hard_names;
};
#endif
