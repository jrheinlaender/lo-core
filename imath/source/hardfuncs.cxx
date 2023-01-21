/*************************************************************************
    hardfuncs.cxx  -  define hard-coded functions for eqc
                             -------------------
    begin                : Sun Oct 23 2022
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

#include <cmath>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning (disable: 4099)
#endif
#include <cln/cln.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef INSIDE_SM
#include <imath/expression.hxx>
#include <imath/func.hxx>
#include <imath/funcmgr.hxx>
#include <imath/msgdriver.hxx>
#include <imath/printing.hxx>
#include <imath/stringex.hxx>
#else
#include "expression.hxx"
#include "func.hxx"
#include "funcmgr.hxx"
#include "msgdriver.hxx"
#include "printing.hxx"
#include "stringex.hxx"
#endif
#include "hardfuncs.hxx"
#include "operands.hxx"

namespace GiNaC {

static ex round_eval(const ex &e, const ex &n) {
  if (!is_a<numeric>(expression(e).evalf()))
    return Functionmanager::create_hard("round", exprseq{e, n});
  numeric num = ex_to<numeric>(expression(e).evalf());
  if (!num.info(info_flags::real))
    throw std::runtime_error("Can only round real numbers");

  numeric digits;
  if (!is_a<numeric>(n)) {
    ex ndig = expression(n).evalf(); // Maybe n becomes a numeric now? Note: Doing evalf() right away converts integers to floats

    if (!is_a<numeric>(ndig)) {
      throw std::runtime_error("Number of digits to round to must be an integer");
    } else {
      digits = ex_to<numeric>(ndig);
    }
  } else {
    digits = ex_to<numeric>(n);
  }
  // Testing for nonnegative is OK, since we expect the original number (before evalf()) to have been an integer
  if (!digits.info(info_flags::nonnegative))
    throw std::runtime_error("Number of digits to round to must be a positive integer");
  // Note: Converting digits to a long limits the number of decimal places that can be rounded to 2^64
  cln::cl_F m = cln::the<cln::cl_F>(cln::expt(cln::cl_float(10.0, cln::default_float_format), cln::the<cln::cl_I>(digits.to_long())));
  return dynallocate<numeric>(truncate1(cln::the<cln::cl_R>(num.to_cl_N()) * m + csgn(num) * cln::cl_float(0.5, cln::default_float_format)) / m);
}

REGISTER_FUNCTION(round, eval_func(round_eval));

static ex floor_eval(const ex &e, const ex &n) {
  if (!is_a<numeric>(expression(e).evalf()))
    return Functionmanager::create_hard("floor", exprseq{e, n});
  numeric num = ex_to<numeric>(expression(e).evalf());
  if (!num.info(info_flags::real))
    throw std::runtime_error("Can only floor real numbers");

  numeric digits;
  if (!is_a<numeric>(n)) {
    ex ndig = expression(n).evalf(); // Maybe n becomes a numeric now? Note: Doing evalf() right away converts integers to floats

    if (!is_a<numeric>(ndig)) {
      throw std::runtime_error("Number of digits to floor to must be an integer");
    } else {
      digits = ex_to<numeric>(ndig);
    }
  } else {
    digits = ex_to<numeric>(n);
  }
  // Testing for nonnegative is OK, since we expect the original number (before evalf()) to have been an integer
  if (!digits.info(info_flags::nonnegative))
    throw std::runtime_error("Number of digits to floor to must be a positive integer");

  cln::cl_F m = cln::the<cln::cl_F>(cln::expt(cln::cl_float(10.0, cln::default_float_format), cln::the<cln::cl_I>(digits.to_long())));
  return dynallocate<numeric>(floor1(cln::the<cln::cl_R>(num.to_cl_N()) * m) / m);
}

REGISTER_FUNCTION(floor, eval_func(floor_eval));

static ex ceil_eval(const ex &e, const ex &n) {
  if (!is_a<numeric>(expression(e).evalf()))
    return Functionmanager::create_hard("ceil", exprseq{e, n});
  numeric num = ex_to<numeric>(expression(e).evalf());
  if (!num.info(info_flags::real))
    throw std::runtime_error("Can only ceil real numbers");

  numeric digits;
  if (!is_a<numeric>(n)) {
    ex ndig = expression(n).evalf(); // Maybe n becomes a numeric now? Note: Doing evalf() right away converts integers to floats

    if (!is_a<numeric>(ndig)) {
      throw std::runtime_error("Number of digits to ceil to must be an integer");
    } else {
      digits = ex_to<numeric>(ndig);
    }
  } else {
    digits = ex_to<numeric>(n);
  }

  // Testing for nonnegative is OK, since we expect the original number (before evalf()) to have been an integer
  if (!digits.info(info_flags::nonnegative))
    throw std::runtime_error("Number of digits to ceil to must be a positive integer");
  cln::cl_F m = cln::the<cln::cl_F>(cln::expt(cln::cl_float(10.0, cln::default_float_format), cln::the<cln::cl_I>(digits.to_long())));
  return dynallocate<numeric>(ceiling1(cln::the<cln::cl_R>(num.to_cl_N()) * m) / m);
}

REGISTER_FUNCTION(ceil, eval_func(ceil_eval));

static ex sum_eval(const ex &lower, const ex &higher, const ex &e) {
  MSG_INFO(3, "Doing hard eval of sum" << endline);
  if (!is_a<relational>(lower))
    throw std::runtime_error("Lower bound must be an equation");
  if (!is_a<symbol>((ex_to<relational>(lower)).lhs()))
    throw std::runtime_error("Lower bound must assign a value to a symbol");
  if (lower.is_equal(higher)) return e;
  // TODO: check if higher < lower
  if (e.is_zero()) return _ex0;
  return Functionmanager::create_hard("sum", exprseq{lower, higher, e});
}

static void sum_print_imath(const ex &lower, const ex &higher, const ex &e, const print_context& c) {
  c.s << "sum from {";
  lower.print(c);
  c.s << "} to {";
  higher.print(c);
  c.s << "} {";
  if (is_a<add>(e))
    c.s << "(";
  e.print(c);
  if (is_a<add>(e))
    c.s << ")";
  c.s << "}";
}

REGISTER_FUNCTION(sum, eval_func(sum_eval).
                       print_func<imathprint>(sum_print_imath));

static ex mindex_eval(const ex &e, const ex &r, const ex &c) {
  // Immediately evaluate if e is a matrix and r and c are integers
  MSG_INFO(3, "mindex eval: " << e << ", " << r << ", " << c << endline);
  ex unchanged = Functionmanager::create_hard("mindex", exprseq{e, r, c});

  if (is_a<matrix>(e)) {
    const matrix& m = ex_to<matrix>(e);
    int row = 0;
    int col = 0;

    if (is_a<numeric>(r)) {
      const numeric& rnum = ex_to<numeric>(r);

      if  (rnum.info(info_flags::posint)) {
        row = rnum.to_int();
      } else { // try harder, since non-integer indixes make no sense
        row = numeric_to_int(rnum);
      }
      row--; // Adjust index to count from 0
      if (row >= (int)m.rows() || row < 0) {
        MSG_WARN(0,  "mindex: Warning: Row index " << row+1 << " out of bounds for " << m << endline);
        return unchanged;
      }
    } else if (is_a<wildcard>(r)) {
      row = -1;
    } else {
      return unchanged;
    }

    MSG_INFO(2, "mindex row: " << row << ", rows(): " << m.rows() << endline);

    if (is_a<numeric>(c)) {
      const numeric& cnum = ex_to<numeric>(c);

      if (cnum.info(info_flags::posint)) {
        col = cnum.to_int();
      } else {
        col = numeric_to_int(cnum);
      }
      col--; // Adjust index to count from 0
      if (col >= (int)m.cols() || (col < 0 && col != -1000)) {
        MSG_WARN(0, "mindex: Warning: Column index " << col+1 << " out of bounds for " << m << endline);
        return unchanged;
      }
    } else if (is_a<wildcard>(c)) {
      col = -1;
    } else {
      return unchanged;
    }

    MSG_INFO(2, "mindex col: " << col << ", cols(): " << m.cols() << endline);

    // Handle special case of vector index where row and column are not distinguished
    if (col == -1000) {
      if (m.rows() == 1)
        return m(0, row);
      else
        return m(row, 0);
    }

    if (row == -1) {
      if (col == -1) {
        return e; // m[wild, wild] returns complete matrix
      } else {
        if (m.rows() == 1) {
          return m(0, col); // Special treatment for vector: Return one element only
        } else {
          matrix result(m.rows(), 1);
          for (unsigned i = 0; i < m.rows(); i++) result(i, 0) = m(i, col);
          return result;
        }
      }
    } else {
      if (col == -1) {
        if (m.cols() == 1) {
          return m(row, 0); // Special treatment for vector: Return one element only
        } else {
          matrix result(1, m.cols());
          for (unsigned i = 0; i < m.cols(); i++) result(0, i) = m(row, i);
          return result;
        }
      } else {
        return m(row, col);
      }
    }
  } else {
    return unchanged;
  }
}

static void mindex_print_imath(const ex &e, const ex &row, const ex &col, const print_context& c) {
  e.print(c);
  c.s << "[";
  if (is_a<wildcard>(row)) {
    c.s << "\"*\"";
  } else {
    row.print(c);
  }
  if (col == numeric(-999)) {
    // Leave away column index for vectors from smathparser.yxx
  } else {
    c.s << ",";
    if (is_a<wildcard>(col))
      c.s << "\"*\"";
    else
      col.print(c);
  }
  c.s << "]";
}

REGISTER_FUNCTION(mindex, eval_func(mindex_eval).print_func<imathprint>(mindex_print_imath));

static ex hadamard_eval(const ex &e1, const ex &e2, const ex& op) {
  if (!op.info(info_flags::real))
    throw std::invalid_argument("Hadamard-operation must be 0, 1 or 2");
  hadamard_operation h_op;
  if (is_equal_int(ex_to<numeric>(op), 0, Digits))
    h_op = h_product;
  else if (is_equal_int(ex_to<numeric>(op), 1, Digits))
    h_op = h_division;
  else if (is_equal_int(ex_to<numeric>(op), 2, Digits))
    h_op = h_power;
  else
    throw std::invalid_argument("Hadamard-operation must be 0, 1 or 2");

  // Immediately evaluate if both expressions are matrices with matching dimensions
  MSG_INFO(1, "hadamard eval: " << e1 << (h_op == h_product ? "*" : (h_op == h_division ? "/" : "^")) << e2 << endline);
  ex unchanged = Functionmanager::create_hard("hadamard", exprseq{e1, e2, op});

  ex e1_e = e1.evalm();
  ex e2_e = e2.evalm();

  if (is_a<matrix>(e1_e) && is_a<matrix>(e2_e)) {
    const matrix& m1 = ex_to<matrix>(e1_e);
    const matrix& m2 = ex_to<matrix>(e2_e);
    unsigned rows = m1.rows();
    unsigned cols = m1.cols();
    matrix result(rows, cols);

    if (rows == m2.rows() && cols == m2.cols()) {
      for (unsigned r = 0; r < rows; ++r) {
        for (unsigned c = 0; c < cols; ++c) {
          switch (h_op) {
            case h_product:  result(r, c) = m1(r, c) * m2(r, c); break;
            case h_division: {
              if (m2(r ,c).is_zero())
                result(r, c) = stringex("NaN");
              else
                result(r, c) = m1(r, c) / m2(r, c);
              break;
            }
            case h_power:    result(r, c) = pow(m1(r, c), m2(r, c)); break;
          }
        }
      }

      return result;
    }
  }

  return unchanged;
}

static void hadamard_print_imath(const ex &e1, const ex &e2, const ex& op, const print_context& c) {
  if (!op.info(info_flags::integer))
    throw std::invalid_argument("Hadamard-operation must be 0, 1 or 2");
  hadamard_operation h_op = (hadamard_operation)ex_to<numeric>(op).to_int();
  if (h_op < h_product || h_op > h_power)
    throw std::invalid_argument("Hadamard-operation must be 0, 1 or 2");

  if (is_a<add>(e1)) c.s << "(";
  e1.print(c);
  if (is_a<add>(e1)) c.s << ")";
  switch (h_op) {
    case h_product: c.s << " ⊗ "; break;
    case h_division: c.s << " ⊘ "; break;
    case h_power: c.s << " ⓔ "; break;
  }
  if (is_a<add>(e2)) c.s << "(";
  e2.print(c);
  if (is_a<add>(e2)) c.s << ")";
}

REGISTER_FUNCTION(hadamard, eval_func(hadamard_eval).print_func<imathprint>(hadamard_print_imath));

static ex transpose_eval(const ex &e) {
  // Immediately evaluate if e is a matrix
  MSG_INFO(3, "transpose: " << e << endline);

  if (is_a<matrix>(e)) {
    return ex_to<matrix>(e).transpose();
  } else {
    return Functionmanager::create_hard("transpose", exprseq{e});
  }
}

static void transpose_print_imath(const ex &e, const print_context& c) {
  e.print(c);
  c.s << "^T";
}

REGISTER_FUNCTION(transpose, eval_func(transpose_eval).
                             print_func<imathprint>(transpose_print_imath));

static ex vecprod_eval(const ex& e1, const ex& e2) {
  // Immediately evaluate if e is a matrix
  MSG_INFO(3, "vecprod: " << e1 << ", " << e2 << endline);

  if (is_a<matrix>(e1) && is_a<matrix>(e2)) {
    matrix v1 = ex_to<matrix>(e1);
    matrix v2 = ex_to<matrix>(e2);

    if (!( ((v1.cols() == 1) && (v2.cols() == 1) && (v1.rows() == v2.rows()) && (v1.rows() == 3)) ||
           ((v1.rows() == 1) && (v2.rows() == 1) && (v1.cols() == v2.cols()) && (v1.cols() == 3)) ))
      throw std::invalid_argument("Error: The vector product can only be calculated with two row or column vectors of three components each");

    if (v1.rows() < v1.cols()) v1 = v1.transpose();
    if (v2.rows() < v2.cols()) v2 = v2.transpose();
    matrix t(v1.rows(), 3);
    symbol e = symbol("e");

    for (unsigned i = 0; i < v1.rows(); i++) {
      t(i,0) = pow(e, i);
      t(i,1) = v1(i,0);
      t(i,2) = v2(i,0);
    }

    ex tdet = t.determinant();
    matrix result(v1.rows(), 1);

    for (unsigned i = 0; i < v1.rows(); i++)
      result(i,0) = tdet.coeff(e, i);

    return result;
  } else {
    return Functionmanager::create_hard("vecprod", exprseq{e1, e2});
  }
}

static void vecprod_print_imath(const ex &e1, const ex& e2, const print_context& c) {
  e1.print(c);
  c.s << " times ";
  e2.print(c);
}

REGISTER_FUNCTION(vecprod, eval_func(vecprod_eval).
                           print_func<imathprint>(vecprod_print_imath));

static ex scalprod_eval(const ex& e1, const ex& e2) {
  // Immediately evaluate if e is a matrix
  MSG_INFO(3, "scalprod: " << e1 << ", " << e2 << endline);
  ex unchanged = Functionmanager::create_hard("scalprod", exprseq{e1, e2});

  if (is_a<matrix>(e1) && is_a<matrix>(e2)) {
    const matrix& v1 = ex_to<matrix>(e1);
    const matrix& v2 = ex_to<matrix>(e2);

    if (!( (v1.rows() == 1) && (v2.cols() == 1) && (v1.rows() == v2.cols())))
      return unchanged; // or should we throw an exception?

    matrix result = ex_to<matrix>((e1 * e2).evalm());
    return result(0,0); // Return a numeric instead of a 1x1 matrix
  } else {
    return unchanged;
  }
}

static void scalprod_print_imath(const ex &e1, const ex& e2, const print_context& c) {
  e1.print(c);
  c.s << " ";
  e2.print(c);
}

REGISTER_FUNCTION(scalprod, eval_func(scalprod_eval).
                            print_func<imathprint>(scalprod_print_imath));

// Here false = 0, true = !false, and -1 means the condition does not evaluate to an integer
int eval_condition(const ex& condition) {
  if (is_a<relational>(condition)) {
    const relational& cond = ex_to<relational>(condition);

    if (is_a<stringex>(cond.lhs()) && is_a<stringex>(cond.rhs())) {
        std::string lhs = ex_to<stringex>(cond.lhs()).get_string();
        std::string rhs = ex_to<stringex>(cond.rhs()).get_string();

        if (cond.info(info_flags::relation_equal))
            return lhs.compare(rhs) == 0;
        else if (cond.info(info_flags::relation_not_equal))
            return lhs.compare(rhs) != 0;
        else if (cond.info(info_flags::relation_less))
            return lhs.compare(rhs) < 0;
        else if (cond.info(info_flags::relation_less_or_equal))
            return lhs.compare(rhs) <= 0;
        else if (cond.info(info_flags::relation_greater))
            return lhs.compare(rhs) > 0;
        else if (cond.info(info_flags::relation_greater_or_equal))
            return lhs.compare(rhs) >= 0;
    } else {
        ex df = cond.lhs() - cond.rhs();
        operands o1(GINAC_MUL), o2(GINAC_MUL);
        operands::split_ex(df, o1, o2);

        if (o1.is_quantity() && o2.is_quantity()) {
            ex coeff = o1.get_coefficient();

            if (cond.info(info_flags::relation_equal))
                return coeff.is_zero();
            else if (cond.info(info_flags::relation_not_equal))
                return !coeff.is_zero();
            else if (cond.info(info_flags::relation_less))
                return coeff.info(info_flags::negative);
            else if (cond.info(info_flags::relation_less_or_equal))
                return coeff.info(info_flags::negative) || coeff.is_zero();
            else if (cond.info(info_flags::relation_greater))
                return coeff.info(info_flags::positive);
            else if (cond.info(info_flags::relation_greater_or_equal))
                return coeff.info(info_flags::nonnegative);
        }
    }
  } else if (is_a<numeric>(condition)) {
    const numeric& c = ex_to<numeric>(condition);
    if (c.info(info_flags::positive))
      return 1;
    else if (c.info(info_flags::nonnegative))
      return 0;
  }

  return -1;
}

static ex ifelse_eval(const ex& condition, const ex& e1, const ex& e2) {
  // Immediately evaluate if condition involves numerics or quantities
  // Do not evaluate otherwise! Reason: If you use ifelse in a user-defined function
  // definition, then the condition will be evaluated already there, e.g.
  // max(x;y) = ifelse(x < y; y; x) will always return false...
  MSG_INFO(3, "eval ifelse: "  << condition << " ? " << e1 << " : " << e2 << endline);

  ex e_condition = condition.eval();
  int result = eval_condition(e_condition);
  if (result == -1)
    return Functionmanager::create_hard("ifelse", exprseq{e_condition, e1.eval(), e2.eval()});

  return result ? e1.eval() : e2.eval();
}

static ex ifelse_evalf(const ex& condition, const ex& e1, const ex& e2) {
  // We must re-implement evalf() because otherwise the condition will not be checked
  MSG_INFO(3, "evalf ifelse: "  << condition << " ? " << e1 << " : " << e2 << endline);

  ex e_condition = condition.evalf();
  int result = eval_condition(e_condition);
  if (result == -1)
    return Functionmanager::create_hard("ifelse", exprseq{e_condition, e1.evalf(), e2.evalf()});

  return result ? e1.evalf() : e2.evalf();
}

REGISTER_FUNCTION(ifelse, eval_func(ifelse_eval).evalf_func(ifelse_evalf));

static ex vmax_eval(const ex& v) {
  // Immediately evaluate if vector contains a smallest member
  MSG_INFO(3, "vmax: "  << v << endline);
  ex unchanged = Functionmanager::create_hard("vmax", exprseq{v});
  if (is_a<matrix>(v)) {
    const matrix& m = ex_to<matrix>(v);

    if (!is_a<numeric>(m.op(0)))
      return unchanged;

    numeric result = ex_to<numeric>(m.op(0));

    for (unsigned i = 1; i < m.nops(); i++) {
      if (!is_a<numeric>(m.op(i)))
        return unchanged;

      const numeric& n = ex_to<numeric>(m.op(i));
      if (n > result)
        result = n;
    }

    return result;
  } else {
    return unchanged;
  }
}

static void vmax_print_imath(const ex& v, const print_context& c) {
  c.s << "max left lbrace stack{";

  if (is_a<matrix>(v)) {
    const matrix& m = ex_to<matrix>(v);
    for (unsigned i = 0; i < m.nops()-1; i++) {
      m.op(i).print(c);
      c.s << " # ";
    }

    m.op(m.nops()-1).print(c);
  } else {
    v.print(c);
  }

  c.s << "} right none";
}

REGISTER_FUNCTION(vmax, eval_func(vmax_eval).
                        print_func<imathprint>(vmax_print_imath));

static ex vmin_eval(const ex& v) {
  // Immediately evaluate if vector contains a smallest member (works only for numerics)
  MSG_INFO(1, "vmin: "  << v << endline);
  ex unchanged = Functionmanager::create_hard("vmin", exprseq{v});

  if (is_a<matrix>(v)) {
    const matrix& m = ex_to<matrix>(v);

    if (!is_a<numeric>(m.op(0)))
      return unchanged;

    numeric result = ex_to<numeric>(m.op(0));

    for (unsigned i = 1; i < m.nops(); i++) {
      if (!is_a<numeric>(m.op(i)))
        return unchanged;

      const numeric& n = ex_to<numeric>(m.op(i));
      if (n < result)
        result = n;
    }

    return result;
  } else {
    return unchanged;
  }
}

static void vmin_print_imath(const ex& v, const print_context& c) {
  c.s << "min left lbrace stack{";

  if (is_a<matrix>(v)) {
    const matrix& m = ex_to<matrix>(v);
    for (unsigned i = 0; i < m.nops()-1; i++) {
      m.op(i).print(c);
      c.s << " # ";
    }

    m.op(m.nops()-1).print(c);
  } else {
    v.print(c);
  }

  c.s << "} right none";
}

REGISTER_FUNCTION(vmin, eval_func(vmin_eval).
                        print_func<imathprint>(vmin_print_imath));

static ex concat_eval(const ex& v1, const ex& v2) {
  // Immediately evaluate if expressions are matrices or lists
  MSG_INFO(3, "concat: "  << v1 << ", " << v2 << endline);
  ex unchanged = Functionmanager::create_hard("concat", exprseq{v1, v2});

  if (is_a<matrix>(v1)) {
    const matrix& m1 = ex_to<matrix>(v1);

    if (is_a<matrix>(v2)) {
      const matrix& m2 = ex_to<matrix>(v2);
      if (m1.cols() != m2.cols()) return unchanged;
      matrix m(m1.rows() + m2.rows(), m1.cols());
      for (unsigned r = 0; r < m.rows(); ++r)
        for (unsigned c = 0; c < m.cols(); ++c)
          m(r, c) = (r < m1.rows() ? m1(r, c) : m2(r - m1.rows(), c));
      return m;
    } else if (is_a<lst>(v2)) {
      const lst& l2 = ex_to<lst>(v2);
      if (l2.nops() != m1.cols()) return unchanged;
      matrix m(m1.rows() + 1, m1.cols());
      for (unsigned r = 0; r < m.rows(); ++r)
        for (unsigned c = 0; c < m.cols(); ++c)
          m(r, c) = (r < m1.rows() ? m1(r, c) : l2.op(c));
      return m;
    } else {
      return unchanged;
    }
  } else if (is_a<lst>(v1)) {
    const lst& l1 = ex_to<lst>(v1);

    if (is_a<matrix>(v2)) {
      const matrix& m2 = ex_to<matrix>(v2);
      if (l1.nops() != m2.cols()) return unchanged;
      matrix m(1 + (unsigned)m2.rows(), (unsigned)l1.nops());
      for (unsigned r = 0; r < m.rows(); ++r)
        for (unsigned c = 0; c < m.cols(); ++c)
          m(r, c) = (r == 0 ? l1.op(r) : m2(r - 1, c));
      return m;
    } else if (is_a<lst>(v2)) {
      const lst& l2 = ex_to<lst>(v2);
      lst result(l1);
      for (const auto& e : l2) result.append(e);
      return result;
    } else {
      return unchanged;
    }
  } else {
    return unchanged;
  }
}

REGISTER_FUNCTION(concat, eval_func(concat_eval));

static ex diagmatrix_eval(const ex &e) {
  // Immediately evaluate if e is a list or a vector
  MSG_INFO(3, "diag: " << e << endline);

  if (is_a<matrix>(e)) {
    const matrix& m = ex_to<matrix>(e);
    if (m.rows() == 1 || m.cols() == 1)
      return diag_matrix(make_lst_from_matrix(ex_to<matrix>(e), true));
  }

  return Functionmanager::create_hard("diag", exprseq{e});
}

static void diagmatrix_print_imath(const ex &e, const print_context& c) {
  c.s << "func diag( ";
  e.print(c);
  c.s << ")";
}

REGISTER_FUNCTION(diagmatrix, eval_func(diagmatrix_eval).
                              print_func<imathprint>(diagmatrix_print_imath));

static ex identmatrix_eval(const ex &r, const ex &c) {
  // Immediately evaluate if r and c reduce to positive integers
  MSG_INFO(3, "ident: " << r << ", " << c << endline);

  if (is_a<numeric>(r) && is_a<numeric>(c)) {
    const numeric& rows = ex_to<numeric>(r);
    const numeric& cols = ex_to<numeric>(c);
    if (rows.info(info_flags::posint) && cols.info(info_flags::posint))
      return unit_matrix(rows.to_int(), cols.to_int());
  }

  return Functionmanager::create_hard("ident", exprseq{r,c});
}

static void identmatrix_print_imath(const ex &r, const ex& col, const print_context& c) {
  c.s << "func ident( ";
  r.print(c);
  c.s << ", ";
  col.print(c);
  c.s << ")";
}

REGISTER_FUNCTION(identmatrix, eval_func(identmatrix_eval).
                               print_func<imathprint>(identmatrix_print_imath));

static ex onesmatrix_eval(const ex &r, const ex &c) {
  // Immediately evaluate if r and c reduce to positive integers
  MSG_INFO(3, "ones: " << r << ", " << c << endline);

  if (is_a<numeric>(r) && is_a<numeric>(c)) {
    const numeric& rows = ex_to<numeric>(r);
    const numeric& cols = ex_to<numeric>(c);
    if (rows.info(info_flags::posint) && cols.info(info_flags::posint)) {
      matrix result(rows.to_int(), cols.to_int());
      for (size_t rr = 0; rr < result.rows(); ++rr)
        for (size_t cc = 0; cc < result.cols(); ++cc)
          result(rr,cc) = _ex1;
      return result;
    }
  }

  return Functionmanager::create_hard("ones", exprseq{r,c});
}

static void onesmatrix_print_imath(const ex &r, const ex& col, const print_context& c) {
  c.s << "func ones( ";
  r.print(c);
  c.s << ", ";
  col.print(c);
  c.s << ")";
}

REGISTER_FUNCTION(onesmatrix, eval_func(onesmatrix_eval).
                              print_func<imathprint>(onesmatrix_print_imath));

static ex submatrix_eval(const ex &e, const ex &r, const ex &nr, const ex& c, const ex& nc) {
  // Immediately evaluate if e is a matrix and r, nr, c, nc reduce to positive integers
  MSG_INFO(3, "submatrix: " << e << ", " << r << ", " << nr << ", " << c << ", " << nc << endline);

  if (is_a<matrix>(e) && is_a<numeric>(r) && is_a<numeric>(nr) && is_a<numeric>(c) && is_a<numeric>(nc)) {
    const matrix& m = ex_to<matrix>(e);
    const numeric& row = ex_to<numeric>(r);
    const numeric& nrows = ex_to<numeric>(nr);
    const numeric& col = ex_to<numeric>(c);
    const numeric& ncols = ex_to<numeric>(nc);
    if (row.info(info_flags::posint) && nrows.info(info_flags::posint) && col.info(info_flags::posint) && ncols.info(info_flags::posint))
      return sub_matrix(m, row.to_int() - 1, nrows.to_int(), col.to_int() - 1, ncols.to_int());
  }

  return Functionmanager::create_hard("submatrix", exprseq{e,r,nr,c,nc});
}

REGISTER_FUNCTION(submatrix, eval_func(submatrix_eval));

static ex reducematrix_eval(const ex &e, const ex &r, const ex& c) {
  // Immediately evaluate if e is a matrix and r and c reduce to positive integers
  MSG_INFO(3, "reducematrix: " << e << ", " << r << ", " << c << endline);

  if (is_a<matrix>(e) && is_a<numeric>(r) && is_a<numeric>(c)) {
    const matrix& m = ex_to<matrix>(e);
    const numeric& rows = ex_to<numeric>(r);
    const numeric& cols = ex_to<numeric>(c);
    if (rows.info(info_flags::posint) && cols.info(info_flags::posint))
      return reduced_matrix(m, rows.to_int() - 1, cols.to_int() - 1);
  }

  return Functionmanager::create_hard("reducematrix", exprseq{e,r,c});
}

REGISTER_FUNCTION(reducematrix, eval_func(reducematrix_eval));

static ex determinant_eval(const ex &e) {
  // Immediately evaluate if e is a matrix
  MSG_INFO(3, "determinant: " << e << endline);

  if (is_a<matrix>(e))
    return ex_to<matrix>(e).determinant();

  return Functionmanager::create_hard("det", exprseq{e});
}

static void determinant_print_imath(const ex &e, const print_context& c) {
  c.s << "lline ";
  e.print(c);
  c.s << " rline";
}

REGISTER_FUNCTION(determinant, eval_func(determinant_eval).
                               print_func<imathprint>(determinant_print_imath));

static ex trace_eval(const ex &e) {
  // Immediately evaluate if e is a matrix
  MSG_INFO(3, "trace: " << e << endline);

  if (is_a<matrix>(e))
    return ex_to<matrix>(e).trace();

  return Functionmanager::create_hard("tr", exprseq{e});
}

static void trace_print_imath(const ex &e, const print_context& c) {
  c.s << "func tr(";
  e.print(c);
  c.s << ")";
}

REGISTER_FUNCTION(trace, eval_func(trace_eval).
                         print_func<imathprint>(trace_print_imath));

static ex charpoly_eval(const ex &e, const ex& var) {
  // Immediately evaluate if e is a matrix
  MSG_INFO(3, "charpoly: " << e << " in variable " << var << endline);

  if (is_a<matrix>(e))
    return ex_to<matrix>(e).charpoly(var);

  return Functionmanager::create_hard("charpoly", exprseq{e, var});
}

static void charpoly_print_imath(const ex &e, const ex& var, const print_context& c) {
  c.s << "func charpoly( ";
  e.print(c);
  c.s << ", ";
  var.print(c);
  c.s << ")";
}

REGISTER_FUNCTION(charpoly, eval_func(charpoly_eval).
                            print_func<imathprint>(charpoly_print_imath));

static ex rank_eval(const ex &e) {
  // Immediately evaluate if e is a matrix
  MSG_INFO(3, "rank: " << e << endline);

  if (is_a<matrix>(e))
    return ex_to<matrix>(e).rank();

  return Functionmanager::create_hard("rank", exprseq{e});
}

REGISTER_FUNCTION(rank, eval_func(rank_eval));

static ex solvematrix_eval(const ex &e, const ex& vars, const ex& rhs) {
  // Immediately evaluate if arguments are matching matrices
  MSG_INFO(3, "solvematrix: " << e << " for " << vars << " with right-hand side " << rhs << endline);

  if (is_a<matrix>(e) && is_a<matrix>(vars) && is_a<matrix>(rhs)) {
    const matrix& m = ex_to<matrix>(e); // Dimensions m x n
    const matrix& v = ex_to<matrix>(vars); // Dimensions n x p
    const matrix& r = ex_to<matrix>(rhs); // Dimensions m x p

    if (m.cols() == v.rows() && r.cols() == v.cols() && r.rows() == m.rows())
      return m.solve(v, r);
  }

  return Functionmanager::create_hard("solvematrix", exprseq{e,vars,rhs});
}

REGISTER_FUNCTION(solvematrix, eval_func(solvematrix_eval));

static ex invertmatrix_eval(const ex &e) {
  // Immediately evaluate if e is a matrix
  MSG_INFO(3, "invertmatrix: " << e << endline);

  if (is_a<matrix>(e))
    return ex_to<matrix>(e).inverse();

  return Functionmanager::create_hard("invertmatrix", exprseq{e});
}

static void invertmatrix_print_imath(const ex &e, const print_context& c) {
  c.s << "func inv(";
  e.print(c);
  c.s << ")";
}

REGISTER_FUNCTION(invertmatrix, eval_func(invertmatrix_eval).
                                print_func<imathprint>(invertmatrix_print_imath));

static ex matrixrows_eval(const ex &e) {
  // Immediately evaluate if e is a matrix
  MSG_INFO(3, "matrixrows: " << e << endline);

  if (is_a<matrix>(e))
    return ex_to<matrix>(e).rows();

  return Functionmanager::create_hard("matrixrows", exprseq{e});
}

static void matrixrows_print_imath(const ex &e, const print_context& c) {
  c.s << "func rows(";
  e.print(c);
  c.s << ")";
}

REGISTER_FUNCTION(matrixrows, eval_func(matrixrows_eval).
                              print_func<imathprint>(matrixrows_print_imath));

static ex matrixcols_eval(const ex &e) {
  // Immediately evaluate if e is a matrix
  MSG_INFO(3, "matrixcols: " << e << endline);

  if (is_a<matrix>(e))
    return ex_to<matrix>(e).cols();

  return Functionmanager::create_hard("matrixcols", exprseq{e});
}

static void matrixcols_print_imath(const ex &e, const print_context& c) {
  c.s << "func cols(";
  e.print(c);
  c.s << ")";
}

REGISTER_FUNCTION(matrixcols, eval_func(matrixcols_eval).
                              print_func<imathprint>(matrixcols_print_imath));

}
