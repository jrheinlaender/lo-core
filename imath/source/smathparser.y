/***************************************************************************
    smathparser.yy  -  rules for reading smath formulas
    - parser generation file for bison
                             -------------------
    begin                : Wed May 21 2008
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

%language "C++"
%skeleton "lalr1.cc"
%require "3.5.0"
%defines
%define api.namespace {imath}
%define api.parser.class {smathparser}
%expect 1
// Enable location tracking
%locations
// enable parser checks, parser tracing, verbose error messages and lookahead correction for better syntax error handling
%define parse.assert
%define parse.trace
%define parse.error detailed
%define parse.lac full
// Standard bison uses a C union, therefore types would have to be trivial. Specifically, std::shared_ptr is not allowed
%define api.value.type variant
// Replace every occurrence of $x with std::move($x)
%define api.value.automove

// The parsing context. It must match YY_DECL in imathparser.hxx
%code requires
{
	namespace imath {
		class imathparse;
    struct parserParameters;
	}
}

%parse-param { imath::parserParameters& params }
%lex-param   { std::shared_ptr<eqc> compiler }
%lex-param   { unsigned include_level }

// Which rule the grammar starts at
%start input

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
#include <smathlexer.hxx>

#undef yylex
#define yylex params.lexer->lex
%}

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
  // Must the current line be auto-formatted?
  must_autoformat = false;
  // Initialize stack
  bracketstack = {};
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

// The code between `%{' and `%}' after the introduction of the `%union'
// is output in the `*.cc' file; it needs detailed knowledge about the params.
%{
  using namespace GiNaC;

  // The current line of the iFormula which we are working on
  iFormulaLine_ptr line;
  // Pointer to the compiler, because the lexer interface cannot handle params.compiler
  // and the auxiliary methods in this file have no access to it either
  std::shared_ptr<eqc> compiler;
  // The current options
  std::shared_ptr<optionmap> current_options;
  // Options that have been parsed for the current line, before the line is actually created
  optionmap line_options;
  // Must the current line be auto-formatted?
  bool must_autoformat;
  // Stack of brackets to check match
  std::stack<std::pair<std::string, std::string>> bracketstack;
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

// Save space in make_shared<iFormulaLine>() calls
using fparts = std::initializer_list<OUString>;
using titems = std::initializer_list<std::shared_ptr<textItem>>;

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
     value = compiler->find_numval_of(var, assignments);
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

// Check for matching brackets. If there is a mismatch, returns the expected closing bracket. Otherwise returns an empty string
std::string checkbrackets(const std::string& sizing, const std::string& bracket) {
  static const std::map<std::string, std::string> matches = {
    {"(", ")"},
    {"{", "}"},
    {"[", "]"},
    {"\\{", "\\}"},
    {"ldbracket", "rdbracket"},
    {"ldline", "rdline"},
    {"lbrace", "rbrace"},
    {"langle", "rangle"},
    {"lceil", "rceil"},
    {"lfloor", "rfloor"},
    {"lline", "rline"}
  };
  MSG_INFO(3,  "checkbrackets() sizing=" << sizing << ", bracket=" << bracket << endline);
  if (bracketstack.empty())
    return "no bracket";

  std::string expected_sizing("");
  if (bracketstack.top().first == "left")
    expected_sizing = "right";

  auto it = matches.find(bracketstack.top().second);
  if (it == matches.end())
    return "no bracket";

  if (sizing != expected_sizing || it->second != bracket)
    return expected_sizing + (expected_sizing.empty() ? "" : " ") + it->second;

  bracketstack.pop();
  return "";
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
  return (line_options.find(o) != line_options.end()) ||
         ((current_options != nullptr) && (current_options->find(o) != current_options->end()));
}

option getLineOption(const option_name& o) {
  if (line_options.find(o) != line_options.end())
    return line_options.at(o);
  else
    return current_options->at(o);
}

bool check_label(const std::string& label, std::string& nslabel) {
  nslabel = compiler->label_ns(label);

  while (compiler->is_label(nslabel)) {
    if (!autorenumberduplicate)
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

  it_option = line_options.find(o_units);
  if (it_option != line_options.end()) { // There are units local to this line
    for (const auto& o : *it_option->second.value.exvec)
      units.append(o);

    if (msg::info().checkprio(3))
      for (const auto& u : *it_option->second.value.exvec)
        msg::info() <<  "Unit LOCAL: " << u << endline;
  }

  return compiler->create_conversions(units, true);
}

// Note: handle_error() assumes that global variables errormessage and errorlocation have been set properly
void handle_error(imath::parserParameters& params, const std::shared_ptr<iFormulaLine>& l, const imath::location& formulaStart) {
  if (include_level == 0) {
    params.lines.push_back(l);
    int fStart = formulaStart.begin.column - (params.rawtext[formulaStart.begin.column - 1] == ' ' ? 0 : 1); // Blank after keyword is added automatically in iFormulaLine::print()
    if (errorlocation.begin.column < fStart) errorlocation.begin.column = fStart;
    params.lines.back()->markError(params.rawtext, fStart, errorlocation.begin.column, errorlocation.end.column, errormessage);
  } else {
    errormessage += ": At line " + OUString::number(errorlocation.begin.line) + ", column " + OUString::number(errorlocation.begin.column);
    while (!locationstack.empty()) {
      std::string parseString = *locationstack.top().begin.filename; // This is %%ii READFILE {"<file name>"}
      errormessage = "Error in include file " + OUS8(parseString.substr(parseString.find("{\"") + 2, parseString.find("\"}") - parseString.find("{\"") - 2)) + "\n" + errormessage;
      locationstack.pop();
    }
    params.lines.back()->markError(params.rawtext, params.rawtext.indexOfAsciiL("{\"", 2), params.rawtext.indexOfAsciiL("{\"", 2) + 2, params.rawtext.indexOfAsciiL("\"}", 2), errormessage);
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

%}

// Token declarations -------------------------------------------------------

%token ENDSTRING               "end of line"
%token NEWLINE                 "newline"
// Basic tokens
%token <std::string>  BOPEN    "left bracket"
%token <std::string>  BCLOSE   "right bracket"
%token <std::string>  LEFT     "left"
%token <std::string>  RIGHT    "right"
%token <std::string>  DIGITS   "digits"
%token        DOUBLEHASH       "##"
%token        TRANSPOSE        "^T"
%token        NEQ              "not equal"
%token        EQUIV            "equiv"
%token        MOD              "mod"
%token        BMOD             "(mod)"
%token <bool> BOOL             "true/false"
%token        NROOT            "nroot"
%token        QUO              "quo"
%token        REM              "rem"
%token        GCD              "gcd"
%token        LCM              "lcm"
%token        AND              "and"
%token        OR               "or"
%token        NEG              "neg"
%token        FROM	           "from"
%token        TO               "to"
// Statements
%token	      READFILE	       "READFILE"
%token        OPTIONS          "OPTIONS"
%token        BEGIN_NS         "begin namespace"
%token        END_NS           "end namespace"
%token	      FUNCTION	       "FUNCTION"
%token        UNITDEF          "UNITDEF"
%token        PREFIXDEF        "PREFIXDEF"
%token        VECTORDEF        "VECTORDEF"
%token        MATRIXDEF        "MATRIXDEF"
%token        REALVARDEF       "REALVARDEF"
%token        POSVARDEF        "POSVARDEF"
%token        CLEAREQUATIONS   "CLEAREQUATIONS"
%token	      DELETE           "DELETE"
%token        UPDATE           "UPDATE"
%token        END 0            "end of file" // The token numbered as 0 corresponds to end of file, this gives nicer error messages
// Expressions
%token        ATTRIBUTE            "attribute"
%token <std::string>  IDENTIFIER   "identifier"
%token <std::string>  LABEL        "label"
%token <std::string>  EXLABEL      "expression label" // Must be separate from LABEL because of conflicts in eq: LABEL
%token <std::string>  STRING       "quoted string"
%token <std::string>  OPERATOR     "operator"
%token <std::string>  COMMENT      "%%"
%token        GENERATED            "%%gg"
%token <std::string>  UNIT         "unit name"
%token <std::string>  FUNC         "function name"
%token        WILD                 "wild"
%token        STACK                "STACK"
%token        MATRIX               "MATRIX"
%token        LHS   	             "LHS"
%token        RHS	                 "RHS"
%token <std::string>  VALUE        "evaluation"
%token <std::string>  VALUEWITH	   "evaluation with parameters"
%token        ITERATE              "ITERATE"
%token <GiNaC::expression> SYMBOL  "variable name"
%token <GiNaC::expression> VSYMBOL "vector name"
%token <GiNaC::expression> MSYMBOL "matrix name"
%token        SUMFROM              "sum"
%token        PRODUCT              "PRODUCT"
%token        INTEGRAL             "INT"
%token <std::string>  DIFFERENTIAL "differential"
%token        IMPMUL               "implicit multiplication"
%token <std::string>  SUPERSCRIPT  "SUPERSCRIPT"
%token        SUPERPLUS            "+"
%token        SUPERMINUS           "-"
%token        PLUSMINUS            "+-"
%token        MINUSPLUS            "-+"
%token        TIMES                "times"
%token				HPRODUCT				     "hadamard product"
%token				HDIVISION				     "hadamard division"
%token				HPOWER					     "hadamard exponentiation"
%token	      EXDEF	               "EXDEF"
%token <std::string> PRINTVALUE    "print value"
%token <std::string> PRINTVALUEWITH "print value with parameters"
%token				EXPLAINVAL		 	     "EXPLAINVAL"
%token        EQDEF                "EQDEF"
%token	      CONSTDEF	           "CONSTDEF"
%token	      FUNCDEF	             "FUNCDEF"
%token	      CHART                "CHART"
%token        ADDSERIES            "ADDSERIES"
%token        SETTABLECELL         "SETTABLECELL"
%token        SETCALCCELLS         "SETCALCCELLS"
%token        TEXT                 "TEXT"
%token				MAGIC						     "_ii_"
%token	      SOLVE                "SOLVE"
%token        SUBST                "SUBST"
%token        SUBSTC               "SUBSTC"
%token        SUBSTV               "SUBSTV"
%token        SUBSTVC              "SUBSTVC"
%token	      SIMPLIFY             "SIMPLIFY"
%token        COLLECT              "COLLECT"
%token        REVERSE              "REV"
%token	      DIFFERENTIATE        "DIFFERENTIATE"
%token	      PDIFFERENTIATE       "PDIFFERENTIATE"
%token	      INTEGRATE            "INTEGRATE"
%token	      TSERIES              "TSERIES"
%token        TEXTFIELD            "TEXTFIELD"
%token        TABLECELL            "TABLECELL"
%token        CALCCELL             "CALCCELL"
%token        SIZE                 "SIZE"
%token <option_name>  OPT_L        "unit list option"
%token <option_name>  OPT_U        "unsigned option"
%token <option_name>  OPT_I        "integer option"
%token <option_name>  OPT_B        "boolean option"
%token <option_name>  OPT_S        "string option"

// Operator declarations ----------------------------------------------
// Note that "bison -W" warns about most of these being useless, but they are here anyway, for reference
// Lowest precedence
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
// Highest precedence

// Print special types ----------------
%printer { std::copy($$.begin(), $$.end(), std::ostream_iterator<std::string>(debug_stream(),"\n")); } <strvec>
%printer { debug_stream() << $$; } <unsigned> <option_name> <bool>

%%
// Input management
input:   %empty
       | input options statement {
           if (line != nullptr)
            for (const auto& o : $2)
              line->setOption(o.first, o.second); // Actually only option echo is relevant for statements
       } comment end
       | input options TEXT usertext { // User-defined text after %%ii {options} TEXT
          if (include_level == 0) {
            std::vector<OUString> formulaParts = {OUS8(rawtext.substr(@4.begin.column-1, @4.end.column-@4.begin.column))}; // not GETARG because it trims the string
            params.lines.push_back(std::make_shared<iFormulaNodeText>(unitConversions(), current_options, $2, std::move(formulaParts), $4));
            line = params.lines.back();
            line_options.clear();
          }
       } comment end
       | input options TEXT error {
          handle_error(params, std::make_shared<iFormulaNodeText>(unitConversions(), current_options, $2, fparts({}), titems({})), @4);
          YYABORT;
        }
       | input usertext { // user-defined text on a line by itself. The end removes 3 shift/reduce conflicts
          if (include_level == 0) {
            params.lines.push_back(std::make_shared<iFormulaNodeText>(unitConversions(), current_options, optionmap(), fparts({GETARG(@2)}), $2));
            line = params.lines.back();
            line_options.clear();
          }
        } comment end
	     | input expr comment end { /* all the work is done in expr */ }
       | input GENERATED end { /* ignore auto-generated lines */ }
       | input COMMENT { // comment
          if (include_level == 0) { // Don't copy comments from include files!
            params.lines.push_back(std::make_shared<iFormulaNodeComment>(current_options, fparts({OUS8($2)})));
            line = params.lines.back();
            line_options.clear();
          }
       } end
       | input READFILE '{' STRING '}' { // We can't use the normal 'end' rule here because it will be called before the include file is parsed
         // TODO: Parse string as an Openoffice URL so that include files are system-independent? But then relative URLS must be possible!
         if (include_level == 0) {
           params.lines.push_back(std::make_shared<iFormulaNodeStmReadfile>(current_options, fparts({OU("{"), GETARG(@4), OU("}")})));
           line = params.lines.back();
           line_options.clear();
         }
         std::string filename = $4;
         if (include_level > 0)
           yyla.location.begin.filename = yyla.location.end.filename = new std::string("%%ii READFILE {\"" + filename + "\"}"); // The filename is only set once (for include_level == 0) in the initial-action
         ++include_level;
	       locationstack.push(yyla.location);
	       yyla.location = location();
         Reference<XStorable> xStorable(params.xDocumentModel, UNO_QUERY_THROW);
         OUString documentURL = xStorable->getLocation();
         Reference< XComponentContext> componentContext = params.xContext;
         OUString result = makeURLFor(OUS8(filename), documentURL, componentContext);
         std::string fpath = STR(makeSystemPathFor(result, componentContext));

	       MSG_INFO(0,  "Trying to open " << fpath << endline);
         if (!params.lexer->begin_include(fpath)) {
           MSG_INFO(3,  "File " << fpath << " not found" << endline);
           errormessage = "Could not open include file " + OUS8(fpath);
           locationstack.pop(); // Remove entry that was just created, and handle the rest
           while (!locationstack.empty()) {
              std::string parseString = *locationstack.top().begin.filename; // This is %%ii READFILE {"<file name>"}
              errormessage = "Error in include file " + OUS8(parseString.substr(parseString.find("{\"") + 2, parseString.find("\"}") - parseString.find("{\"") - 2)) + "\n" + errormessage;
              locationstack.pop();
           }
           params.lines.back()->markError(params.rawtext, @3.begin.column - 1, params.rawtext.indexOfAsciiL("{\"", 2) + 2, params.rawtext.indexOfAsciiL("\"}", 2), errormessage);

           YYABORT;
         }

	       params.cacheable = false;
	     }
	     | input end { // Empty line
          if (include_level == 0) {
            params.lines.push_back(std::make_shared<iFormulaNodeEmptyLine>(current_options));
            line = params.lines.back();
            line_options.clear();
          }
       }
       | input error {
          auto loc = @2;
          loc.begin.column += 5; // Skip the %%i
          handle_error(params, std::make_shared<iFormulaNodeError>(current_options, params.rawtext), loc);
          YYABORT;
       }
