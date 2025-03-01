/***************************************************************************
    printing.cpp  -  Functions for pretty-printing expressions in iMath format
                             -------------------
    begin                : Sat Mar 2 2002
    copyright            : (C) 2025 by Jan Rheinlaender
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

#include "imath/utils.hxx"
#include <iomanip>
#include <sstream>
#include <cmath>
#include <cfloat>
#include <regex>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4099 4100 4996)
#endif
#include <ginac/mul.h>
#include <ginac/operators.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#ifdef INSIDE_SM
#include <imath/printing.hxx>
#include <imath/msgdriver.hxx>
#include <imath/equation.hxx>
#include <imath/stringex.hxx>
#include <imath/extintegral.hxx>
#include <imath/differential.hxx>
#include <imath/func.hxx>
#include <imath/unit.hxx>
#include <imath/eqc.hxx>
#else
#include "printing.hxx"
#include "msgdriver.hxx"
#include "equation.hxx"
#include "stringex.hxx"
#include "extintegral.hxx"
#include "differential.hxx"
#include "func.hxx"
#include "unit.hxx"
#include "eqc.hxx"
#endif
#include "operands.hxx"
#include "exderivative.hxx"

namespace GiNaC {

GINAC_IMPLEMENT_PRINT_CONTEXT(imathprint, print_dflt)

imathprint::imathprint() : print_dflt(std::cout) {
    ata = new bool(false);
}

imathprint::imathprint(std::ostream & os, optionmap* popt) : print_dflt(os), poptions(popt) {
    ata = new bool(false);
}

imathprint::imathprint(std::ostream & os, const imathprint& c) :
  print_dflt(os, c.options), poptions(c.poptions)
{
    ata = new bool(c.add_turn_around());
}

imathprint::~imathprint() {
    delete ata;
}

void imathprint::enter_fraction() const {
  unsigned fractionlevel = (*poptions)[o_fractionlevel].value.uinteger;

  if ((*poptions)[o_autofraction].value.boolean && (fractionlevel > 0)) {
    unsigned basefontheight = (*poptions)[o_basefontheight].value.uinteger;

    if ((basefontheight >= (*poptions)[o_minimumtextsize].value.uinteger + fractionlevel))
      s << "size-1{";
  }

  (*poptions)[o_fractionlevel] = fractionlevel + 1;
}

void imathprint::exit_fraction() const {
  (*poptions)[o_fractionlevel] = (*poptions)[o_fractionlevel].value.uinteger - 1;

  unsigned fractionlevel = (*poptions)[o_fractionlevel].value.uinteger;

  if ((*poptions)[o_autofraction].value.boolean && (fractionlevel > 0)) {
    unsigned basefontheight = (*poptions)[o_basefontheight].value.uinteger;

    if ((basefontheight >= (*poptions)[o_minimumtextsize].value.uinteger + fractionlevel))
      s << "}";
  }
}

std::string imathprint::decimalpoint = ".";

void imathprint::init() {
    // Initialize print functions
    set_print_func<add, imathprint>(&imathprint_add);
    set_print_func<constant, imathprint>(&imathprint_constant);
    set_print_func<exprseq, imathprint>(&imathprint_exprseq);
    set_print_func<function, imathprint>(&imathprint_function);
    set_print_func<matrix, imathprint>(&imathprint_matrix);
    set_print_func<mul, imathprint>(&imathprint_mul);
    set_print_func<ncmul, imathprint>(&imathprint_ncmul);
    set_print_func<numeric, imathprint>(&imathprint_numeric);
    set_print_func<power, imathprint>(&imathprint_power);
    set_print_func<relational, imathprint>(&imathprint_relational);
    set_print_func<symbol, imathprint>(&imathprint_symbol);
    set_print_func<wildcard, imathprint>(&imathprint_wildcard);
}

/***************************************************************************
    Functions for pretty-printing expressions in starmath
 ***************************************************************************/


std::string ex_get_name(const ex& e);
std::string differential_get_name(const differential& d);

std::string power_get_name(const power& p) {
  ex b = get_basis(p);
  if (is_a<symbol>(b)) {
    return std::string("012") + ex_to<symbol>(b).get_name();
  } else if (is_a<constant>(b)) {
    std::ostringstream os;
    os << "012" << ex_to<constant>(b);
    return os.str();
  } else if (is_a<exderivative>(b)) {
    return std::string("055") + differential_get_name(ex_to<exderivative>(b).get_numer());
  } else if (is_a<differential>(b)) {
    return std::string("060") + differential_get_name(ex_to<differential>(b));
  } else {
    return std::string("030") + ex_get_name(b);
  }
} // power_get_name()

std::string differential_get_name(const differential& d) {
  std::string result;

  // Print smaller grades first
  if (d.get_ngrade() >= 0) {
    // Numeric grade (assumed to be less than 999)
    std::ostringstream grade;
    grade << std::setfill('0') << std::setw(3) << d.get_ngrade();
    result = grade.str();
  } else {
    result = "999";
  }

  // Print partials second
  if (d.is_partial())
    result += "p";
  else
    result += "d";

  // Order alphabetically by argument, e.g. dx befor dy
  return result + ex_get_name(d.argument());
}

