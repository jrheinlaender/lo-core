/***************************************************************************
    begin                : Sun May 08 2016
    copyright            : (C) 2016 by Jan Rheinlaender
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
#include <imath/extsymbol.hxx>
#include <imath/msgdriver.hxx>
#else
#include "extsymbol.hxx"
#include "msgdriver.hxx"
#endif

namespace GiNaC {

GINAC_IMPLEMENT_REGISTERED_CLASS_OPT(extsymbol, symbol,
  print_func<print_context>(&extsymbol::do_print).
  print_func<imathprint>(&imathprint_symbol))

extsymbol::extsymbol() : symbol(), _domain(domain::complex), _return_type(return_types::commutative) {
  MSG_INFO(3, "Constructing empty extsymbol" << endline);
}

extsymbol::extsymbol(const std::string & initname) : symbol(initname), _domain(domain::complex), _return_type(return_types::commutative) {
  MSG_INFO(3, "Constructing extsymbol with " << initname << endline);
}

extsymbol::extsymbol(const std::string & initname, const std::string & texname) : symbol(initname, texname), _domain(domain::complex), _return_type(return_types::commutative) {
  MSG_INFO(3, "Constructing extsymbol with " << initname << " and textname " << texname << endline);
}

#ifdef DEBUG_CONSTR_DESTR
  extsymbol::extsymbol(const extsymbol& other) : symbol(other) {
    MSG_INFO(3, "Copying extsymbol from " << other.name << endline);
    _domain = other._domain;
    _return_type = other._return_type;
  }
  extsymbol& extsymbol::operator=(const extsymbol& other) {
    MSG_INFO(3, "Assigning extsymbol from " << other.name << endline);
    symbol::operator=(other);
    _domain = other._domain;
    _return_type = other._return_type;
    return *this;
  }
  extsymbol::~extsymbol() {
    MSG_INFO(3, "Destructing extsymbol " << name << endline);
  }
#endif

int extsymbol::compare_same_type(const basic & other) const
{
  return symbol::compare_same_type(other);
}

void extsymbol::do_print(const print_context & c, unsigned level) const
{
  symbol::do_print(c, level);
}

ex extsymbol::conjugate() const {
  if (_domain == domain::real || _domain == domain::positive)
    return *this;
  else
    return symbol::conjugate();
}

ex extsymbol::real_part() const {
  if (_domain == domain::real || _domain == domain::positive)
    return *this;
  else
    return symbol::real_part();
}

ex extsymbol::imag_part() const {
  if (_domain == domain::real || _domain == domain::positive)
    return 0;
  else
    return symbol::imag_part();
}

extsymbol_unarchiver::extsymbol_unarchiver() {}
extsymbol_unarchiver::~extsymbol_unarchiver() {}

}