;

%type <std::vector<std::shared_ptr<textItem>>> usertext "text";
usertext: STRING  {
            OUString text = OUS8($1).trim();
            if (!text.isEmpty())
              $$.push_back(std::make_shared<textItem>(text));
          }
          | OPERATOR {
						$$.push_back(std::make_shared<textItem>(OUS8($1), true));
					}
					| MAGIC ex MAGIC {
						$$.push_back(std::make_shared<textItem>($2));
					}
          | NEWLINE {
            $$.push_back(std::make_shared<textItem>(OU("newline")));
          }
          | usertext STRING   {
            $$ = $1;
            OUString text = OUS8($2).trim();
            if (!text.isEmpty())
            $$.push_back(std::make_shared<textItem>(text));
          }
          | usertext OPERATOR {
            $$ = $1;
            $$.push_back(std::make_shared<textItem>(OUS8($2), true));
          }
					| usertext MAGIC ex MAGIC {
            $$ = $1;
						$$.push_back(std::make_shared<textItem>($3));
					}
          | usertext NEWLINE  { $$ = $1; $$.push_back(std::make_shared<textItem>(OU("newline"))); }
;

end:        '\n' {
              // propagate the options and units to a new line
              if (line != nullptr)
                current_options = line->getGlobalOptions();
              line_options.clear();
							// Reset internal options to global value
							set_inhibit_floating_point_underflow(current_options->at(o_underflow).value.boolean);
							MSG_INFO(3, "Inhibit floating point underflow exception: " << (current_options->at(o_underflow).value.boolean ? "true" : "false") << endline);
							expression::evalf_real_roots_flag = (current_options->at(o_evalf_real_roots).value.boolean);
							MSG_INFO(3, "Evaluate odd negative roots to the positive real value: " << (current_options->at(o_evalf_real_roots).value.boolean ? "true" : "false") << endline);
            }
            | ENDSTRING {
              if (params.lexer->finish_include()) return(0);

              --include_level;
              yyla.location = locationstack.top();
              locationstack.pop();

              if (line != nullptr)
                current_options = line->getGlobalOptions();
              line_options.clear();
							// Reset internal options to global value
							set_inhibit_floating_point_underflow(current_options->at(o_underflow).value.boolean);
							MSG_INFO(3, "Inhibit floating point underflow exception: " << (current_options->at(o_underflow).value.boolean ? "true" : "false") << endline);
							expression::evalf_real_roots_flag = (current_options->at(o_evalf_real_roots).value.boolean);
							MSG_INFO(3, "Evaluate odd negative roots to the positive real value: " << (current_options->at(o_evalf_real_roots).value.boolean ? "true" : "false") << endline);
	            // continue parsing
            }
            | '\n' ENDSTRING { // One shift-reduce conflict (but writing end instead of \n will produce 8 conflicts!)
							if (params.lexer->finish_include()) return(0);

              --include_level;
              yyla.location = locationstack.top();
              locationstack.pop();

              if (line != nullptr)
                current_options = line->getGlobalOptions();
              line_options.clear();
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

           for (const auto& o : $2) {
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
             params.lines.push_back(std::make_shared<iFormulaNodeStmOptions>(current_options, fparts({GETARG(@2)})));
             line = params.lines.back();
             line_options.clear();
           }

           params.cacheable = false;
         }
         | OPTIONS error {
            handle_error(params, std::make_shared<iFormulaNodeStmOptions>(current_options, fparts({})), @2);
            YYABORT;
         }
         | BEGIN_NS IDENTIFIER {
           params.compiler->begin_namespace($2);

           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmNamespace>(current_options, OU("BEGIN"), fparts({GETARG(@2)})));
             line = params.lines.back();
             line_options.clear();
           }
         }
         | BEGIN_NS error {
            handle_error(params, std::make_shared<iFormulaNodeStmNamespace>(current_options, OU("BEGIN"), fparts({})), @2);
            YYABORT;
         }
         | END_NS IDENTIFIER {
           params.compiler->end_namespace($2);

           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmNamespace>(current_options, OU("END"), fparts({GETARG(@2)})));
             line = params.lines.back();
             line_options.clear();
           }
         }
         | END_NS error {
            handle_error(params, std::make_shared<iFormulaNodeStmNamespace>(current_options, OU("END"), fparts({})), @2);
            YYABORT;
          }
         | FUNCTION '{' funchints ',' gsymbol ',' ex '}' {
           // Using IDENTIFIER does not work because the symbol might have been used in a library function previously, and even
           // CLEAREQUATIONS won't remove it then
           // Must register function first because the iFormulaNodeStmFunction needs it
           std::string fname = ex_to<symbol>($5).get_name();
           size_t pos = fname.find_last_of("::");
           std::string printname = (pos == std::string::npos ? fname : fname.substr(pos+1));
           params.compiler->register_function(fname, {$7}, $3, printname);
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU("}")};
             params.lines.push_back(std::make_shared<iFormulaNodeStmFunction>(current_options, std::move(formulaParts), params.compiler->create_function(fname)));
             line = params.lines.back();
             line_options.clear();
           }
           params.cacheable = false;
         }
         | FUNCTION '{' funchints ',' gsymbol ',' exvec '}' {
           std::string fname = ex_to<symbol>($5).get_name();
           size_t pos = fname.find_last_of("::");
           std::string printname = (pos == std::string::npos ? fname : fname.substr(pos+1));
           params.compiler->register_function(fname, $7, $3, printname);
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU("}")};
             params.lines.push_back(std::make_shared<iFormulaNodeStmFunction>(current_options, std::move(formulaParts), params.compiler->create_function(fname)));
             line = params.lines.back();
             line_options.clear();
           }
           params.cacheable = false;
         }
         | FUNCTION '{' funchints ',' STRING ',' gsymbol ',' ex '}' {
           std::string fname = ex_to<symbol>($7).get_name();
           params.compiler->register_function(fname, {$9}, $3, $5);
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU(","), GETARG(@9), OU("}")};
             params.lines.push_back(std::make_shared<iFormulaNodeStmFunction>(current_options, std::move(formulaParts), params.compiler->create_function(fname)));
             line = params.lines.back();
             line_options.clear();
           }
           params.cacheable = false;
         }
         | FUNCTION '{' funchints ',' STRING ',' gsymbol ',' exvec '}' {
           std::string fname = ex_to<symbol>($7).get_name();
           params.compiler->register_function(fname, $9, $3, $5);
           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU(","), GETARG(@9), OU("}")};
             params.lines.push_back(std::make_shared<iFormulaNodeStmFunction>(current_options, std::move(formulaParts), params.compiler->create_function(fname)));
             line = params.lines.back();
             line_options.clear();
           }
           params.cacheable = false;
         }
         | FUNCTION '{' error {
           errormessage = OU("Function options must be enclosed in {}, empty options are represented by {none}");
           handle_error(params, std::make_shared<iFormulaNodeStmFunction>(current_options, fparts({}), params.compiler->create_function("square")), @2);
           YYABORT;
         }
         | FUNCTION error {
            handle_error(params, std::make_shared<iFormulaNodeStmFunction>(current_options, fparts({}), params.compiler->create_function("square")), @2);
            YYABORT;
          }
         | UNITDEF '{' STRING ',' unitname '=' ex '}' {
           params.compiler->addUnit($5, $3, $7);

           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU("="), GETARG(@7), OU("}")};
             params.lines.push_back(std::make_shared<iFormulaNodeStmUnitdef>(current_options, std::move(formulaParts)));
             line = params.lines.back();
             line_options.clear();
           }

           params.cacheable = false;
         }
         | UNITDEF error {
            handle_error(params, std::make_shared<iFormulaNodeStmUnitdef>(current_options, fparts({})), @2);
            YYABORT;
          }
         | PREFIXDEF '{' prefixname '=' numeric_ex '}' {
           params.compiler->addPrefix($3, ex_to<numeric>($5));

           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU("="), GETARG(@5), OU("}")};
             params.lines.push_back(std::make_shared<iFormulaNodeStmPrefixdef>(current_options, std::move(formulaParts)));
             line = params.lines.back();
             line_options.clear();
           }

           params.cacheable = false;
         }
         | PREFIXDEF error {
            handle_error(params, std::make_shared<iFormulaNodeStmPrefixdef>(current_options, fparts({})), @2);
            YYABORT;
         }
         | VECTORDEF vsymbol {
           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmVectordef>(current_options, fparts({GETARG(@2)})));
             line = params.lines.back();
             line_options.clear();
           }
           params.cacheable = false;
         }
         | VECTORDEF error {
            handle_error(params, std::make_shared<iFormulaNodeStmVectordef>(current_options, fparts({})), @2);
            YYABORT;
          }
         | MATRIXDEF msymbol {
           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmMatrixdef>(current_options, fparts({GETARG(@2)})));
             line = params.lines.back();
             line_options.clear();
           }
           params.cacheable = false;
         }
         | MATRIXDEF error {
            handle_error(params, std::make_shared<iFormulaNodeStmMatrixdef>(current_options, fparts({})), @2);
            YYABORT;
          }
         | REALVARDEF rsymbol {
           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmRealvardef>(current_options, fparts({GETARG(@2)})));
             line = params.lines.back();
             line_options.clear();
           }
           params.cacheable = false;
         }
         | REALVARDEF error {
            handle_error(params, std::make_shared<iFormulaNodeStmRealvardef>(current_options, fparts({})), @2);
            YYABORT;
          }
         | POSVARDEF psymbol {
           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmPosvardef>(current_options, fparts({GETARG(@2)})));
             line = params.lines.back();
             line_options.clear();
           }
           params.cacheable = false;
         }
         | POSVARDEF error {
            handle_error(params, std::make_shared<iFormulaNodeStmPosvardef>(current_options, fparts({})), @2);
            YYABORT;
          }
         | CLEAREQUATIONS {
           params.compiler->clear();

           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmClearall>(current_options));
             line = params.lines.back();
             line_options.clear();
           }

           params.cacheable = false;
         }
         | DELETE '{' labellist '}' {
           for (const auto& i : $3)
             params.compiler->deleq(i);

           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmDelete>(current_options, fparts({OU("{"), GETARG(@3), OU("}")})));
             line = params.lines.back();
             line_options.clear();
           }

           params.cacheable = false;
         }
         | DELETE error {
            handle_error(params, std::make_shared<iFormulaNodeStmDelete>(current_options, fparts({})), @2);
            YYABORT;
          }
         | UPDATE STRING {
           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmUpdate>(current_options, fparts({GETARG(@2)})));
             line = params.lines.back();
             line_options.clear();
           }
           params.updateFormulas.push_back(OUS8($2));
           params.cacheable = false;
         }
         | UPDATE error {
            handle_error(params, std::make_shared<iFormulaNodeStmUpdate>(current_options, fparts({})), @2);
            YYABORT;
          }
         | CHART '{' STRING ',' colvec_ex ',' ex ',' colvec_ex ',' ex ',' uinteger ',' STRING '}' {
           std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU(","), GETARG(@9), OU(","), GETARG(@11), OU(","), GETARG(@13), OU(","), GETARG(@15), OU("}")};
           if (!checkHasChartsAndTables(params.xDocumentModel)) {
             error(@1, "This document type does not support the CHART statement");
             handle_error(params, std::make_shared<iFormulaNodeStmChart>(current_options, std::move(formulaParts)), @2);
             YYABORT;
           }

           if (!isGlobalDocument(params.xDocumentModel)) { // Access to chart data throws an exception for global documents
            expression vec1 = expression($9 / $11).evalm();
            auto chartname = $3;
            auto xvector = $5;
            auto snum = $13;
            if (xvector.info(info_flags::integer)) {
              setChartData(params.xDocumentModel, OUS8(chartname), ex_to<matrix>(vec1), std::move(snum));
            } else {
              expression vec2 = expression(std::move(xvector) / $7).evalm();
              setChartData(params.xDocumentModel, OUS8(chartname), ex_to<matrix>(vec2), ex_to<matrix>(vec1), snum);
            }
            setSeriesDescription(params.xDocumentModel, OUS8(std::move(chartname)), OUS8($15), snum == 1 ? 1 : snum - 1);
           }

           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmChart>(current_options, std::move(formulaParts)));
             line = params.lines.back();
             line_options.clear();
           }
         }
         | CHART '{' STRING ',' symbol '=' colvec_ex ',' ex ',' eq ',' ex ',' uinteger ',' STRING '}' {
           std::vector<OUString> formulaParts =
               {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU("="), GETARG(@7), OU(","), GETARG(@9), OU(","), GETARG(@11), OU(","), GETARG(@13), OU(","), GETARG(@15), OU(","), GETARG(@17), OU("}")};
           if (!checkHasChartsAndTables(params.xDocumentModel)) {
             error(@1, "This document type does not support the CHART statement");
             handle_error(params, std::make_shared<iFormulaNodeStmChart>(current_options, std::move(formulaParts)), @2);
             YYABORT;
           }

           if (!isGlobalDocument(params.xDocumentModel)) { // Access to chart data throws an exception for global documents
             auto units = $9;
             expression vec = expression($7 / units).evalm();
             auto chartname = OUS8($3);
             auto sym = $5;
             auto snum = $15;
             setChartData(params.xDocumentModel, chartname, ex_to<extsymbol>(sym), ex_to<matrix>(vec), subs(ex($11.rhs()) / ex($13), ex_to<extsymbol>(sym) == ex_to<extsymbol>(sym) * units), snum);
             setSeriesDescription(params.xDocumentModel, std::move(chartname), OUS8($17), snum == 1 ? 1 : snum - 1);
           }

           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmChart>(current_options, std::move(formulaParts)));
             line = params.lines.back();
             line_options.clear();
           }
         }
         | CHART '{' STRING ',' symbol '=' colvec_ex ',' ex ',' ex ',' ex ',' uinteger ',' STRING '}' {
           std::vector<OUString> formulaParts =
               {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU("="), GETARG(@7), OU(","), GETARG(@9), OU(","), GETARG(@11), OU(","), GETARG(@13), OU(","), GETARG(@15), OU(","), GETARG(@17), OU("}")};
           if (!checkHasChartsAndTables(params.xDocumentModel)) {
             error(@1, "This document type does not support the CHART statement");
             handle_error(params, std::make_shared<iFormulaNodeStmChart>(current_options, std::move(formulaParts)), @2);
             YYABORT;
           }

           if (!isGlobalDocument(params.xDocumentModel)) { // Access to chart data throws an exception for global documents
             auto units = $9;
             expression vec = expression($7 / units).evalm();
             auto chartname = OUS8($3);
             auto sym = $5;
             auto snum = $15;
             setChartData(params.xDocumentModel, chartname, ex_to<extsymbol>(sym), ex_to<matrix>(vec), subs($11 / $13, ex_to<extsymbol>(sym) == ex_to<extsymbol>(sym) * units), snum);
             setSeriesDescription(params.xDocumentModel, std::move(chartname), OUS8($17), snum == 1 ? 1 : snum - 1);
           }

           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmChart>(current_options, std::move(formulaParts)));
             line = params.lines.back();
             line_options.clear();
           }
         }
         | CHART error {
            handle_error(params, std::make_shared<iFormulaNodeStmChart>(current_options, fparts({})), @2);
            YYABORT;
          }
         | SETTABLECELL '{' string_ex ',' ex ',' ex '}' {
           std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU("}")};
           if (!checkHasChartsAndTables(params.xDocumentModel)) {
             error(@1, "This document cannot hold any tables");
             handle_error(params, std::make_shared<iFormulaNodeStmTablecell>(current_options, std::move(formulaParts)), @2);
             YYABORT;
           }

           auto tableName = $3;
					 auto cname = $5;
					 auto values = $7;
					 if (is_a<stringex>(cname) && !params.copyPasteActive) {
						 setTableCell(params.xDocumentModel, tableName, OUS8(ex_to<stringex>(std::move(cname)).get_string()), values);
					 } else if (is_a<matrix>(cname)) {
						 const matrix& l = ex_to<matrix>(std::move(cname));
						 if (is_a<matrix>(values)) {
							 const matrix& v = ex_to<matrix>(values);
							 if (l.rows() != v.rows() || (l.cols() != v.cols())) {
                 error(@5, "Rows and columns of the matrix of values must match matrix of cell references");
                 handle_error(params, std::make_shared<iFormulaNodeStmTablecell>(current_options, std::move(formulaParts)), @2);
                 YYABORT;
               }
						 }

						 for (unsigned r = 0; r < l.rows(); ++r) {
							 for (unsigned c = 0; c < l.cols(); ++c) {
								 if (is_a<stringex>(l(r,c))) {
									 ex val;
									 if (is_a<matrix>(values)) {
										 const matrix& v = ex_to<matrix>(values);
										 val = v(r,c);
									 } else {
										 val = values;
									 }
									  if (!params.copyPasteActive) setTableCell(params.xDocumentModel, tableName, OUS8(ex_to<stringex>(l(r,c)).get_string()), val);
								 }
							 }
						 }
					 } else {
             error(@5, "Cell reference must be a string or a list of strings");
             handle_error(params, std::make_shared<iFormulaNodeStmTablecell>(current_options, std::move(formulaParts)), @2);
             YYABORT;
           }

           if (include_level == 0) {
             params.lines.push_back(std::make_shared<iFormulaNodeStmTablecell>(current_options, std::move(formulaParts)));
             line = params.lines.back();
             line_options.clear();
           }

           params.cacheable = false;
         }
         | SETTABLECELL error {
            handle_error(params, std::make_shared<iFormulaNodeStmTablecell>(current_options, fparts({})), @2);
            YYABORT;
          }
         | SETCALCCELLS '{' string_ex ',' string_ex ',' string_ex ',' ex '}' {
           Reference<XStorable> xStorable(params.xDocumentModel, UNO_QUERY_THROW);
           OUString documentURL = xStorable->getLocation();
           Reference< XComponentContext> componentContext = params.xContext;
           OUString calcURL = makeURLFor($3, documentURL, componentContext); // Handle relative paths in the URL
           setCalcCellRange(componentContext, calcURL, $5, $7, $9);

           if (include_level == 0) {
             std::vector<OUString> formulaParts = {OU("{"), GETARG(@3), OU(","), GETARG(@5), OU(","), GETARG(@7), OU(","), GETARG(@9), OU("}")};
             params.lines.push_back(std::make_shared<iFormulaNodeStmCalccell>(current_options, std::move(formulaParts)));
             line = params.lines.back();
             line_options.clear();
           }

           params.cacheable = false;
         }
         | SETCALCCELLS error {
            handle_error(params, std::make_shared<iFormulaNodeStmCalccell>(current_options, fparts({})), @2);
            YYABORT;
          }