// Return a code for the expression type and a symbol name, if possible
std::string ex_get_name(const ex& e) {
  MSG_INFO(3, "Finding sort name for " << e << endline);
  if (is_a<numeric>(e))
    return std::string("005");
  else if (is_a<symbol>(e)) {
    std::string name = ex_to<symbol>(e).get_name();
    if (name[0] == '%')
      return std::string("013") + name; // greek letters etc. // TODO: Sort by greek alphabet...
    else
      return std::string("012") + name;
  } else if (is_a<constant>(e)) {
    std::ostringstream name;
    name << ex_to<constant>(e);
    if (name.str()[0] == '%')
      return std::string("011") + name.str(); // greek letters etc.
    else
      return std::string("010") + name.str();
  } else if (is_a<function>(e))
    return std::string("020") + ex_to<function>(e).get_name();
  else if (is_a<func>(e))
    return std::string("020") + ex_to<func>(e).get_name();
  else if (is_a<power>(e))
    return power_get_name(ex_to<power>(e));
  else if (is_a<mul>(e)) {
    if ((e.nops() == 2) && is_a<numeric>(e.op(1)) && (is_a<symbol>(e.op(0)) || is_a<constant>(e.op(0)))) {
      // treat this like a symbol
      return ex_get_name(e.op(0));
    } else {
      std::string largest("");
      for (const auto& i : e) {
        std::string current = ex_get_name(i);
        if (current > largest) largest = current;
      }
      return std::string("040") + largest;
    }
  } else if (is_a<extintegral>(e))
    return std::string("050");
  else if (is_a<exderivative>(e))
    return std::string("055") + differential_get_name(ex_to<exderivative>(e).get_numer());
  else if (is_a<differential>(e))
    return std::string("060") + differential_get_name(ex_to<differential>(e));

  return std::string("999");
} // ex_get_name()

bool ex_compare(const ex& l, const ex& r) {
  std::string lname = ex_get_name(l);
  std::string rname = ex_get_name(r);
  MSG_INFO(2, "Comparing " << lname << " with " << rname << endline);

  if ((lname == "") || (rname == ""))
    return (l < r);
  else
    return (lname < rname);
} // ex_is_less();

std::vector<ex> order_ex(const ex& e) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning (disable: 4996)
#endif
  std::vector<ex> result(e.begin(), e.end());
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  std::sort(result.begin(), result.end(), ex_compare);

  return result;
} // order_ex()

namespace {
exvector orderDerivatives(const ex& derivatives) {
    // Move partial derivatives to the front
    exvector result;

    if (is_a<exderivative>(derivatives))
        result.emplace_back(derivatives);
    else if (is_a<power>(derivatives)) {
        if (ex_to<exderivative>(get_basis(ex_to<power>(derivatives))).is_partial())
            result.emplace(result.begin(), derivatives);
        else
            result.emplace_back(derivatives);
    } else {
        for (const auto& e : derivatives) {
            if ((is_a<exderivative>(e) && ex_to<exderivative>(e).is_partial()) ||
                (is_a<power>(e) && ex_to<exderivative>(get_basis(ex_to<power>(e))).is_partial()))
                result.emplace(result.begin(), e);
            else
                result.emplace_back(e);
        }
    }

    return result;
}
void printSortedAdd(const ex& a, const imathprint& c, const char op) {
    MSG_INFO(1, "printSortedAdd() for " << a << " with operator " << op << endline);

    if (is_a<add>(a)) {
        std::vector<ex> sorted = order_ex(a);
        for (auto i = sorted.begin(); i != sorted.end(); ) {
            i->print(c, 1);
            ++i;
            if (i != sorted.end())
                c.s << " " << op << " ";
        }
    } else
        a.print(c, 1);
}

std::string printAddItem(const std::string& item, const operands& ops, const imathprint& c, const char op) {
    std::stringstream resultstream;
    imathprint result(resultstream, c);

    if (item == "n") {
        printSortedAdd(ops.get_coefficient(), result, op);
    } else if (item == "c") {
        printSortedAdd(ops.get_constants(), result, op);
    } else if (item == "x") {
        printSortedAdd(ops.get_symbols(), result, op);
    } else if (item == "u") {
        printSortedAdd(ops.get_units(), result, op);
    } else if (item == "e") {
        printSortedAdd(ops.get_powers(), result, op);
    } else if (item == "f") {
        printSortedAdd(ops.get_functions(), result, op);
    } else if (item == "i") {
        printSortedAdd(ops.get_integrals(), result, op);
    } else if (item == "d") {
        printSortedAdd(ops.get_differentials(), result, op);
    } else if (item == "r") {
        exvector deriv = orderDerivatives(ops.get_derivatives());

        for (auto r = deriv.begin(); r != deriv.end(); ) {
            r->print(result, 1);
            ++r;
            if (r != deriv.end())
                result.s << op;
        }
    } else if (item == "a") {
        result.s << " ADD? "; // This shouldn't happen
    } else if (item == "p") {
        const ex& p = ops.get_muls();

        if (op != '-') {
            printSortedAdd(p, result, op);
        } else if (is_a<mul>(p)) {
            // Only muls can contain other adds, which may be turned around to get rid of the negative sign
            printSortedAdd(_ex_1 * p, result, op);
            std::string resultstring = resultstream.str();
            if (resultstring[0] == '-')
                return resultstring.substr(1); // Remove - that was multiplied into the expression. Caller will supply leading minus sign
            else
                return "-" + resultstring; // Caller will detect double minus sign
        } else if (is_a<add>(p)) {
            // Print several muls
            for (auto a = p.begin(); a != p.end(); ++a) {
                std::stringstream innerresultstream;
                imathprint innerresult(innerresultstream, c);
                printSortedAdd(_ex_1 * *a, innerresult, op);
                std::string innerresultstring = innerresultstream.str();

                if (a == p.begin()) {
                    if (innerresultstring[0] == '-')
                        result.s << innerresultstring.substr(1); // Remove - that was multiplied into the expression. Caller will supply leading minus sign
                    else
                        result.s << "-" + innerresultstring; // Caller will detect leading double minus sign
                } else {
                    if (innerresultstring[0] == '-')
                        result.s << " " << innerresultstring; // Use - that was multiplied into the expression
                    else
                        result.s << " + " + innerresultstring; // Double minus sign
                }
            }
        } else
            printSortedAdd(p, result, op);
    } else if (item == "m") {
        printSortedAdd(ops.get_matrices(), result, op);
    } else if (item == "o") {
        printSortedAdd(ops.get_others(), result, op);
    }

    return resultstream.str();
}
ex collectSubmatch(const std::string& subMatch, const operands& ops) {
    ex result(_ex0);

    for (size_t m = 0; m < subMatch.size(); ++m) {
        char sm = static_cast<char>(std::tolower(subMatch[m]));

        if (sm == 'n')
            result += ops.get_coefficient();
        else if (sm == 'c')
            result += ops.get_constants();
        else if (sm == 'x')
            result += ops.get_symbols();
        else if (sm == 'u')
            result += ops.get_units();
        else if (sm == 'e')
            result += ops.get_powers();
        else if (sm == 'f')
            result += ops.get_functions();
        else if (sm == 'i')
            result += ops.get_integrals();
        else if (sm == 'd')
            result += ops.get_differentials();
        else if (sm == 'r')
            result += ops.get_derivatives();
        else if (sm == 'a')
            result += stringex(" ADD? "); // This shouldn't happen
        else if (sm == 'p')
            result += ops.get_muls();
        else if (sm == 'm')
            result += ops.get_matrices();
        else if (sm == 'o')
            result += ops.get_others();
    }

    return result;
}
// Syntax notes:
// The default operand order, as returned by operands::pattern(), is nucxeapfirdmo
// $x is a submatch, which will be printed in the matched order
// $*x is a submatch, but all elements will be skipped in the order given by sort_ex()
// Empty submatches will be silently skipped
static const std::vector<std::pair<std::regex, std::string>> addPrintFormats = {
    // Units by themselves always require a numeric
    {std::regex("(u|U)([^-]*)"),            "1 $0 $1"},
    {std::regex("-(u|U)(.*)"),              "- 1 $0 $1"},
    {std::regex("(u|U)([^-]*)-(u|U)(.*)"),  "1 $0 $1 - 1 $2 $3"},

    {std::regex("nx"),                      "x n"},

    // $*: treat single symbols as part of the products, to avoid things like c + 3a + 2b + 4d
    {std::regex("([nuc]*)(x[pP])([^-]*)"),                "$0 $*1 $2"},
    {std::regex("-(x[pP])(.*)"),                          "- $*0 $1"},
    {std::regex("([nuc]*)(x[pP])([^-]*)-(x?[pP]?)(.*)"),  "$0 $*1 $2 - $*3 $4"},

    // Catchall for everything that remains. Prints operands in the default order
    {std::regex("([^-]+)"),                 "$0"},
    {std::regex("-(.+)"),                   "- $0"},
    {std::regex("([^-]+)-(.+)"),            "$0 - $1"}
};
}

