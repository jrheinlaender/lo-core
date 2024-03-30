/***************************************************************************
    smathlexer.lxx  -  rules for reading smath formula text
    - scanner generation file for flex
                             -------------------
    begin                : Wed May 21 2008
    copyright            : (C) 2021 by Jan Rheinlaender
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

%{
#include <smathlexer.hxx>

int yyFlexLexer::yylex() { abort(); }

using token = imath::smathparser::token;
using token_type = imath::smathparser::token_type;

/* By default yylex returns int, we use token_type.
   Unfortunately yyterminate by default returns 0, which is
   not of token_type.  */
#define yyterminate() BEGIN(INITIAL); return token::ENDSTRING

// ----------------- Helper functions ----------------
// TODO: Is there any way to define this as a function??? The problem is accessing last_token etc, and yyless
#define CHECK_IMPLICIT_MUL(thetoken) \
  if (implicit_mul_might_follow) { \
    if ((last_token == token::FUNC)  && (thetoken == token::BOPEN)) { \
      checkresult = false; /* No implicit mul may exist between these two tokens */ \
    } else { \
      implicit_mul_might_follow = false; \
      last_token = token_type(' '); \
      yylloc->columns(-yyleng); \
      yyless(0); \
      checkresult = true; \
    } \
  } else { \
    checkresult = false;\
  }\

  // Note: An implicit multiplication occurs when the multiplication sign '*' is omitted, i.e.
  // '3x+4y' has two implicit multiplications. Parsing this is difficult because adding a rule
  // like exp exp -> exp leads to numerous shift/reduce conflicts. The way to avoid it is to add
  // some ugly hacks to the scanner. The logic behind these hacks is:
  // An implicit multiplication might follow the tokens SYMBOL, DIGIT, UNIT or any kind of closing bracket
  // It will only follow if the next token is SYMBOL, DIGIT, UNIT, FUNC, FRAC, VALUE or any kind
  // of opening bracket.
  // There are some exceptions:
  // - No implicit mul may occur between a function name and an opening bracket. Note that this
  //   case is ambiguous! EQC assumes that any opening bracket after a function name belongs to
  //   the function
  // In case an implicit mul is detected, the scanned token is put back on the input stream and IMPMUL returned instead.

  // Variables for keeping track of the scanner state
  bool implicit_mul_might_follow;
  token_type last_token;
  std::string last_attribute = "";
  std::string argument = "";
  bool checkresult;
%}

/* Target the C++ implementation */
%option c++
/* Leave buffer-switching to us */
%option	noyywrap
/* Don't generate a default rule on our behalf */
%option nodefault
/* Don't try to #include unistd */
%option nounistd
/* %option debug */
/* Debugging is turned on by setting yy_flex_debug to 1 in scan_begin() */
%option warn
/* Align tables to improve performance, at the cost of increasing memory usage */
%option align
/* We're not writing an interpreter */
%option never-interactive batch

/* Unicode, see http://groups.google.com/group/comp.compilers/browse_thread/thread/2d2bff9c324d8e05 */
UTF8_BYTE_ORDER_MARK   [\xEF][\xBB][\xBF]

ASCII       [\x0-\xFF]{-}[⁰¹²³⁴⁵⁶⁷⁸⁹]
/* Excludes ²³¹ = C2 B2, C2 B3, C2 B9 */
U2_1        \xC2[\x80-\xB1]
U2_2        \xC2[\xB4-\xB8]
U2_3        \xC2[\xBA-\xBF]
U2          {U2_1}|{U2_2}|{U2_3}|[\xC3-\xDF][\x80-\xBF]
U3          [\xE0][\xA0-\xBF][\x80-\xBF]
/* Excludes ⁰⁴⁵⁶⁷⁸⁹ = E2 81 B0, E2 81 B4, E2 81 B5, E2 81 B6, E2 81 B7, E2 81 B8 */
U4_1        \xE1[\x80-\xBF][\x80-\xBF]
U4_2        \xE2\x80[\x80-\xBF]
U4_3        \xE2\x81[\x80-\xAF]
U4_4        \xE2\x81[\xB1-\xB3]
U4_5        \xE2\x81[\xBA-\xBF]
U4          {U4_1}|{U4_2}|{U4_3}|{U4_4}|{U4_5}|[\xE3-\xEC][\x80-\xBF][\x80-\xBF]
U5          [\xED][\x80-\x9F][\x80-\xBF]
U6          [\xEE-\xEF][\x80-\xBF][\x80-\xBF]
U7          [\xF0][\x90-\xBF][\x80-\xBF][\x80-\xBF]
U8          [\xF1-\xF3][\x80-\xBF][\x80-\xBF][\x80-\xBF]
U9          [\xF4][\x80-\x8F][\x80-\xBF][\x80-\xBF]

UNICODE     {ASCII}|{U2}|{U3}|{U4}|{U5}|{U6}|{U7}|{U8}|{U9}
/* Adding [a-zA-Z] at the end might seem superfluous, but it isn't (why? */
LASCII      [\x0-\xFF]{-}[[:blank:]]{-}[[:cntrl:]]{-}[[:digit:]]{-}[[:graph:]]{-}[[:print:]]{-}[[:punct:]]{-}[[:space:]]{-}[⁰¹²³⁴⁵⁶⁷⁸⁹]{+}[a-zA-Z]
LUNICODE    {LASCII}|{U2}|{U3}|{U4}|{U5}|{U6}|{U7}|{U8}|{U9}