;

%type <std::string> unitname "name of unit";
unitname: IDENTIFIER {
          $$ = $1;
          if ($$[0] == '%') {
             $$.erase(0,1);
          } else {
            error(@1, "Unit name should start with '%'");
            YYERROR;
          }
        }
        | STRING {
          $$ = $1;
          if ($$[0] == '%') {
            error(@1, "Quoted unit name should not start with '%'");
            YYERROR;
          }
        }
        | UNIT {
          error(@1, "Unit '" + $1 + "' already exists. Please choose a different name");
          YYERROR;
        }
;
%type <std::string> prefixname "name of prefix";
prefixname: IDENTIFIER {
            $$ = $1;
            if ($$[0] == '%') {
              $$.erase(0,1);
            } else {
              error(@1, "Prefix name should start with '%'");
              YYERROR;
            }
          }
          | UNIT {
            // This rule exists to handle the SI short prefixes T(era) and m(illi) which correspond to units T(esla) and m(etre)
            $$ = $1;
          }
;

%type <strvec> labellist "list of labels";
labellist: LABEL  {
            $$ = std::vector<std::string>{};
            $$.emplace_back(params.compiler->label_ns($1, true));
          }
          | labellist ';' LABEL {
            $$ = $1;
            $$.emplace_back(params.compiler->label_ns($3, true));
          }
;