void imathprint_add(const add& a, const imathprint& c, unsigned level) {
  MSG_INFO(3, "imathprint_add " << a << endline);
  if (level > 0)
      c.s << "(";

  bool add_turn_around = false;
  if (c.add_turn_around()) {
      // Prevent propagation of this flag to lower levels where it would be erroneous
      add_turn_around = true;
      c.set_add_turn_around(false);
  }

  // Print polynomials nicely
  ex sym;
  if (check_polynomial(a, sym)) {
    MSG_INFO(3, a << " is a polynomial in " << sym << endline);
    bool first_coeff_printed = false;

    for (int i = a.degree(sym); i >= a.ldegree(sym); --i) {
      ex coeff = a.coeff(sym,i);
      MSG_INFO(3, "coeff of degree " << i << ": " << coeff << endline);

      if (!coeff.is_zero()) {
        bool changed_sign = false;
        if (is_negex(coeff)) {
          changed_sign = true;
          c.s << " -";
          coeff = -coeff;
        } else
          if (first_coeff_printed)
            c.s << " + ";

        if (i >= 0) {
          if (!coeff.is_equal(_ex1)) {
            if (is_a<add>(coeff) && ((i != 0) || changed_sign)) c.s << "(";
            coeff.print(c, level+1);
            if (is_a<add>(coeff) && ((i != 0) || changed_sign)) c.s << ")";
          } else if (i == 0) {
            coeff.print(c, level+1);
          }

          if (i > 0) {
            c.s << " ";
            imathprint_power(power(sym, i), c, level+1);
          }
        } else {
          (coeff * power(sym,i)).print(c, level+1);
        }

        first_coeff_printed = true;
      }
    }

    if (level > 0)
      c.s << ")";

    return;
  }

  operands posops(GINAC_ADD), negops(GINAC_ADD);
  operands::split_ex(a, posops, negops);

  // Extract a minus sign, if requested and possible
  if (add_turn_around && !negops.is_trivial()) {
      std::swap(posops, negops);
      MSG_INFO(1, "Turning around add" << endline);
      add_turn_around = false;
  }

  // TODO: It would be nice to print all the negops first onto separate strings, check for leading minus signs, and move them over into the posops

  // Print expression according to pattern
  std::string pattern = posops.pattern();
  if (!negops.is_trivial())
      pattern += "-" + negops.pattern();
  MSG_INFO(1, "Additive pattern '" << pattern << "'" << endline);

  for (const auto& [pat, format] : addPrintFormats) {
    std::smatch subMatches;
    if (!std::regex_match(pattern, subMatches, pat))
        continue;

    //c.s << " \"H: |" << pattern << "|\" " << std::endl;

    // Extract pattern into a vector (required for iterating with index)
    std::vector<std::string> itemVector;
    std::istringstream formatstream(format);
    std::string item;
    while (std::getline(formatstream, item, ' '))
        itemVector.emplace_back(item);

    bool is_positive(true);
    bool firstItem(true); // Note: Checking for i > 0 is not sufficient, since there might be empty submatches at the beginning of the pattern
    char sign('+');

    for (size_t i = 0; i < itemVector.size(); ++i) {
        item = itemVector[i]; // Need to iterate by index to be able to test for i > 0

        if (item == "+") {
            is_positive = true;
            sign = '+';
        } else if (item == "-") {
            is_positive = false;
            sign = '-';
        } else if (item.substr(0, 2) == "$*") {
            size_t subMatchIdx = std::stoi(item.substr(2));
            if (subMatches.size() > 1)
                ++subMatchIdx; // Index 0 is the whole pattern
            assert(subMatchIdx < subMatches.size());
            const std::string& subMatch = subMatches[subMatchIdx++].str();
            MSG_INFO(1, "Printing mixed submatch '" << subMatch << "'" << endline);
            if (!subMatch.empty()) {
                if (!firstItem || sign == '-')
                    c.s << " " << sign;
                // Note: No attempt to turn around adds here, since priority is on the ordering of the operands
                printSortedAdd(collectSubmatch(subMatch, is_positive ? posops : negops), c, sign);
                firstItem = false;
            }
        } else if (item[0] == '$') {
            // TODO If the last submatch is empty, then we will have a trailing + or - sign
            size_t subMatchIdx = std::stoi(item.substr(1));
            if (subMatches.size() > 1)
                ++subMatchIdx; // Index 0 is the whole pattern
            assert(subMatchIdx < subMatches.size());
            const std::string& subMatch = subMatches[subMatchIdx++].str();
            MSG_INFO(1, "Printing ordered submatch '" << subMatch << "'" << endline);
            for (size_t m = 0; m < subMatch.size(); ++m) {
                // Note: printAddItem tries to eliminate a negative sign by turning around one add inside a product
                auto result = printAddItem(std::string(1, static_cast<char>(std::tolower(subMatch[m]))), is_positive ? posops : negops, c, sign);

                if (result[0] == '-') { // Note: Result can only start with - if sign == '-'
                    if (!firstItem)
                        c.s << " +"; // Double minus becomes plus
                    result = result.substr(1);
                } else if (!firstItem || m > 0 || sign == '-') {
                    c.s << " " << sign;
                }

                c.s << " " << result;
                firstItem = false;
            }
        } else {
            auto result = printAddItem(item, is_positive ? posops : negops, c, sign);

            if (result[0] == '-') {
                if (!firstItem)
                    c.s << " +";
                result = result.substr(1);
            } else if (!firstItem || sign == '-')
                c.s << " " << sign;

            c.s << " " << result;
            firstItem = false;
        }
    }

    if (level > 0)
      c.s << ")";

    // Set flag so caller knows what happend
    c.set_add_turn_around(add_turn_around);

    return;
  }

  c.s << " \"NH: |" << pattern << "|\" " << std::endl;
} // imathprint_add()