LETTER  [a-zA-Z]
ULETTER {LUNICODE}
DIGIT   [0-9]
NAME    {ULETTER}+
/* [] brackets are only for indexing matrices, {} brackets are for grouping! */
SEPARATOR [*"=<&;{[}\]#]
BLANK     [ \t]

/* A subscript is enclosed in {} and can contain everything except closing brackets.
   TODO: This definition should correspond with what starmath allows in a subscript!!!
   For example, "a_3 rsub 3" is illegal (double right subscript) or "a rsup 3 rsup 4" */
SUBSCRIPT _\{[^}]*\}|_({ULETTER}|{DIGIT})+
SUBPSCRIPT {BLANK}+([lcr]su[bp])(\{[^}]*\}|{BLANK}+{NAME}|{BLANK}+{DIGIT}+)
SUBSUPERSCRIPT {SUBSCRIPT}{SUBPSCRIPT}*|{SUBPSCRIPT}
DASH '|'|\xfe32
IDENTIFIER \%?{NAME}{DASH}*{SUBSUPERSCRIPT}*

ATTRIBUTE acute|grave|check|breve|circle|vec|tilde|hat|bar|dot|widevec|widetilde|widehat|ddot|overline|underline|overstrike|dddot|bold|ital|font\ sans|font\ serif|font\ fixed|color\ black|color\ blue|color\ green|color\ red|color\ cyan|color\ magenta|color\ yellow|color\ teal|color\ silver|color\ gray|color\ maroon|color\ purple|color\ lime|color\ olive|color\ navy|color\ aqua|color\ fuchsia|phantom

%{
  // Code run each time a pattern is matched. We only track columns, not lines, except inside include files
  #define YY_USER_ACTION yylloc->columns(yyleng);
%}

/* define iMath parsing states */
%x II
%x SCANSTR
%x SCANUSERSTR
%x SCANCOMMENT
%x SCANUSERTEXT
%x SCANSUPERSCRIPT

%%
%{
  // Code run each time yylex is called
  yylloc->step ();
%}
BEGIN(INITIAL);

{UTF8_BYTE_ORDER_MARK}

%{
  // Beginning and end -----------------------------------------
%}
^{BLANK}*\%\%ii  { // iFormula marker
          BEGIN(II);
          implicit_mul_might_follow = false;
        }
<INITIAL><<EOF>>  {
          implicit_mul_might_follow = false;
          last_token = token_type('\n');
          return token::ENDSTRING;
        }

%{
  // auto-generated lines
%}
^[^\n]*\%\%gg[^\n]*$ { // auto-generated line (discard)
         MSG_INFO(4, "Found generated line" << endline);
         return (token::GENERATED);
        }

%{
  // Arbitrary user text -----------------------------------------
%}
^{BLANK}*\%\%   { // user comment
         MSG_INFO(4,  "Found comment line" << endline);
         BEGIN(SCANCOMMENT);
         yylval->emplace<std::string>("%%");
       }
<SCANCOMMENT>[^\n]*$ { // scan to end of line. Note we cannot combine the two rules for comments because that would also match lines beginning with %%ii
          yylval->as<std::string>() += std::string(yytext);
          BEGIN(INITIAL);
          return (token::COMMENT);
        }
\n      { MSG_INFO(4, "Found \\n" << endline);
          if (include_level > 0) yylloc->lines(1); // line count for include files
          implicit_mul_might_follow = false;
          last_token = token_type('\n');
          return token_type('\n');
        }
.       { MSG_INFO(4,  "Found user text" << endline); // arbitrary user text
          yylloc->columns(-yyleng);
          yyless(0); // Scan again in SCANUSERTEXT state
          yylval->emplace<std::string>(""); // Prepare for accumulating text
          BEGIN(SCANUSERTEXT);
        }

%{
  // Arbitrary user text, with intermingled alignment tokens, ending with a \n or EOF
  // The idea is to accumulate text until a keyword is found, then return the text and scan the keyword again
  // THe BEGIN(INITIAL) is required to prepare yylval with an empty string
%}
<SCANUSERTEXT>(?i:newline) { // user-defined newline
            if (!yylval->as<std::string>().empty()) {
              yylloc->columns(-yyleng);
              yyless(0);
              BEGIN(INITIAL);
              return (token::STRING);
            } else {
              return (token::NEWLINE);
            }
          }
<SCANUSERTEXT>\\\"    { // escaped quotes are OK
            yylval->as<std::string>() += std::string("\\\"");
          }
<SCANUSERTEXT>\n      { // end of user text
            yylloc->columns(-yyleng);
            yyless(0);
            BEGIN(INITIAL);
            if (!yylval->as<std::string>().empty())
              return (token::STRING);
            else
              yylval->destroy<std::string>(); // handle this in INITIAL start condition
          }
<SCANUSERTEXT><<EOF>>    {
            BEGIN(INITIAL);
            if (!yylval->as<std::string>().empty())
              return (token::STRING);
            else
              yylval->destroy<std::string>(); // handle this in INITIAL start condition
          }