// Complete expressions, they have no return value and cannot be nested
expr:   options EXDEF asterisk ex { // If we add an optional label (that may be empty) then there are two shift/reduce conflicts
        if (include_level == 0) {
          params.lines.push_back(std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, $1, fparts({GETARG(@4)}), OU(""), $4, $3));
          line = params.lines.back();
          line_options.clear();
          line->force_autoformat(must_autoformat);
        }
        params.cacheable = false;
        must_autoformat = false;
      }
      | options EXDEF asterisk error {
        handle_error(params, std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, $1, fparts({}), OU(""), _expr0, $3), @4);
        YYABORT;
      }
      | LABEL options EXDEF asterisk ex { // Therefore we need a second rule
        // Note: This is always a new label, the duplicate label case is handled by the next rule
        auto label = params.compiler->exlabel_ns($1);
        auto expr = $5;
        if (!label.empty()) params.compiler->register_expression(std::move(expr), std::move(label)); // Only expressions with labels need to be registered

        if (include_level == 0) {
          params.lines.push_back(std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, $2, fparts({GETARG(@5)}), OUS8(label), expr, $4));
          line = params.lines.back();
          line_options.clear();
          line->force_autoformat(must_autoformat);
        }

        params.cacheable = false;
        must_autoformat = false;
      }
      | LABEL options EXDEF asterisk error {
        handle_error(params, std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->label_ns($1)), _expr0, $4), @5);
        YYABORT;
      }
      | EXLABEL options EXDEF asterisk ex { // Duplicate expression label
        auto _label = $1;
        auto options = $2;
        auto hide = $4;
        auto expr = $5;

        if (autorenumberduplicate) {
          std::string label = params.compiler->exlabel_ns(std::move(_label)) + "_1"; // TODO What if this label also exists?
          if (!label.empty())
            params.compiler->register_expression(std::move(expr), label); // Only expressions with labels need to be registered

          if (include_level == 0) {
            params.lines.push_back(std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, std::move(options), fparts({GETARG(@5)}), OUS8(label), expr, std::move(hide)));
            line = params.lines.back();
            line_options.clear();
            line->force_autoformat(must_autoformat);
          }
          params.cacheable = false;
          must_autoformat = false;
        } else {
          handle_label_error(@1, std::move(_label), params, std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, std::move(options), fparts({}), OUS8(_label), std::move(expr), std::move(hide)), @5);
          YYABORT;
        }
      }
      | EXLABEL options EXDEF asterisk error {
        handle_error(params, std::make_shared<iFormulaNodeEx>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->exlabel_ns($1)), _expr0, $4), @5);
        YYABORT;
      }
			| options PRINTVALUE ex {
				if (include_level == 0) {
          bool algebraic = $2.substr(5,1) == "A";
          std::string type = (algebraic ? "AVAL" : "VAL");
          auto expr = $3;

          params.lines.push_back(std::make_shared<iFormulaNodePrintval>(
            unitConversions(), current_options, $1,
            fparts({GETARG(@3)}), OU(""),
            (is_a<matrix>(expr) ? calcvalueofmatrix(type, expr, lst()) : calcvalue(type, expr, lst())), false,
            expr,
            algebraic, false
          ));
          line = params.lines.back();
          line_options.clear();
					line->force_autoformat(must_autoformat);
        }
				params.cacheable = false;
        must_autoformat = false;
			}
			| options PRINTVALUE error {
        bool algebraic = $2.substr(5,1) == "A";
        handle_error(params, std::make_shared<iFormulaNodePrintval>(unitConversions(), current_options, $1, fparts({}), OU(""), _expr0, false, _expr0, algebraic, false), @3);
        YYABORT;
      }
			| options PRINTVALUEWITH '{' ex ',' eqlist '}' {
				if (include_level == 0) {
          bool algebraic = $2.substr(5,1) == "A";
          std::string type = (algebraic ? "AVALWITH" : "VALWITH");
          std::vector<OUString> formulaParts = {OU("{"), GETARG(@4), OU(","), GETARG(@6), OU("}")};
          auto expr = $4;
          auto list = $6;

          params.lines.push_back(std::make_shared<iFormulaNodePrintval>(
            unitConversions(), current_options, $1,
            std::move(formulaParts), OU(""),
            (is_a<matrix>(expr) ? calcvalueofmatrix(type, expr, std::move(list)) : calcvalue(type, expr, std::move(list))), false,
            expr,
            algebraic, true
          ));
          line = params.lines.back();
          line_options.clear();
					line->force_autoformat(must_autoformat);
        }
				params.cacheable = false;
        must_autoformat = false;
			}
			| options PRINTVALUEWITH error {
        bool algebraic = $2.substr(5,1) == "A";
        handle_error(params, std::make_shared<iFormulaNodePrintval>(unitConversions(), current_options, $1, fparts({}), OU(""), _expr0, false, _expr0, algebraic, true), @3);
        YYABORT;
      }
			| options EXPLAINVAL asterisk ex {
				if (include_level == 0) {
          // What is the value of the expression?
          auto expr = $4;
          expression value = (is_a<matrix>(expr) ? calcvalueofmatrix("VAL", expr, lst()) : calcvalue("VAL", expr, lst()));
          expression definition = (is_a<symbol>(expr) ? ex_to<relational>(params.compiler->get_assignment(ex_to<symbol>(expr))).rhs() : expr);

          params.lines.push_back(std::make_shared<iFormulaNodeExplainval>(
            unitConversions(), current_options, $1,
            fparts({GETARG(@4)}), OU(""),
            std::move(value), $3,
            std::move(expr), definition,
            // How is the value of the expression defined?
            // Extract all symbols from the definition and store their value in a map
            // Note: Since we just called calcvalue() on the expression, all symbols contained in it have had their values calculated
            params.compiler->find_variable_values(std::move(definition))
          ));
          line = params.lines.back();
          line_options.clear();
					line->force_autoformat(must_autoformat);
        }
				params.cacheable = false;
        must_autoformat = false;
			}
			| options EXPLAINVAL asterisk error {
        handle_error(params, std::make_shared<iFormulaNodeExplainval>(unitConversions(), current_options, $1, fparts({}), OU(""), _expr0, $3, _expr0, _expr0, exhashmap<ex>()), @4);
        YYABORT;
      }
      |	LABEL options EQDEF asterisk eq {
        auto options = $2;
        auto hide = $4;
        auto eq = $5;
        std::string nslabel;
        if (!check_label($1, nslabel)) {
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeEq>(unitConversions(), current_options, options, fparts({}), OUS8(nslabel), eq, hide), @5);
          YYABORT;
        }
        params.compiler->check_and_register(eq, nslabel);

        if (include_level == 0) {
          params.lines.push_back(std::make_shared<iFormulaNodeEq>(
            unitConversions(), current_options, std::move(options),
            fparts({GETARG(@5)}), OUS8(nslabel),
            eq, std::move(hide)));
          line = params.lines.back();
          line_options.clear();
          line->force_autoformat(must_autoformat);
        }
        must_autoformat = false;

        if (params.cacheable && params.cached_results != nullptr) params.cached_results->emplace_back(nslabel, std::move(eq));
      }
      | LABEL options EQDEF asterisk error {
        handle_error(params, std::make_shared<iFormulaNodeEq>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->label_ns($1)), equation(_expr0, _expr0), $4), @5);
        YYABORT;
      }
      | LABEL options CONSTDEF asterisk IDENTIFIER '=' ex {
        auto label = $1;
        auto options = $2;
        auto hide = $4;
        auto eq = GiNaC::dynallocate<equation>(compiler->getsym($5), $7);
        std::string nslabel;
        if (!check_label(label, nslabel)) {
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeConst>(unitConversions(), current_options, options, fparts({}), OUS8(nslabel), eq, hide), @5);
          YYABORT;
        }
        try {
          params.compiler->register_constant(ex_to<equation>(std::move(eq))); // TODO: Label is unused - no equation is registered, just the value!
        } catch(const std::exception& e) {
          error(@7, e.what());
          handle_error(params, std::make_shared<iFormulaNodeConst>(unitConversions(), current_options, options, fparts({}), OUS8(nslabel), eq, hide), @5);
          YYABORT;
        }

        if (include_level == 0) {
          params.lines.push_back(std::make_shared<iFormulaNodeConst>(unitConversions(), current_options, std::move(options), fparts({GETARG(@5), "=", GETARG(@7)}), OUS8(nslabel), eq, std::move(hide)));
          line = params.lines.back();
          line_options.clear();
          line->force_autoformat(must_autoformat);
        }

        must_autoformat = false;
        params.cacheable = false;
      }
      | LABEL options CONSTDEF '*' error {
        // Note: Two rules are required for error handling (with and without the '*') otherwise the global errorhandling (input error) is hit instead
        handle_error(params, std::make_shared<iFormulaNodeConst>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->label_ns($1)), equation(_expr0, _expr0), true), @5);
        YYABORT;
      }
      | LABEL options CONSTDEF error {
        handle_error(params, std::make_shared<iFormulaNodeConst>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->label_ns($1)), equation(_expr0, _expr0), false), @4);
        YYABORT;
      }
      | LABEL options FUNCDEF asterisk FUNC leftbracket ex rightbracket '=' ex {
        auto label = $1;
        auto options = $2;
        auto hide = $4;
        auto fname = $5;
        auto expr = $10;
        expression f = params.compiler->create_function(fname, {$7});

        std::vector<OUString> formulaParts = {GETARG(@5), GETARG(@6), GETARG(@7), GETARG(@8), OU("="), GETARG(@10)};
				if (expr.has(params.compiler->create_function(fname)) || expr.has(f)) {
          error(@10, "Recursive function definition");
          handle_error(params, std::make_shared<iFormulaNodeFuncdef>(unitConversions(), current_options, std::move(options), std::move(formulaParts), OUS8(params.compiler->label_ns(label)), equation(_expr0, _expr0), std::move(hide)), @5);
          YYABORT;
        }

        params.compiler->define_function(std::move(fname), expr); // TODO: Should we check the arguments in $7 ?
        expression result = dynallocate<equation>(f, std::move(expr), relational::equal, _expr0);
        std::string nslabel;
        if (!check_label(label, nslabel)) {
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeFuncdef>(unitConversions(), current_options, options, fparts({}), OUS8(nslabel), result, hide), @5);
          YYABORT;
        }
        if (!ex_to<func>(f).is_expand())
          params.compiler->check_and_register(result, nslabel);

        if (include_level == 0) {
          params.lines.push_back(std::make_shared<iFormulaNodeFuncdef>(
            unitConversions(), current_options, std::move(options),
            std::move(formulaParts), OUS8(nslabel),
            result, std::move(hide)));
          line = params.lines.back();
          line_options.clear();
          line->force_autoformat(must_autoformat);
        }

        must_autoformat = false;
        params.cacheable = false;
      }
      | LABEL options FUNCDEF asterisk FUNC leftbracket exvec rightbracket '=' ex {
        auto label = $1;
        auto options = $2;
        auto hide = $4;
        auto fname = $5;
        auto expr = $10;
        expression f = params.compiler->create_function(fname, $7);

        std::vector<OUString> formulaParts = {GETARG(@5), GETARG(@6), GETARG(@7), GETARG(@8), OU("="), GETARG(@10)};
				if (expr.has(params.compiler->create_function(fname)) ||expr.has(f)) {
          error(@10, "Recursive function definition");
          handle_error(params, std::make_shared<iFormulaNodeFuncdef>(unitConversions(), current_options, std::move(options), std::move(formulaParts), OUS8(params.compiler->label_ns(label)), equation(_expr0, _expr0), std::move(hide)), @5);
          YYABORT;
        }

        params.compiler->define_function(std::move(fname), expr); // TODO: Should we check the arguments in $7 ?
        expression result = dynallocate<equation>(f, std::move(expr), relational::equal, _expr0);
        std::string nslabel;
        if (!check_label(label, nslabel)) {
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeFuncdef>(unitConversions(), current_options, options, fparts({}), OUS8(nslabel), result, hide), @5);
          YYABORT;
        }
        if (!ex_to<func>(f).is_expand())
          params.compiler->check_and_register(result, nslabel);

        if (include_level == 0) {
          params.lines.push_back(std::make_shared<iFormulaNodeFuncdef>(
            unitConversions(), current_options, std::move(options),
            std::move(formulaParts),OUS8(nslabel),
            result, std::move(hide)));
          line = params.lines.back();
          line_options.clear();
          line->force_autoformat(must_autoformat);
        }

        must_autoformat = false;
        params.cacheable = false;
      }
      | LABEL options FUNCDEF '*' error {
        handle_error(params, std::make_shared<iFormulaNodeFuncdef>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->label_ns($1)), equation(_expr0, _expr0), true), @5);
        YYABORT;
      }
      | LABEL options FUNCDEF error {
        handle_error(params, std::make_shared<iFormulaNodeFuncdef>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->label_ns($1)), equation(_expr0, _expr0), false), @4);
        YYABORT;
      }
      | LABEL options VECTORDEF asterisk vsymbol '=' ex {
        auto options = $2;
        auto hide = $4;
        expression result = dynallocate<equation>($5, $7, relational::equal, _expr0);
        std::string nslabel;
        if (!check_label($1, nslabel)) {
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeVectordef>(unitConversions(), current_options, options, fparts({}), OUS8(nslabel), result, hide), @5);
          YYABORT;
        }
        params.compiler->check_and_register(std::move(result), std::move(nslabel));

        if (include_level == 0) {
          params.lines.push_back(std::make_shared<iFormulaNodeVectordef>(unitConversions(), current_options, std::move(options), fparts({GETARG(@5), OU("="), GETARG(@7)}), OUS8(nslabel), result, std::move(hide)));
          line = params.lines.back();
          line_options.clear();
        }

        params.cacheable = false;
      }
      | LABEL options VECTORDEF asterisk vsymbol {
        error(@1, "Vector declaration may not have a label nor options. Please remove them");
        handle_error(params, std::make_shared<iFormulaNodeStmVectordef>(current_options, fparts({GETARG(@5)})), @5);
        YYABORT;
      }
      | LABEL options VECTORDEF '*' error {
        handle_error(params, std::make_shared<iFormulaNodeVectordef>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->label_ns($1)), equation(_expr0, _expr0), true), @5);
        YYABORT;
      }
      | LABEL options VECTORDEF error {
        handle_error(params, std::make_shared<iFormulaNodeVectordef>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->label_ns($1)), equation(_expr0, _expr0), false), @4);
        YYABORT;
      }
      | LABEL options MATRIXDEF asterisk msymbol '=' ex {
        auto options = $2;
        auto hide = $4;
        expression result = dynallocate<equation>($5, $7, relational::equal, _expr0);
        std::string nslabel;
        if (!check_label($1, nslabel)) {
          handle_label_error(@1, nslabel, params, std::make_shared<iFormulaNodeMatrixdef>(unitConversions(), current_options, options, fparts({}), OUS8(nslabel), result, hide), @5);
          YYABORT;
        }
        params.compiler->check_and_register(std::move(result), std::move(nslabel));

        if (include_level == 0) {
          params.lines.push_back(std::make_shared<iFormulaNodeMatrixdef>(unitConversions(), current_options, std::move(options), fparts({GETARG(@5), OU("="), GETARG(@7)}), OUS8(nslabel), result, std::move(hide)));
          line = params.lines.back();
          line_options.clear();
        }

        params.cacheable = false;
      }
      | LABEL options MATRIXDEF asterisk msymbol {
        error(@1, "Matrix declaration may not have a label nor options. Please remove them");
        handle_error(params, std::make_shared<iFormulaNodeStmMatrixdef>(current_options, fparts({GETARG(@5)})), @5);
        YYABORT;
      }
      | LABEL options MATRIXDEF '*' error {
        handle_error(params, std::make_shared<iFormulaNodeMatrixdef>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->label_ns($1)), equation(_expr0, _expr0), true), @5);
        YYABORT;
      }
      | LABEL options MATRIXDEF error {
        handle_error(params, std::make_shared<iFormulaNodeMatrixdef>(unitConversions(), current_options, $2, fparts({}), OUS8(params.compiler->label_ns($1)), equation(_expr0, _expr0), false), @4);
        YYABORT;
      }
