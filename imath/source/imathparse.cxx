/* **************************************************************************
                          imathparse.cxx  - Front-end to the smathparser
                             -------------------
    begin               : Tue Jan 24 17:00:00 CEST 2023
    copyright           : (C) 2023 by Jan Rheinlaender
    email               : jrheinlaender@users.sourceforge.net
 ***************************************************************************/

/* **************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify *
 *   it under the terms of the GNU General Public License as published by *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <sstream>
#ifdef INSIDE_SM
#include <imath/imathparse.hxx>
#else
#include "imathparse.hxx"
#endif
#include <smathlexer.hxx>

namespace imath
{
    int imathparse::parse(parserParameters& params)
    {
        std::istringstream input(STR(params.rawtext));
        params.lexer = std::make_shared<smathlexer>();
        params.lexer->scan_begin(input);
        smathparser parser(params);
        int parse_result = parser.parse();
        params.lexer->scan_end();
        return parse_result;
    }
}