void imathprint_constant(const constant& cn, const imathprint& c, unsigned level) {
  (void)level;
  std::ostringstream os;

  if (cn == Pi)
    os << "%pi";
  else if (cn == Euler_number)
    os << "func e";
  else
    os << latex << cn;

  c.s << os.str();
}

void imathprint_exprseq(const exprseq& es, const imathprint& c, unsigned level) {
  MSG_INFO(4, "Inside imathprint_exprseq()" << endline);
  for (func::const_iterator i = es.begin(); i != es.end(); i++) {
    i->print(c, level+1);
    if (i != es.end() - 1) c.s << ", ";
  }
}

void imathprint_function(const function& f, const imathprint& c, unsigned level) {
  MSG_INFO(4, "Inside imathprint_function()" << endline); // Important for printing hard-coded functions
  (void)level;
  c.s << f.get_name() << "(GiNaC-Function) ";

    c.s << "({";
    for (func::const_iterator i = f.begin(); i != f.end(); i++) {
      i->print(c, level+1);
      if (i != f.end() - 1) c.s << ", ";
    }
    c.s << "})";
} // imathprint_function()

void imathprint_matrix(const matrix& m, const imathprint& c, unsigned level) {
  (void)level;

  if (m.cols() == 1) { // stack{} is only useable for matrices with one single column
    c.s << "(alignc STACK{";
    for (unsigned r = 0; r < m.rows(); ) {
      m(r, 0).print(c, 1);
      if (++r != m.rows()) c.s << " # ";
    }
  } else {
    c.s << "(alignc MATRIX{";
    for (unsigned r = 0; r < m.rows(); ) {
      for (unsigned col = 0; col < m.cols(); ) {
        m(r, col).print(c, 1);
        if (++col != m.cols()) c.s << " # ";
      }
      if (++r != m.rows()) c.s << " ## ";
    }
  }
  c.s << "})";
}