;

%type <GiNaC::optionmap> options "options";
options: %empty { $$ = optionmap(); line_options = $$; }
         | '{' keyvallist '}' { $$ = $2; line_options = $$; }
         | IMPMUL '{' keyvallist '}' { $$ = $3; line_options = $$; }
;
%type <GiNaC::optionmap> keyvallist "option list";
keyvallist: keyvalpair {
              $$ = optionmap();
              $$.insert($1);
            }
            | { canonicalize_units = false; } unitpair {
              $$ = optionmap();
              auto pair = $2;
              $$.emplace(std::pair<option_name, option>(o_units, pair.first));
              $$.emplace(std::pair<option_name, option>(o_unitstr, pair.second));
            }
            | keyvallist ';' keyvalpair {
              $$ = $1;
              $$.insert($3);
            }
            | keyvallist ';' { canonicalize_units = false; } unitpair {
              $$ = $1;
              auto pair = $4;
              $$.emplace(std::pair<option_name, option>(o_units, pair.first));
              $$.emplace(std::pair<option_name, option>(o_unitstr, pair.second));
            }
;
%type <std::pair<option_name, option>> keyvalpair "option";
keyvalpair:   OPT_U '=' force_nonnegint_ex {
              $$ = std::pair<option_name, option>($1, option($3));

              // Ensure that the precision setting takes effect for numeric calculations, not just for printing!
              // TODO: How do we put it back to what it was before, so that it remains local to this line?
              //if ($1 == o_precision)
              //  Digits = v.to_int() + 5;
            }
            | OPT_I '=' force_int_ex {
              $$ = std::pair<option_name, option>($1, option($3));
            }
            | OPT_S '=' STRING {
              $$ = std::pair<option_name, option>($1, option($3));
            }
            | OPT_B '=' BOOL {
              auto opt = $1;
              auto val = $3;
              switch (opt) {
                case o_eqraw:
                case o_fixeddigits:
                  $$ = std::pair<option_name, option>(std::move(opt), option(!val));
                  break;
                case o_eqalign:
                  $$ = std::pair<option_name, option>(std::move(opt), option(val ? both : none));
                  break;
                case o_underflow:
                  MSG_INFO(3, "Inhibit floating point underflow exception: " << (val ? "true" : "false") << endline);
                  $$ = std::pair<option_name, option>(std::move(opt), option(std::move(val)));
                  set_inhibit_floating_point_underflow(std::move(val));
                  break;
                case o_evalf_real_roots:
                  MSG_INFO(3, "Evaluate odd negative roots to the positive real value: " << (val ? "true" : "false") << endline);
                  $$ = std::pair<option_name, option>(std::move(opt), option(val));
                  expression::evalf_real_roots_flag = std::move(val);
                  break;
                default:
                  $$ = std::pair<option_name, option>(std::move(opt), option(std::move(val)));
              }
            }
;
%type <std::pair<GiNaC::exvector, std::string>> unitpair "units option";
unitpair:     OPT_L  '=' '{' ex '}' {
              canonicalize_units = true;
              $$ = std::pair<exvector, std::string>({$4}, trimstring(rawtext.substr(@4.begin.column-1, @4.end.column-@4.begin.column)));
            }
            | OPT_L  '=' '{' exvec '}' { // Unit options: units (local to line)
                canonicalize_units = true;
                $$ = std::pair<exvector, std::string>($4, trimstring(rawtext.substr(@4.begin.column-1, @4.end.column-@4.begin.column)));
              }
;

%type <unsigned>  funchints "function hints";
funchints: '{' hints '}' {
            $$ = $2;
          }
;
%type <unsigned>  hints "hints";
hints:      IDENTIFIER { $$ = Functionmanager::hint($1); }
          | hints ';' IDENTIFIER {
            $$ = $1 | Functionmanager::hint($3);
          }
;
%type <bool> asterisk "*";
asterisk:   %empty { $$ = false; }
          | '*'    { $$ = true; }
;

// Processing of smath formulas
%type <GiNaC::expression>  eq "equation";
eq:   ex '=' ex             { $$ = dynallocate<equation>($1, $3, relational::equal, _expr0); }
    | ex '=' '=' ex         { $$ = dynallocate<equation>($1, $4, relational::equal, _expr0); }
    | ex NEQ ex             { $$ = dynallocate<equation>($1, $3, relational::not_equal, _expr0); }
    | ex '<' ex             { $$ = dynallocate<equation>($1, $3, relational::less, _expr0); }
    | ex '>' ex             { $$ = dynallocate<equation>($1, $3, relational::greater, _expr0); }
    | ex '>' '=' ex         { $$ = dynallocate<equation>($1, $4, relational::greater_or_equal, _expr0); }
    | ex '<' '=' ex         { $$ = dynallocate<equation>($1, $4, relational::less_or_equal, _expr0); }
    | ex EQUIV ex MOD ex { // Note: Writing '(' MOD ex ')' results in too many conflicts
      // Note: Defining an int_ex similar to numeric_ex etc. will not work because of shift/reduce conflicts, e.g.
		  // ex EQUIV ex BMOD ex '+' ex
		  // against
		  // ex EQUIV ex BMOD ex '+' eq
		  // Bison does not know whether to shift or to reduce when the '+' is encountered
      auto expr1 = $1;
      auto expr2 = $3;
      auto mod = $5;
      if (is_a<numeric>(expr1) && !expr1.info(info_flags::integer)) {
        error(@1, "Left-hand expression must be integer");
        YYERROR;
      } else if (is_a<numeric>(expr2) && !expr2.info(info_flags::integer)) {
        error(@3, "Right-hand expression must be integer");
        YYERROR;
      } else if (!check_modulus(mod)) {
        error(@5, "Modulo must be a positive or gaussian integer");
        YYERROR;
      } else
        $$ = dynallocate<equation>(std::move(expr1), std::move(expr2), relational::equal, std::move(mod));
		}
		| ex EQUIV ex BMOD ex ')' {
      auto expr1 = $1;
      auto expr2 = $3;
      auto mod = $5;
      if (is_a<numeric>(expr1) && !expr1.info(info_flags::integer)) {
        error(@1, "Left-hand expression must be integer");
        YYERROR;
      } else if (is_a<numeric>(expr2) && !expr2.info(info_flags::integer)) {
        error(@3, "Right-hand expression must be integer");
        YYERROR;
      } else if (!check_modulus(mod)) {
        error(@5, "Modulo must be a positive or gaussian integer");
        YYERROR;
      } else
        $$ = dynallocate<equation>(std::move(expr1), std::move(expr2), relational::equal, std::move(mod));
    }
    | LABEL { // An equation label referencing another equation
      must_autoformat = true;
      $$ = params.compiler->at(params.compiler->label_ns($1, true));
    }
    | SOLVE '(' eq ',' ex ',' uinteger ')' {
      $$ = ex_to<equation>($3).solve($5, numeric($7));
      must_autoformat = true;
    }
     | SUBST '(' eq ',' eqlist ')' {
      $$ = ex_to<equation>($3).subs($5).evalm();
      must_autoformat = true;
    }
    | SUBSTC '(' eq ',' eqlist ')' {
      $$ = ex_to<equation>($3).csubs($5).evalm();
      must_autoformat = true;
    }
    | SIMPLIFY '(' eq ',' simplifications ')' {
      $$ = ex_to<equation>($3).simplify($5);
      must_autoformat = true;
    }
    | COLLECT '(' eq ')' {
      $$ = ex_to<equation>($3).collect();
      must_autoformat = true;
    }
    | COLLECT '(' eq ',' ex ')' {
      $$ = ex_to<equation>($3).collect($5);
      must_autoformat = true;
    }
    | DIFFERENTIATE '(' eq ',' ex ',' ex ')' { // Immediately differentiate
      $$ = ex_to<equation>($3).diff($5, $7, true);
      must_autoformat = true;
    }
    | PDIFFERENTIATE '(' eq ',' ex ',' ex ')' { // Immediately differentiate
      $$ = ex_to<equation>($3).pdiff($5, $7, true);
      must_autoformat = true;
    }
    | INTEGRATE '(' eq ',' ex ',' symbol_ex ')' {
      auto var = $5;
      auto iconst = $7;
      $$ = ex_to<equation>($3).integrate(var, ex_to<symbol>(iconst), var, ex_to<symbol>(iconst));
      must_autoformat = true;
    }
    | INTEGRATE '(' eq ',' two_ex ',' two_symbols ')' {
      auto vars = $5;
      auto constants = $7;
      $$ = ex_to<equation>($3).integrate(vars[0], ex_to<symbol>(constants[0]), vars[1], ex_to<symbol>(constants[1]));
      must_autoformat = true;
    }
    | INTEGRATE '(' eq ',' ex ',' ex ',' ex ')' {
      auto var = $5;
      auto lower = $7;
      auto upper = $9;
		  $$ = ex_to<equation>($3).integrate(var, lower, upper, var, lower, upper);
      must_autoformat = true;
    }
    | INTEGRATE '(' eq ',' two_ex ',' two_ex ',' two_ex ')' {
      auto vars  = $5;
      auto lower = $7;
      auto upper = $9;
      $$ = ex_to<equation>($3).integrate(vars[0], lower[0], upper[0], vars[1], lower[1], upper[1]);
      must_autoformat = true;
    }
    | leftbracket eq rightbracket {
      $$ = $2;
    }
    | REVERSE eq {
      $$ = ex_to<equation>($2).reverse();
      must_autoformat = true;
    }
    | FUNC leftbracket eq rightbracket {
      auto fname = $1;
      const equation& eq = ex_to<equation>($3);
      MSG_INFO(1, "Applying function " << fname << " to " << eq << endline);
      $$ = dynallocate<equation>(params.compiler->create_function(fname, {eq.lhs()}), params.compiler->create_function(fname, {eq.rhs()}), eq.getop(), eq.getmod());
    }
    | SIZE sizestr IMPMUL eq { $$ = $4; }
    | '-' eq %prec NEGATION {
      const equation& eq = ex_to<equation>($2);
      $$ = dynallocate<equation>(-eq.lhs(), -eq.rhs(), eq.getop(), eq.getmod());
    }
    | '+' eq %prec NEGATION { $$ = $2; }
    | eq '+' eq    { $$ = ($1 + $3).evalm(); }
    | eq '+' ex    { $$ = ($1 + $3).evalm(); }
    | ex '+' eq    { $$ = ($1 + $3).evalm(); }
    | eq '-' eq    { $$ = ($1 - $3).evalm(); }
    | eq '-' ex    { $$ = ($1 - $3).evalm(); }
    | ex '-' eq    { $$ = ($1 - $3).evalm(); }
    | eq PLUSMINUS eq { $$ = $1 * $3 * expression(stringex("+-")); }
    | eq PLUSMINUS ex { $$ = $1 * $3 * expression(stringex("+-")); }
    | ex PLUSMINUS eq { $$ = $1 * $3 * expression(stringex("+-")); }
    | eq MINUSPLUS eq { $$ = $1 * $3 * expression(stringex("-+")); }
    | eq MINUSPLUS ex { $$ = $1 * $3 * expression(stringex("-+")); }
    | ex MINUSPLUS eq { $$ = $1 * $3 *expression( stringex("-+")); }
    | eq '*' eq    { $$ = ($1 * $3).evalm(); }
    | eq IMPMUL eq {
      if (hasLineOption(o_implicitmul) && (getLineOption(o_implicitmul).value.boolean == false)) {
        error(@3, "Implicit multiplication is not allowed in this expression");
        YYERROR;
      }
      $$ = ($1 * $3).evalm();
    }
    | eq TIMES eq  { $$ = ($1 * $3).evalm(); }
    | eq '*' ex    { $$ = ($1 * $3).evalm(); }
    | eq IMPMUL ex {
      if (hasLineOption(o_implicitmul) && (getLineOption(o_implicitmul).value.boolean == false)) {
        error(@3, "Implicit multiplication is not allowed in this expression");
        YYERROR;
      }
      $$ = ($1 * $3).evalm();
    }
    | eq TIMES ex  { $$ = ($1 * $3).evalm(); }
    | ex '*' eq    { $$ = ($1 * $3).evalm(); }
    | ex IMPMUL eq {
      if (hasLineOption(o_implicitmul) && (getLineOption(o_implicitmul).value.boolean == false)) {
        error(@3, "Implicit multiplication is not allowed in this expression");
        YYERROR;
      }
      $$ = ($1 * $3).evalm();
    }
    | ex TIMES eq  { $$ = ($1 * $3).evalm(); }
    | eq '/' eq    { $$ = ($1 / $3).evalm(); }
    | eq '/' ex    { $$ = ($1 / $3).evalm(); }
    | eq '^' ex    { $$ = ex_to<equation>($1).apply_power($3).evalm(); }
    | eq superscript { $$ = ex_to<equation>($1).apply_power($2).evalm(); }
    | VALUE '(' eq ')' { // Calculate the value of this equation
      // Don't calculate value of LHS if it is a symbol - otherwise the result will be identical on both sides!
      const equation& eq = ex_to<equation>($3);
      ex lhs = eq.lhs();
      auto value = $1;
      if (!is_a<symbol>(lhs))
        lhs = calcvalue(value, lhs, lst());
      $$ = dynallocate<equation>(lhs, calcvalue(std::move(value), eq.rhs(), lst()), eq.getop(), eq.getmod());
      must_autoformat = true;
    }
    | VALUEWITH '(' eq ',' eqlist ')' { // Find the value of this equation using an optional list of assignments to find it
      const equation& eq = ex_to<equation>($3);
      ex lhs = eq.lhs();
      auto value = $1;
      auto list = $5;
      if (!is_a<symbol>(lhs))
        lhs = calcvalue(value, lhs, list);
      $$ = dynallocate<equation>(lhs, calcvalue(std::move(value), eq.rhs(), std::move(list)), eq.getop(), eq.getmod());
      must_autoformat = true;
    }
