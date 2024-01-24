/***************************************************************************
    smathparser.yy  -  rules for reading smath formulas
    - parser generation file for bison
                             -------------------
    begin                : Wed May 21 2008
    copyright            : (C) 2014 by Jan Rheinlaender
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

%skeleton "lalr1.cc"
%require "3.0.0"
%defines
%define api.prefix {imath}
%define api.parser.class {smathparser}%expect 1
%define parse.trace

%{
  #include <iostream>
  #include <stdio.h>
  #include <map>
  #include <vector>
  #include <stack>
  #include <sstream>
  #include <numeric>
  #include <com/sun/star/frame/XStorable.hpp>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4099 4100 4996)
#endif
  #include <ginac/normal.h>
  #include <ginac/operators.h>
  #include <ginac/pseries.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef INSIDE_SM
  #include <imath/imathparse.hxx>
  #include <imath/func.hxx>
  #include <imath/funcmgr.hxx>
  #include <imath/stringex.hxx>
  #include <imath/extintegral.hxx>
  #include <imath/differential.hxx>
  #include <officecfg/Office/iMath.hxx>
#else
  #include "imathparse.hxx"
  #include "func.hxx"
  #include "funcmgr.hxx"
  #include "stringex.hxx"
  #include "extintegral.hxx"
  #include "differential.hxx"
#endif

  namespace smathlexer {
    /// Parser/lexer handling routines
    bool begin_include(const std::string &fname);
    bool finish_include();
  };
%}

// The parsing context. It must match YY_DECL in imathparser.hxx
%parse-param { parserParameters& params }
%lex-param   { std::shared_ptr<eqc> compiler }
%lex-param   { unsigned include_level }

// // Enable location tracking
%locations
%initial-action {
  // Initialize the initial location.
  // Note that we do not track lines in the input, except when reading an include file. Otherwise, it is very difficult
  // to access the parts of rawtext which we need.
  rawtext = STR(params.rawtext);
  @$.begin.filename = @$.end.filename = new std::string(rawtext);
  // Initialize the line iterator (the list of lines is empty at this point)
  line = nullptr;
  // Give lexer access to the compiler
  compiler = params.compiler;
  // Set the current options to the global options from the iFormula
  current_options = params.global_options;
  line_options = nullptr;
  // Must the current line be auto-formatted?
  must_autoformat = false;
  // Are we inside an include file (or a nested include file)?
  include_level = 0;
  canonicalize_units = true;
  // Set trace mode. Might also require #define IMATHDEBUG 1 and yydebug_ = 1
  if (msg::info().checkprio(8))
    set_debug_level(1);
  // Get setting for automatic renumbering of duplicate equation labels
  autorenumberduplicate = false;
#ifdef INSIDE_SM
  autorenumberduplicate = officecfg::Office::iMath::Miscellaneous::O_Autorenumberduplicate::get();
#else
  Reference< XComponentContext> componentContext = params.xContext;
  Reference< XHierarchicalPropertySet > xProperties = getRegistryAccess(componentContext, OU("/de.gmx.rheinlaender.jan.imath.iMathOptionData/"));
  Any Aautorenumberduplicate = xProperties->getHierarchicalPropertyValue(OU("Miscellaneous/O_Autorenumberduplicate"));
  Aautorenumberduplicate >>= autorenumberduplicate;
#endif
};

// enable parser tracing, verbose error messages and lookahead correction for better syntax error handling
%debug
%define parse.error verbose
%define parse.lac full

//  Return type declarations ----------------------------------------------
%union {
  std::string *str; // For parsing strings
  GiNaC::expression  *expr; // For parsing expressions
  unsigned uint; // For parsing flag arguments
  option_name opt; // For parsing option name arguments
  std::pair<option_name, option> *optpair; // For parsing option values
  std::pair<GiNaC::exvector*, std::string> *upair; // For parsing unit lists
  GiNaC::optionmap *optmap; // For parsing option lists. Note: shared_ptr is not possible in a union
  bool boolean;
  GiNaC::exvector *exvect; // For parsing expression lists
  GiNaC::lst *list; // For parsing lists
  strvec *strlist; // For parsing simplification lists
  std::vector<std::shared_ptr<textItem>> *textlist; // For parsing user text
}

// The code between `%{' and `%}' after the introduction of the `%union'
// is output in the `*.cc' file; it needs detailed knowledge about the params.
%{
  // Declare the flex lexing function for the parser's sake
  YY_DECL;

  using namespace GiNaC;

  // The current line of the iFormula which we are working on
  iFormulaLine_ptr line;
  // Pointer to the compiler, because the lexer interface cannot handle params.compiler
  // and the auxiliary methods in this file have no access to it either
  std::shared_ptr<eqc> compiler;
  // The current options
  std::shared_ptr<optionmap> current_options;
  // Options that have been parsed for the current line, before the line is actually created
  optionmap* line_options;
  // Must the current line be auto-formatted?
  bool must_autoformat;
  // Are we inside an include file (or a nested include file)?
  int include_level;
  // The raw text we are scanning (different from params.rawtext because that is OUString)
  std::string rawtext;
  // Stack to keep track of locations for include files
  std::stack<imath::location> locationstack;
  // Whether to canonicalize units directly after parsing them
  bool canonicalize_units;
  // Whether duplicate equation labels are automatically renumbered
  bool autorenumberduplicate;
  // Error location and message
  imath::location errorlocation;
  OUString errormessage;

const ex calcvalue(const std::string& whatval, const ex& expr, const lst &assignments) {
  extsymbol var;
  if (!is_a<extsymbol>(expr)) {
    var = ex_to<extsymbol>(compiler->getsym(VALSYM));
    compiler->check_and_register(equation(var, expr, relational::equal, _expr0), VALLABEL);
  } else {
    var = ex_to<extsymbol>(expr);
  }

  ex value;
  try {
   // The substr is necessary to catch the "VALWITH" statements
   if (whatval.substr(0,3) == "VAL")  {
     value = compiler->find_value_of(var, assignments);
   } else if (whatval.substr(0,4) == "AVAL")  {
     value = compiler->find_value_of(var, assignments, false); // Do not force to float
   } else if (whatval.substr(0,8) == "QUANTITY")
     value = compiler->find_quantity_of(var, assignments);
   else if (whatval.substr(0,6) == "NUMVAL")
     value = expression(compiler->find_numval_of(var, assignments));
   else if (whatval.substr(0,4) == "UNIT")
     value = compiler->find_units_of(var, assignments);
  } catch (std::exception &e) {
		if (whatval.substr(0,3) == "VAL")
			value = stringex(std::string("error: ") + e.what());
		else
    // TODO: Create an option to choose between three different behaviours
    // throw syntax_error(e.what()); // Error message
			value = stringex("???"); // Display ??? as the value
    //value = var; // leave everything as it is
  }

  return value;
}

// Search value for each element separately
// It is assumed that expr really is a matrix
expression calcvalueofmatrix(const std::string& whatval, const ex& expr, const lst &assignments) {
  matrix result = ex_to<matrix>(expr);
  for (unsigned r = 0; r < result.rows(); ++r)
    for (unsigned c = 0; c < result.cols(); ++c)
      result(r,c) = calcvalue(whatval, result(r,c), assignments);

  return result;
}

bool checkbrackets(const std::string& left, const std::string& right) {
  MSG_INFO(3,  "checkbrackets()" << endline);
  std::string l = left.size()  > 4 ? left.substr(0,4)  : "";
  std::string r = right.size() > 5 ? right.substr(0,5) : "";
  //MSG_INFO(3, "LEFT: '" << l << "', RIGHT: '" << r << "'" << endline);
  std::string lb = left;
  std::string rb = right;

  if (l == "left") {
    if (!(r == "right")) return false;
    lb = left.substr(4);
    if (lb[0] == ' ') lb = lb.substr(1);
  }
  if (r == "right") {
    if (!(l == "left")) return false;
    rb = right.substr(5);
    if (rb[0] == ' ') rb = rb.substr(1);
  }
  //MSG_INFO(3, "BRACKET: '" << lb << "', '" << rb << "'" << endline);

  if ((lb == "none") || (rb == "none")) return true;

  if ((lb == "(")   && (rb != ")")) return false;
  if ((lb == "{")   && (rb != "}")) return false;
  if ((lb == "[")   && (rb != "]")) return false;
  if ((lb == "\\{") && (rb != "\\}")) return false;

  if ((lb[0] == 'l') && (rb[0] == 'r')) // e.g. lline rline
    if (! ((lb.size() > 1) && (rb.size() > 1) && (lb.substr(1) == rb.substr(1)))) return false;

  return true;
} // checkbrackets()

bool check_anyvector(const ex& e) {
  if (is_a<symbol>(e)) {
    const symbol& s = ex_to<symbol>(e);
    return compiler->getsymprop(s.get_name()) == p_vector;
  }

  if (!is_a<matrix>(e)) return false;

  const matrix& result = ex_to<matrix>(e);
  if ((result.cols() > 1) && (result.rows() > 1)) return false;

  return true;
}

lst make_lst_from_ex(const ex& e) {
  if (!is_a<matrix>(e)) {
    lst result;
    result.append(e);
    return result;
  } else {
    lst result;
    const matrix& m = ex_to<matrix>(e);
    for (unsigned r = 0; r < m.rows(); ++r)
      result.append(m(r,0));
    return result;
  }
}

bool hasLineOption(const option_name& o) {
  return ((line_options != nullptr) && (line_options->find(o) != line_options->end())) ||
         ((current_options != nullptr) && (current_options->find(o) != current_options->end()));
}

option getLineOption(const option_name& o) {
  if ((line_options != nullptr) && (line_options->find(o) != line_options->end()))
    return line_options->at(o);
  else
    return current_options->at(o);
}

bool check_label(const std::string& label, std::string& nslabel) {
  nslabel = compiler->label_ns(label);

  if (compiler->is_label(nslabel)) {
    //if (!autorenumberduplicate)
      return false;

    MSG_INFO(1, "Automatically correcting duplicate equation label " << nslabel << endline);
    nslabel += "_1";
  }

  return true;
}

GiNaC::unitvec unitConversions() {
  // Create a combined list with all global and local units. Local units have precedence, so they must come last
  // Note: create_conversions() only works properly if ALL units are processed in one go!
  MSG_INFO(2, "unitConversions()" << endline);
  auto it_option = current_options->find(o_units);
  lst units;
  if (it_option != current_options->end())
    for (const auto& u : *(it_option->second.value.exvec))
      units.append(u);

  if (msg::info().checkprio(3))
      for (const auto& u : units)
        msg::info() <<  "Unit GLOBAL: " << u << endline;

  if (line_options != nullptr) {
    it_option = line_options->find(o_units);
    if (it_option != line_options->end()) { // There are units local to this line
      for (const auto& o : *(it_option->second.value.exvec))
        units.append(o);

      if (msg::info().checkprio(3))
        for (const auto& u : *(it_option->second.value.exvec))
          msg::info() <<  "Unit LOCAL: " << u << endline;
    }
  }

  return compiler->create_conversions(units, true);
}

// Note: handle_error() assumes that global variables errormessage and errorlocation have been set properly
void handle_error(imath::parserParameters& params, const std::shared_ptr<iFormulaLine>& l, const imath::location& formulaStart) {
  if (include_level == 0) {
    params.lines->push_back(l);
    params.lines->back()->markError(params.rawtext.trim(), formulaStart.begin.column - 1, errorlocation.begin.column, errorlocation.end.column, errormessage);
  } else {
    errormessage += ": At line " + OUString::number(errorlocation.begin.line) + ", column " + OUString::number(errorlocation.begin.column);
    while (!locationstack.empty()) {
      std::string parseString = *locationstack.top().begin.filename; // This is %%ii READFILE {"<file name>"}
      errormessage = "Error in include file " + OUS8(parseString.substr(parseString.find("{\"") + 2, parseString.find("\"}") - parseString.find("{\"") - 2)) + "\n" + errormessage;
      locationstack.pop();
    }
    params.lines->back()->markError(params.rawtext.trim(), params.rawtext.indexOfAsciiL("{\"", 2), params.rawtext.indexOfAsciiL("{\"", 2) + 2, params.rawtext.indexOfAsciiL("\"}", 2), errormessage);
  }
}

void handle_label_error(const imath::location& labelStart,const std::string& label, imath::parserParameters& params, const std::shared_ptr<iFormulaLine>& l, const imath::location& formulaStart) {
  errorlocation = labelStart;
  errorlocation.begin.column += 5; // Ignore %%ii<space> and @ at start of label TODO Adjust in markError() if changed here!
  errorlocation.end.column = params.rawtext.indexOfAsciiL("@", 1, errorlocation.begin.column);
  errormessage = "Duplicate label: " + OUS8(label);
  handle_error(params, l, formulaStart);
}

#define GETARG(index) \
OUS8(rawtext.substr(index.begin.column-1, index.end.column-index.begin.column)).trim()

#define MAKE_ERROR(location,message) {\
errorlocation = location; \
errormessage = message; \
YYERROR; \
}
%}

// Token declarations -------------------------------------------------------

%token ENDSTRING               "end of input string"
%token NEWLINE                 "newline (in smath formula)"
// Basic tokens
%token <str>  BOPEN            "opening bracket"
%token <str>  BCLOSE           "closing bracket"
%token <str>  LEFT             "left bracket sizing"
%token <str>  RIGHT            "right bracket sizing"
%token <str>  DIGITS           "sequence of digits"
%token        DOUBLEHASH       "matrix row separator"
%token        TRANSPOSE        "transposition"
%token        NEQ              "not equal"
%token        EQUIV            "congruent"
%token        MOD              "modulo"
%token        BMOD             "bracketed modulo"
%token <boolean> BOOL          "boolean"
%token        NROOT            "multiple root"
%token        QUO              "quotient of division"
%token        REM              "remainder of division"
%token        GCD              "greatest common divisor"
%token        LCM              "lowest common multiple"
%token        AND              "logical and"
%token        OR               "logical or"
%token        NEG              "logical negation"
%token        FROM	           "from"
%token        TO               "to"
// Statements
%token	      READFILE	       "read file"
%token        OPTIONS          "set options"
%token        BEGIN_NS         "begin namespace"
%token        END_NS           "end namespace"
%token	      FUNCTION	       "function declaration"
%token        UNITDEF          "unit definition"
%token        PREFIXDEF        "prefix definition"
%token        VECTORDEF        "vector declaration/definition"
%token        MATRIXDEF        "matrix declaration/definition"
%token        REALVARDEF       "real-valued variable definition"
%token        POSVARDEF        "positive real-valued variable definition"
%token        CLEAREQUATIONS   "clear equations"
%token	      DELETE           "delete equations"
%token        UPDATE           "update previous value"
%token        END 0            "end of file" // The token numbered as 0 corresponds to end of file, this gives nicer error messages
// Expressions
%token        ATTRIBUTE        "attribute"
%token <str>  IDENTIFIER       "identifier"
%token        VEC              "arrow"
%token <str>  LABEL            "equation label"
%token <str>  EXLABEL          "expression label" // Must be separate from LABEL because of conflicts in eq: LABEL
%token <str>  STRING           "quoted string"
%token <str>  OPERATOR         "operator in string"
%token <str>  COMMENT          "comment"
%token        GENERATED        "generated line (ignored in recalculation)"
%token <str>  UNIT             "unit name"
%token <str>  FUNC             "function name"
%token        WILD             "wildcard"
%token        STACK            "vector definition"
%token        MATRIX           "matrix definition"
%token        LHS   	         "left-hand-side"
%token        RHS	             "right-hand-side"
%token <str>  VALUE            "variable value"
%token <str>  VALUEWITH	       "variable value WITH"
%token        ITERATE          "iterate equation"
%token <expr> SYMBOL           "variable name"
%token <expr> VSYMBOL          "vector name"
%token <expr> MSYMBOL          "matrix name"
%token        SUMFROM          "sum from"
%token        PRODUCT          "product"
%token        INTEGRAL         "integral"
%token <str>  DIFFERENTIAL     "differential"
%token        IMPMUL           "implicit multiplication"
%token <str>  SUPERSCRIPT      "unicode superscript"
%token        SUPERPLUS        "superscript addition"
%token        SUPERMINUS       "superscript subtraction"
%token        PLUSMINUS        "plus-minus"
%token        MINUSPLUS        "minus-plus"
%token        TIMES            "vector product/multiplication"
%token				HPRODUCT				 "hadamard product"
%token				HDIVISION				 "hadamard division"
%token				HPOWER					 "hadamard exponentiation"
%token	      EXDEF	           "expression definition"
%token <str>	PRINTVALUE  		 "print value"
%token <str>	PRINTVALUEWITH	 "print value WITH"
%token				EXPLAINVAL		 	 "explain value"
%token        EQDEF            "equation definition"
%token	      CONSTDEF	       "constant definition"
%token	      FUNCDEF	         "function definition"
%token	      CHART            "chart definition"
%token        ADDSERIES        "chart series definition"
%token        SETTABLECELL     "set value of a table cell"
%token        SETCALCCELLS     "set value of calc cell range"
%token        TEXT             "arbitrary user text"
%token				MAGIC						 "magic in user text"
%token	      SOLVE            "solve equation"
%token        SUBST            "substitute into equation"
%token        SUBSTC           "substitute consecutively into equation"
%token        SUBSTV           "substitute vector into equation"
%token        SUBSTVC          "substitute vector consecutively into  equation"
%token	      SIMPLIFY         "simplify expression"
%token        COLLECT          "format expression as polynomial"
%token        REVERSE          "reverse equation"
%token	      DIFFERENTIATE    "differentiate expression"
%token	      PDIFFERENTIATE   "partial differentiate expression"
%token	      INTEGRATE        "integrate expression"
%token	      TSERIES          "taylor series"
%token        TEXTFIELD        "scalar text field content"
%token        TABLECELL        "table cell content"
%token        CALCCELL         "scalar cell content from calc document"
%token        SIZE             "font size"
%type <uint>  funchints        "function hint list"
%type <uint>  hints            "function printing hints"
%token <opt>  OPT_L            "option name (unit list type)"
%token <opt>  OPT_U            "option name (unsigned integer type)"
%token <opt>  OPT_I            "option name (integer type)"
%token <opt>  OPT_B            "option name (boolean type)"
%token <opt>  OPT_S            "option name (string type)"
%type <boolean> asterisk       "keyword modifier asterisk"
%type <str>   left             "optional left bracket sizing"
%type <str>   right            "optional right bracket sizing"
%type <str>   leftbracket      "left bracket"
%type <str>   rightbracket     "right bracket"
%type <str>   sizestr          "font size specification"
%type <uint>  uinteger         "unsigned integer"
%type <expr>  number           "number"
%type <expr>  superscript      "superscript"
%type <expr>  symbol           "symbol (complex-valued)"
%type <expr>  rsymbol          "real-valued symbol"
%type <expr>  psymbol          "positive real-valued symbol"
%type <expr>  vsymbol          "symbol (vector)"
%type <expr>  msymbol          "symbol (matrix)"
%type <expr>  gsymbol          "general symbol"
%type <expr>  vector           "vector"
%type <list>  lvector          "vector (as list)"
%type <expr>  matrix           "matrix"
%type <list>  lmatrix          "matrix (as list)"
%type <expr>  eq               "equation"
%type <expr>  ex               "expression"
%type <expr>  condition        "logical condition"
%type <expr>  combinedcondition "combined logical condition"
%type <expr>  exponent         "exponent"
%type <expr>  lowerbound       "lower bound of sum or integral"
%type <expr>  upperbound       "upper bound of sum or integral"
%type <expr>  intvar	         "integration variable"
%type <exvect>exvec            "expression vector"
%type <expr>  exvec_or_ex			 "expression or expression vector"
%type <list>  eqlist           "equation list"
%type <strlist>simplifications "simplification list"
%type <strlist>labellist       "equation label list"
%type <textlist>usertext       "unquoted arbitrary text as list"
%type <optmap>options          "option list"
%type <optmap>keyvallist       "option key-value pair list"
%type <optpair>keyvalpair      "option key-value pair"
%type <upair>unitpair          "units key-value pair"

// Operator declarations ----------------------------------------------
// Note that "bison -W" warns about most of these being useless, but they are here anyway, for reference
%right EXDEF PRINTVALUE PRINTVALUEWITH
%left  DOUBLEHASH
%left ';' '#'
%precedence MOD BMOD
%left OR
%left AND
%precedence NEG
%precedence '=' '<' '>' NEQ EQUIV
%left ':'
%left '-' '+' PLUSMINUS MINUSPLUS
%left '*' '/' IMPMUL TIMES HPRODUCT HDIVISION
%precedence NEGATION     /* negation (unary minus)  */
%precedence SUPERPLUS SUPERMINUS
%precedence FUNC WILD LHS RHS REVERSE VALUE VALUEWITH ITERATE INTEGRAL NROOT QUO REM GCD LCM
%right DIFFERENTIAL
%right '^' HPOWER        /* exponentiation        */
%nonassoc SUPERSCRIPT
%precedence TRANSPOSE  /* matrix or vector transposition */
%precedence '!'        /* faculty */
%left '['         /* Matrix index */
%nonassoc SIZE        /* font size specification */