namespace {
void printSortedMul(const ex& m, imathprint& c, unsigned level) {
    if (is_a<mul>(m)) {
        std::vector<ex> sorted = order_ex(m);
        for (auto i = sorted.begin(); i != sorted.end(); ) {
            i->print(c, 1); // Multiple adds must always be bracketed. Other types ignore the level
            ++i;
            if (i != sorted.end())
                c.s << " ";
        }
    } else {
        m.print(c, level);
    }
}

bool printMulItem(const std::string& item, const operands& ops, imathprint& c, unsigned level, bool& turn_around) {
    bool handled = true;

    if (item == "n") {
        printSortedMul(ops.get_coefficient(), c, level);
    } else if (item == "c") {
        printSortedMul(ops.get_constants(), c, level);
    } else if (item == "x") {
        printSortedMul(ops.get_symbols(), c, level);
    } else if (item == "u") {
        printSortedMul(ops.get_units(), c, level);
    } else if (item == "e") {
        // TODO turn-around would be possible for adds with integer exponents
        printSortedMul(ops.get_powers(), c, level);
    } else if (item == "f") {
        // TODO turn-around would be possible for some functions
        printSortedMul(ops.get_functions(), c, level);
    } else if (item == "i") {
        c.set_add_turn_around(turn_around);
        printSortedMul(ops.get_integrals(), c, level);
        turn_around = c.add_turn_around();
        c.set_add_turn_around(false);
    } else if (item == "d") {
        printSortedMul(ops.get_differentials(), c, level);
    } else if (item == "r") {
        exvector deriv = orderDerivatives(ops.get_derivatives());

        for (const auto& d : deriv) {
            d.print(c, 1);
            c.s << " ";
        }
    } else if (item == "a") {
        // Note: imathprint_add handles the turn_around but prevents propagation to higher levels
        c.set_add_turn_around(turn_around);
        printSortedMul(ops.get_adds(), c, level);
        turn_around = c.add_turn_around();
        c.set_add_turn_around(false);
    } else if (item == "p") {
        c.s << " MUL? "; // This shouldn't happen
    } else if (item == "m") {
        printSortedMul(ops.get_matrices(), c, level);
    } else if (item == "o") {
        printSortedMul(ops.get_others(), c, level);
    } else
        handled = false;

    return handled;
}

// Syntax notes:
// The default operand order, as returned by operands::pattern(), is nucxeapfirdmo
// Any 'a' appearing on the right-hand side will be bracketed automatically, put it inside a submatch to avoid that
// Empty submatches will print the number 1
// TODO treat user-defined functions separately?
static const std::vector<std::pair<std::regex, std::string>> mulPrintFormats = {
    // Units by themselves always require a numeric
    {std::regex("(u|U)"),                           "1 $0"},

    // Print units by themselves, in front of everything
    {std::regex("n([uU]?)/[uU]"),                   "n { frac{ alignc $0 } over { alignc u frac} }"},
    {std::regex("n([uU]?)([^/]+)/[uU]"),            "n { frac{ alignc $0 } over { alignc u frac} } $1"},
    {std::regex("n([uU]?)([^/]*)/[uU]([^/]+)"),     "n { frac{ alignc $0 } over { alignc u frac} } { frac{ alignc $1 } over { alignc $2 } }"},

    {std::regex("([^/]+)"),                         "$0"}, // Catchall for muls without fractions. Prints operands in the default order

    // Print differentials and derivatives separately
    {std::regex("([ef]?)([rRdD]+)/([ef])"),                       "{ frac{ alignc $0 } over { alignc $2 frac} } $1"},
    {std::regex("([ef]?)([rRdD]*)/([ef])([rRdD]+)"),              "{ frac{ alignc $0 } over { alignc $2 frac} } { frac{ alignc $1 } over { alignc $3 frac} }"},
    {std::regex("([nNcCxX]+)([ef]?)([rRdD]+)/([ef])"),            "$0 { frac{ alignc $1 } over { alignc $3 frac} } $2"},
    {std::regex("([nNcCxX]+)([ef]?)([rRdD]*)/([ef])([rRdD]+)"),   "$0 { frac{ alignc $1 } over { alignc $3 frac} } { frac{ alignc $2 } over { alignc $4 frac} }"},
    {std::regex("([nNcCxX]+)([ef]?)([rRdD]+)/([nNcCxX]+)([ef])"), "{ frac{ alignc $0 } over { alignc $3 frac} } { frac{ alignc $1 } over { alignc $4 frac} } $2"},
    {std::regex("([nNcCxX]+)([ef]?)([rRdD]*)/([nNcCxX]+)([ef])([rRdD]+)"),
        "{ frac{ alignc $0 } over { alignc $3 frac} } { frac{ alignc $1 } over { alignc $4 frac} } { frac{ alignc $2 } over { alignc $5 frac} }"},
    {std::regex("([^rRdD]*)([rRdD]*)/([rRdD]+)"),                 "$0 { frac{ alignc $1 } over { alignc $2 frac} }"},
    {std::regex("([^rRdD]*)([rRdD]*)/([^rRdD]*)([rRdD]+)"),       "{ frac{ alignc $0 } over { alignc $2 frac} } { frac{ alignc $1 } over { alignc $3 frac} }"},

    // Ensure proper bracketing of single adds
    {std::regex("([nNcCxX]+)a/([nNcCxX]+)"),              "{ frac{ alignc $0 } over { alignc $1 frac} } a"},
    {std::regex("([nNcCxX]+)([^/nNcCxX]+)/([nNcCxX]+)"),  "{ frac{ alignc $0 } over { alignc $2 frac} } $1"},
    {std::regex("a/([nNcXxX]+)"),                         "{ frac{ alignc 1 }  over { alignc $0 frac} } a"},
    {std::regex("([^/nNcCxX]+)/([nNcCxX]+)"),             "{ frac{ alignc 1 }  over { alignc $1 frac} } $0"},

    // Catchall for everything that remains
    {std::regex("/(.+)"),                           "{ frac{ alignc 1 }  over { alignc $0 frac} }"},
    {std::regex("([^/]+)/(.+)"),                    "{ frac{ alignc $0 } over { alignc $1 frac} }"}
};

}