;

%type <GiNaC::lst>  eqlist "equation list";
eqlist:   eq            { $$ = lst{$1}; }
        | eqlist ';' eq { $$ = $1; $$.append($3); }
;

%type <int> force_int_ex "integer number";
force_int_ex: ex {
          expression expr = $1;
          if (!is_a<numeric>(expr))
            expr = expr.evalf();
          if (!is_a<numeric>(expr) || !expr.info(info_flags::integer)) {
            error(@1, "Expected integer");
            YYERROR;
          }
          $$ = ex_to<numeric>(expr).to_int();
        }
;
%type <unsigned> force_nonnegint_ex "non-negative integer";
force_nonnegint_ex: ex {
                expression expr = $1;
                if (!is_a<numeric>(expr))
                  expr = expr.evalf();
                if (!is_a<numeric>(expr) || !expr.info(info_flags::nonnegint)) {
                  error(@1, "Expected integer greater or equal to zero");
                  YYERROR;
                }
                $$ = ex_to<numeric>(expr).to_int();
              }
;
%type <GiNaC::expression> real_ex "real number";
real_ex: ex {
          $$ = $1;
          if (!$$.info(info_flags::real)) {
            error(@1, "Expected real number");
            YYERROR;
          }
        }
;
%type <GiNaC::expression> numeric_ex "numeric";
numeric_ex: ex {
            $$ = $1;
            if (!is_a<numeric>($$)) {
              error(@1, "Expected numeric value");
              YYERROR;
            }
          }
;
%type <GiNaC::expression> symbol_ex "symbol expression";
symbol_ex: ex {
            $$ = $1;
            if (!is_a<symbol>($$)) {
              error(@1, "Expected symbol");
              YYERROR;
            }
          }
;
%type <GiNaC::expression> colvec_ex "column vector";
colvec_ex: ex {
            $$ = $1;
            if (!is_a<matrix>($$) || ex_to<matrix>($$).cols() > 1) {
              error(@1, "Expected column vector");
              YYERROR;
            }
          }
;
%type <GiNaC::exvector> real_exvec "vector of real numbers";
real_exvec: exvec {
              $$ = $1;
              for (unsigned s = 0; s < $$.size(); ++s) {
                if (!$$[s].info(info_flags::real)) {
                  error(@1, "Expected real number");
                  YYERROR;
                }
              }
            }
;
%type <GiNaC::exvector> two_symbols "two symbols";
two_symbols: exvec {
              $$ = $1;
              if (!($$.size() == 2 && is_a<symbol>($$[0]) && is_a<symbol>($$[1]))) {
                error(@1, "Expected two symbols");
                YYERROR;
              }
            }
;
%type <GiNaC::exvector> two_ex "two expressions";
two_ex: exvec {
          $$ = $1;
          if ($$.size() > 2) {
            error(@1, "Expected vector of two expressions");
            YYERROR;
          }
        }
;
%type <OUString> string_ex "string expression";
string_ex: ex {
            auto expr = $1;
            if (!is_a<stringex>(expr)) {
              error(@1, "Expected string");
              YYERROR;
            }
            $$ = OUS8(ex_to<stringex>(std::move(expr)).get_string());
          }
;

%type <GiNaC::expression> ex "expression";
ex:   SUBST '(' ex ',' eqlist ')' {
        $$ = $3.subs($5).evalm();
        must_autoformat = true;
    }
    | SUBSTC '(' ex ',' eqlist ')' {
      $$ = $3.csubs($5).evalm();
      must_autoformat = true;
    }
    | SUBSTV '(' ex ',' eqlist ')' {
      $$ = $3.subsv($5, false).evalm();
      must_autoformat = true;
    }
    | SUBSTVC '(' ex ',' eqlist ')' {
      $$ = $3.subsv($5, true).evalm();
      must_autoformat = true;
    }
    | SUBSTV '(' eq ',' eqlist ')' { // SUBSTV always results in a vector (a vector of equations in this case)
      $$ = ex_to<equation>($3).subsv($5, false).evalm();
      must_autoformat = true;
    }
    | SUBSTVC '(' eq ',' eqlist ')' {
      $$ = ex_to<equation>($3).subsv($5, true).evalm();
      must_autoformat = true;
    }
    | SIMPLIFY '(' ex ',' simplifications ')' {
      $$ = $3.simplify($5);
      must_autoformat = true;
    }
    | COLLECT '(' ex ')' {
      $$ = $3.collect();
      must_autoformat = true;
    }
    | COLLECT '(' ex ',' ex ')' {
      $$ = $3.collect($5);
      must_autoformat = true;
    }
		| DIFFERENTIATE '(' ex ',' ex ',' ex ')' {
      $$ = $3.diff($5, $7, true);
      must_autoformat = true;
    }
    | PDIFFERENTIATE '(' ex ',' ex ',' ex ')' {
      $$ = $3.pdiff($5, $7, true);
      must_autoformat = true;
    }
    | INTEGRATE '(' ex ',' ex ',' symbol ')' {
			$$ = $3.integrate($5, ex_to<symbol>($7));
      must_autoformat = true;
    }
		| INTEGRATE '(' ex ',' ex ',' ex ',' ex ')' {
      $$ = $3.integrate($5, $7, $9);
      must_autoformat = true;
    }
    | TSERIES '(' ex ',' eq ',' uinteger ')' { // TODO: Why can't tseries be a normal function?
      $$ = series_to_poly($3.series($5, $7));
      must_autoformat = true;
    }
    | TEXTFIELD '(' string_ex ')' {
      if (!checkHasChartsAndTables(params.xDocumentModel)) {
        error(@1, "This document cannot hold any text fields");
        YYERROR;
      }
      Reference< XTextDocument > xDoc(params.xDocumentModel, UNO_QUERY_THROW);
      $$ = getExpressionFromString(getTextFieldContent(xDoc, $3));
      must_autoformat = true;
    }
    | TABLECELL '(' string_ex ',' exvec_or_ex ')' {
      if (!checkHasChartsAndTables(params.xDocumentModel)) {
        error(@1, "This document cannot hold any tables");
        YYERROR;
      }

      OUString tableName = $3;
      Reference< XTextDocument > xDoc(params.xDocumentModel, UNO_QUERY_THROW);
      auto expr = $5;

			if (is_a<stringex>(expr)) {
        Reference< XCell > xCell = getTableCell(xDoc, tableName, OUS8(ex_to<stringex>(std::move(expr)).get_string()).toAsciiUpperCase());
				$$ = getCellExpression(xCell);
			} else if (is_a<matrix>(expr)) {
				const matrix& l = ex_to<matrix>(std::move(expr));
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

				$$ = std::move(m);
			} else {
				error(@5, "Cell reference must be a string or a list of strings");
				YYERROR;
			}

			params.cacheable = false; // Because changes in tables are not tracked by iMath
			must_autoformat = true;
    }
    | CALCCELL '(' string_ex ',' string_ex ',' string_ex ')' {
      Reference<XStorable> xStorable(params.xDocumentModel, UNO_QUERY_THROW);
      OUString documentURL = xStorable->getLocation();
      Reference< XComponentContext> componentContext = params.xContext;
      OUString calcURL = makeURLFor($3, documentURL, componentContext); // Handle relative paths in the URL
      $$ = calcCellRangeContent(componentContext, calcURL, $5, $7);
      must_autoformat = true;
    }
    | symbol
    | EXLABEL { // An expression label referencing another expression
      must_autoformat = true;
      $$ = params.compiler->expression_at(params.compiler->exlabel_ns($1, true));
    }
    | UNIT {
      auto unit = $1;
      if (canonicalize_units)
        $$ = params.compiler->getCanonicalizedUnit(std::move(unit));
      else
        $$ = params.compiler->getUnit(std::move(unit));
    }
    | STRING {
      auto unit = $1;
      if (params.compiler->isUnit(unit)) {
        if (canonicalize_units)
          $$ = params.compiler->getCanonicalizedUnit(std::move(unit));
        else
          $$ = params.compiler->getUnit(std::move(unit));
      } else {
        $$ = stringex(std::move(unit));
      }
    }
    | number
    | vector
    | matrix
    | leftbracket ex rightbracket {
      $$ = $2;
    }
    | LHS '(' eq ')' {
      $$ = $3.lhs();
      must_autoformat = true;
    }
    | RHS '(' eq ')' {
      $$ = $3.rhs();
      must_autoformat = true;
    }
    | WILD '(' uinteger ')' {
      $$ = wild($3);
    }
    | WILD { $$ = wild(); }
    | FUNC leftbracket ex rightbracket {
      $$ = params.compiler->create_function($1, {$3}).evalm();
    }
    | FUNC leftbracket exvec rightbracket {
      $$ = params.compiler->create_function($1, $3).evalm();
    }
    | FUNC leftbracket condition ';' exvec rightbracket { // Currently only ifelse() uses this format
      exvector fargs({$3});
      auto list = $5;
      fargs.insert(fargs.end(), list.begin(), list.end());
      $$ = params.compiler->create_function($1, fargs).evalm();
    }
    | FUNC { // a function may be used without arguments
      $$ = params.compiler->create_function($1);
    }
    | NROOT number IMPMUL number { // We can't prevent the IMPMUL appearing here
      $$ = dynallocate<power>($4, 1 / $2);
    }
    | NROOT number IMPMUL '{' ex '}' {
      $$ = dynallocate<power>($5, 1 / $2);
    }
    | NROOT '{' ex '}' IMPMUL '{' ex '}' {
      $$ = dynallocate<power>($7, 1 / $3);
    }
    | QUO '(' ex ',' ex ')' {
      // Note: Using numeric_ex here gives conflicts with the following rule
      auto expr1 = $3;
      auto expr2 = $5;
      if (!is_a<numeric>(expr1)) {
        error(@3, "Expression must be a numeric");
        YYERROR;
      }
      if (!is_a<numeric>(expr2)) {
        error(@5, "Expression must be a numeric");
        YYERROR;
      }
      $$ = iquo(ex_to<numeric>(std::move(expr1)), ex_to<numeric>(std::move(expr2)));
      must_autoformat = true;
    }
    | QUO '(' ex ',' ex ',' symbol ')' {
      // Note: Using polynomial_ex here is not possible because the symbol is required to determine the polynomial
      auto expr1 = $3;
      auto expr2 = $5;
      auto sym = $7;
      if (!expr1.is_polynomial(sym)) {
        error(@3, "Expression must be a polynomial in the given variable");
        YYERROR;
      }
      if (!expr2.is_polynomial(sym)) {
        error(@5, "Expression must be a polynomial in the given variable");
        YYERROR;
      }

      $$ = quo(std::move(expr1), std::move(expr2), std::move(sym));
      must_autoformat = true;
    }
    | REM '(' ex ',' ex ')' {
      auto expr1 = $3;
      auto expr2 = $5;
      if (!is_a<numeric>(expr1)) {
        error(@3, "Expression must be a numeric");
        YYERROR;
      }
      if (!is_a<numeric>(expr2)) {
        error(@5, "Expression must be a numeric");
        YYERROR;
      }
      $$ = irem(ex_to<numeric>(std::move(expr1)), ex_to<numeric>(std::move(expr2)));
      must_autoformat = true;
    }
    | REM '(' ex ',' ex ',' symbol ')' {
      auto expr1 = $3;
      auto expr2 = $5;
      auto sym = $7;
      if (!expr1.is_polynomial(sym)) {
        error(@3, "Expression must be a polynomial in the given variable");
        YYERROR;
      }
      if (!expr2.is_polynomial(sym)) {
        error(@5, "Expression must be a polynomial in the given variable");
        YYERROR;
      }

      $$ = rem(std::move(expr1), std::move(expr2), std::move(sym));
      must_autoformat = true;
    }
    | GCD '(' ex ',' ex ')' {
      $$ = gcd($3, $5);
      must_autoformat = true;
    }
    | LCM '(' ex ',' ex ')' {
      $$ = lcm($3, $5);
      must_autoformat = true;
    }
    | SUMFROM lowerbound TO upperbound IMPMUL '{' ex '}' { // We can't prevent the IMPMUL appearing here
      $$ = Functionmanager::create_hard("sum", exprseq{$2, $4, $7}).evalm();
    }
