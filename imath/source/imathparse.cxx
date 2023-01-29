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

#ifdef INSIDE_SM
#include <imath/imathparse.hxx>
#else
#include "imathparse.hxx"
#endif
#include <smathparser.hxx>

namespace smathlexer {
    /// Parser/lexer handling routines
    void scan_begin(const std::string& input);
    void scan_end();
};


namespace imath
{
    int parse(parserParameters& params)
    {
        smathparser parser(params);
        smathlexer::scan_begin(STR(params.rawtext));
        int parse_result = parser.parse();
        smathlexer::scan_end();
        return parse_result;
    }
}