// Free memory on error recovery (when a symbol is discarded) ----------------
%printer { if ($$ == NULL) debug_stream() << "NULL"; else debug_stream() << *$$; } <str> <expr> <exvect> <list>
%printer { if ($$ == NULL) debug_stream() << "NULL"; else std::copy($$->begin(), $$->end(), std::ostream_iterator<std::string>(debug_stream(),"\n")); } <strlist>
%printer { debug_stream() << $$; } <uint> <opt> <boolean>
// This introduces a malloc() double free error!
//%destructor { if ($$ != NULL) free ($$); } <str> <expr> <exvect> <list> <strlist>
%%
%start input;
// Input management
input:   %empty
       | input options statement {
           if (line != nullptr)
            for (const auto& o : *$2)
              line->setOption(o.first, o.second); // Actually only option echo is relevant for statements
       } comment end
       | input options TEXT usertext { // User-defined text after %%ii {options} TEXT
          if (include_level == 0) {
            std::vector<OUString> formulaParts = {OUS8(rawtext.substr(@4.begin.column-1, @4.end.column-@4.begin.column))}; // not GETARG because it trims the string
            params.lines->push_back(std::make_shared<iFormulaNodeText>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), std::move(*$4)));
            line = params.lines->back();
            line_options = nullptr;
          }
          delete($2); delete($4);
       } comment end
       | input options TEXT error {
          std::vector<OUString> formulaParts = {};
          std::vector<std::shared_ptr<textItem>> textItems = {};
          handle_error(params, std::make_shared<iFormulaNodeText>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), std::move(textItems)), @4);
          delete($2);
          YYABORT;
        }
       | input usertext { // user-defined text on a line by itself. The end removes 3 shift/reduce conflicts
          if (include_level == 0) {
            std::vector<OUString> formulaParts = {GETARG(@2)};
            params.lines->push_back(std::make_shared<iFormulaNodeText>(unitConversions(), current_options, optionmap(), std::move(formulaParts), std::move(*$2)));
            line = params.lines->back();
            line_options = nullptr;
          }
          delete($2);
        } comment end
	     | input expr comment end { /* all the work is done in expr */ }
       | input GENERATED end { /* ignore auto-generated lines */ }
       | input COMMENT { // comment
          if (include_level == 0) { // Don't copy comments from include files!
            std::vector<OUString> formulaParts = {OUS8(*$2)};
            params.lines->push_back(std::make_shared<iFormulaNodeComment>(current_options, std::move(formulaParts)));
            line = params.lines->back();
            line_options = nullptr;
          }
          delete($2);
       } end
       | input READFILE '{' STRING '}' { // We can't use the normal 'end' rule here because it will be called before the include file is parsed
         // TODO: Parse string as an Openoffice URL so that include files are system-independent? But then relative URLS must be possible!
         if (include_level == 0) {
           std::vector<OUString> formulaParts = {OU("{"), GETARG(@4), OU("}")};
           params.lines->push_back(std::make_shared<iFormulaNodeStmReadfile>(current_options, std::move(formulaParts)));
           line = params.lines->back();
           line_options = nullptr;
         }
         if (include_level > 0)
          yyla.location.begin.filename = yyla.location.end.filename = new std::string("%%ii READFILE {\"" + *$4 + "\"}"); // The filename is only set once (for include_level == 0) in the initial-action
         ++include_level;
	       locationstack.push(yyla.location);
	       yyla.location = location();
         Reference<XStorable> xStorable(params.xDocumentModel, UNO_QUERY_THROW);
         OUString documentURL = xStorable->getLocation();
         Reference< XComponentContext> componentContext = params.xContext;
         OUString result = makeURLFor(OUS8(*$4), documentURL, componentContext);
         std::string fpath = STR(makeSystemPathFor(result, componentContext));

	       MSG_INFO(0,  "Trying to open " << fpath << endline);
         if (!smathlexer::begin_include(fpath)) {
           MSG_INFO(3,  "File " << fpath << " not found" << endline);
           errormessage = "Could not open include file " + OUS8(fpath);
           locationstack.pop(); // Remove entry that was just created, and handle the rest
            while (!locationstack.empty()) {
              std::string parseString = *locationstack.top().begin.filename; // This is %%ii READFILE {"<file name>"}
              errormessage = "Error in include file " + OUS8(parseString.substr(parseString.find("{\"") + 2, parseString.find("\"}") - parseString.find("{\"") - 2)) + "\n" + errormessage;
              locationstack.pop();
            }
            params.lines->back()->markError(params.rawtext.trim(), @3.begin.column - 1, params.rawtext.indexOfAsciiL("{\"", 2) + 2, params.rawtext.indexOfAsciiL("\"}", 2), errormessage);

           delete($4);
           YYABORT;
         }

	       params.cacheable = false;
	       delete ($4);
	     }
	     | input end { // Empty line
          if (include_level == 0) {
            params.lines->push_back(std::make_shared<iFormulaNodeEmptyLine>(current_options));
            line = params.lines->back();
            line_options = nullptr;
          }
       }
       | input error {
          handle_error(params, std::make_shared<iFormulaNodeError>(current_options, params.rawtext.trim()), @2);
          YYABORT;
       }
;

usertext: STRING  {
            $$ = new std::vector<std::shared_ptr<textItem>>;
            OUString str = OUS8(*$1).trim();
            if (str.getLength() > 0)
              $$->push_back(std::make_shared<textItem>(str));
            delete($1);
            yyla.value.str=NULL; /* Shouldn't delete($1) do that job? */
          }
          | OPERATOR {
						$$ = new std::vector<std::shared_ptr<textItem>>;
						$$->push_back(std::make_shared<textItem>(OUS8(*$1), true));
						delete($1);
					}
					| MAGIC ex MAGIC {
						$$ = new std::vector<std::shared_ptr<textItem>>;
						$$->push_back(std::make_shared<textItem>(*$2));
						delete($2);
					}
          | NEWLINE {
            $$ = new std::vector<std::shared_ptr<textItem>>;
            $$->push_back(std::make_shared<textItem>(OU("newline")));
          }
          | usertext STRING   {
            OUString str = OUS8(*$2).trim();
            if (str.getLength() > 0)
              $1->push_back(std::make_shared<textItem>(str));
            $$ = $1;
            delete($2);
            yyla.value.str = NULL;
          }
          | usertext OPERATOR { $1->push_back(std::make_shared<textItem>(OUS8(*$2), true)); $$ = $1; delete($2); }
					| usertext MAGIC ex MAGIC {
						$1->push_back(std::make_shared<textItem>(*$3));
						$$ = $1;
						delete($3);
					}
          | usertext NEWLINE  { $1->push_back(std::make_shared<textItem>(OU("newline"))); $$ = $1; }
;