/*    | PRODUCT FROM ex TO upperbound IMPMUL '{' ex '}' { // We can't prevent the IMPMUL appearing here
        $$ = product(*$3, *$5, *$8);
      delete($3); delete($5); delete($8);
    }*/
    | INTEGRAL '{' ex '}' {
      $$ = dynallocate<extintegral>($3, params.compiler->getsym("C"));
    }
    | INTEGRAL FROM lowerbound TO upperbound IMPMUL '{' ex '}' { // We can't prevent the IMPMUL appearing here
      auto lower = $3;
			$$ = dynallocate<extintegral>(lower.lhs(), lower.rhs(), $5, $8 / differential(lower.lhs(), false, _ex1)); // TODO: A sanity check für *$8 to contain the correct differential would be nice
    }
    | DIFFERENTIAL '(' ex ')' {
        $$ = dynallocate<differential>($3, $1 == "PARTIAL", _ex1);
        must_autoformat = true;
    }
    | DIFFERENTIAL '(' ex ',' ex ')' {
      $$ = dynallocate<differential>($3, $1 == "PARTIAL", $5);
      must_autoformat = true;
    }
    | SIZE sizestr IMPMUL ex {
      $$ = $4;
    }
/*    | vector '[' ex ']' { // get vector element
      $$ = func("mindex", exprseq{*$1, *$3, wild()});
    }*/
    | ex TRANSPOSE {
      auto expr = $1;
      if (is_a<matrix>(expr))
        $$ = expression(ex_to<matrix>(std::move(expr)).transpose()).evalm();
      else
        $$ = Functionmanager::create_hard("transpose", {std::move(expr)});
    }
    | VSYMBOL '[' ex ']' {
      auto sym = $1;
      expression val = sym;
      const symbol& s = ex_to<symbol>(sym);

      try {
        if (params.compiler->has_value(s)) {
          val = params.compiler->get_value(s); // save some time
        } else {
          val = params.compiler->find_value_of(s);
        }
      } catch(std::exception &) { /* e.g. s does not have a value, ignore error */ }

      auto expr = $3;
      $$ = Functionmanager::create_hard("mindex", exprseq{val, expr, -999});
      // TODO: This test will fail on vectors that contain functions as elements!
      if (is_a<func>($$)) $$ = Functionmanager::create_hard("mindex", exprseq{std::move(sym), std::move(expr), -999}); // mindex::eval() changed nothing
    }
/*    | matrix '[' ex ',' ex ']' {
      $$ = func("mindex", mindex(*$1, *$3, *$5));
    }*/
    | MSYMBOL '[' ex ',' ex ']' {
      auto sym = $1;
      expression val = sym;
      const symbol& s = ex_to<symbol>(sym);

      try {
        if (params.compiler->has_value(s)) {
          val = params.compiler->get_value(s);
        } else {
          val = params.compiler->find_value_of(s);
        }
      } catch(std::exception &e) { (void)e; /* ignore (no value found) */ }

      auto expr1 = $3;
      auto expr2 = $5;
      $$ = Functionmanager::create_hard("mindex", exprseq{val, expr1, expr2});
      if (is_a<func>($$)) $$ = Functionmanager::create_hard("mindex", exprseq{std::move(sym), std::move(expr1), std::move(expr2)}); // mindex::eval() changed nothing
    }
    | ex '!' {
      $$ = Functionmanager::create_hard("fact", {$1});
    }
    | '-' ex %prec NEGATION { $$ = $2 * _expr_1; }
    | '+' ex %prec NEGATION { $$ = $2; }
    | ex '+' ex    { $$ = ($1 + $3).evalm(); }
    | ex '-' ex    { $$ = ($1 - $3).evalm(); }
    | ex PLUSMINUS ex { $$ = $1 * $3 * expression(stringex("+-")); }
    | ex MINUSPLUS ex { $$ = $1 * $3 * expression(stringex("-+")); }
    | ex '*' ex    { $$ = ($1 * $3).evalm(); }
    | ex IMPMUL ex {
      auto expr = $3;
      if (hasLineOption(o_implicitmul) && (getLineOption(o_implicitmul).value.boolean == false) && !is_unit(expr)) {
        error(@3, "Implicit multiplication is not allowed in this expression");
        YYERROR;
      }
      $$ = ($1 * std::move(expr)).evalm();
    }
    | ex TIMES ex  {
      auto expr1 = $1;
      auto expr2 = $3;
      if (check_anyvector(expr1) && check_anyvector(expr2))
        $$ = Functionmanager::create_hard("vecprod", exprseq{std::move(expr1), std::move(expr2)});
      else
        $$ = (std::move(expr1) * std::move(expr2)).evalm();
    }
		| ex HPRODUCT ex { // Hadamard product (element-wise)
      $$ = Functionmanager::create_hard("hadamard", exprseq{$1, $3, h_product});
		}
    | ex '/' ex    { $$ = ($1 / $3).evalm(); }
		| ex HDIVISION ex { // Hadamard division (element-wise)
      $$ = Functionmanager::create_hard("hadamard", exprseq{$1, $3, h_division});
		}
    | ex '^' exponent { $$ = pow($1, $3).evalm(); }
    | ex superscript { $$ = pow($1, $2).evalm(); }
		| ex HPOWER ex { // Hadamard product (element-wise)
      $$ = Functionmanager::create_hard("hadamard", exprseq{$1, $3, h_power});
		}
    | VALUE '(' ex ')' { // Calculate the value of this expression
      auto valstr = $1;
      auto expr = $3;
      if (is_a<matrix>(expr))
        $$ = calcvalueofmatrix(std::move(valstr), std::move(expr), lst());
      else
        $$ = calcvalue(std::move(valstr), std::move(expr), lst());

      must_autoformat = true;
    }
    | VALUEWITH '(' ex ',' eqlist ')' { // Find the value of this expression using an optional list of assignments to find it
      auto valstr = $1;
      auto expr = $3;
      auto with = $5;
      if (is_a<matrix>(expr))
        $$ = calcvalueofmatrix(std::move(valstr), std::move(expr), std::move(with));
      else
        $$ = calcvalue(std::move(valstr), std::move(expr), std::move(with));

      must_autoformat = true;
    }
    | ITERATE '(' eqlist ',' ex ',' real_ex ',' uinteger ')' { // One shift-reduce conflict if we write eq instead of eqlist
      // Iterate an equation
      equation eq = ex_to<equation>($3.op(0));
      matrix syms(1,1); syms(0,0) = eq.lhs();
      matrix exprs(1,1); exprs(0,0) = eq.rhs();
      matrix start(1,1); start(0,0) = $5;
      matrix conv(1,1); conv(0,0) = $7;

      matrix result = params.compiler->iterate(syms, exprs, start, conv, $9);
      must_autoformat = true;
      $$ = result(0,0);
    }
    | ITERATE '(' eqlist ',' exvec ',' real_exvec ',' uinteger ')' {
      // Iterate a number of equations
      auto list = $3;
      size_t rows = list.nops();
      matrix syms ((unsigned int)rows, 1);
      matrix exprs((unsigned int)rows, 1);
      for (size_t eq = 0; eq < rows; ++eq) {
        syms ((unsigned int)eq, 0) = ex_to<equation>(list.op(eq)).lhs();
        exprs((unsigned int)eq, 0) = ex_to<equation>(list.op(eq)).rhs();
      }

      auto startlist = $5;
      auto convlist = $7;
      matrix start((unsigned int)startlist.size(), 1);
      matrix conv((unsigned int)convlist.size(), 1);
      for (unsigned s = 0; s < startlist.size(); ++s) start(s, 0) = startlist[s];
      for (unsigned s = 0; s < convlist.size(); ++s)
        conv(s, 0) = convlist[s];

      if ((start.rows() == rows) && (conv.rows() == rows)) {
        $$ = params.compiler->iterate(syms, exprs, start, conv, $9);
      } else {
        error(@3, "All arguments must have the same number of elements");
        YYERROR;
      }
      must_autoformat = true;
    }
;
%type <GiNaC::expression> condition "condition";
condition: eq {
             const equation& eq = ex_to<equation>($1);
             $$ = dynallocate<relational>(eq.lhs(), eq.rhs(), eq.getop());
           }
           | combinedcondition { $$ = $1; }