<SCANUSERTEXT>=|<>|>|<|>=|<=|(?i:equiv) {
            BEGIN(INITIAL);
            if (!yylval->as<std::string>().empty()) {
              yylloc->columns(-yyleng);
              yyless(0);
              return (token::STRING);
            } else {
              yylval->as<std::string>() = std::string(yytext);
              return token::OPERATOR;
            }
          }
<SCANUSERTEXT>_ii_ {
            implicit_mul_might_follow = false;
            if (!yylval->as<std::string>().empty()) {
              yylloc->columns(-yyleng);
              yyless(0);
              BEGIN(INITIAL);
              return (token::STRING);
            } else {
              BEGIN(II);
              yylval->destroy<std::string>();
              return (token::MAGIC);
            }
          }
<SCANUSERTEXT>\" { // begin string inside usertext (important because operator signs in here are not aligned)
            if (!yylval->as<std::string>().empty()) {
              yylloc->columns(-yyleng);
              yyless(0);
              BEGIN(INITIAL);
              return (token::STRING);
            } else {
              BEGIN(SCANUSERSTR);
              yylval->as<std::string>() = "\"";
            }
          }
<SCANUSERSTR>\\\"       { // allow escaped quotes
                          yylval->as<std::string>() += "\\\"";
                        }
<SCANUSERSTR>\\          { // Deal with single escaped backslashes
                          yylval->as<std::string>() += "\\";
                        }
<SCANUSERSTR>\n         { // closing quotes were forgotten, add them and continue
                          yylval->as<std::string>() += "\"";
                          yylloc->columns(-yyleng);
                          yyless(0);
                          BEGIN(INITIAL);
                          return token::STRING;
                        }
<SCANUSERSTR><<EOF>>    { // closing quotes were forgotten
                          yylval->as<std::string>() += "\"";
                          BEGIN(INITIAL);
                          return token::STRING;
                        }
<SCANUSERSTR>[^\\"\n]*  { // Everything else
                          yylval->as<std::string>() += std::string(yytext);
                        }
<SCANUSERSTR>\"         { // end of string
                          yylval->as<std::string>() += "\"";
                          BEGIN(INITIAL);
                          return (token::STRING);
                        }
<SCANUSERTEXT>. { MSG_INFO(4, "'" << yytext << "' found" << endline);// any other text
                 yylval->as<std::string>() += std::string(yytext);
                }

%{
// iMath file operations -----------------------------------------
%}
<II>(?i:readfile) { return (token::READFILE);  }

%{
// iMath options
%}
<II>(?i:options) { return (token::OPTIONS); }

%{
// iMath namespaces
%}
<II>(?i:begin) { implicit_mul_might_follow = false; return (token::BEGIN_NS); }
<II>(?i:end)   { implicit_mul_might_follow = false; return (token::END_NS); }

%{
// iMath declarations ----------------------------------
%}
<II>(?i:function)   { implicit_mul_might_follow = false; return (token::FUNCTION); }
<II>(?i:unitdef)    { implicit_mul_might_follow = false; return (token::UNITDEF); }
<II>(?i:prefixdef)  { implicit_mul_might_follow = false; return (token::PREFIXDEF); }
<II>(?i:vectordef)  { implicit_mul_might_follow = false; return (token::VECTORDEF); }
<II>(?i:matrixdef)  { implicit_mul_might_follow = false; return (token::MATRIXDEF); }
<II>(?i:realvardef) { implicit_mul_might_follow = false; return (token::REALVARDEF); }
<II>(?i:posvardef)  { implicit_mul_might_follow = false; return (token::POSVARDEF); }
<II>(?i:clearequations) { return (token::CLEAREQUATIONS); }
<II>(?i:delete)     { return (token::DELETE); }
<II>(?i:update)     { return (token::UPDATE); }
%{
// iMath definitions
%}
<II>(?i:exdef)    { implicit_mul_might_follow = false; return (token::EXDEF); }
<II>(?i:eqdef)    { implicit_mul_might_follow = false; return (token::EQDEF); }
<II>(?i:constdef) { implicit_mul_might_follow = false; return (token::CONSTDEF); }
<II>(?i:funcdef)  { implicit_mul_might_follow = false; return (token::FUNCDEF); }
%{
// Extended printing
%}
<II>(?i:print(a?)val) {
              implicit_mul_might_follow = false;
              std::string kw = std::string(yytext);
              std::transform(kw.begin(), kw.end(), kw.begin(), ::toupper);
              yylval->emplace<std::string>(kw);
              return (token::PRINTVALUE);
            }
<II>(?i:print(a?)valwith) {
              implicit_mul_might_follow = false;
              std::string kw = std::string(yytext);
              std::transform(kw.begin(), kw.end(), kw.begin(), ::toupper);
              yylval->emplace<std::string>(kw);
              return (token::PRINTVALUEWITH);
            }
<II>(?i:explainval) { implicit_mul_might_follow = false; return (token::EXPLAINVAL); }
%{
// Graphics and text
%}
<II>(?i:TEXT)[ ]? {
              implicit_mul_might_follow = false;
              yylval->emplace<std::string>(""); // Prepare for accumulating text
              BEGIN(SCANUSERTEXT);
              return (token::TEXT);
            }