end:        '\n' {
              // propagate the options and units to a new line
              if (line != nullptr)
                current_options = line->getGlobalOptions();
				line_options = nullptr;
							// Reset internal options to global value
							set_inhibit_floating_point_underflow(current_options->at(o_underflow).value.boolean);
							MSG_INFO(3, "Inhibit floating point underflow exception: " << (current_options->at(o_underflow).value.boolean ? "true" : "false") << endline);
							expression::evalf_real_roots_flag = (current_options->at(o_evalf_real_roots).value.boolean);
							MSG_INFO(3, "Evaluate odd negative roots to the positive real value: " << (current_options->at(o_evalf_real_roots).value.boolean ? "true" : "false") << endline);
            }
            | ENDSTRING {
              if (smathlexer::finish_include()) return(0);

              --include_level;
              yyla.location = locationstack.top();
              locationstack.pop();

              if (line != nullptr)
                current_options = line->getGlobalOptions();
				line_options = nullptr;
							// Reset internal options to global value
							set_inhibit_floating_point_underflow(current_options->at(o_underflow).value.boolean);
							MSG_INFO(3, "Inhibit floating point underflow exception: " << (current_options->at(o_underflow).value.boolean ? "true" : "false") << endline);
							expression::evalf_real_roots_flag = (current_options->at(o_evalf_real_roots).value.boolean);
							MSG_INFO(3, "Evaluate odd negative roots to the positive real value: " << (current_options->at(o_evalf_real_roots).value.boolean ? "true" : "false") << endline);
	            // continue parsing
            }
            | '\n' ENDSTRING { // One shift-reduce conflict (but writing end instead of \n will produce 8 conflicts!)
							if (smathlexer::finish_include()) return(0);

              --include_level;
              yyla.location = locationstack.top();
              locationstack.pop();

              if (line != nullptr)
                current_options = line->getGlobalOptions();
				line_options = nullptr;
							// Reset internal options to global value
							set_inhibit_floating_point_underflow(current_options->at(o_underflow).value.boolean);
							MSG_INFO(3, "Inhibit floating point underflow exception: " << (current_options->at(o_underflow).value.boolean ? "true" : "false") << endline);
							expression::evalf_real_roots_flag = (current_options->at(o_evalf_real_roots).value.boolean);
							MSG_INFO(3, "Evaluate odd negative roots to the positive real value: " << (current_options->at(o_evalf_real_roots).value.boolean ? "true" : "false") << endline);
	            // continue parsing
            }
;
comment: %empty
         | COMMENT
;