;
%type <GiNaC::expression> combinedcondition "combined conditions";
combinedcondition:
           condition AND condition {
             $$ = Functionmanager::create_hard("ifelse", exprseq{$1, Functionmanager::create_hard("ifelse", exprseq{$3, 1, 0}), 0});
           }
           | condition OR condition {
             $$ = Functionmanager::create_hard("ifelse", exprseq{$1, 1, Functionmanager::create_hard("ifelse", exprseq{$3, 1, 0})});
           }
           | NEG condition {
             auto condition = $2;
             if (condition.info(info_flags::relation_equal))
               $$ = dynallocate<relational>(condition.lhs(), condition.rhs(), relational::not_equal);
             else if (condition.info(info_flags::relation_not_equal))
               $$ = dynallocate<relational>(condition.lhs(), condition.rhs(), relational::equal);
             else if (condition.info(info_flags::relation_less))
               $$ = dynallocate<relational>(condition.lhs(), condition.rhs(), relational::greater_or_equal);
             else if (condition.info(info_flags::relation_greater))
               $$ = dynallocate<relational>(condition.lhs(), condition.rhs(), relational::less_or_equal);
             else if (condition.info(info_flags::relation_less_or_equal))
               $$ = dynallocate<relational>(condition.lhs(), condition.rhs(), relational::greater);
             else if (condition.info(info_flags::relation_greater_or_equal))
               $$ = dynallocate<relational>(condition.lhs(), condition.rhs(), relational::less);
             else
               $$ = Functionmanager::create_hard("ifelse", exprseq{std::move(condition), 0, 1});
           }
           | leftbracket combinedcondition rightbracket { $$ = $2; }
;

%type <std::string> left "opening bracket sizing";
left: %empty  { $$ = std::string(""); }
      | LEFT  { $$ = $1; }
;
%type <std::string> right "closing bracket sizing";
right: %empty { $$ = std::string(""); }
      | RIGHT { $$ = $1; }
;
%type <std::string> openbracket "'(', '{' or '['";
openbracket:    '(' { $$ = std::string("("); }
              | '{' { $$ = std::string("{"); }
              | '[' { $$ = std::string("["); }
              | BOPEN { $$ = $1; }
;
%type <std::string> closebracket "')', '}' or ']'";
closebracket:   ')' { $$ = std::string(")"); }
              | '}' { $$ = std::string("}"); }
              | ']' { $$ = std::string("]"); }
              | BCLOSE { $$ = $1; }
;
%type <std::string> leftbracket "opening bracket";
leftbracket:  left openbracket {
                $$ = $1;
                auto bracket = $2;
                bracketstack.push({$$, bracket});
                $$ += " " + std::move(bracket);
              }
;
%type <std::string> rightbracket "closing bracket";
rightbracket: right closebracket {
                $$ = $1;
                auto bracket = $2;
                auto expected = checkbrackets($$, bracket);
                if (!expected.empty()) {
                  error(@2, "Bracket mismatch: Found '" + bracket + "', expected '" + expected + "'");
                  YYERROR;
                }
                $$ += " " + std::move(bracket);
              }
;

%type <GiNaC::expression> lowerbound "lower bound";
lowerbound: intvar '=' ex { $$ = dynallocate<equation>($1, $3); }
          | '{' intvar '=' ex '}' { $$ = dynallocate<equation>($2, $4); }
;
%type <GiNaC::expression> intvar "integration variable";
intvar:   symbol
	| FUNC { $$ = params.compiler->create_function($1); }
;
%type <GiNaC::expression> upperbound "upper bound";
upperbound: number
          | '-' number { $$ = _expr_1 * $2; }
          | '+' number { $$ = $2; }
          | symbol
          | '{' ex '}' { $$ = $2; }
;

/* Vectors */
%type <GiNaC::expression> vector "vector";
vector:   VSYMBOL { $$ = $1; }
        | STACK '{' lvector '}' {
          auto vec = $3;
          $$ = dynallocate<matrix>((unsigned int)vec.nops(), 1, vec);
        }
        | MATRIX '{' lvector '}' {
          auto vec = $3;
          matrix& m = dynallocate<matrix>(1, (unsigned int)vec.nops());
          for (unsigned i = 0; i < vec.nops(); i++) m(0, i) = vec.op(i);
          $$ = std::move(m);
        }
        | MSYMBOL '[' ex ',' '*' ']' {
          auto sym = $1;
          expression val = sym;
          const symbol& s = ex_to<symbol>(sym);

          try {
            if (params.compiler->has_value(s)) {
              val = params.compiler->get_value(s);
            } else {
              val = params.compiler->find_value_of(s);
            }
          } catch(std::exception &) { /* ignore (no value found) */ }

          auto expr = $3;
          $$ = Functionmanager::create_hard("mindex", exprseq{val, expr, wild()});
          if (is_a<func>($$)) $$ = Functionmanager::create_hard("mindex", exprseq{std::move(sym), std::move(expr), wild()}); // mindex::eval() changed nothing
        }
        | MSYMBOL '[' '*' ',' ex ']' {
          auto sym = $1;
          expression val = sym;
          const symbol& s = ex_to<symbol>(sym);

          try {
            if (params.compiler->has_value(s)) {
              val = params.compiler->get_value(s);
            } else {
              val = params.compiler->find_value_of(s);
            }
          } catch(std::exception &e) { (void)e; } // ignore (no value found)

          auto expr = $5;
          $$ = Functionmanager::create_hard("mindex", exprseq{val, wild(), expr});
          if (is_a<func>($$)) $$ = Functionmanager::create_hard("mindex", exprseq{std::move(sym), wild(), std::move(expr)}); // mindex::eval() changed nothing
        }
        | ex ':' ex {
          // Note: Writing colvec_ex ':' ex produces 25 shift/reduce conflicts
          auto expr1 = $1;
          auto expr2 = $3;
          if (!is_a<matrix>(expr1)) {
            // Create vector, automatic step size
            unsigned size = (current_options->at(o_vecautosize)).value.uinteger;
            if (size < 2) size = 20; // Prevent nonsensical values
            expression step = expression(expr2 - expr1) / expression(size - 1);
            matrix& m = dynallocate<matrix>(size, 1);
            for (unsigned i = 0; i < size-1; i++) m(i, 0) = expr1 + expression(step * i);
            m(size-1, 0) = std::move(expr2); // Make last element be without numerical errors
            $$ = std::move(m);
            MSG_INFO(2, "Resulting vector with auto step: " << $$ << endline);
          } else {
            // Create vector with user-defined step. Note that this will re-size an existing vector, to allow for b:e:s syntax
            if (!is_a<matrix>(expr1) || ex_to<matrix>(expr1).cols() > 1) {
              error(@1, "Column vector expected");
              YYERROR;
            }
            matrix v = ex_to<matrix>(std::move(expr1));
            expression esize = (expression(v(v.rows()-1, 0) - v(0,0)) / expr2 + 1).evalf();

            if (!esize.info(info_flags::positive)) {
              error(@3, "The number of vector elements resulting from the step must be a positive value");
              YYERROR;
            } else {
              unsigned size = numeric_to_uint(ex_to<numeric>(esize));
              if (ex_to<numeric>(esize).to_double() - size > 1E-2) size++; // TODO: The 1E-2 is arbitrary here
              matrix& m = dynallocate<matrix>(size, 1);
              for (unsigned i = 0; i < size-1; i++) m(i, 0) = expression(v(0,0)) + expression(expr2 * i);
              m(size-1, 0) = v(v.rows()-1, 0);
              $$ = std::move(m);
              MSG_INFO(2, "Resulting vector with user-defined step: " << $$ << endline);
            }
          }
        }
        | leftbracket exvec rightbracket {
          auto expr = $2;
          matrix& m = dynallocate<matrix>((unsigned int)expr.size(), 1);
          for (unsigned i = 0; i < expr.size(); ++i) m(i, 0) = expr[i];
          $$ = std::move(m);
        }
;
%type <GiNaC::exvector> exvec "list of expressions";
exvec:     ex ';' ex      {
           $$ = exvector{$1, $3};
           }
         | exvec ';' ex  { $$ = $1; $$.emplace_back($3); }
;
%type <GiNaC::lst> lvector "matrix line";
lvector:   ex '#' ex      {
           $$ = lst{$1, $3};
        }
         | lvector '#' ex { $$ = $1; $$.append($3); }
;
%type <GiNaC::expression> exvec_or_ex "single expression or list of expressions";
exvec_or_ex:  exvec {
              auto vec = $1;
							matrix& m = dynallocate<matrix>((unsigned int)vec.size(), 1);
							for (unsigned i = 0; i < vec.size(); ++i) m(i, 0) = vec[i];
							$$ = std::move(m);
						}
						| ex {
							$$ = $1;
						}
;

/* Matrices */
%type <GiNaC::expression>  matrix "matrix";
matrix:   MSYMBOL { $$ = $1; }
        | MATRIX '{' lmatrix '}' {
          auto expr = $3;
          matrix m = dynallocate<matrix>((unsigned int)expr.nops(), (unsigned int)expr.op(0).nops());

          for (unsigned r = 0; r < expr.nops(); r++) {
            for (unsigned c = 0; c < expr.op(r).nops(); c++) {
              m(r, c) = expr.op(r).op(c);
            }
          }

          $$ = std::move(m);
        }
;
%type <GiNaC::lst> lmatrix "matrix lines";
lmatrix:   lvector DOUBLEHASH lvector { $$ = lst{$1, $3}; }
         | lmatrix DOUBLEHASH lvector { $$ = $1; $$.append($3); }
;

/* Symbols */
%type <GiNaC::expression> symbol "complex-valued symbol";
symbol:   SYMBOL
        | IDENTIFIER { $$ = params.compiler->getsym(params.compiler->varname_ns($1)); }
;
%type <GiNaC::expression> vsymbol "vector symbol";
vsymbol:  VSYMBOL
        | IDENTIFIER { $$ = params.compiler->getsym(params.compiler->varname_ns($1), p_vector); }
        | SYMBOL {
          // Redefine symbol type. Only VECTORDEF is allowed to do this!
          std::string sname = ex_to<symbol>($1).get_name();
          params.compiler->setsymprop(sname, p_vector);
          $$ = params.compiler->getsym(sname);
        }
;
%type <GiNaC::expression> msymbol "matrix symbol";
msymbol:  MSYMBOL
        | IDENTIFIER { $$ = params.compiler->getsym(params.compiler->varname_ns($1), p_matrix); }
        | SYMBOL {
          // Redefine symbol type. Only MATRIXDEF is allowed to do this!
          std::string sname = ex_to<symbol>($1).get_name();
          params.compiler->setsymprop(sname, p_matrix);
          $$ = params.compiler->getsym(sname);
        }
;
%type <GiNaC::expression> rsymbol "real-valued symbol";
rsymbol:  IDENTIFIER { $$ = params.compiler->getsym(params.compiler->varname_ns($1), p_real); }
        | SYMBOL {
          std::string sname = ex_to<symbol>($1).get_name();
          params.compiler->setsymprop(sname, p_real);
          $$ = params.compiler->getsym(sname);
        }
;
%type <GiNaC::expression> psymbol "positive symbol";
psymbol:  IDENTIFIER { $$ = params.compiler->getsym(params.compiler->varname_ns($1), p_pos); }
        | SYMBOL {
          std::string sname = ex_to<symbol>($1).get_name();
          params.compiler->setsymprop(sname, p_pos);
          $$ = params.compiler->getsym(sname);
        }
;
%type <GiNaC::expression> gsymbol "symbol";
gsymbol: symbol | VSYMBOL | MSYMBOL;

%type <strvec> simplifications "simplifications";
simplifications: STRING {
       $$ = std::vector<std::string>{$1};
     }
     | simplifications ';' STRING {
       $$ = $1;
       $$.emplace_back($3);
     }
;

%type <GiNaC::expression> exponent "exponent";
exponent: number
          | '-' number { $$ = _expr_1 * $2; }
          | '+' number { $$ = $2; }
          | symbol
          | leftbracket ex rightbracket {
            $$ = $2;
          }
;
%type <GiNaC::expression> number "number";
number: DIGITS { $$ = dynallocate<numeric>($1.c_str()); }
        | DIGITS '.' DIGITS {
          auto str = $1;
          str.append("." + $3);
          $$ = dynallocate<numeric>(str.c_str());
        }
;

%type <unsigned> uinteger "unsigned integer";
uinteger: DIGITS { $$ = atoi($1.c_str()); }
;

%type <GiNaC::expression> superscript "superscript";
superscript: SUPERSCRIPT {
            $$ = dynallocate<numeric>($1.c_str());
          }
          | SUPERMINUS superscript %prec NEGATION { $$ = _expr_1 * $2; }
          | SUPERPLUS superscript %prec NEGATION { $$ = $2; }
;

%type <std::string> sizestr "font size";
sizestr:    DIGITS
          | '-' DIGITS { $$ = $2; $$.insert(0, "-"); }
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