<II>(?i:CHART) { implicit_mul_might_follow = false; return (token::CHART); }
%{
// Data exchange
%}
<II>(?i:settablecell) { implicit_mul_might_follow = false; return (token::SETTABLECELL); }
<II>(?i:setcalccells) { implicit_mul_might_follow = false; return (token::SETCALCCELLS); }
%{
// Names for key-value pairs
%}
<II>units          yylval->emplace<option_name>(o_units);        return(token::OPT_L);
<II>precision      yylval->emplace<option_name>(o_precision);    return(token::OPT_U);
<II>fixedpoint     yylval->emplace<option_name>(o_fixeddigits);  return(token::OPT_B);
<II>suppress_units yylval->emplace<option_name>(o_suppress_units); return(token::OPT_B);
<II>showlabels     yylval->emplace<option_name>(o_showlabels);   return(token::OPT_B);
<II>underflow      yylval->emplace<option_name>(o_underflow);    return(token::OPT_B);
<II>realroots      yylval->emplace<option_name>(o_evalf_real_roots); return(token::OPT_B);
<II>exponent       yylval->emplace<option_name>(o_exponent);     return(token::OPT_I);
<II>difftype       yylval->emplace<option_name>(o_difftype);     return(token::OPT_S);
<II>autoformat     yylval->emplace<option_name>(o_eqraw);        return(token::OPT_B);
<II>implicitmul    yylval->emplace<option_name>(o_implicitmul);  return(token::OPT_B);
<II>autoalign      yylval->emplace<option_name>(o_eqalign);      return(token::OPT_B);
<II>autochain      yylval->emplace<option_name>(o_eqchain);      return(token::OPT_B);
<II>autofraction   yylval->emplace<option_name>(o_autofraction); return(token::OPT_B);
<II>minimumtextsize   yylval->emplace<option_name>(o_minimumtextsize); return(token::OPT_U);
<II>autotextmode   yylval->emplace<option_name>(o_autotextmode); return(token::OPT_B);
<II>echo           yylval->emplace<option_name>(o_echoformula);  return(token::OPT_B);
<II>lowsclimit     yylval->emplace<option_name>(o_lowsclimit);   return(token::OPT_I);
<II>highsclimit    yylval->emplace<option_name>(o_highsclimit);  return(token::OPT_I);

%{/*<II>eqsplit        yylval->opt = o_eqsplit;      return(token::OPT_E);
<II>eqsplittext    yylval->opt = o_eqsplittext;  return(token::OPT_S);
<II>vecautosize    yylval->opt = o_vecautosize;  return(token::OPT_E);
<II>save           return(SAVE);
<II>restore        return(RESTORE);*/
%}

%{
// smath operators ------------------------------------------------
// These must come BEFORE the symbols etc., otherwise they will not be matched
%}
<II>(?i:left) { /* Opening sized brackets */
        CHECK_IMPLICIT_MUL(token::BOPEN)
        if (checkresult) return (token::IMPMUL);
        implicit_mul_might_follow = false;
        yylval->emplace<std::string>(yytext);
        last_token = token::LEFT;
        return (token::LEFT);
      }
<II>(?i:right) { /* Closing sized brackets */
        implicit_mul_might_follow = false;
        yylval->emplace<std::string>(yytext);
        last_token = token::RIGHT;
        return (token::RIGHT);
      }
<II>(?i:stack) {
              CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::STACK;
              return (token::STACK);
            }
<II>(?i:matrix) {
              CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::MATRIX;
              return (token::MATRIX);
            }
<II>(?i:over)|(?i:div)|(?i:wideslash)|(?i:widebslash)|÷|∕|⁄|⧸ { /* U+00F7 U+2215 U+2044 U+29F8 and note that Unicode will not work inside a character class */
              implicit_mul_might_follow = false;
              return token_type('/'); /* Treat as division symbol */
            }
<II>(?i:cdot)|·|∙ { /* U+00B7 U+2219 */
              implicit_mul_might_follow = false;
              return token_type('*'); /* Treat as multiplication symbol */
           }
<II>(?i:times)|×  { /* U+00D7 */
              implicit_mul_might_follow = false;
              return (token::TIMES);
           }
<II>[⁰¹²³⁴⁵⁶⁷⁸⁹] {
             BEGIN(SCANSUPERSCRIPT);
             yylval->emplace<std::string>("");
             yylloc->columns(-yyleng);
             yyless(0);
           }
<SCANSUPERSCRIPT>⁰ { yylval->as<std::string>() += "0"; }
<SCANSUPERSCRIPT>¹ { yylval->as<std::string>() += "1"; }
<SCANSUPERSCRIPT>² { yylval->as<std::string>() += "2"; }
<SCANSUPERSCRIPT>³ { yylval->as<std::string>() += "3"; }
<SCANSUPERSCRIPT>⁴ { yylval->as<std::string>() += "4"; }
<SCANSUPERSCRIPT>⁵ { yylval->as<std::string>() += "5"; }
<SCANSUPERSCRIPT>⁶ { yylval->as<std::string>() += "6"; }
<SCANSUPERSCRIPT>⁷ { yylval->as<std::string>() += "7"; }
<SCANSUPERSCRIPT>⁸ { yylval->as<std::string>() += "8"; }
<SCANSUPERSCRIPT>⁹ { yylval->as<std::string>() += "9"; }
<SCANSUPERSCRIPT>.|\n {
             implicit_mul_might_follow = (*yytext != '\n');
             yylloc->columns(-yyleng);
             yyless(0);
             BEGIN(II);
             last_token = token::SUPERSCRIPT;
             return (token::SUPERSCRIPT);
           }