// Processing of statements
// Statements have no return value
statement: OPTIONS options {
           // Create a copy of the global options and update it with the new options
           // Note: Option values that are pointers are NOT copied when the new options are created. Replacing the pointer leaves the old value
           // untouched, which is what we want, since it is referenced by the old copy of current_options
           current_options = std::make_shared<optionmap>(current_options->begin(), current_options->end());

           for (const auto& o : *$2) {
             if ((o.first == o_units) && (o.second.value.exvec->size() > 0)) {
               // Units are not overwritten, but appended. Duplicate units can be removed because they have no effect
               exvector* units = new exvector(current_options->at(o.first).value.exvec->begin(), current_options->at(o.first).value.exvec->end());
               if (!units->empty() && is_a<stringex>(units->back()) && ex_to<stringex>(units->back()).get_string().empty()) units->pop_back();

               for (const auto& u_new : *o.second.value.exvec) {
                  // Erase duplicates
                  auto u_old = units->begin();
                  //if (is_a<Unit>(u_new)) u_new = ex_to<Unit>(u_new).get_canonical();

                  while (u_old != units->end()) {
                    //if (is_a<Unit>(*u_old)) *u_old = ex_to<Unit>(*u_old).get_canonical();
                    MSG_INFO(3, "Checking units for duplicate: " << *u_old << " and " << u_new << " = " << params.compiler->canonicalizeUnits(*u_old/u_new) << endline);
                    if (is_a<numeric>(params.compiler->canonicalizeUnits(*u_old/u_new)))
                      u_old = units->erase(u_old);
                    else
                      ++u_old;
                  }

                  units->push_back(u_new);
               }
               MSG_INFO(1, "Appended and cleaned unit list: " << *units << endline);
               current_options->at(o.first).value.exvec = units;
               delete(o.second.value.exvec);
             } else if ((o.first == o_unitstr) && (o.second.value.str->size() > 0)) {
               // Units (string representation) are not overwritten, but appended TODO This code is pretty much a duplicate of the code for o_units
               std::vector<std::string> units;
               std::istringstream old_units(*current_options->at(o.first).value.str);
               std::string unit;
               while (getline(old_units, unit, ';')) units.push_back(unit);
               if (!units.empty() && units.back() == "\"\"") units.pop_back();

               std::istringstream new_units(*o.second.value.str);
               while (getline(new_units, unit, ';')) {
                 std::string u_str = unit.substr(1); // Remove preceding % or "
                 if (u_str.back() == '\"') u_str.pop_back(); // Remove trailing "
                 auto u_old = units.begin();

                  while (u_old != units.end()) {
                    std::string u_old_str = u_old->substr(1);
                    if (u_old_str.back() == '\"') u_old_str.pop_back();
                    MSG_INFO(3, "Checking units for duplicate: " << u_old_str << " and " << u_str << " = " << params.compiler->getCanonicalizedUnit(u_old_str)/params.compiler->getCanonicalizedUnit(u_str) << endline);

                    if (is_a<numeric>(params.compiler->getCanonicalizedUnit(u_old_str)/params.compiler->getCanonicalizedUnit(u_str)))
                      u_old = units.erase(u_old);
                    else
                      ++u_old;
                  }

                  units.push_back(unit);
               }

               current_options->at(o.first).value.str = new std::string(std::accumulate(std::begin(units) + 1, std::end(units), *units.begin(), [](std::string &ss, std::string &s) { return ss + ";" + s;}));
               MSG_INFO(1, "Appended and cleaned unit string list: " << *current_options->at(o.first).value.str << endline);
               delete(o.second.value.str);
             } else {
               current_options->at(o.first) = o.second;
             }
           }

           if (include_level == 0) {
             std::vector<OUString> formulaParts = {GETARG(@2)};
             params.lines->push_back(std::make_shared<iFormulaNodeStmOptions>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }

           params.cacheable = false;
           delete($2);
         }
         | OPTIONS error {
            std::vector<OUString> formulaParts = {}; // TODO change function and pass by value with std::move on both sides
            handle_error(params, std::make_shared<iFormulaNodeStmOptions>(current_options, std::move(formulaParts)), @2);
            YYABORT;
         }
         | BEGIN_NS IDENTIFIER {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {GETARG(@2)};
             params.lines->push_back(std::make_shared<iFormulaNodeStmNamespace>(current_options, OU("BEGIN"), std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.compiler->begin_namespace(*$2);
           delete $2;
         }
         | BEGIN_NS error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmNamespace>(current_options, OU("BEGIN"), std::move(formulaParts)), @2);
            YYABORT;
         }
         | END_NS IDENTIFIER {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {GETARG(@2)};
             params.lines->push_back(std::make_shared<iFormulaNodeStmNamespace>(current_options, OU("END"), std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.compiler->end_namespace(*$2);
           delete $2;
         }
         | END_NS error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmNamespace>(current_options, OU("END"), std::move(formulaParts)), @2);
            YYABORT;
          }
         | FUNCTION '{' funchints ',' gsymbol ',' ex '}' {
           // Using IDENTIFIER does not work because the symbol might have been used in a library function previously, and even
           // CLEAREQUATIONS won't remove it then
           // Must register function first because the iFormulaNodeStmFunction needs it
           std::string fname = ex_to<symbol>(*$5).get_name();
           size_t pos = fname.find_last_of("::");
           std::string printname = (pos == std::string::npos ? fname : fname.substr(pos+1));
           params.compiler->register_function(fname, {*$7}, $3, printname);
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmFunction>(current_options, std::move(formulaParts), params.compiler->create_function(fname)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.cacheable = false;
           delete ($5); delete ($7);
         }
         | FUNCTION '{' funchints ',' gsymbol ',' exvec '}' {
           std::string fname = ex_to<symbol>(*$5).get_name();
           size_t pos = fname.find_last_of("::");
           std::string printname = (pos == std::string::npos ? fname : fname.substr(pos+1));
           params.compiler->register_function(fname, *$7, $3, printname);
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmFunction>(current_options, std::move(formulaParts), params.compiler->create_function(fname)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.cacheable = false;
           delete ($5); delete ($7);
         }
         | FUNCTION '{' funchints ',' STRING ',' gsymbol ',' ex '}' {
           std::string fname = ex_to<symbol>(*$7).get_name();
           params.compiler->register_function(fname, {*$9}, $3, *$5);
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU(","), GETARG(@9), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmFunction>(current_options, std::move(formulaParts), params.compiler->create_function(fname)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.cacheable = false;
           delete ($5); delete ($7); delete ($9);
         }
         | FUNCTION '{' funchints ',' STRING ',' gsymbol ',' exvec '}' {
           std::string fname = ex_to<symbol>(*$7).get_name();
           params.compiler->register_function(fname, *$9, $3, *$5);
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU(","), GETARG(@9), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmFunction>(current_options, std::move(formulaParts), params.compiler->create_function(fname)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.cacheable = false;
           delete ($5); delete ($7); delete ($9);
         }
         | FUNCTION error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmFunction>(current_options, std::move(formulaParts), _ex0), @2);
            YYABORT;
          }
         | UNITDEF '{' STRING ',' IDENTIFIER '=' ex '}' {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU("="), GETARG(@7), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmUnitdef>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }
           std::string name = *$5;
           if (name[0] == '%')
             name.erase(0,1);
           else
             MAKE_ERROR(@5, "Unit name should start with '%'");
           params.compiler->addUnit(name, *$3, *$7);
           params.cacheable = false;
           delete($3); delete ($5); delete($7);
         }
         | UNITDEF '{' STRING ',' STRING '=' ex '}' {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU("="), GETARG(@7), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmUnitdef>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }
           std::string name = *$5;
           if (name[0] == '%')
             MAKE_ERROR(@5, "Quoted unit name should not start with '%'");
           params.compiler->addUnit(name, *$3, *$7);
           params.cacheable = false;
           delete($3); delete ($5); delete($7);
         }
         | UNITDEF '{' STRING ',' UNIT '=' ex '}' {
           std::string name = *$5;
           delete($3); delete($5);
           MAKE_ERROR(@5, "Unit '" + OUS8(name) + "' already exists. Please choose a different name");
         }
         | UNITDEF error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmUnitdef>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
         | PREFIXDEF '{' IDENTIFIER '=' ex '}' {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU("="), GETARG(@5), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmPrefixdef>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }

           std::string name = *$3;
           if (name[0] == '%')
             name.erase(0,1);
           else
             MAKE_ERROR(@3, "Prefix name should start with '%'");

           if (!is_a<numeric>(*$5))
             MAKE_ERROR(@3, "Prefix must have a numeric value");

           params.compiler->addPrefix(name, ex_to<numeric>(*$5));
           params.cacheable = false;
           delete($3); delete ($5);
         }
         | PREFIXDEF '{' UNIT '=' ex '}' {
           // This rule exists to handle the SI short prefixes T(era) and m(illi) which correspond to units T(esla) and m(etre)
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU("="), GETARG(@5), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmPrefixdef>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }

           if (!is_a<numeric>(*$5))
             MAKE_ERROR(@5, "Prefix must have a numeric value");

           params.compiler->addPrefix(*$3, ex_to<numeric>(*$5));
           params.cacheable = false;
           delete($3); delete ($5);
         }
         | PREFIXDEF error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmPrefixdef>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
         | VECTORDEF vsymbol {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {GETARG(@2)};
             params.lines->push_back(std::make_shared<iFormulaNodeStmVectordef>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.cacheable = false;
           delete($2);
         }
         | VECTORDEF error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmVectordef>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
         | MATRIXDEF msymbol {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {GETARG(@2)};
             params.lines->push_back(std::make_shared<iFormulaNodeStmMatrixdef>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.cacheable = false;
           delete($2);
         }
         | MATRIXDEF error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmMatrixdef>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
         | REALVARDEF rsymbol {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {GETARG(@2)};
             params.lines->push_back(std::make_shared<iFormulaNodeStmRealvardef>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.cacheable = false;
           delete($2);
         }
         | REALVARDEF error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmRealvardef>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
         | POSVARDEF psymbol {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {GETARG(@2)};
             params.lines->push_back(std::make_shared<iFormulaNodeStmPosvardef>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.cacheable = false;
           delete($2);
         }
         | POSVARDEF error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmPosvardef>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
         | CLEAREQUATIONS {
           if (include_level == 0) {
             params.lines->push_back(std::make_shared<iFormulaNodeStmClearall>(current_options));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.cacheable = false;
           params.compiler->clear();
         }
         | DELETE '{' labellist '}' {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmDelete>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }
           for (const auto& i : *$3)
             params.compiler->deleq(i);
           params.cacheable = false;
           delete($3);
         }
         | DELETE error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmDelete>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
         | UPDATE STRING {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {GETARG(@2)};
             params.lines->push_back(std::make_shared<iFormulaNodeStmUpdate>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }
           params.updateFormulas.push_back(OUS8(*$2));
           params.cacheable = false;
           delete($2);
         }
         | UPDATE error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmUpdate>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
         | CHART '{' STRING ',' ex ',' ex ',' ex ',' ex ',' uinteger ',' STRING '}' {
           if (!checkHasChartsAndTables(params.xDocumentModel))
             MAKE_ERROR(@1, "This document type does not support the CHART statement");
           if (include_level == 0) {
             std::vector<OUString> formulaParts =
               {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU(","), GETARG(@9), OU(","), GETARG(@11), OU(","), GETARG(@13), OU(","), GETARG(@15), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmChart>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }

           if (!isGlobalDocument(params.xDocumentModel)) { // Access to chart data throws an exception for global documents
            expression vec1 = expression(*$9 / *$11).evalm();
            if (!is_a<matrix>(vec1) || ex_to<matrix>(vec1).cols() > 1)
              MAKE_ERROR(@9, "Column vector expected");
            if ($5->info(info_flags::integer)) {
              setChartData(params.xDocumentModel, OUS8(*$3), ex_to<matrix>(vec1), $13);
            } else {
              expression vec2 = expression(*$5 / *$7).evalm();
              if (!is_a<matrix>(vec2) || ex_to<matrix>(vec2).cols() > 1)
                MAKE_ERROR(@5, "Column vector expected");
              setChartData(params.xDocumentModel, OUS8(*$3), ex_to<matrix>(vec2), ex_to<matrix>(vec1), $13);
            }
            setSeriesDescription(params.xDocumentModel, OUS8(*$3), OUS8(*$15), $13 == 1 ? 1 : $13 - 1);
           }

           delete($3); delete($5); delete($7); delete($9); delete($11); delete($15);
         }
         | CHART '{' STRING ',' symbol '=' ex ',' ex ',' eq ',' ex ',' uinteger ',' STRING '}' {
           if (!checkHasChartsAndTables(params.xDocumentModel))
            MAKE_ERROR(@1, "This document type does not support the CHART statement")

           if (include_level == 0) {
             std::vector<OUString> formulaParts =
               {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU("="), GETARG(@7), OU(","), GETARG(@9), OU(","), GETARG(@11), OU(","), GETARG(@13), OU(","), GETARG(@15), OU(","), GETARG(@17), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmChart>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }

           if (!isGlobalDocument(params.xDocumentModel)) { // Access to chart data throws an exception for global documents
             expression vec = expression(*$7 / *$9).evalm();
             if (!is_a<matrix>(vec) || ex_to<matrix>(vec).cols() > 1)
               MAKE_ERROR(@7, "Column vector expected");
             setChartData(params.xDocumentModel, OUS8(*$3), ex_to<extsymbol>(*$5), ex_to<matrix>(vec), subs(ex($11->rhs()) / ex(*$13), ex_to<extsymbol>(*$5) == ex_to<extsymbol>(*$5) * *$9), $15);
             setSeriesDescription(params.xDocumentModel, OUS8(*$3), OUS8(*$17), $15 == 1 ? 1 : $15 - 1);
           }

           delete($3); delete($5); delete($7); delete($9); delete($11); delete($13);  delete($17);
         }
         | CHART '{' STRING ',' symbol '=' ex ',' ex ',' ex ',' ex ',' uinteger ',' STRING '}' {
           if (!checkHasChartsAndTables(params.xDocumentModel))
            MAKE_ERROR(@1, "This document type does not support the CHART statement")

           if (include_level == 0) {
             std::vector<OUString> formulaParts =
               {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU("="), GETARG(@7), OU(","), GETARG(@9), OU(","), GETARG(@11), OU(","), GETARG(@13), OU(","), GETARG(@15), OU(","), GETARG(@17), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmChart>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }

           if (!isGlobalDocument(params.xDocumentModel)) { // Access to chart data throws an exception for global documents
             expression vec = expression(*$7 / *$9).evalm();
             if (!is_a<matrix>(vec) || ex_to<matrix>(vec).cols() > 1)
               MAKE_ERROR(@7, "Column vector expected");
             setChartData(params.xDocumentModel, OUS8(*$3), ex_to<extsymbol>(*$5), ex_to<matrix>(vec), subs(*$11 / *$13, ex_to<extsymbol>(*$5) == ex_to<extsymbol>(*$5) * *$9), $15);
             setSeriesDescription(params.xDocumentModel, OUS8(*$3), OUS8(*$17), $15 == 1 ? 1 : $15 - 1);
           }

           delete($3); delete($5); delete($7); delete($9); delete($11); delete($13);  delete($17);
         }
         | CHART error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmChart>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
         | SETTABLECELL '{' ex ',' ex ',' ex '}' {
           if (!checkHasChartsAndTables(params.xDocumentModel))
            MAKE_ERROR(@1, "This document cannot hold any tables")

           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmTablecell>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }

            if (!is_a<stringex>(*$3))
              MAKE_ERROR(@3, "Table name must be a string")

					 OUString tableName(OUS8(ex_to<stringex>(*$3).get_string()));

					 if (is_a<stringex>(*$5) && !params.copyPasteActive) {
						 setTableCell(params.xDocumentModel, tableName, OUS8(ex_to<stringex>(*$5).get_string()), *$7);
					 } else if (is_a<matrix>(*$5)) {
						 const matrix& l = ex_to<matrix>(*$5);
						 if (is_a<matrix>(*$7)) {
							 const matrix& v = ex_to<matrix>(*$7);
							 if (l.rows() != v.rows() || (l.cols() != v.cols()))
                MAKE_ERROR(@5, "Rows and columns of the matrix of values must match matrix of cell references")
						 }

						 for (unsigned r = 0; r < l.rows(); ++r) {
							 for (unsigned c = 0; c < l.cols(); ++c) {
								 if (is_a<stringex>(l(r,c))) {
									 ex val;
									 if (is_a<matrix>(*$7)) {
										 const matrix& v = ex_to<matrix>(*$7);
										 val = v(r,c);
									 } else {
										 val = *$7;
									 }
									  if (!params.copyPasteActive) setTableCell(params.xDocumentModel, tableName, OUS8(ex_to<stringex>(l(r,c)).get_string()), val);
								 }
							 }
						 }
					 } else {
              MAKE_ERROR(@5, "Cell reference must be a string or a list of strings")
            }

           params.cacheable = false;
           delete ($3); delete($5); delete ($7);
         }
         | SETTABLECELL error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmTablecell>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
         | SETCALCCELLS '{' ex ',' ex ',' ex ',' ex '}' {
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU(","), GETARG(@9), OU("}")};
             params.lines->push_back(std::make_shared<iFormulaNodeStmCalccell>(current_options, std::move(formulaParts)));
             line = params.lines->back();
             line_options = nullptr;
           }

					 if (!is_a<stringex>(*$3))
              MAKE_ERROR(@3, "File name must be a string")
           else if (!is_a<stringex>(*$5))
              MAKE_ERROR(@5, "Table name must be a string")
           else if (!is_a<stringex>(*$7))
              MAKE_ERROR(@7, "Cell reference must be a string");

           Reference<XStorable> xStorable(params.xDocumentModel, UNO_QUERY_THROW);
           OUString documentURL = xStorable->getLocation();
           Reference< XComponentContext> componentContext = params.xContext;
           OUString calcURL = makeURLFor(OUS8(ex_to<stringex>(*$3).get_string()), documentURL, componentContext); // Handle relative paths in the URL
           setCalcCellRange(componentContext, calcURL, OUS8(ex_to<stringex>(*$5).get_string()), OUS8(ex_to<stringex>(*$7).get_string()), *$9);
           params.cacheable = false;
           delete ($3); delete($5); delete ($7); delete($9);
         }
         | SETCALCCELLS error {
            std::vector<OUString> formulaParts = {};
            handle_error(params, std::make_shared<iFormulaNodeStmCalccell>(current_options, std::move(formulaParts)), @2);
            YYABORT;
          }
;

labellist: LABEL  {
            $$ = new std::vector<std::string>;
            $$->emplace_back(params.compiler->label_ns(*$1, true));
            delete($1);
          }
          | labellist ';' LABEL {
            $1->emplace_back(params.compiler->label_ns(*$3, true));
            $$ = $1;
            delete ($3);
          }
;

// Complete expressions, they cannot be nested
expr:   options EXDEF asterisk ex { // If we add an optional label (that may be empty) then there are two shift/reduce conflicts
        if (include_level == 0) {
          std::vector<OUString> formulaParts = {GETARG(@4)};
          params.lines->push_back(std::make_shared<iFormulaNodeEx>(
            unitConversions(), current_options, std::move(*$1),
            std::move(formulaParts), OU(""),
            *$4, $3));
          line = params.lines->back();
          line_options = nullptr;
          line->force_autoformat(must_autoformat);
        }
        params.cacheable = false;
        must_autoformat = false;
        delete($1); delete($4);
      }
      | options EXDEF asterisk error {
        std::vector<OUString> formulaParts = {};
        handle_error(params, std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, std::move(*$1), std::move(formulaParts), OU(""), _expr0, $3), @4);
        delete($1);
        YYABORT;
      }
      | LABEL options EXDEF asterisk ex { // Therefore we need a second rule
        // Note: This is always a new label, the duplicate label case is handled by the next rule
        if (include_level == 0) {
          std::vector<OUString> formulaParts = {GETARG(@5)};
          params.lines->push_back(std::make_shared<iFormulaNodeEx>(
            unitConversions(), current_options, std::move(*$2),
            std::move(formulaParts), OUS8(params.compiler->exlabel_ns(*$1)),
            *$5, $4));
          line = params.lines->back();
          line_options = nullptr;
          line->force_autoformat(must_autoformat);
        }
        params.cacheable = false;
        must_autoformat = false;
        if ($1->size() > 0) params.compiler->register_expression(*$5, params.compiler->exlabel_ns(*$1)); // Only expressions with labels need to be registered
        delete($1); delete($2); delete($5);
      }
      | LABEL options EXDEF asterisk error {
        std::vector<OUString> formulaParts = {};
        handle_error(params, std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(params.compiler->label_ns(*$1)), _expr0, $4), @5);
        delete($1); delete($2);
        YYABORT;
      }
      | EXLABEL options EXDEF asterisk ex { // Duplicate expression label
        if (autorenumberduplicate) {
          std::string label = params.compiler->exlabel_ns(*$1) + "_1";

          if (include_level == 0) {
            std::vector<OUString> formulaParts = {GETARG(@5)};
            params.lines->push_back(std::make_shared<iFormulaNodeEx>(
              unitConversions(), current_options, std::move(*$2),
              std::move(formulaParts), OUS8(label),
              *$5, $4));
            line = params.lines->back();
            line_options = nullptr;
            line->force_autoformat(must_autoformat);
          }
          params.cacheable = false;
          must_autoformat = false;
          if (label.size() > 0) params.compiler->register_expression(*$5, label); // Only expressions with labels need to be registered
          delete($1); delete($2); delete($5);
        } else {
          std::vector<OUString> formulaParts = {};
          handle_label_error(@1, *$1, params, std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(*$1), *$5, $4), @5);
          delete($1), delete($2); delete ($5);
          YYABORT;
        }
      }
      | EXLABEL options EXDEF asterisk error {
        std::vector<OUString> formulaParts = {};
        handle_error(params, std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(params.compiler->exlabel_ns(*$1)), _expr0, $4), @5);
        delete($1); delete($2);
        YYABORT;
      }
			| options PRINTVALUE ex {
				if (include_level == 0) {
          bool algebraic = $2->substr(5,1) == "A";
          std::string type = (algebraic ? "AVAL" : "VAL");
          std::vector<OUString> formulaParts = {GETARG(@3)};

          params.lines->push_back(std::make_shared<iFormulaNodePrintval>(
            unitConversions(), current_options, std::move(*$1),
            std::move(formulaParts), OU(""),
            (is_a<matrix>(*$3) ? calcvalueofmatrix(type, *$3, lst()) : calcvalue(type, *$3, lst())), false,
            *$3,
            algebraic, false
          ));
          line = params.lines->back();
          line_options = nullptr;
					line->force_autoformat(must_autoformat);
        }
				params.cacheable = false;
        must_autoformat = false;
				delete($1); delete($3);
			}
			| options PRINTVALUE error {
        bool algebraic = $2->substr(5,1) == "A";
        std::vector<OUString> formulaParts = {};
        handle_error(params, std::make_shared<iFormulaNodePrintval>(unitConversions(), current_options, std::move(*$1), std::move(formulaParts), OU(""), _expr0, false, _expr0, algebraic, false), @3);
        delete($1);
        YYABORT;
      }
			| options PRINTVALUEWITH '{' ex ',' eqlist '}' {
				if (include_level == 0) {
          bool algebraic = $2->substr(5,1) == "A";
          std::string type = (algebraic ? "AVALWITH" : "VALWITH");
          std::vector<OUString> formulaParts = {OU("{"), GETARG(@4), OU(","), GETARG(@6), OU("}")};

          params.lines->push_back(std::make_shared<iFormulaNodePrintval>(
            unitConversions(), current_options, std::move(*$1),
            std::move(formulaParts), OU(""),
            (is_a<matrix>(*$4) ? calcvalueofmatrix(type, *$4, *$6) : calcvalue(type, *$4, *$6)), false,
            *$4,
            algebraic, true
          ));
          line = params.lines->back();
          line_options = nullptr;
					line->force_autoformat(must_autoformat);
        }
				params.cacheable = false;
        must_autoformat = false;
				delete($1); delete($4); delete($6);
			}
			| options PRINTVALUEWITH error {
        bool algebraic = $2->substr(5,1) == "A";
        std::vector<OUString> formulaParts = {};
        handle_error(params, std::make_shared<iFormulaNodePrintval>(unitConversions(), current_options, std::move(*$1), std::move(formulaParts), OU(""), _expr0, false, _expr0, algebraic, true), @3);
        delete($1);
        YYABORT;
      }
			| options EXPLAINVAL asterisk ex {
				if (include_level == 0) {
          // What is the value of the expression?
          expression value = (is_a<matrix>(*$4) ? calcvalueofmatrix("VAL", *$4, lst()) : calcvalue("VAL", *$4, lst()));
          expression definition = (is_a<symbol>(*$4) ? ex_to<relational>(params.compiler->get_assignment(ex_to<symbol>(*$4))).rhs() : *$4);
          std::vector<OUString> formulaParts = {GETARG(@4)};

          params.lines->push_back(std::make_shared<iFormulaNodeExplainval>(
            unitConversions(), current_options, std::move(*$1),
            std::move(formulaParts), OU(""),
            value, $3,
            *$4, definition,
            // How is the value of the expression defined?
            // Extract all symbols from the definition and store their value in a map
            // Note: Since we just called calcvalue() on the expression, all symbols contained in it have had their values calculated
            params.compiler->find_variable_values(definition)
          ));
          line = params.lines->back();
          line_options = nullptr;
					line->force_autoformat(must_autoformat);
        }
				params.cacheable = false;
        must_autoformat = false;
				delete($1); delete($4);
			}
			| options EXPLAINVAL asterisk error {
        std::vector<OUString> formulaParts = {};
        GiNaC::exhashmap<GiNaC::ex> symbols = {};
        handle_error(params, std::make_shared<iFormulaNodeExplainval>(unitConversions(), current_options, std::move(*$1), std::move(formulaParts), OU(""), _expr0, $3, _expr0, _expr0, std::move(symbols)), @4);
        delete($1);
        YYABORT;
      }
      |	LABEL options EQDEF asterisk eq {
        std::string nslabel;
        if (!check_label(*$1, nslabel)) {
          std::vector<OUString> formulaParts = {};
          handle_label_error(@1, *$1, params, std::make_shared<iFormulaNodeEq>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(*$1), *$5, $4), @5);
          delete($1), delete($2); delete ($5);
          YYABORT;
        }

        if (include_level == 0) {
          std::vector<OUString> formulaParts = {GETARG(@5)};
          params.lines->push_back(std::make_shared<iFormulaNodeEq>(
            unitConversions(), current_options, std::move(*$2),
            std::move(formulaParts), OUS8(nslabel),
            *$5, $4));
          line = params.lines->back();
          line_options = nullptr;
          line->force_autoformat(must_autoformat);
        }
        must_autoformat = false;

        params.compiler->check_and_register(*$5, nslabel); // TODO: Is there an original equation to look after? That would have to be stored inside the equation!
        if (params.cacheable) params.cached_results->emplace_back(nslabel, *$5);

        delete($1); delete($2); delete($5);
      }
      | LABEL options EQDEF asterisk error {
        std::vector<OUString> formulaParts = {};
        handle_error(params, std::make_shared<iFormulaNodeEq>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(params.compiler->label_ns(*$1)), equation(_expr0, _expr0), $4), @5);
        delete($1); delete($2);
        YYABORT;
      }
      | LABEL options CONSTDEF asterisk eq {
        std::string nslabel;
        if (!check_label(*$1, nslabel)) {
          std::vector<OUString> formulaParts = {};
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeConst>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(nslabel), *$5, $4), @5);
          delete($1), delete($2); delete ($5);
          YYABORT;
        }

        if (include_level == 0) {
          std::vector<OUString> formulaParts = {GETARG(@5)};
          params.lines->push_back(std::make_shared<iFormulaNodeConst>(
            unitConversions(), current_options, std::move(*$2),
            std::move(formulaParts), OUS8(nslabel),
            *$5, $4));
          line = params.lines->back();
          line_options = nullptr;
          line->force_autoformat(must_autoformat);
        }
        must_autoformat = false;

        params.compiler->register_constant(ex_to<equation>(*$5)); // TODO: Label is unused - no equation is registered, just the value!

        params.cacheable = false;
        delete ($1); delete($2); delete($5);
      }
      | LABEL options CONSTDEF asterisk error {
        std::vector<OUString> formulaParts = {};
        handle_error(params, std::make_shared<iFormulaNodeConst>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(params.compiler->label_ns(*$1)), equation(_expr0, _expr0), $4), @5);
        delete($1), delete($2);
        YYABORT;
      }
      | LABEL options FUNCDEF asterisk FUNC leftbracket ex rightbracket '=' ex {
        if (!checkbrackets(*$6, *$8))
          MAKE_ERROR(@8, "Bracket type mismatch");

        expression f = params.compiler->create_function(*$5, {*$7});
				if ($10->has(params.compiler->create_function(*$5)) || $10->has(f))
          MAKE_ERROR(@10, "Recursive function definition");

        params.compiler->define_function(*$5, *$10); // TODO: Should we check the arguments in $7 ?
        expression* result = new expression(dynallocate<equation>(f, *$10, relational::equal, _expr0));
        std::string nslabel;
        if (!check_label(*$1, nslabel)) {
          std::vector<OUString> formulaParts = {};
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeFuncdef>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(nslabel), *result, $4), @5);
          delete($1); delete($2); delete ($5); delete($7); delete($10);
          YYABORT;
        }

        if (include_level == 0) {
          std::vector<OUString> formulaParts = {GETARG(@5), GETARG(@6), GETARG(@7), GETARG(@8), OU("="), GETARG(@10)};
          params.lines->push_back(std::make_shared<iFormulaNodeFuncdef>(
            unitConversions(), current_options, std::move(*$2),
            std::move(formulaParts), OUS8(nslabel),
            *result, $4));
          line = params.lines->back();
          line_options = nullptr;
          line->force_autoformat(must_autoformat);
        }
        must_autoformat = false;

        if (!ex_to<func>(f).is_expand())
          params.compiler->check_and_register(*result, nslabel);

        params.cacheable = false;
	      delete($1); delete($2); delete ($5); delete($7); delete($10);
      }
      | LABEL options FUNCDEF asterisk FUNC leftbracket exvec rightbracket '=' ex {
        if (!checkbrackets(*$6, *$8))
          MAKE_ERROR(@8, "Bracket type mismatch");

        expression f = params.compiler->create_function(*$5, *$7);
				if ($10->has(params.compiler->create_function(*$5)) || $10->has(f))
          MAKE_ERROR(@10, "Recursive function definition");

        params.compiler->define_function(*$5, *$10); // TODO: Should we check the arguments in $7 ?
        expression* result = new expression(dynallocate<equation>(f, *$10, relational::equal, _expr0));
        std::string nslabel;
        if (!check_label(*$1, nslabel)) {
          std::vector<OUString> formulaParts = {};
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeFuncdef>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(nslabel), *result, $4), @5);
          delete($1); delete($2); delete ($5); delete($7); delete($10);
          YYABORT;
        }

        if (include_level == 0) {
          std::vector<OUString> formulaParts = {GETARG(@5), GETARG(@6), GETARG(@7), GETARG(@8), OU("="), GETARG(@10)};
          params.lines->push_back(std::make_shared<iFormulaNodeFuncdef>(
            unitConversions(), current_options, std::move(*$2),
            std::move(formulaParts),OUS8(nslabel),
            *result, $4));
          line = params.lines->back();
          line_options = nullptr;
          line->force_autoformat(must_autoformat);
        }
        must_autoformat = false;

        if (!ex_to<func>(f).is_expand())
          params.compiler->check_and_register(*result, nslabel);

        params.cacheable = false;
	      delete($1); delete($2); delete ($5); delete($7); delete($10);
      }
      | LABEL options FUNCDEF asterisk error {
        std::vector<OUString> formulaParts = {};
        handle_error(params, std::make_shared<iFormulaNodeFuncdef>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(params.compiler->label_ns(*$1)), equation(_expr0, _expr0), $4), @5);
        delete($1), delete($2);
        YYABORT;
      }
      | LABEL options VECTORDEF asterisk vsymbol '=' ex {
        equation& result = dynallocate<equation>(*$5, *$7, relational::equal, _expr0);
        std::string nslabel;
        if (!check_label(*$1, nslabel)) {
          std::vector<OUString> formulaParts = {};
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeVectordef>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(nslabel), expression(result), $4), @5);
          delete($1), delete($2); delete ($5); delete($7);
          YYABORT;
        }

        if (include_level == 0) {
          std::vector<OUString> formulaParts = {GETARG(@5), OU("="), GETARG(@7)};
          params.lines->push_back(std::make_shared<iFormulaNodeVectordef>(
            unitConversions(), current_options, std::move(*$2),
            std::move(formulaParts), OUS8(nslabel),
            expression(result), $4));
          line = params.lines->back();
          line_options = nullptr;
        }

        params.compiler->check_and_register(result, nslabel);

        params.cacheable = false;
	      delete($1); delete($2); delete ($5); delete($7);
      }
      | LABEL options VECTORDEF asterisk error {
        std::vector<OUString> formulaParts = {};
        handle_error(params, std::make_shared<iFormulaNodeVectordef>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(params.compiler->label_ns(*$1)), equation(_expr0, _expr0), $4), @5);
        delete($1); delete($2);
        YYABORT;
      }
      | LABEL options MATRIXDEF asterisk msymbol '=' ex {
        equation& result = dynallocate<equation>(*$5, *$7, relational::equal, _expr0);
        std::string nslabel;
        if (!check_label(*$1, nslabel)) {
          std::vector<OUString> formulaParts = {};
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeMatrixdef>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(nslabel), expression(result), $4), @5);
          delete($1), delete($2); delete ($5); delete($7);
          YYABORT;
        }

        if (include_level == 0) {
          std::vector<OUString> formulaParts = {GETARG(@5), OU("="), GETARG(@7)};
          params.lines->push_back(std::make_shared<iFormulaNodeMatrixdef>(
            unitConversions(), current_options, std::move(*$2),
            std::move(formulaParts), OUS8(nslabel),
            expression(result), $4));
          line = params.lines->back();
          line_options = nullptr;
        }

        params.compiler->check_and_register(result, nslabel);

        params.cacheable = false;
	      delete($1); delete($2); delete ($5); delete($7);
      }
      | LABEL options MATRIXDEF asterisk error {
        std::vector<OUString> formulaParts = {};
        handle_error(params, std::make_shared<iFormulaNodeMatrixdef>(unitConversions(), current_options, std::move(*$2), std::move(formulaParts), OUS8(params.compiler->label_ns(*$1)), equation(_expr0, _expr0), $4), @5);
        delete($1); delete($2);
        YYABORT;
      }
