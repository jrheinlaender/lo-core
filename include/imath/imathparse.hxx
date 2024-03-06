/***************************************************************************
    imathparse.hxx  -  Parser definition for smathparser and smathlexer
                             -------------------
    begin                : Mon Jan 23 2023
    copyright            : (C) 2023 by Jan Rheinlaender
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

#ifndef IMATHPARSE_H
#define IMATHPARSE_H

#ifdef INSIDE_SM
  #include <imath/eqc.hxx>
  #include <imath/iFormulaLine.hxx>
#else
  #include <eqc.hxx>
  #include <iFormulaLine.hxx>
#endif


namespace imath {
    class smathlexer;

    // Front-end to the parser
    struct parserParameters {
    // Input
        /// Document access
        css::uno::Reference<css::uno::XComponentContext> xContext;
        css::uno::Reference<css::frame::XModel> xDocumentModel;

        /// the raw formula text from the UI
        OUString rawtext;

        /// Is copy+paste mode active in the parent document (important to avoid crashes during recalculation)
        bool copyPasteActive;

    // Input and output
        std::shared_ptr<smathlexer> lexer;
        /// The parsed formula text split into lines
        std::list<iFormulaLine_ptr>& lines;
        /// The compiler
        std::shared_ptr<eqc> compiler;
        /// The options
        std::shared_ptr<GiNaC::optionmap> global_options;

    // Output
        /// The compiled equations of the iFormula are cacheable (saving time on re-compilation)
        bool cacheable; // TODO: Caching is not implemented yet
        /// The results of the last compilation (for cacheable iFormulas only)
        // Note: This is a pointer because the actual data is stored in the iFormula object
        std::vector<std::pair<std::string, GiNaC::expression> >* cached_results;

        /// List of formulas for which an update should be inserted
        std::list<OUString> updateFormulas;

    // Constructor (required to initalize reference to lines)
        parserParameters(std::list<iFormulaLine_ptr>& l) : lines(l) {};
    };

    class IMATH_DLLPUBLIC imathparse {
    public:
        imathparse() {}
        int parse(parserParameters& params);
    };
}

#endif