<SCANSUPERSCRIPT><<EOF>> {
             BEGIN(INITIAL);
             return (token::SUPERSCRIPT);
           }
<II>⁺      { implicit_mul_might_follow = false; return token::SUPERPLUS; } /* U+207A */
<II>⁻      { implicit_mul_might_follow = false; return token::SUPERMINUS; } /* U+207B */
<II>\.\*|⊗|(?i:otimes)|(?i:odot) { /* U+2297 */
             implicit_mul_might_follow = false;
             return (token::HPRODUCT);
           }
<II>\.\/|⊘|(?i:odivide) { /* U+2298 */
             implicit_mul_might_follow = false;
             return (token::HDIVISION);
           }
<II>\.\^|ⓔ { /* U+24D4 */
             implicit_mul_might_follow = false;
             return (token::HPOWER);
           }
<II>-|‒|–|—  { /* U+2012 U+2013 U+2014 */
              implicit_mul_might_follow = false;
              return token_type('-'); /* Treat as minus sign */
            }
<II>-\+     { implicit_mul_might_follow = false;
              return (token::MINUSPLUS);
            }
<II>\+-     { implicit_mul_might_follow = false;
              return (token::PLUSMINUS);
            }
<II>(?i:nroot) {
              CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::NROOT;
              return (token::NROOT);
           }
<II>(?i:quo) { CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::QUO;
              return (token::QUO);
           }
<II>(?i:rem) { CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::REM;
              return (token::REM);
           }
<II>(?i:gcd) { CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::GCD;
              return (token::GCD);
           }
<II>(?i:lcm) { CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::LCM;
              return (token::LCM);
           }
<II>(?i:and) { implicit_mul_might_follow = false;
              last_token = token::AND;
              return (token::AND);
            }
<II>(?i:or) { implicit_mul_might_follow = false;
              last_token = token::OR;
              return (token::OR);
            }
<II>(?i:neg) { implicit_mul_might_follow = false;
              last_token = token::NEG;
              return (token::NEG);
            }
<II>(?i:equiv) {
              implicit_mul_might_follow = false;
              last_token = token::EQUIV;
              return (token::EQUIV);
            }
<II>(?i:mod)|\"(?i:mod)\" {
              implicit_mul_might_follow = false;
              last_token = token::MOD;
              return (token::MOD);
            }
<II>\({BLANK}*((?i:mod)|\"(?i:mod)\") {
              // Note: The closing bracket is handled by the parser
              implicit_mul_might_follow = false;
              last_token = token::BMOD;
              return (token::BMOD);
            }
<II>(?i:from) { implicit_mul_might_follow = false;
              return (token::FROM); /* sum from .. to .. */
           }
<II>(?i:to) { implicit_mul_might_follow = false;
              return (token::TO); /* sum from .. to .. */
           }
<II>(?i:lhs) {
              CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::LHS;
              return (token::LHS);
            }
<II>(?i:rhs) {
              CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::RHS;
              return (token::RHS);
            }
<II>(?i:rev) {
              CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::REVERSE;
              return (token::REVERSE);
            }
<II>(?i:wild) {
              CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::WILD;
              return (token::WILD);
            }
<II>(?i:a?val)|(?i:numval)|(?i:unit)|(?i:quantity) {
              CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::VALUE;
              std::string kw = std::string(yytext);
              std::transform(kw.begin(), kw.end(), kw.begin(), ::toupper);
              yylval->emplace<std::string>(kw);
              return (token::VALUE);
            }
<II>(?i:a?valwith)|(?i:numvalwith)|(?i:unitwith)|(?i:quantitywith) {
              CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::VALUEWITH;
              std::string kw = std::string(yytext);
              std::transform(kw.begin(), kw.end(), kw.begin(), ::toupper);
              yylval->emplace<std::string>(kw);
              return (token::VALUEWITH);
            }
<II>(?i:iterate) {
              CHECK_IMPLICIT_MUL(token::FUNC)
              if (checkresult) return (token::IMPMUL);
              implicit_mul_might_follow = false;
              last_token = token::ITERATE;
              return (token::ITERATE);
            }
<II>(?i:solve) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::SOLVE;
             return (token::SOLVE);
           }
<II>(?i:subst) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::SUBST;
             return (token::SUBST);
           }
<II>(?i:substc) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::SUBST;
             return (token::SUBSTC);
           }
<II>(?i:substv) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::SUBSTV;
             return (token::SUBSTV);
           }
<II>(?i:substvc) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::SUBSTVC;
             return (token::SUBSTVC);
           }
<II>(?i:simplify) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::SIMPLIFY;
             return (token::SIMPLIFY);
           }
<II>(?i:collect) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::COLLECT;
             return (token::COLLECT);
           }