void imathprint_mul(const mul& m, const imathprint& c, unsigned level) {
  MSG_INFO(1, "imathprint_mul() for " << m << endline);
  // Note: The level parameter is ignored and used for other purposes

  operands numer(GINAC_MUL), denom(GINAC_MUL), tempn(GINAC_MUL), tempd(GINAC_MUL), temp(GINAC_MUL);
  operands::split_ex(m, numer, denom);

  // Print expression according to pattern
  std::string pattern = numer.pattern();
  if (!denom.is_trivial())
      pattern += "/" + denom.pattern();
  MSG_INFO(1, "Multiplicative pattern '" << pattern << "'" << endline);

  // This avoids having duplicate mulPrintFormats entries for everything, differing just by the minus sign
  bool negative = false;
  if (pattern[0] == '-') {
      negative = true;
      pattern.erase(0, 1);
      numer.include(_ex_1);
  }

  for (const auto& [pat, format] : mulPrintFormats) {
    std::smatch subMatches;
    if (!std::regex_match(pattern, subMatches, pat))
        continue;

    //c.s << " \"H: |" << pattern << "|\" " << std::endl;

    // Extract pattern into a vector (required for lookahead functionality)
    std::vector<std::string> itemVector;
    std::istringstream formatstream(format);
    std::string item;
    while (std::getline(formatstream, item, ' '))
        itemVector.emplace_back(item);

    // GiNaC likes to pull out a minus sign from adds and put it in the coefficient
    // Print to intermediate stream, so that leading minus can be removed after "turning around" an add, if possible
    std::stringstream resultstream;
    imathprint result(resultstream, c);

    bool is_numer = true;

    for (size_t i = 0; i < itemVector.size(); ++i) {
        item = itemVector[i];

        if (item == "frac{") {
            result.enter_fraction();
            result.s << "{";
            is_numer = true;
        } else if (item == "over") {
            result.s << " over ";
            is_numer = false;
        } else if (item == "frac}") {
            result.s << "}";
            result.exit_fraction();
            is_numer = true;
        } else if (item[0] == '$') {
            size_t subMatchIdx = std::stoi(item.substr(1));
            if (subMatches.size() > 1)
                ++subMatchIdx; // Index 0 is the whole pattern
            assert(subMatchIdx < subMatches.size());

            const std::string& subMatch = subMatches[subMatchIdx++].str();
            MSG_INFO(1, "Printing submatch '" << subMatch << "'" << endline);
            if (subMatch.empty())
                result.s << "1"; // Empty matches by definition print 1

            for (size_t m = 0; m < subMatch.size(); ++m) {
                if (subMatch.size() != 1 && subMatch[m] == 'a')
                    printMulItem("a", is_numer ? numer : denom, result, 1, negative); // A single add with preceding or following other operands must be bracketed
                else
                    printMulItem(std::string(1, static_cast<char>(std::tolower(subMatch[m]))), is_numer ? numer : denom, result, 0, negative);
                result.s << " ";
            }
        } else {
            if (!printMulItem(item, is_numer ? numer : denom, result, item == "a" ? 1 : 0, negative))
                result.s << item; // Everything else
        }

        result.s << " ";
    }

    if (negative)
        c.s << "-"; // "turn around" was not successful
    c.s << resultstream.str();
    return;
  }

  c.s << " \"NH: |" << pattern << "|\" " << std::endl;
}

void print_ncmul_fraction(const expression& n, const expression& d, const imathprint& c, unsigned level) {
  MSG_INFO(2, "Printing ncmul fraction " << n << " / " << d << endline);
  c.enter_fraction();
  c.s << "{{alignc ";
  n.print(c, level+1);
  c.s << "} over {alignc ";
  d.print(c, level+1);
  c.s << "}}";
  c.exit_fraction();
}

void imathprint_ncmul(const ncmul& m, const imathprint& c, unsigned level) {
  MSG_INFO(2, "Printing ncmul " << m << endline);
  expression n = _expr1;
  expression d = _expr1;

  // Collect consecutive numerators and denominators and put them into a fraction
  for (const auto& f: m) {
    if (is_negpower(f)) {
      d = d / expression(f);
    } else {
      if (!d.is_equal(_ex1)) {
        // Print the fraction that was accumulated
        print_ncmul_fraction(n, d, c, level);
        n = f;
        d = _expr1;
      } else {
        n = n * expression(f);
      }
    }
  }

  if (!d.is_equal(_ex1)) {
    // Print the last fraction that was accumulated
    print_ncmul_fraction(n, d, c, level);
  } else if (!n.is_equal(_ex1)) {
    if (is_a<ncmul>(n)) {
      for (size_t i = 0; i < n.nops(); ++i) {
        n.op(i).print(c, level+1);
        if (i < n.nops() - 1) c.s << " ";
      }
    } else {
      n.print(c, level+1);
    }
  }
}

std::string roundNumber(const std::string& number, const int pos, bool& overflow) {
  MSG_INFO(4, "Rounding " << number << " to " << pos << endline);
  if (pos >= (int)number.size()) return number;
  if (pos <= 0) return "";

  numeric rnumber(number.substr(0, pos).c_str());
  if ((number.at(pos) - '0') >= 5)
    rnumber++;
  std::ostringstream str;
  str << rnumber;
  std::string result = str.str();

  if ((int)result.size() > pos) {
    // Rounding added a digit, e.g. 9995 rounded at position 3 results in 1000
    overflow = true;
    result.erase(result.size()-1);
  }

  return result;
}

