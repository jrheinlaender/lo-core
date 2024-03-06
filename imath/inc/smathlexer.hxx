/***************************************************************************
    smathlexer.hxx  -  Lexer definition for smathparser and smathlexer
                             -------------------
    begin                : Mon Jan 29 2024
    copyright            : (C) 2024 by Jan Rheinlaender
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

#ifndef SMATHLEXER_H
#define SMATHLEXER_H

#ifndef __FLEX_LEXER_H
#include "FlexLexer.h"
#endif
#include <istream>

// Prototype for the lexing function. This must match the declaration of lex_param at the top of smathparser.y
#undef  YY_DECL
#define YY_DECL \
        imath::smathparser::token_type imath::smathlexer::lex( \
            imath::smathparser::semantic_type* yylval, \
            imath::smathparser::location_type* yylloc, \
            std::shared_ptr<eqc> compiler, \
            unsigned include_level \
        )

// These includes are required for smathparser.hxx
#if defined INSIDE_SM
  #include <imath/option.hxx>
  #include <imath/printing.hxx>
  #include <imath/imathutils.hxx>
  #include <imath/unit.hxx>
  #include <imath/func.hxx>
  #include <imath/msgdriver.hxx>
  #include <imath/iFormulaLine.hxx>
  #include <imath/eqc.hxx>
#else
  #include "unit.hxx"
  #include "func.hxx"
  #include "msgdriver.hxx"
  #include "iFormulaLine.hxx"
  #include "eqc.hxx"
#endif
#include <smathparser.hxx>

namespace imath {
    class smathlexer : public yyFlexLexer {
    public:
        smathlexer() {}

        // Lexer input handling
        void scan_begin(std::istream& input);
        void scan_end();
        bool begin_include(const std::string &fname);
        bool finish_include();

        // Must match YY_DECL above
        virtual smathparser::token_type lex(smathparser::semantic_type* yylval, smathparser::location_type* yylloc, std::shared_ptr<eqc> compiler, unsigned include_level);
    };
}

#endif