<II>(?i:differentiate) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::DIFFERENTIATE;
             return (token::DIFFERENTIATE);
           }
<II>(?i:pdifferentiate) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::PDIFFERENTIATE;
             return (token::PDIFFERENTIATE);
           }
<II>(?i:integrate) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::INTEGRATE;
             return (token::INTEGRATE);
           }
<II>(?i:tseries) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::TSERIES;
             return (token::TSERIES);
           }
%{
// smath formatting -------------------------------------------------------
%}
<II>(?i:newline) { /* TODO: Can or should this occur at all ? */ }
<II>(?i:size) {
             CHECK_IMPLICIT_MUL(token::FUNC)
             if (checkresult) return (token::IMPMUL);
             implicit_mul_might_follow = false;
             last_token = token::SIZE;
             return (token::SIZE);
           }
<II>(?i:align[clr]) { /* skip */ }
<II>\~|` { /* skip */ }
<II>(?i:func) { /* Not handled yet */ }
%{
// ------------------------------------------------------
// Brackets
// Note: Extended brackets must come before names, otherwise they will not be recognized
%}
<II>[{([] { /* Normal opening brackets */
        if (((last_token == token::MSYMBOL) || (last_token == token::VSYMBOL)) && (yytext[0] == '[')) {
          checkresult = false; // Special case for matrix indexing
        } else {
          CHECK_IMPLICIT_MUL(token::BOPEN)
        }
        if (checkresult) return (token::IMPMUL);
        implicit_mul_might_follow = false;
        last_token = token::BOPEN;
        return (token_type(yytext[0]));
      }
<II>[})\]] { /* Normal closing brackets */
        implicit_mul_might_follow = true;
        last_token = token::BCLOSE;
        return (token_type(yytext[0]));
      }
<II>\\\{|ldbracket|ld?line|lbrace|langle|lceil|lfloor { /* Extended opening brackets */
        CHECK_IMPLICIT_MUL(token::BOPEN)
        if (checkresult) return (token::IMPMUL);
        implicit_mul_might_follow = false;
        last_token = token::BOPEN;
        yylval->emplace<std::string>(yytext);
        return (token::BOPEN);
      }
<II>\\\}|rdbracket|rd?line|rbrace|rangle|rceil|rfloor { /* Extended closing brackets */
        implicit_mul_might_follow = true;
        last_token = token::BCLOSE;
        yylval->emplace<std::string>(yytext);
        return (token::BCLOSE);
      }
%{
// ------------------------------------------------------
// normal symbols, unit or function names, incomplete subscripts
%}
<II>true  { yylval->emplace<bool>(true); return token::BOOL; }
<II>false { yylval->emplace<bool>(false); return token::BOOL; }
<II>@prev@ {
        // equation label referencing the previous equation
        CHECK_IMPLICIT_MUL(token::LABEL)
        if (checkresult) return (token::IMPMUL);
        implicit_mul_might_follow = true;
        last_token = token::LABEL;
        yylval->emplace<std::string>("prev");
        return (token::LABEL);
      }
<II>@prev[[:digit:]]+@ {
        // equation label referencing the nth previous equation
        CHECK_IMPLICIT_MUL(token::LABEL)
        if (checkresult) return (token::IMPMUL);
        implicit_mul_might_follow = true;
        last_token = token::LABEL;
        std::string label(yytext);

        try {
          yylval->emplace<std::string>(compiler->getPreviousEquationLabel(std::stoi(label.substr(5, label.size() - 6))));
          if (yylval->as<std::string>().empty())
            yylval->as<std::string>() = std::string(yytext); // nth previous equation does not exist
        } catch (std::exception&) {
          yylval->emplace<std::string>(yytext); // Invalid string for conversion to integer
        }

        return (token::LABEL);
      }
<II>@[^@]+@ {
        // an equation label
        std::string label(yytext);
        label.erase(0,1);
        label.erase(label.length()-1, 1);
        auto toktype = (compiler->is_expression_label(compiler->exlabel_ns(label, true)) ? token::EXLABEL : token::LABEL);
        CHECK_IMPLICIT_MUL(toktype)
        if (checkresult) return (token::IMPMUL);
        implicit_mul_might_follow = true;
        last_token = toktype;
        yylval->emplace<std::string>(std::move(label));
        // must_autoformat is set in smathparser
        return (toktype);
      }

<II>(?i:sum){BLANK}*(?i:from) {
         CHECK_IMPLICIT_MUL(token::FUNC)
         if (checkresult) return (token::IMPMUL);
         implicit_mul_might_follow = false;
         last_token = token::FUNC;
         return (token::SUMFROM);
       }
<II>(?i:product) {
         CHECK_IMPLICIT_MUL(token::FUNC)
         if (checkresult) return (token::IMPMUL);
         implicit_mul_might_follow = false;
         last_token = token::FUNC;
         return (token::PRODUCT);
       }
<II>(?i:int) {
         CHECK_IMPLICIT_MUL(token::FUNC)
         if (checkresult) return (token::IMPMUL);
         implicit_mul_might_follow = false;
         last_token = token::FUNC;
         return (token::INTEGRAL);
       }
<II>(?i:differential)|(?i:partial) {
         CHECK_IMPLICIT_MUL(token::FUNC)
         if (checkresult) return (token::IMPMUL);
         implicit_mul_might_follow = false;
         last_token = token::FUNC;
         std::string kw = std::string(yytext);
         std::transform(kw.begin(), kw.end(), kw.begin(), ::toupper);
         yylval->emplace<std::string>(kw);
         return (token::DIFFERENTIAL);
       }
<II>(?i:textfield) {
         CHECK_IMPLICIT_MUL(token::FUNC)
         if (checkresult) return (token::IMPMUL);
         implicit_mul_might_follow = false;
         last_token = token::FUNC;
         return (token::TEXTFIELD);
       }
<II>(?i:tablecell) {
         CHECK_IMPLICIT_MUL(token::FUNC)
         if (checkresult) return (token::IMPMUL);
         implicit_mul_might_follow = false;
         last_token = token::FUNC;
         return (token::TABLECELL);
       }
<II>(?i:calccell) {
         CHECK_IMPLICIT_MUL(token::FUNC)
         if (checkresult) return (token::IMPMUL);
         implicit_mul_might_follow = false;
         last_token = token::FUNC;
         return (token::CALCCELL);
       }

<II>{ATTRIBUTE} {
         CHECK_IMPLICIT_MUL(token::IDENTIFIER)
         if (checkresult) return (token::IMPMUL);
         implicit_mul_might_follow = false;
         last_attribute = last_attribute + (last_attribute.empty() ? "" : " ") + yytext;
         last_token = token::ATTRIBUTE;
         // don't return the attribute because it must be concatenated with the identifier
       }
<II>({IDENTIFIER}?::)?{IDENTIFIER}|({IDENTIFIER}?::)?nospace\{{IDENTIFIER}{BLANK}+{IDENTIFIER}\}|({IDENTIFIER}?::)\{{ATTRIBUTE}{BLANK}+{IDENTIFIER}\} {
         // Note: f \rhs{"prev"} does not mean f * rhs{"prev"}, but f(\rhs{"prev"})
         CHECK_IMPLICIT_MUL(token::IDENTIFIER)
         if (checkresult) return (token::IMPMUL);
         std::string name(yytext);

         MSG_INFO(4,  "Found string "  << name << endline);

         if ((name[0] == '%') && (name.length() > 1) && compiler->isUnit(name.substr(1))) {
           // The name is a unit
           name.erase(0,1);
           implicit_mul_might_follow = true;
           last_token = token::UNIT;
           yylval->emplace<std::string>(name);
           return (token::UNIT);
         }

         if (last_token == token::ATTRIBUTE) // Concatenate the attribute with the name
           name = "{" + last_attribute + " " + name + "}";
         last_attribute = ""; // Clear attribute
         name = compiler->varname_ns(name);

         if (compiler->getsymtype(name) == t_function) {
           MSG_INFO(4,  "Found existing function " << name << endline);
           // Note that the function might or might not be followed by an argument!
           // The case FUNC BOPEN was handled in CHECK_IMPLICIT_MUL
           if (last_token == token::FUNC) {
                // Check a second time to handle the case FUNC FUNC
                CHECK_IMPLICIT_MUL(token::FUNC)
                if (checkresult) return (token::IMPMUL);
           }
           last_token = token::FUNC;
           yylval->emplace<std::string>(name); // Handle namespace in function name
         } else if (compiler->getsymprop(name) == p_vector) {
           MSG_INFO(4,  "Found existing vector variable " << name << endline);
           last_token = token::VSYMBOL;
           yylval->emplace<GiNaC::expression>(compiler->getsym(name));
         } else if (compiler->getsymprop(name) == p_matrix) {
           MSG_INFO(4,  "Found existing matrix variable " << name << endline);
           last_token = token::MSYMBOL;
           yylval->emplace<GiNaC::expression>(compiler->getsym(name));
         } else if (compiler->getsymtype(name) == t_variable) {
           MSG_INFO(4,  "Found existing variable " << name << endline);
           last_token = token::SYMBOL;
           yylval->emplace<GiNaC::expression>(compiler->getsym(name));
         } else if (compiler->getsymtype(name) == t_constant) {
           MSG_INFO(4,  "Found existing constant " << name << endline);
           last_token = token::SYMBOL;
           yylval->emplace<GiNaC::expression>(compiler->getsym(name));
         } else {
           MSG_INFO(4,  "Found new identifier " << name << endline);
           last_token = token::IDENTIFIER;
           yylval->emplace<std::string>(name);
         }

         implicit_mul_might_follow = true;
         return (last_token);
       }
%{
// ------------------------------------------------------
// Numbers, strings
%}
<II>{DIGIT}+ { CHECK_IMPLICIT_MUL(token::DIGITS)
          if (checkresult) return (token::IMPMUL);
          implicit_mul_might_follow = true;
          last_token = token::DIGITS;
          yylval->emplace<std::string>(yytext);
          return (token::DIGITS);
        }
<II>\"   { // begin string
          BEGIN(SCANSTR);
          yylval->emplace<std::string>();
        }
<SCANSTR>\\\"   { // allow escaped quotes
                  yylval->as<std::string>() += "\"";
                }
<SCANSTR>\\     { // single escaped backslash
                  yylval->as<std::string>() += "\\";
                }
<SCANSTR>\n     { // closing quotes were forgotten, add them and continue
                  CHECK_IMPLICIT_MUL(token::STRING)
                  if (checkresult) return (token::IMPMUL);

                  yylval->as<std::string>() += "\"";
                  yylloc->columns(-yyleng);
                  yyless(0);
                  BEGIN(II);
                  implicit_mul_might_follow = true;
                  last_token = token::STRING;
                  return (token::STRING);
                }
<SCANSTR><<EOF>> { // closing quotes were forgotten, add them
                  if (implicit_mul_might_follow) {
                    implicit_mul_might_follow = false;
                    last_token = token_type(' ');
                    return token::IMPMUL;
                  }
                  yylval->as<std::string>() += "\""; // Note: This quote gets lost, probably when creating the iFormulaLine
                  BEGIN(II);
                  return token::STRING;
                }
<SCANSTR>[^\\"\n]* {
                 yylval->as<std::string>() += std::string(yytext);
               }
<SCANSTR>\"    { // end string. This might also be a UNIT, therefore we do the IMPMUL checks
                 CHECK_IMPLICIT_MUL(token::STRING)
                 if (checkresult) return (token::IMPMUL);
                 BEGIN(II);
                 implicit_mul_might_follow = true;
                 last_token = token::STRING;
                 return (token::STRING);
                }

%{
// ------------------------------------------------------
// One-letter tokens and the rest
%}
<II>\%\%.*$ { // Comment in a line starting with %%ii
              BEGIN(INITIAL);
              yylval->emplace<std::string>(yytext);
              return (token::COMMENT);
            }
<II>\n%%ii\+ { /* extended formula */ }
<II>\n   { // smath formulas cannot extend over line breaks
              if (include_level > 0) yylloc->lines (1); // line count for include files
              implicit_mul_might_follow = false;
              last_token = token_type(' ');
              BEGIN(INITIAL);
              return (token_type('\n'));
            }
<II>{BLANK}+    {
              // Eat whitespace
              if (last_token == token::DIGITS) // for cases like 5 1/%s
                last_token = token_type(' ');
            }
<II>##      { // Matrix row separator
              implicit_mul_might_follow = false;
              last_token = token::DOUBLEHASH;
              return (token::DOUBLEHASH);
            }
<II>\^T     { // Matrix or vector transposition
              implicit_mul_might_follow = true;
              last_token = token::TRANSPOSE;
              return (token::TRANSPOSE);
            }
<II>!=|<>   {
              implicit_mul_might_follow = false;
              last_token = token::NEQ;
              return (token::NEQ);
            }

<II>{SEPARATOR} {
              MSG_INFO(4,  "Found separator: " << yytext << endline);
              implicit_mul_might_follow = false;
              last_token = token_type(*yytext);
              return (token_type(*yytext));
            }
<II>.           { /* everything else */
              MSG_INFO(4,  "Found token: '" << yytext << "'" << endline);
              if (yytext[0] == '!')
                implicit_mul_might_follow = true; // factorial
              else
                implicit_mul_might_follow = false;
              // register the last token for correct parsing of x^y_n -> (x_n)^y, not x^(y_n)
              last_token = token_type(*yytext);
              return token_type(*yytext);
            }
<II>_ii_ {
              MSG_INFO(1, "Found magic token in iFormula" << endline);
              yylval->emplace<std::string>("");
              BEGIN(SCANUSERTEXT);
              return (token::MAGIC);
            }
<II><<EOF>> {
              implicit_mul_might_follow = false;
              last_token = token_type('\n');
              BEGIN(INITIAL);
              return token::ENDSTRING;
            }
%%

// Input handling
#include <istream>
#include <fstream>

namespace imath {
  void smathlexer::scan_begin(std::istream& input) {
    if (YY_CURRENT_BUFFER != NULL)
      yy_delete_buffer(YY_CURRENT_BUFFER);
    auto scan_buffer = yy_create_buffer(input, YY_BUF_SIZE);
    yy_switch_to_buffer(scan_buffer);

    implicit_mul_might_follow = false;
    last_token = imath::smathparser::token_type(' ');
    BEGIN(INITIAL);
    yy_flex_debug = 1;
  }

  void smathlexer::scan_end() {
    if (YY_CURRENT_BUFFER != NULL)
      yy_delete_buffer(YY_CURRENT_BUFFER);
    BEGIN(INITIAL);
  }

  bool smathlexer::begin_include(const std::string& fname) {
    auto input = new std::ifstream(fname, std::ios_base::in);
    if (!input->good() || input->eof())
      return false;

    auto new_buffer = yy_create_buffer(*input, YY_BUF_SIZE);
    yypush_buffer_state(new_buffer);

    implicit_mul_might_follow = false;
    last_token = imath::smathparser::token_type(' ');
    BEGIN(INITIAL);
    return true;
  }

  bool smathlexer::finish_include() {
    if (YY_CURRENT_BUFFER != nullptr)
      yypop_buffer_state();

    implicit_mul_might_follow = false;
    last_token = imath::smathparser::token_type(' ');
    BEGIN(INITIAL);
    return (YY_CURRENT_BUFFER == NULL);
  }
}