void imathprint_real(const numeric& num, const imathprint& c) {
  unsigned precision = (*c.poptions)[o_precision].value.uinteger;
  bool fixeddigits = (*c.poptions)[o_fixeddigits].value.boolean;

  if (num.info(info_flags::rational)) { // integer or rational
    if (num.info(info_flags::integer)) {
      c.s << num;
      if (fixeddigits == false && precision > 0)
        c.s << "." << std::string(precision, '0'); // Add trailing zeros
    } else { // print rational as a fraction
      if (num < 0) c.s << "-";
      c.enter_fraction();
      c.s << "{{alignc ";
      imathprint_real(abs(num.numer()), c);
      c.s << "} over {alignc ";
      imathprint_real(num.denom(), c);
      c.s << "}}";
      c.exit_fraction();
    }
  } else { // print float
    std::ostringstream numstream;
    numstream << num; // Use standard printing routine of numeric
    std::string number = numstream.str();
    MSG_INFO(4, "Original number=" << number << endline);

    // Normalize the number to the form 0.<number> * 10^<exponent>
    bool negative = (number[0] == '-');
    if (negative) number.erase(0,1);
    if (number[0] == '0') number.erase(0,1); // Remove leading zero
    std::size_t epos = number.find("E"); // numeric.cpp: print_real_number() forces CLN exponent marker to 'E'
    int exponent;
    if (epos == std::string::npos) {
      exponent = 0;
    } else {
      exponent = std::stoi(number.substr(epos+1));
      number.erase(epos);
    }
    std::size_t ppos = number.find(".");
    if (ppos != std::string::npos) {
      number.erase(ppos, 1);
      exponent += (int)ppos;
    }
    std::size_t bpos = number.find_first_not_of('0');
    if (bpos == std::string::npos) { c.s << "0"; return; /* Should never happen */ }
    if (bpos > 0) {
      number.erase(0, bpos);
      exponent -= (int)bpos;
    }
    MSG_INFO(4, "Precision " << (*c.poptions)[o_precision].value.uinteger);
    MSG_INFO(4, ", Fixed digits " << ((*c.poptions)[o_fixeddigits].value.boolean ? "yes" : "no"));
    MSG_INFO(4, ", Forced exponent " << (*c.poptions)[o_exponent].value.integer);
    MSG_INFO(4, ", High limit " << (*c.poptions)[o_highsclimit].value.integer);
    MSG_INFO(4, ", Low limit " << (*c.poptions)[o_lowsclimit].value.integer << endline);


    if (fixeddigits && (precision == 0))
      throw std::runtime_error("It is not possible to print a number with zero significant digits (precision=0;fixedpoint=false)");

    // Place the decimal point. Note that move=0 is equivalent to an exponent of 1
    int move = 0; // Number of places to move the decimal point: + to the right (decreasing the exponent), - to the left (increasing the exponent)
    int fixedexponent = (*c.poptions)[o_exponent].value.integer;
    bool scientific = ((exponent > (*c.poptions)[o_highsclimit].value.integer) || (exponent <= -(*c.poptions)[o_lowsclimit].value.integer));
    if (fixedexponent != 0) {
      move = exponent - fixedexponent;
    } else if (scientific) {
      move = 1; // Scientific notation d.ddddd * 10^ddd
    } else {
      move = exponent; // Eliminate the need for an exponent;
    }
    MSG_INFO(4, "Moving point by " << move << endline);

    // Rounding
    bool overflow = false;
    if (fixeddigits)
      number = roundNumber(number, precision, overflow);
    else
      number = roundNumber(number, precision + move, overflow);
    if (overflow) {
      if (!scientific) move++;
      exponent++;
    }
    if (fixeddigits) number.erase(number.find_last_not_of('0') + 1); // Remove trailing zeros
    MSG_INFO(4, "Rounded number='" << number << "', move=" << move << ", exponent=" << exponent << endline);

    if (move < 0) {
       // Note that in fixed point notation this might result in things like 0.0000
      std::string zeros = std::string(-move, '0');

      if (!fixeddigits) {
        if ((number.size() == 0) && ((int)precision < -move))
          zeros = std::string(precision, '0'); // Avoid too many trailing zeros

        // Borderline case...
        if (precision > number.size() - move)
          number += std::string(precision - (number.size() - move), '0'); // Add some trailing zeros
      }

      number = "0" + imathprint::decimalpoint + zeros + number;
    } else if (move > (int)number.size()) {
      number = number + std::string(move - number.size(), '0');
      if (!fixeddigits) number = number + imathprint::decimalpoint + std::string(precision, '0'); // Add trailing zeros
    } else {
      if (!fixeddigits && (number.size() <= precision + move))
          number = number + std::string(precision + move - number.size(), '0');

      if (move == 0)
        number = "0" + imathprint::decimalpoint + number;
      else if (move != (int)number.size()) // Avoid trailing decimal point
        number.insert(move, imathprint::decimalpoint);
    }
    MSG_INFO(4, "Number after moving: " << number << endline);

    int remainingexponent = exponent - move;
    if (remainingexponent != 0)
      number = number + " cdot 10^" + std::to_string(remainingexponent);
    if (negative)
      number = "-" + number;
    MSG_INFO(4, "Final result: " << number << endline);

    c.s << number;
  }
} // imathprint_real()