;

options: %empty { $$ = new optionmap(); line_options = $$; }
         | '{' keyvallist '}' { $$ = $2; line_options = $$; }
         | IMPMUL '{' keyvallist '}' { $$ = $3; line_options = $$; }
;
keyvallist: keyvalpair {
              $$ = new optionmap();
              $$->insert(*$1);
              delete $1;
            }
            | { canonicalize_units = false; } unitpair {
              $$ = new optionmap();
              $$->emplace(std::pair<option_name, option>(o_units, $2->first));
              $$->emplace(std::pair<option_name, option>(o_unitstr, $2->second));
              delete $2;
            }
            | keyvallist ';' keyvalpair {
              $$ = $1;
              $$->insert(*$3);
              delete $3;
            }
            | keyvallist ';' { canonicalize_units = false; } unitpair {
              $$ = $1;
              $$->emplace(std::pair<option_name, option>(o_units, $4->first));
              $$->emplace(std::pair<option_name, option>(o_unitstr, $4->second));
              delete $4;
            }
;
keyvalpair:   OPT_U '=' ex {
              numeric v = get_val_from_ex(*$3);
              if (!v.info(info_flags::nonnegint))
                MAKE_ERROR(@3, "Option value must be an integer greater or equal to zero");
              $$ = new std::pair<option_name, option>($1, option((unsigned)v.to_int()));
              delete($3);

              // Ensure that the precision setting takes effect for numeric calculations, not just for printing!
              // TODO: How do we put it back to what it was before, so that it remains local to this line?
              //if ($1 == o_precision)
              //  Digits = v.to_int() + 5;
            }
            | OPT_I '=' ex {
              numeric v = get_val_from_ex(*$3);
              if (!v.info(info_flags::integer))
                MAKE_ERROR(@3, "Option value must be an integer");
              $$ = new std::pair<option_name, option>($1, option(v.to_int()));
              delete($3);
            }
            | OPT_S '=' STRING {
              $$ = new std::pair<option_name, option>($1, option($3));
            }
            | OPT_B '=' BOOL {
              switch ($1) {
                case o_eqraw:
                case o_fixeddigits:
                  $$ = new std::pair<option_name, option>($1, option(!$3));
                  break;
                case o_eqalign:
                  $$ = new std::pair<option_name, option>($1, option($3 ? both : none));
                  break;
                case o_underflow:
                  $$ = new std::pair<option_name, option>($1, option($3));
                  set_inhibit_floating_point_underflow($3);
                  MSG_INFO(3, "Inhibit floating point underflow exception: " << ($3 ? "true" : "false") << endline);
                  break;
                case o_evalf_real_roots:
                  $$ = new std::pair<option_name, option>($1, option($3));
                  expression::evalf_real_roots_flag = $3;
                  MSG_INFO(3, "Evaluate odd negative roots to the positive real value: " << ($3 ? "true" : "false") << endline);
                  break;
                default:
                  $$ = new std::pair<option_name, option>($1, option($3));
              }
            }
;
unitpair:     OPT_L  '=' '{' ex '}' {
              canonicalize_units = true;
              exvector* units = new exvector{*$4};
              $$ = new std::pair<exvector*, std::string>(units, trimstring(rawtext.substr(@4.begin.column-1, @4.end.column-@4.begin.column)));
              delete($4);
            }
            | OPT_L  '=' '{' exvec '}' { // Unit options: units (local to line)
                canonicalize_units = true;
                $$ = new std::pair<exvector*, std::string>($4, trimstring(rawtext.substr(@4.begin.column-1, @4.end.column-@4.begin.column)));
              }
;

funchints: '{' hints '}' {
            $$ = $2;
          }
;
hints:      IDENTIFIER { $$ = Functionmanager::hint(*$1); delete($1); }
          | hints ';' IDENTIFIER {
            $$ = $1 | Functionmanager::hint(*$3);
            delete($3);
          }
;
asterisk:   %empty { $$ = false; }
          | '*'    { $$ = true; }
;