void imathprint_numeric(const numeric& n, const imathprint& c, unsigned level) {
  MSG_INFO(4, "imathprint_numeric()" << endline);
  (void)level;
  numeric r = ex_to<numeric>(n.real_part());
  numeric i = ex_to<numeric>(n.imag_part());

  if (is_equal_int(i, 0, Digits)) { // case 1, real:  x  or  -x
    imathprint_real(r, c);
  } else {
    if (is_equal_int(r, 0, Digits)) { // case 2, imaginary:  y*I  or  -y*I
      if (is_equal_int(i, 1, Digits))
        c.s << " i ";
      else {
        if (is_equal_int(i, -1, Digits))
          c.s << " - i ";
        else {
          imathprint_real(i, c);
          c.s << " i ";
        }
      }
    } else { // case 3, complex:  x+y*I  or  x-y*I  or  -x+y*I  or  -x-y*I
      imathprint_real(r, c);
      if (i < 0) {
        if (is_equal_int(i, -1, Digits)) {
          c.s << " - i ";
        } else {
          imathprint_real(i, c);
          c.s << " i ";
        }
      } else {
        if (is_equal_int(i, 1, Digits)) {
          c.s << " + i ";
        } else {
          c.s << "+";
          imathprint_real(i, c);
          c.s << " i ";
        }
      }
    }
  }
}

void imathprint_power(const power& p, const imathprint& c, unsigned level) {
  MSG_INFO(1, "imathprint_power() for " << ex(p) << endline);
  // Is it correct to print x^(y^2) as x^y^2 or must we use brackets?
  (void)level;
  ex basis = get_basis(p);
  ex expon = get_exp(p);

  if (is_a<numeric>(expon) && (ex_to<numeric>(expon)).is_rational()) { // exponent is a rational number
    const numeric& exponent(ex_to<numeric>(expon));

    if (exponent.is_equal(*_num1_p)) {
      basis.print(c);
      return;
    } else if (exponent.is_negative()) { // exponent is negative
      c.enter_fraction();
      c.s << "{alignc 1 over {alignc ";
      pow(basis, -exponent).print(c);
      c.s << "}}";
      c.exit_fraction();
      return;
    } else if ((exponent.is_positive()) && (!exponent.is_integer())) { // exponent is a positive fraction
      if (exponent.denom().is_equal(2))
        c.s << "sqrt{";
      else {
        c.s << "nroot{";
        exponent.denom().print(c);
        c.s << "}{";
      }
      if (!exponent.numer().is_equal(*_num1_p)) {
        if (is_a<func>(basis) && ex_to<func>(basis).is_trig()) {
          ex_to<func>(basis).print_imath(c, exponent.numer());
          c.s << "}";
        } else {
          bool bracket = is_a<expairseq>(basis) || is_a<func>(basis) || is_a<power>(basis) || is_a<exderivative>(basis) ||
            (is_a<differential>(basis) &&
              (ex_to<differential>(basis).is_numerator() || !ex_to<differential>(basis).get_grade().is_equal(_ex1)));
          if (bracket) c.s << "(";
          basis.print(c);
          if (bracket) c.s << ")";
          c.s << "^{";
          exponent.numer().print(c);
          c.s << "}}";
        }
      } else {
        basis.print(c);
        c.s << "}";
      }
      return;
    }
  }
  // exponent is a positive integer, or is not rational, or is not even a numeric
  // Should we print things like x^(-a) as \frac(1)(x^a) ?
  if (is_a<func>(basis) && (ex_to<func>(basis).is_trig() || ex_to<func>(basis).is_pure())) {
    ex_to<func>(basis).print_imath(c, expon);
  } else {
    bool bracket = false;
    if (is_a<func>(basis))
        bracket = !ex_to<func>(basis).is_nobracket();
    else if (is_a<differential>(basis) && (ex_to<differential>(basis).is_numerator() || !ex_to<differential>(basis).get_grade().is_equal(_ex1)))
      bracket = true;
    else if (is_a<expairseq>(basis) || is_a<power>(basis) || is_a<exderivative>(basis) || (is_a<numeric>(basis) && basis.info(info_flags::negative)))
      bracket = true;

    if (bracket) c.s << "(";
    if (is_a<func>(basis)) c.s << "{"; // Required for abs otherwise starmath displays the power inside the function
    basis.print(c);
    if (is_a<func>(basis)) c.s << "}";
    if (bracket) c.s << ")";

    c.s << "^{";
    expon.print(c);
    c.s << "}";
  }
} // imathprint_power()

void imathprint_relational(const relational& r, const imathprint& c, unsigned level) {
  MSG_INFO(4, "imathprint_relational()" << endline);
  unsigned prec = r.precedence();
  if (level >= prec)
    c.s << '(';
  r.lhs().print(c, level+1);

  if (r.info(info_flags::relation_equal))
    c.s << " = "; // TODO: = or ==?
  else if (r.info(info_flags::relation_not_equal))
    c.s << " <> ";
  else if (r.info(info_flags::relation_less))
    c.s << " < ";
  else if (r.info(info_flags::relation_less_or_equal))
    c.s << " leslant ";
  else if (r.info(info_flags::relation_greater))
    c.s << " > ";
  else if (r.info(info_flags::relation_greater_or_equal))
    c.s << " geslant ";
  else
    c.s << " [unknown relational operator] ";

  r.rhs().print(c, level+1);
  if (level >= prec)
    c.s << ')';
}

void imathprint_symbol(const symbol& s, const imathprint& c, unsigned level) {
  MSG_INFO(4, "imathprint_symbol()" << endline);
  (void)level;
  std::string name = s.get_name();
  size_t pos = name.find_last_of("::");
  c.s << (pos == std::string::npos ? name : name.substr(pos+1));
}

void imathprint_wildcard(const wildcard& w, const imathprint& c, unsigned level) {
  MSG_INFO(4, "imathprint_wildcard()" << endline);
  (void)level;
  c.s << "$_" << w.get_label();
}

void imathprint_generic(const basic& b, const imathprint& c, unsigned level) {
  MSG_INFO(0, "!!! imathprint_generic() for '" << ex(b) << "'" << endline);
  (void)level;
  std::ostringstream temp;
  temp << latex << b;

  c.s << temp.str();
}

}