// Processing of smath formulas
eq:   ex '=' ex             { $$ = new expression(dynallocate<equation>(*$1, *$3, relational::equal, _expr0)); delete ($1); delete($3); }
    | ex '=' '=' ex         { $$ = new expression(dynallocate<equation>(*$1, *$4, relational::equal, _expr0)); delete ($1); delete($4); }
    | ex NEQ ex             { $$ = new expression(dynallocate<equation>(*$1, *$3, relational::not_equal, _expr0)); delete ($1); delete($3); }
    | ex '<' ex             { $$ = new expression(dynallocate<equation>(*$1, *$3, relational::less, _expr0)); delete ($1); delete($3); }
    | ex '>' ex             { $$ = new expression(dynallocate<equation>(*$1, *$3, relational::greater, _expr0)); delete ($1); delete($3); }
    | ex '>' '=' ex         { $$ = new expression(dynallocate<equation>(*$1, *$4, relational::greater_or_equal, _expr0)); delete ($1); delete($4); }
    | ex '<' '=' ex         { $$ = new expression(dynallocate<equation>(*$1, *$4, relational::less_or_equal, _expr0)); delete ($1); delete($4); }
    | ex EQUIV ex MOD ex { // Note: Writing '(' MOD ex ')' results in too many conflicts
      if (is_a<numeric>(*$1) && !$1->info(info_flags::integer))
        MAKE_ERROR(@1, "Left-hand expression must be integer")
      else if (is_a<numeric>(*$3) && !$3->info(info_flags::integer))
        MAKE_ERROR(@3, "Right-hand expression must be integer")
      else if (!check_modulus(*$5))
        MAKE_ERROR(@5, "Modulo must be a positive or gaussian integer")
      else
        $$ = new expression(dynallocate<equation>(*$1, *$3, relational::equal, *$5));

      delete($1); delete($3); delete($5);
		}
		| ex EQUIV ex BMOD ex ')' {
      if (is_a<numeric>(*$1) && !$1->info(info_flags::integer))
        MAKE_ERROR(@1, "Left-hand expression must be integer")
      else if (is_a<numeric>(*$3) && !$3->info(info_flags::integer))
        MAKE_ERROR(@3, "Right-hand expression must be integer")
      else if (!check_modulus(*$5))
        MAKE_ERROR(@5, "Modulo must be a positive or gaussian integer")
      else
        $$ = new expression(dynallocate<equation>(*$1, *$3, relational::equal, *$5));

      delete($1); delete($3); delete($5);
    }
    | LABEL { // An equation label referencing another equation
      must_autoformat = true;
      $$ = new expression(params.compiler->at(params.compiler->label_ns(*$1, true)));
      delete ($1);
    }
    | SOLVE '(' eq ',' ex ',' uinteger ')' {
      $$ = new expression(ex_to<equation>(*$3).solve(*$5, numeric($7)));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | SUBST '(' eq ',' eqlist ')' {
      $$ = new expression(ex_to<equation>(*$3).subs(*$5).evalm());
      must_autoformat = true;
      delete($3); delete($5);
    }
    | SUBSTC '(' eq ',' eqlist ')' {
      $$ = new expression(ex_to<equation>(*$3).csubs(*$5).evalm());
      must_autoformat = true;
      delete($3); delete($5);
    }
    | SIMPLIFY '(' eq ',' simplifications ')' {
      $$ = new expression(ex_to<equation>(*$3).simplify(*$5));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | COLLECT '(' eq ')' {
      $$ = new expression(ex_to<equation>(*$3).collect());
      must_autoformat = true;
      delete($3);
    }
    | COLLECT '(' eq ',' ex ')' {
      $$ = new expression(ex_to<equation>(*$3).collect(*$5));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | DIFFERENTIATE '(' eq ',' ex ',' ex ')' { // Immediately differentiate
      $$ = new expression(ex_to<equation>(*$3).diff(*$5, *$7, true));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | PDIFFERENTIATE '(' eq ',' ex ',' ex ')' { // Immediately differentiate
      $$ = new expression(ex_to<equation>(*$3).pdiff(*$5, *$7, true));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | INTEGRATE '(' eq ',' ex ',' ex ')' {
		  if (!is_a<symbol>(*$7))
        MAKE_ERROR(@7, "Integration constant must be a symbol");
      $$ = new expression(ex_to<equation>(*$3).integrate(*$5, ex_to<symbol>(*$7), *$5, ex_to<symbol>(*$7)));
      must_autoformat = true;
      delete($3); delete($5); delete($7);
    }
    | INTEGRATE '(' eq ',' exvec ',' exvec ')' {
      const exvector& vars = *$5;
      const exvector& constants = *$7;
      if (vars.size() > 2)
        MAKE_ERROR(@5, "Second argument to INTEGRATE must be two variables");
      if (constants.size() > 2)
        MAKE_ERROR(@7, "Third argument to INTEGRATE must be two symbols");
      if (!is_a<symbol>(constants[0]) || !is_a<symbol>(constants[1]))
        MAKE_ERROR(@7, "Integration constants must be symbols");
      $$ = new expression(ex_to<equation>(*$3).integrate(vars[0], ex_to<symbol>(constants[0]), vars[1], ex_to<symbol>(constants[1])));
      must_autoformat = true;
      delete($3); delete($5); delete($7);
    }
    | INTEGRATE '(' eq ',' ex ',' ex ',' ex ')' {
		  $$ = new expression(ex_to<equation>(*$3).integrate(*$5, *$7, *$9, *$5, *$7, *$9));
      must_autoformat = true;
      delete($3); delete($5); delete($7);  delete($9);
    }
    | INTEGRATE '(' eq ',' exvec ',' exvec ',' exvec ')' {
      const exvector& vars  = *$5;
      const exvector& lower = *$7;
      const exvector& upper = *$9;
      if (vars.size() > 2)
        MAKE_ERROR(@5, "Second argument to INTEGRATE must be two variables");
      if (lower.size() > 2)
        MAKE_ERROR(@7, "Third argument to INTEGRATE must be two symbols");
      if (upper.size() > 2)
        MAKE_ERROR(@9, "Fourth argument to INTEGRATE must be two variables");
      $$ = new expression(ex_to<equation>(*$3).integrate(vars[0], lower[0], upper[0], vars[1], lower[1], upper[1]));
      must_autoformat = true;
      delete($3); delete($5); delete($7); delete($9);
    }
    | leftbracket eq rightbracket {
      if (!checkbrackets(*$1, *$3))
        MAKE_ERROR(@3, "Bracket type mismatch");
      $$ = $2;
      delete($1); delete($3);
    }
    | REVERSE eq {
      $$ = new expression(ex_to<equation>(*$2).reverse());
      must_autoformat = true;
      delete ($2);
    }
    | FUNC leftbracket eq rightbracket {
      if (!checkbrackets(*$2, *$4))
        MAKE_ERROR(@4, "Bracket type mismatch");
      if (!params.compiler->is_func(*$1))
        MAKE_ERROR(@1, "Argument must be a function name");
      MSG_INFO(1, "Applying function " << *$1 << " to " << *$3 << endline);
      const equation& eq = ex_to<equation>(*$3);
      $$ = new expression(dynallocate<equation>(params.compiler->create_function(*$1, {eq.lhs()}), params.compiler->create_function(*$1, {eq.rhs()}), eq.getop(), eq.getmod()));
      delete ($1); delete($2); delete($3); delete($4);
    }
    | SIZE sizestr IMPMUL eq { $$ = $4; delete($2); }
    | '-' eq %prec NEGATION {
      const equation& eq = ex_to<equation>(*$2);
      $$ = new expression(dynallocate<equation>(-eq.lhs(), -eq.rhs(), eq.getop(), eq.getmod()));
      delete($2);
    }
    | '+' eq %prec NEGATION { $$ = $2; }
    | eq '+' eq    { $$ = new expression((*$1 + *$3).evalm()); delete ($1); delete($3); }
    | eq '+' ex    { $$ = new expression((*$1 + *$3).evalm()); delete ($1); delete($3); }
    | ex '+' eq    { $$ = new expression((*$1 + *$3).evalm()); delete ($1); delete($3); }
    | eq '-' eq    { $$ = new expression((*$1 - *$3).evalm()); delete ($1); delete($3); }
    | eq '-' ex    { $$ = new expression((*$1 - *$3).evalm()); delete ($1); delete($3); }
    | ex '-' eq    { $$ = new expression((*$1 - *$3).evalm()); delete ($1); delete($3); }
    | eq PLUSMINUS eq { $$ = new expression(*$1 * *$3 * expression(stringex("+-"))); delete ($1); delete($3); }
    | eq PLUSMINUS ex { $$ = new expression(*$1 * *$3 * expression(stringex("+-"))); delete ($1); delete($3); }
    | ex PLUSMINUS eq { $$ = new expression(*$1 * *$3 * expression(stringex("+-"))); delete ($1); delete($3); }
    | eq MINUSPLUS eq { $$ = new expression(*$1 * *$3 * expression(stringex("-+"))); delete ($1); delete($3); }
    | eq MINUSPLUS ex { $$ = new expression(*$1 * *$3 * expression(stringex("-+"))); delete ($1); delete($3); }
    | ex MINUSPLUS eq { $$ = new expression(*$1 * *$3 * expression(stringex("-+"))); delete ($1); delete($3); }
    | eq '*' eq    { $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete($3); }
    | eq IMPMUL eq {
      if (hasLineOption(o_implicitmul) && (getLineOption(o_implicitmul).value.boolean == false))
        MAKE_ERROR(@3, "Implicit multiplication is not allowed in this expression");
      $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete($3);
    }
    | eq TIMES eq  { $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete($3); }
    | eq '*' ex    { $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete($3); }
    | eq IMPMUL ex {
      if (hasLineOption(o_implicitmul) && (getLineOption(o_implicitmul).value.boolean == false))
        MAKE_ERROR(@3, "Implicit multiplication is not allowed in this expression");
      $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete($3);
    }
    | eq TIMES ex  { $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete($3); }
    | ex '*' eq    { $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete($3); }
    | ex IMPMUL eq {
      if (hasLineOption(o_implicitmul) && (getLineOption(o_implicitmul).value.boolean == false))
        MAKE_ERROR(@3, "Implicit multiplication is not allowed in this expression");
      $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete($3);
    }
    | ex TIMES eq  { $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete($3); }
    | eq '/' eq    { $$ = new expression((*$1 / *$3).evalm()); delete ($1); delete($3); }
    | eq '/' ex    { $$ = new expression((*$1 / *$3).evalm()); delete ($1); delete($3); }
    | eq '^' ex    { $$ = new expression(ex_to<equation>(*$1).apply_power(*$3).evalm()); delete ($1); delete ($3); }
    | eq superscript { $$ = new expression(ex_to<equation>(*$1).apply_power(*$2).evalm()); delete ($1); delete ($2); }
    | VALUE '(' eq ')' { // Calculate the value of this equation
      // Don't calculate value of LHS if it is a symbol - otherwise the result will be identical on both sides!
      const equation& eq = ex_to<equation>(*$3);
      ex lhs = eq.lhs();
      if (!is_a<symbol>(lhs))
        lhs = calcvalue(*$1, lhs, lst());
      $$ = new expression(dynallocate<equation>(lhs, calcvalue(*$1, eq.rhs(), lst()), eq.getop(), eq.getmod()));
      must_autoformat = true;
      delete($1); delete($3);
    }
    | VALUEWITH '(' eq ',' eqlist ')' { // Find the value of this equation using an optional list of assignments to find it
      const equation& eq = ex_to<equation>(*$3);
      ex lhs = eq.lhs();
      if (!is_a<symbol>(lhs))
        lhs = calcvalue(*$1, lhs, *$5);
      $$ = new expression(dynallocate<equation>(lhs, calcvalue(*$1, eq.rhs(), *$5), eq.getop(), eq.getmod()));
      must_autoformat = true;
      delete($1); delete($3); delete($5);
    }
;

eqlist:   eq            {
          $$ = new lst{*$1};
          delete($1); }
        | eqlist ';' eq { $$ = $1; $$->append(*$3); delete($3); }
;

ex:   SUBST '(' ex ',' eqlist ')' {
        $$ = new expression($3->subs(*$5).evalm());
        must_autoformat = true;
        delete($3); delete($5);
    }
    | SUBSTC '(' ex ',' eqlist ')' {
      $$ = new expression($3->csubs(*$5).evalm());
      must_autoformat = true;
      delete($3); delete($5);
    }
    | SUBSTV '(' ex ',' eqlist ')' {
      $$ = new expression($3->subsv(*$5, false).evalm());
      must_autoformat = true;
      delete($3); delete($5);
    }
    | SUBSTVC '(' ex ',' eqlist ')' {
      $$ = new expression($3->subsv(*$5, true).evalm());
      must_autoformat = true;
      delete($3); delete($5);
    }
    | SUBSTV '(' eq ',' eqlist ')' { // SUBSTV always results in a vector (a vector of equations in this case)
      $$ = new expression(ex_to<equation>(*$3).subsv(*$5, false).evalm());
      must_autoformat = true;
      delete($3); delete($5);
    }
    | SUBSTVC '(' eq ',' eqlist ')' {
      $$ = new expression(ex_to<equation>(*$3).subsv(*$5, true).evalm());
      must_autoformat = true;
      delete($3); delete($5);
    }
    | SIMPLIFY '(' ex ',' simplifications ')' {
      $$ = new expression($3->simplify(*$5));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | COLLECT '(' ex ')' {
      $$ = new expression($3->collect());
      must_autoformat = true;
      delete($3);
    }
    | COLLECT '(' ex ',' ex ')' {
      $$ = new expression($3->collect(*$5));
      must_autoformat = true;
      delete($3); delete($5);
    }
		| DIFFERENTIATE '(' ex ',' ex ',' ex ')' {
      $$ = new expression($3->diff(*$5, *$7, true));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | PDIFFERENTIATE '(' ex ',' ex ',' ex ')' {
      $$ = new expression($3->pdiff(*$5, *$7, true));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | INTEGRATE '(' ex ',' ex ',' symbol ')' {
			$$ = new expression($3->integrate(*$5, ex_to<symbol>(*$7)));
      must_autoformat = true;
      delete($3); delete($5); delete($7);
    }
		| INTEGRATE '(' ex ',' ex ',' ex ',' ex ')' {
      $$ = new expression($3->integrate(*$5, *$7, *$9));
      must_autoformat = true;
      delete($3); delete($5); delete($7); delete($9);
    }
    | TSERIES '(' ex ',' eq ',' uinteger ')' { // TODO: Why can't tseries be a normal function?
      $$ = new expression(series_to_poly($3->series(*$5, $7)));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | TEXTFIELD '(' ex ')' {
      if (!checkHasChartsAndTables(params.xDocumentModel))
        MAKE_ERROR(@1, "This document cannot hold any text fields");
			if (!is_a<stringex>(*$3))
				MAKE_ERROR(@3, "Textfield name must be a string");

      Reference< XTextDocument > xDoc(params.xDocumentModel, UNO_QUERY_THROW);
      $$ = new expression(getExpressionFromString(getTextFieldContent(xDoc, OUS8(ex_to<stringex>(*$3).get_string()))));
      must_autoformat = true;
      delete($3);
    }
    | TABLECELL '(' ex ',' exvec_or_ex ')' {
			if (!is_a<stringex>(*$3))
				MAKE_ERROR(@3, "Table name must be a string");
      if (!checkHasChartsAndTables(params.xDocumentModel))
        MAKE_ERROR(@1, "This document cannot hold any tables");

      OUString tableName(OUS8(ex_to<stringex>(*$3).get_string()));
      Reference< XTextDocument > xDoc(params.xDocumentModel, UNO_QUERY_THROW);

			if (is_a<stringex>(*$5)) {
        Reference< XCell > xCell = getTableCell(xDoc, tableName, OUS8(ex_to<stringex>(*$5).get_string()).toAsciiUpperCase());
				$$ = new expression(getCellExpression(xCell));
			} else if (is_a<matrix>(*$5)) {
				const matrix& l = ex_to<matrix>(*$5);
				matrix& m = dynallocate<matrix>(l.rows(), l.cols());

				for (unsigned r = 0; r < l.rows(); ++r) {
					for (unsigned c = 0; c < l.cols(); ++c) {
						if (is_a<stringex>(l(r,c))) {
              Reference< XCell > xCell = getTableCell(xDoc, tableName, OUS8(ex_to<stringex>(l(r,c)).get_string()).toAsciiUpperCase());
							m(r,c) = getCellExpression(xCell);
						} else {
							m(r,c) = dynallocate<stringex>("Error: Cell reference must be a string");
						}
					}
				}

				$$ = new expression(m);
			} else {
				MAKE_ERROR(@5, "Cell reference must be a string or a list of strings");
			}

			params.cacheable = false; // Because changes in tables are not tracked by iMath
			must_autoformat = true;
      delete($3); delete($5);
    }
    | CALCCELL '(' ex ',' ex ',' ex ')' {
			if (!is_a<stringex>(*$3) || !is_a<stringex>(*$5) || !is_a<stringex>(*$7))
				MAKE_ERROR(@7, "File name, table name and cell reference must be a string");
      Reference<XStorable> xStorable(params.xDocumentModel, UNO_QUERY_THROW);
      OUString documentURL = xStorable->getLocation();
      Reference< XComponentContext> componentContext = params.xContext;
      OUString calcURL = makeURLFor(OUS8(ex_to<stringex>(*$3).get_string()), documentURL, componentContext); // Handle relative paths in the URL
      $$ = new expression(calcCellRangeContent(componentContext, calcURL, OUS8(ex_to<stringex>(*$5).get_string()), OUS8(ex_to<stringex>(*$7).get_string())));
      must_autoformat = true;
      delete($3); delete($5); delete($7);
    }
    | symbol
    | EXLABEL { // An expression label referencing another expression
      must_autoformat = true;
      $$ = new expression(params.compiler->expression_at(params.compiler->exlabel_ns(*$1, true)));
      delete ($1);
    }
    | UNIT {
      if (canonicalize_units)
        $$ = new expression(params.compiler->getCanonicalizedUnit(*$1));
      else
        $$ = new expression(params.compiler->getUnit(*$1));
      delete($1);
    }
    | STRING {
      expression newUnit;

      if (params.compiler->isUnit(*$1)) {
        if (canonicalize_units)
          newUnit = params.compiler->getCanonicalizedUnit(*$1);
        else
          newUnit = params.compiler->getUnit(*$1);

        $$ = new expression(newUnit);
      } else {
        $$ = new expression(stringex(*$1));
      }

      delete($1);
    }
    | number
    | vector
    | matrix
    | leftbracket ex rightbracket {
      if (!checkbrackets(*$1, *$3))
        MAKE_ERROR(@3, "Bracket type mismatch");
      $$ = $2;
      delete($1); delete($3);
    }
    | LHS '(' eq ')' {
      $$ = new expression($3->lhs());
      must_autoformat = true;
      delete ($3);
    }
    | RHS '(' eq ')' {
      $$ = new expression($3->rhs());
      must_autoformat = true;
      delete ($3);
    }
    | WILD '(' uinteger ')' {
      $$ = new expression(wild($3));
    }
    | WILD { $$ = new expression(wild()); }
    | FUNC leftbracket ex rightbracket {
      if (!checkbrackets(*$2, *$4))
        MAKE_ERROR(@4, "Bracket type mismatch");
      $$ = new expression(params.compiler->create_function(*$1, {*$3}).evalm());
      delete ($1); delete($2); delete($3); delete($4);
    }
    | FUNC leftbracket exvec rightbracket {
      if (!checkbrackets(*$2, *$4))
        MAKE_ERROR(@4, "Bracket type mismatch");
      $$ = new expression(params.compiler->create_function(*$1, {*$3}).evalm());
      delete ($1); delete($2); delete($3); delete($4);
    }
    | FUNC leftbracket condition ';' exvec rightbracket { // Currently only ifelse() uses this format
      if (!checkbrackets(*$2, *$6))
        MAKE_ERROR(@6, "Bracket type mismatch");
      exvector fargs({*$3});
      fargs.insert(fargs.end(), $5->begin(), $5->end());
      $$ = new expression(params.compiler->create_function(*$1, fargs).evalm());
      delete ($1); delete($2); delete($3); delete($5); delete($6);
    }
    | FUNC { // a function may be used without arguments
      $$ = new expression(params.compiler->create_function(*$1));
      delete ($1);
    }
    | NROOT number IMPMUL number { // We can't prevent the IMPMUL appearing here
      $$ = new expression(dynallocate<power>(*$4, 1 / *$2));
      delete($2); delete($4);
    }
    | NROOT number IMPMUL '{' ex '}' {
      $$ = new expression(dynallocate<power>(*$5, 1 / *$2));
      delete($2); delete($5);
    }
    | NROOT '{' ex '}' IMPMUL '{' ex '}' {
      $$ = new expression(dynallocate<power>(*$7, 1 / *$3));
      delete($3); delete($7);
    }
    | QUO '(' ex ',' ex ')' {
      if (!is_a<numeric>(*$3))
        MAKE_ERROR(@3, "Expression must be a numeric");
      if (!is_a<numeric>(*$5))
        MAKE_ERROR(@5, "Expression must be a numeric");

      $$ = new expression(iquo(ex_to<numeric>(*$3), ex_to<numeric>(*$5)));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | QUO '(' ex ',' ex ',' symbol ')' {
      if (!$3->is_polynomial(*$7))
        MAKE_ERROR(@3, "Expression must be a polynomial in the given variable");
      if (!$5->is_polynomial(*$7))
        MAKE_ERROR(@5, "Expression must be a polynomial in the given variable");

      $$ = new expression(quo(*$3, *$5, *$7));
      must_autoformat = true;
      delete($3); delete($5); delete($7);
    }
    | REM '(' ex ',' ex ')' {
      if (!is_a<numeric>(*$3))
        MAKE_ERROR(@3, "Expression must be a numeric");
      if (!is_a<numeric>(*$5))
        MAKE_ERROR(@5, "Expression must be a numeric");

      $$ = new expression(irem(ex_to<numeric>(*$3), ex_to<numeric>(*$5)));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | REM '(' ex ',' ex ',' symbol ')' {
      if (!$3->is_polynomial(*$7))
        MAKE_ERROR(@3, "Expression must be a polynomial in the given variable");
      if (!$5->is_polynomial(*$7))
        MAKE_ERROR(@5, "Expression must be a polynomial in the given variable");

      $$ = new expression(rem(*$3, *$5, *$7));
      must_autoformat = true;
      delete($3); delete($5); delete($7);
    }
    | GCD '(' ex ',' ex ')' {
      $$ = new expression(gcd(*$3, *$5));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | LCM '(' ex ',' ex ')' {
      $$ = new expression(lcm(*$3, *$5));
      must_autoformat = true;
      delete($3); delete($5);
    }
    | SUMFROM lowerbound TO upperbound IMPMUL '{' ex '}' { // We can't prevent the IMPMUL appearing here
      $$ = new expression(Functionmanager::create_hard("sum", exprseq{*$2, *$4, *$7}).evalm());
      delete($2); delete($4); delete($7);
    }
/*    | PRODUCT FROM ex TO upperbound IMPMUL '{' ex '}' { // We can't prevent the IMPMUL appearing here
        $$ = new expression(product(*$3, *$5, *$8));
      delete($3); delete($5); delete($8);
    }*/
    | INTEGRAL '{' ex '}' {
      $$ = new expression(dynallocate<extintegral>(*$3, params.compiler->getsym("C")));
			delete($3);
    }
    | INTEGRAL FROM lowerbound TO upperbound IMPMUL '{' ex '}' { // We can't prevent the IMPMUL appearing here
			$$ = new expression(dynallocate<extintegral>($3->lhs(), $3->rhs(), *$5, *$8 / differential($3->lhs(), false, _ex1))); // TODO: A sanity check für *$8 to contain the correct differential would be nice
			delete($3); delete($5); delete($8);
    }
    | DIFFERENTIAL '(' ex ')' {
        $$ = new expression(dynallocate<differential>(*$3, *$1 == "PARTIAL", _ex1));
        must_autoformat = true;
        delete($1); delete($3);
    }
    | DIFFERENTIAL '(' ex ',' ex ')' {
      $$ = new expression(dynallocate<differential>(*$3, *$1 == "PARTIAL", *$5));
      must_autoformat = true;
      delete($1); delete($3); delete($5);
    }
    | SIZE sizestr IMPMUL ex {
      $$ = $4;
      delete($2);
    }
/*    | vector '[' ex ']' { // get vector element
      $$ = new expression(func("mindex", exprseq{*$1, *$3, wild()}));
      delete($1); delete($3);
    }*/
    | ex TRANSPOSE {
      if (is_a<matrix>(*$1))
        $$ = new expression(expression(ex_to<matrix>(*$1).transpose()).evalm());
      else
        $$ = new expression(Functionmanager::create_hard("transpose", {*$1}));
      delete($1);
    }
    | VSYMBOL '[' ex ']' {
      expression val = *$1;
      const symbol& s = ex_to<symbol>(*$1);

      try {
        if (params.compiler->has_value(s)) {
          val = params.compiler->get_value(s); // save some time
        } else {
          val = params.compiler->find_value_of(s);
        }
      } catch(std::exception &e) { (void)e; /* e.g. s does not have a value, ignore error */ }

      expression result = Functionmanager::create_hard("mindex", exprseq{val, *$3, -999});
      // TODO: This test will fail on vectors that contain functions as elements!
      if (is_a<func>(result)) result = Functionmanager::create_hard("mindex", exprseq{*$1, *$3, -999}); // mindex::eval() changed nothing
      $$ = new expression(result);
      delete ($1); delete($3);
    }
/*    | matrix '[' ex ',' ex ']' {
      $$ = new expression(func("mindex", mindex(*$1, *$3, *$5)));
      delete($1); delete($3); delete($5);
    }*/
    | MSYMBOL '[' ex ',' ex ']' {
      expression val = *$1;
      const symbol& s = ex_to<symbol>(*$1);

      try {
        if (params.compiler->has_value(s)) {
          val = params.compiler->get_value(s);
        } else {
          val = params.compiler->find_value_of(s);
        }
      } catch(std::exception &e) { (void)e; /* ignore (no value found) */ }

      expression result(Functionmanager::create_hard("mindex", exprseq{val, *$3, *$5}));
      if (is_a<func>(result)) result = Functionmanager::create_hard("mindex", exprseq{*$1, *$3, *$5}); // mindex::eval() changed nothing
      $$ = new expression(result);
      delete ($1); delete($3); delete($5);
    }
    | ex '!' {
      $$ = new expression(Functionmanager::create_hard("fact", {*$1}));
      delete($1);
    }
    | '-' ex %prec NEGATION { $$ = new expression(*$2 * _expr_1); delete($2); }
    | '+' ex %prec NEGATION { $$ = $2; }
    | ex '+' ex    { $$ = new expression((*$1 + *$3).evalm()); delete ($1); delete($3); }
    | ex '-' ex    { $$ = new expression((*$1 - *$3).evalm()); delete ($1); delete($3); }
    | ex PLUSMINUS ex { $$ = new expression(*$1 * *$3 * expression(stringex("+-"))); delete ($1); delete($3); }
    | ex MINUSPLUS ex { $$ = new expression(*$1 * *$3 * expression(stringex("-+"))); delete ($1); delete($3); }
    | ex '*' ex    { $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete($3); }
    | ex IMPMUL ex {
      if (hasLineOption(o_implicitmul) && (getLineOption(o_implicitmul).value.boolean == false) && !is_unit(*$3))
        MAKE_ERROR(@3, "Implicit multiplication is not allowed in this expression");
      $$ = new expression((*$1 * *$3).evalm()); delete ($1); delete ($3);
    }
    | ex TIMES ex  {
      if (check_anyvector(*$1) && check_anyvector(*$3))
        $$ = new expression(Functionmanager::create_hard("vecprod", exprseq{*$1, *$3}));
      else
        $$ = new expression((*$1 * *$3).evalm());
      delete ($1); delete ($3);
    }
		| ex HPRODUCT ex { // Hadamard product (element-wise)
      $$ = new expression(Functionmanager::create_hard("hadamard", exprseq{*$1, *$3, h_product}));
      delete ($1); delete($3);
		}
    | ex '/' ex    { $$ = new expression((*$1 / *$3).evalm()); delete ($1); delete($3); }
		| ex HDIVISION ex { // Hadamard division (element-wise)
      $$ = new expression(Functionmanager::create_hard("hadamard", exprseq{*$1, *$3, h_division}));
      delete ($1); delete($3);
		}
    | ex '^' exponent { $$ = new expression(pow(*$1, *$3).evalm()); delete ($1); delete ($3); }
    | ex superscript { $$ = new expression(pow(*$1, *$2).evalm()); delete ($1); delete ($2); }
		| ex HPOWER ex { // Hadamard product (element-wise)
      $$ = new expression(Functionmanager::create_hard("hadamard", exprseq{*$1, *$3, h_power}));
      delete ($1); delete($3);
		}
    | VALUE '(' ex ')' { // Calculate the value of this expression
      if (is_a<matrix>(*$3))
        $$ = new expression(calcvalueofmatrix(*$1, *$3, lst()));
      else
        $$ = new expression(calcvalue(*$1, *$3, lst()));

      must_autoformat = true;
      delete($1); delete($3);
    }
    | VALUEWITH '(' ex ',' eqlist ')' { // Find the value of this expression using an optional list of assignments to find it
      if (is_a<matrix>(*$3))
        $$ = new expression(calcvalueofmatrix(*$1, *$3, *$5));
      else
        $$ = new expression(calcvalue(*$1, *$3, *$5));

      must_autoformat = true;
      delete($1); delete($3); delete($5);
    }
    | ITERATE '(' eqlist ',' ex ',' ex ',' uinteger ')' { // One shift-reduce conflict if we write eq instead of eqlist
      // Iterate an equation
      if (!is_a<equation>($3->op(0)))
        MAKE_ERROR(@3, "Expected equation");
      const equation& e = ex_to<equation>($3->op(0));
      matrix syms(1,1); syms(0,0) = e.lhs();
      matrix exprs(1,1); exprs(0,0) = e.rhs();
      matrix start(1,1); start(0,0) = *$5;
      matrix conv(1,1); conv(0,0) = *$7;
      if (!$7->info(info_flags::real))
        MAKE_ERROR(@7, "All convergence criteria must be real numbers");

      matrix result = params.compiler->iterate(syms, exprs, start, conv, $9);
      $$ = new expression(result(0,0));
      must_autoformat = true;
      delete($3); delete($5); delete($7);
    }
    | ITERATE '(' eqlist ',' exvec ',' exvec ',' uinteger ')' {
      // Iterate a number of equations
      size_t rows = $3->nops();
      matrix syms ((unsigned int)rows, 1);
      matrix exprs((unsigned int)rows, 1);
      for (size_t eq = 0; eq < rows; ++eq) {
        if (!is_a<equation>($3->op(eq)))
          MAKE_ERROR(@3, "All elements must be equations");
        syms ((unsigned int)eq, 0) = ex_to<equation>($3->op(eq)).lhs();
        exprs((unsigned int)eq, 0) = ex_to<equation>($3->op(eq)).rhs();
      }

      matrix start((unsigned int)$5->size(), 1);
      matrix conv((unsigned int)$7->size(), 1);
      for (unsigned s = 0; s < $5->size(); ++s) start(s, 0) = (*$5)[s];
      for (unsigned s = 0; s < $7->size(); ++s) {
        if (!(*$7)[s].info(info_flags::real))
          MAKE_ERROR(@7, "All convergence criteria must be real numbers");
        conv(s, 0) = (*$7)[s];
      }

      if ((start.rows() == rows) && (conv.rows() == rows)) {
        $$ = new expression(params.compiler->iterate(syms, exprs, start, conv, $9));
      } else {
        MAKE_ERROR(@3, "All arguments must have the same number of elements");
      }
      must_autoformat = true;
      delete($3); delete($5); delete($7);
    }
;
condition: eq {
             const equation& eq = ex_to<equation>(*$1);
             $$ = new expression(dynallocate<relational>(eq.lhs(), eq.rhs(), eq.getop()));
             delete($1);
           }
           | combinedcondition { $$ = $1; }
;
combinedcondition:
           condition AND condition {
             $$ = new expression(Functionmanager::create_hard("ifelse", exprseq{*$1, Functionmanager::create_hard("ifelse", exprseq{*$3, 1, 0}), 0}));
             delete($1); delete($3);
           }
           | condition OR condition {
             $$ = new expression(Functionmanager::create_hard("ifelse", exprseq{*$1, 1, Functionmanager::create_hard("ifelse", exprseq{*$3, 1, 0})}));
             delete($1); delete($3);
           }
           | NEG condition {
             if ($2->info(info_flags::relation_equal))
               $$ = new expression(dynallocate<relational>($2->lhs(), $2->rhs(), relational::not_equal));
             else if ($2->info(info_flags::relation_not_equal))
               $$ = new expression(dynallocate<relational>($2->lhs(), $2->rhs(), relational::equal));
             else if ($2->info(info_flags::relation_less))
               $$ = new expression(dynallocate<relational>($2->lhs(), $2->rhs(), relational::greater_or_equal));
             else if ($2->info(info_flags::relation_greater))
               $$ = new expression(dynallocate<relational>($2->lhs(), $2->rhs(), relational::less_or_equal));
             else if ($2->info(info_flags::relation_less_or_equal))
               $$ = new expression(dynallocate<relational>($2->lhs(), $2->rhs(), relational::greater));
             else if ($2->info(info_flags::relation_greater_or_equal))
               $$ = new expression(dynallocate<relational>($2->lhs(), $2->rhs(), relational::less));
             else
               $$ = new expression(Functionmanager::create_hard("ifelse", exprseq{*$2, 0, 1}));
             delete($2);
           }
           | leftbracket combinedcondition rightbracket { $$ = $2; delete($1); delete($3); }
;

left: %empty  { $$ = new std::string(""); }
      | LEFT  { $$ = $1; }
;
right: %empty { $$ = new std::string(""); }
      | RIGHT { $$ = $1; }
;
leftbracket: left '('   { $$ = new std::string(*$1 + '('); delete($1); }
           | left '{'   { $$ = new std::string(*$1 + '{'); delete($1); }
           | left '['   { $$ = new std::string(*$1 + '['); delete($1); }
           | left BOPEN { $$ = new std::string(*$1 + ' ' + *$2); delete($1); delete($2); }
;
rightbracket: right ')'    { $$ = new std::string(*$1 + ')'); delete($1); }
            | right '}'    { $$ = new std::string(*$1 + '}'); delete($1); }
            | right ']'    { $$ = new std::string(*$1 + ']'); delete($1); }
            | right BCLOSE { $$ = new std::string(*$1 + ' ' + *$2); delete($1); delete($2); }
;

lowerbound: intvar '=' ex { $$ = new expression(dynallocate<equation>(*$1, *$3)); delete($1); delete($3); }
          | '{' intvar '=' ex '}' { $$ = new expression(dynallocate<equation>(*$2, *$4)); delete($2); delete($4);  }
;
intvar:   symbol
	| FUNC { $$ = new expression(params.compiler->create_function(*$1)); }
;
upperbound: number
          | '-' number { $$ = new expression(_expr_1 * *$2); delete($2); }
          | '+' number { $$ = $2; }
          | symbol
          | '{' ex '}' { $$ = $2; }
;

/* Vectors */
vector:   VSYMBOL { $$ = new expression(*$1); delete($1); }
        | STACK '{' lvector '}' {
          $$ = new expression(dynallocate<matrix>((unsigned int)$3->nops(), 1, *$3));
          delete($3);
        }
        | MATRIX '{' lvector '}' {
          matrix& m = dynallocate<matrix>(1, (unsigned int)$3->nops());
          for (unsigned i = 0; i < $3->nops(); i++) m(0, i) = $3->op(i);
          $$ = new expression(m);
          delete($3);
        }
        | MSYMBOL '[' ex ',' '*' ']' {
          expression val = *$1;
          const symbol& s = ex_to<symbol>(*$1);

          try {
            if (params.compiler->has_value(s)) {
              val = params.compiler->get_value(s);
            } else {
              val = params.compiler->find_value_of(s);
            }
          } catch(std::exception &e) { (void)e; } // ignore (no value found)

          expression result = Functionmanager::create_hard("mindex", exprseq{val, *$3, wild()});
          if (is_a<func>(result)) result = Functionmanager::create_hard("mindex", exprseq{*$1, *$3, wild()}); // mindex::eval() changed nothing
          $$ = new expression(result);
          delete ($1); delete($3);
        }
        | MSYMBOL '[' '*' ',' ex ']' {
          expression val = *$1;
          const symbol& s = ex_to<symbol>(*$1);

          try {
            if (params.compiler->has_value(s)) {
              val = params.compiler->get_value(s);
            } else {
              val = params.compiler->find_value_of(s);
            }
          } catch(std::exception &e) { (void)e; } // ignore (no value found)

          expression result = Functionmanager::create_hard("mindex", exprseq{val, wild(), *$5});
          if (is_a<func>(result)) result = Functionmanager::create_hard("mindex", exprseq{*$1, wild(), *$5}); // mindex::eval() changed nothing
          $$ = new expression(result);
          delete ($1); delete($5);
        }
        | ex ':' ex {
          if (!is_a<matrix>(*$1)) {
            // Create vector, automatic step size
            unsigned size = (current_options->at(o_vecautosize)).value.uinteger;
            if (size < 2) size = 20; // Prevent nonsensical values
            expression step = expression(*$3 - *$1) / expression(size - 1);
            matrix& m = dynallocate<matrix>(size, 1);
            for (unsigned i = 0; i < size-1; i++) m(i, 0) = *$1 + expression(step * i);
            m(size-1, 0) = *$3; // Make last element be without numerical errors
            $$ = new expression(m);
            MSG_INFO(2, "Resulting vector with auto step: " << *$$ << endline);
          } else {
            // Create vector with user-defined step. Note that this will re-size an existing vector, to allow for b:e:s syntax
            expression step = *$3;
            if (!is_a<matrix>(*$1) || ex_to<matrix>(*$1).cols() > 1)
              MAKE_ERROR(@1, "Column vector expected");
            matrix v = ex_to<matrix>(*$1);
            expression esize = (expression(v(v.rows()-1, 0) - v(0,0)) / step + 1).evalf();

            if (!esize.info(info_flags::positive)) {
              MAKE_ERROR(@3, "The number of vector elements resulting from the step must be a positive value");
            } else {
              unsigned size = numeric_to_uint(ex_to<numeric>(esize));
              if (ex_to<numeric>(esize).to_double() - size > 1E-2) size++; // TODO: The 1E-2 is arbitrary here
              matrix& m = dynallocate<matrix>(size, 1);
              for (unsigned i = 0; i < size-1; i++) m(i, 0) = expression(v(0,0)) + expression(step * i);
              m(size-1, 0) = expression(v(v.rows()-1, 0));
              $$ = new expression(m);
              MSG_INFO(2, "Resulting vector with user-defined step: " << *$$ << endline);
            }
          }
          delete($1); delete($3);
        }
        | leftbracket exvec rightbracket {
          if (!checkbrackets(*$1, *$3))
            MAKE_ERROR(@3, "Bracket type mismatch");
          matrix& m = dynallocate<matrix>((unsigned int)$2->size(), 1);
          for (unsigned i = 0; i < $2->size(); ++i) m(i, 0) = (*$2)[i];
          $$ = new expression(m);
          delete($1); delete($2); delete($3);
        }
;
exvec:     ex ';' ex      {
           $$ = new exvector{*$1, *$3};
           delete($1); delete($3); }
         | exvec ';' ex  { $$ = $1; $$->emplace_back(*$3); delete($3); }
;
lvector:   ex '#' ex      {
           $$ = new lst{*$1, *$3};
           delete($1); delete($3);
        }
         | lvector '#' ex { $$ = $1; $$->append(*$3); delete($3); }
;
exvec_or_ex:  exvec {
							matrix& m = dynallocate<matrix>((unsigned int)$1->size(), 1);
							for (unsigned i = 0; i < $1->size(); ++i) m(i, 0) = (*$1)[i];
							$$ = new expression(m);
							delete($1);
						}
						| ex {
							$$ = $1;
						}
;

/* Matrices */
matrix:   MSYMBOL { $$ = new expression(*$1); delete($1); }
        | MATRIX '{' lmatrix '}' {
          matrix& m = dynallocate<matrix>((unsigned int)$3->nops(), (unsigned int)$3->op(0).nops());

          for (unsigned r = 0; r < $3->nops(); r++) {
            for (unsigned c = 0; c < $3->op(r).nops(); c++) {
              m(r, c) = $3->op(r).op(c);
            }
          }

          $$ = new expression(m);
          delete($3);
        }
;
lmatrix:   lvector DOUBLEHASH lvector {
           $$ = new lst{*$1, *$3};
           delete($1); delete($3); }
         | lmatrix DOUBLEHASH lvector { $$ = $1; $$->append(*$3); delete($3); }
;

/* Symbols */
symbol:   SYMBOL
        | IDENTIFIER { $$ = params.compiler->getsymp(params.compiler->varname_ns(*$1)); delete ($1); }
;
vsymbol:  VSYMBOL
        | IDENTIFIER { $$ = params.compiler->getsymp(params.compiler->varname_ns(*$1), p_vector); delete ($1); }
        | SYMBOL {
          // Redefine symbol type. Only VECTORDEF is allowed to do this!
          std::string sname = ex_to<symbol>(*$1).get_name();
          params.compiler->setsymprop(sname, p_vector);
          $$ = params.compiler->getsymp(sname);
          delete ($1);
        }
;
msymbol:  MSYMBOL
        | IDENTIFIER { $$ = params.compiler->getsymp(params.compiler->varname_ns(*$1), p_matrix); delete ($1); }
        | SYMBOL {
          // Redefine symbol type. Only MATRIXDEF is allowed to do this!
          std::string sname = ex_to<symbol>(*$1).get_name();
          params.compiler->setsymprop(sname, p_matrix);
          $$ = params.compiler->getsymp(sname);
          delete ($1);
        }
;
rsymbol:  IDENTIFIER { $$ = params.compiler->getsymp(params.compiler->varname_ns(*$1), p_real); delete ($1); }
        | SYMBOL {
          std::string sname = ex_to<symbol>(*$1).get_name();
          params.compiler->setsymprop(sname, p_real);
          $$ = params.compiler->getsymp(sname);
          delete ($1);
        }
;
psymbol:  IDENTIFIER { $$ = params.compiler->getsymp(params.compiler->varname_ns(*$1), p_pos); delete ($1); }
        | SYMBOL {
          std::string sname = ex_to<symbol>(*$1).get_name();
          params.compiler->setsymprop(sname, p_pos);
          $$ = params.compiler->getsymp(sname);
          delete ($1);
        }
;
gsymbol: symbol | VSYMBOL | MSYMBOL;

simplifications: STRING {
       //MSG_INFO(3,  "Simplification " << *$1 << endline);
       $$ = new std::vector<std::string>;
       $$->emplace_back(*$1);
       delete($1);
     }
     | simplifications ';' STRING {
       $1->emplace_back(*$3);
       $$ = $1;
       delete ($3);
     }
;

exponent: number
          | '-' number { $$ = new expression(_expr_1 * *$2); delete($2); }
          | '+' number { $$ = $2; }
          | symbol
          | leftbracket ex rightbracket {
            $$ = $2; delete($1); delete($3);
          }
;
number: DIGITS { $$ = new expression(dynallocate<numeric>($1->c_str())); delete($1); }
        | DIGITS '.' DIGITS {
          $1->append("." + *$3);
          $$ = new expression(dynallocate<numeric>($1->c_str()));
          delete($1); delete ($3);
        }
;

uinteger: DIGITS { $$ = atoi($1->c_str()); delete($1); }
;

superscript: SUPERSCRIPT {
            $$ = new expression(dynallocate<numeric>($1->c_str()));
            delete($1);
          }
          | SUPERMINUS superscript %prec NEGATION { $$ = new expression(_expr_1 * *$2); delete($2); }
          | SUPERPLUS superscript %prec NEGATION { $$ = $2; }
;

sizestr:    DIGITS
          | '-' DIGITS { $2->insert(0, "-"); $$ = $2; }
          | '+' DIGITS { $$ = $2; }
;
%%

/// The `error' member function registers the errors to the formula
// Note: This function is called after throwing a syntax error, and then the parser finishes and returns a value > 0
// But we want to continue with YYERROR, thus no syntax errors should be thrown
void imath::smathparser::error(const imath::location& l, const std::string& m) {
  errorlocation = l;
  --errorlocation.begin.column; // Reason for this is unknown ...
  --errorlocation.end.column;
  errormessage = OUS8(m);
}
