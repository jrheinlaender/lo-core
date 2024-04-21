/***************************************************************************
    iFormulaLine.cxx  -  iFormulaLine - definition file
    internal representation of an smath formula line in a text document
                             -------------------
    begin                : Sun Feb 5 2012
    copyright            : (C) 2012 by Jan Rheinlaender
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

#include <sstream>
#include <regex>
#include <ginac/operators.h>
#ifdef INSIDE_SM
#include <imath/msgdriver.hxx>
#include <imath/equation.hxx>
#include <imath/func.hxx>
#include <imath/settingsmanager.hxx>
#include <imath/unit.hxx>
#include <imath/iFormulaLine.hxx>
#else
#include "msgdriver.hxx"
#include "equation.hxx"
#include "func.hxx"
#include "settingsmanager.hxx"
#include "unit.hxx"
#include "iFormulaLine.hxx"
#endif
#include "operands.hxx"

using namespace GiNaC;

// iFormulaLine implementation =================================================
iFormulaLine::iFormulaLine(std::vector<OUString> formulaParts) :
    _formulaParts(std::move(formulaParts)), error(no_error) {
  MSG_INFO(3,  "Constructing iFormulaLine with formula" << endline);
}

iFormulaLine::iFormulaLine(std::shared_ptr<optionmap> g_options) :
    global_options(g_options), error(no_error) {
  MSG_INFO(3,  "Constructing iFormulaLine with options" << endline);
} // iFormulaLine()

iFormulaLine::iFormulaLine(std::shared_ptr<optionmap> g_options, optionmap l_options, std::vector<OUString> formulaParts) :
    global_options(g_options), options(std::move(l_options)), _formulaParts(std::move(formulaParts)), error(no_error)
{
  MSG_INFO(3,  "Constructing iFormulaLine with global and local options" << endline);
} // iFormulaLine()

iFormulaLine_ptr iFormulaLine::clone() const {
  return std::make_shared<iFormulaLine>(*this);
}

OUString iFormulaLine::print() const {
  return OU("%%ii ") + getCommand() + OU(" ") + getFormula();
}

std::vector<std::vector<OUString>> iFormulaLine::display(const Reference<XModel>&) const
{
    if (error == no_error)
        return {}; // Should not happen, display() is handled by all subclasses

    OUString errorPart = (_formulaParts[1].isEmpty() ? OUString(sal_Unicode(u'\u21B5')) : _formulaParts[1]);

    return
    {
        {"newline "},
        {_formulaParts[0] + "{}bold color red{\"" + errorPart.replace('"', u'\u201C') + "\"}{}" + _formulaParts[2], "{}newline "},
        {"color blue{\"" + _formulaParts[3] + "\"}", "newline "}
    };
}

// We assume that all possible options have values in global_options
const option& iFormulaLine::getOption(const option_name o, const bool force_global) const {
  if (!force_global && hasOption(o)) {
    MSG_INFO(3,  "getOption(): local option found" << endline);
    return options.at(o);
  } else {
    MSG_INFO(3,  "getOption(): global option used" << endline);
    if (global_options->find(o) == global_options->end())
      throw std::runtime_error(std::string("Option #") + std::to_string(o) + " does not exist");
    return global_options->at(o);
  }
} // getOption()

void iFormulaLine::setOptionForce(const option_name name, const option& o) {
  if (!canHaveOptions()) return;
  if (msg::info().checkprio(2)) {
    MSG_INFO(2,  "Forcing line option to '");
    std::ostringstream os;
    o.print(os);
    MSG_INFO(2,  os.str() << "'" << endline);
  }

  if (hasOption(name)) { // option exists already
    option current_option = options.at(name);
    if (msg::info().checkprio(2)) {
      MSG_INFO(2,  "Current local option value is '");
      std::ostringstream os;
      o.print(os);
      MSG_INFO(2,  "'" << endline);
    }

    if (current_option == o) {// has identical value
      MSG_INFO(2,  "Same value, not changing" << endline);
      return;
    } else { // give option the new value
      MSG_INFO(2,  "Setting new local value" << endline);
      options[name] = o;
    }
  } else {
    MSG_INFO(2,  "Adding new local option" << endline);
    options[name] = o;
  }

  changed = true;
} // setOptionForce()

void iFormulaLine::setOption(const option_name name, const option& o) {
  if (!canHaveOptions()) return;

  if (o == global_options->at(name)) {
    MSG_INFO(2,  "Global option has same value, not changing" << endline);
    // Remove the local option if it exists
    if (hasOption(name))
      options.erase(name);
    return;
  }

  setOptionForce(name, o);
} // setOption()

#ifdef _MSC_VER
#include <regex>
#else
#include <regex.h>
#include <unistd.h>
#endif

// Handle decimal point troubles...
OUString adjustLocale(const OUString& s) {
  char dp = imathprint::decimalpoint[0];
  if (dp == '.') return s;
#ifdef _MSC_VER
  // This code can only be compiled with gcc-4.9 or higher and -std=c++11
  //std::regex r("[0-9]*\\.[0-9]*");
  //std::string repl(std::string("$1") + dp + "$2");
  std::regex r("(\\.)([[:digit:]]+)");
  std::string repl = std::string(1, dp) + "$2";
  return OUS8(std::regex_replace(STR(s), r, repl));
#else
  regex_t r;
  regcomp(&r, "\\.[[:digit:]]+", REG_EXTENDED|REG_NEWLINE);
  const int nmatches = 100; // maximum allowed number of matches
  regmatch_t pmatch[nmatches];
  std::string str(STR(s));

  if (regexec(&r, str.c_str(), nmatches, pmatch, 0) == 0) {
    for (unsigned m = 0; m < nmatches; ++m) {
      if (pmatch[m].rm_so == -1)
        break;
      str[pmatch[m].rm_so] = dp;
    }
  }

  regfree(&r);
  return OUS8(str);
#endif
} // adjustLocale()

OUString iFormulaLine::printOptions() const {
  MSG_INFO(3,  "printOptions()" << endline);
  OUString result = Settingsmanager::createOptionString(options);
  if (!result.equalsAscii(""))
    result = result.replaceAt(0,2,OU("{")) + OU("} "); // Removes the leading "; " as a side-effect

  MSG_INFO(3,  "printOptions(): " << STR(result) << endline);
  return result;
} // printOptions()

void iFormulaLine::force_autoformat(const bool value) {
  if (value == true)
    setOptionForce(o_forceautoformat, value);
}

sal_Bool iFormulaLine::autoformat_required() const {
  // Preserving the original formatting of the iFormula is not possible if
  // 1. the user specified the option "autoformat = true|false"
  // 2. the parser determined that autoformatting is required, and set the option "forceautoformat = true"
  // 3. the user set at least one local formatting option that requires autoformatting
  if ((hasOption(o_eqraw)) && (options.size() == 1))
    return !options.at(o_eqraw).value.boolean; // Obey user specification

  if (hasOption(o_forceautoformat) && options.at(o_forceautoformat).value.boolean == true)
    return true; // Parser has forced automatic formatting

  for (const auto& o : options)
      if (o.first != o_autotextmode && o.first != o_echoformula && o.first != o_showlabels)
          return true;

  return (!getOption(o_eqraw).value.boolean);
}

void iFormulaLine::markError(const OUString& compiledText, const int formulaStart, const int errorStart, const int errorEnd, const OUString& errorMessage)
{
    MSG_INFO(0, "iFormulaLine::markError: " << STR(errorMessage) << endline);
    // Note: compiledText always terminates with a newline, which we remove because it is added by iFormula::rebuildRawtext()
    int _errorEnd = (errorEnd > compiledText.getLength() - 1 ? compiledText.getLength() - 1 : errorEnd);
    OUString offendingText = compiledText.copy(errorStart, _errorEnd - errorStart);
    if (errorMessage.indexOf("implicit multiplication") > 0)
        offendingText = OUString(sal_Unicode(u'\u00D7')); // Unexpected implicit multiplication // TODO implement this independently of the text of the error message

    _formulaParts.clear();
    _formulaParts.emplace_back(compiledText.copy(formulaStart, errorStart - formulaStart));
    _formulaParts.emplace_back(offendingText);
    _formulaParts.emplace_back(_errorEnd == compiledText.getLength() - 1 ? "" : compiledText.copy(_errorEnd, compiledText.getLength() - _errorEnd - 1));
    _formulaParts.emplace_back(errorMessage);
    error = formula_error;
}

std::string iFormulaLine::getGraphLabel() const {
  // return node in dot language
  std::ostringstream address;
  address << (void const *)this;
  // TODO: in and out

  return STR(getCommand()) + address.str();
}

OUString iFormulaLine::getFormula() const {
    switch (error)
    {
        case no_error:
        {
            OUString formula = OU("");
            for (const auto& p : _formulaParts)
                formula += p;
            return formula;
        }
        case label_error:
            return _formulaParts[2];
        case general_error:
            return _formulaParts[0];
        default:
            // All other errors
            return  _formulaParts[0] + _formulaParts[1] + _formulaParts [2];
    }
}

void iFormulaLine::setFormula(const OUString& f) {
  _formulaParts = {f};
  error = no_error;
}

OUString iFormulaLine::getErrorMessage() const {
    if (error == no_error || _formulaParts.size() <= 3)
        return OU("");

    return _formulaParts.at(3);
}

OUString iFormulaLine::printFormula() const {
    OUString formula;

    if (error == formula_error)
        // TODO: A unmatched quote in _formulaParts[0] will mess up the formatting
        formula = _formulaParts[0]  + OU("<span foreground='red' font='bold'>") + (_formulaParts[1].isEmpty() ? OUString(sal_Unicode(u'\u21B5')) : _formulaParts[1]) + OU("</span>") + _formulaParts[2];
    else
        formula = getFormula();

    return adjustLocale(replaceString(formula, OU("\n%%ii+"), OU("")));
}

std::set<expression, expr_is_less> collectSymbols(const expression& e) {
  std::set<expression, expr_is_less> result;
  for (const_preorder_iterator i = e.preorder_begin(); i != e.preorder_end(); ++i) {
      if (is_a<symbol>(*i))
        result.emplace(*i);
      else if (is_a<func>(*i))
        // TODO Since funcs are not created in a factory, the expressions do not compare equal
        result.emplace(ex_to<func>(*i).makepure()); // Use pure func to avoid differences in the arguments
  }
  return result;
}

// NodeComment
iFormulaNodeComment::iFormulaNodeComment(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
  iFormulaLine(g_options, optionmap(), std::move(formulaParts))
{
}

OUString iFormulaNodeComment::print() const {
  return getFormula();
}

// NodeEmptyLine
iFormulaNodeEmptyLine::iFormulaNodeEmptyLine(std::shared_ptr<optionmap> g_options) :
  iFormulaLine(g_options)
{
}

// NodeResult
iFormulaNodeResult::iFormulaNodeResult(const OUString& text) :
    iFormulaLine({text})
{
}

OUString iFormulaNodeResult::print() const {
  return getFormula() + OU(" %%gg");
}

// NodeError
iFormulaNodeError::iFormulaNodeError(std::shared_ptr<GiNaC::optionmap> g_options, const OUString& compiledText) :
    iFormulaLine(g_options)
{
    _formulaParts = {compiledText.copy(5)}; // Drop the %%ii
    error = general_error;
}

OUString iFormulaNodeError::print() const {
    return "%%ii " + getFormula();
}

// NodeStatement
iFormulaNodeStatement::iFormulaNodeStatement(std::shared_ptr<optionmap> g_options) :
    iFormulaLine(g_options) {
}

iFormulaNodeStatement::iFormulaNodeStatement(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
    iFormulaLine(g_options, optionmap(), std::move(formulaParts)) {
}

OUString iFormulaNodeStatement::print() const {
  return OU("%%ii ") + (options.size() > 0 ? printOptions() + OU(" ") : OU("")) + getCommand() + OU(" ") + getFormula();
}

// NodeStmOptions
iFormulaNodeStmOptions::iFormulaNodeStmOptions(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
}

// NodeStmNamespace
iFormulaNodeStmNamespace::iFormulaNodeStmNamespace(std::shared_ptr<optionmap> g_options, const OUString& cmd, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
     isBegin = cmd.equalsAscii("BEGIN");
}

// NodeStmFunction
iFormulaNodeStmFunction::iFormulaNodeStmFunction(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts, const GiNaC::expression& f) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
  out.insert(f);
}

// NodeStmUnitdef
iFormulaNodeStmUnitdef::iFormulaNodeStmUnitdef(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
  // TODO: unit -> out?
}

// NodeStmPrefixdef
iFormulaNodeStmPrefixdef::iFormulaNodeStmPrefixdef(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
}

// NodeStmVectordef
iFormulaNodeStmVectordef::iFormulaNodeStmVectordef(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
  // TODO: vector -> out?
}

// NodeStmMatrixdef
iFormulaNodeStmMatrixdef::iFormulaNodeStmMatrixdef(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
  // TODO: matrix -> out?
}

// NodeStmRealvardef
iFormulaNodeStmRealvardef::iFormulaNodeStmRealvardef(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
  // TODO: var -> out?
}

// NodeStmPosvardef
iFormulaNodeStmPosvardef::iFormulaNodeStmPosvardef(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
  // TODO: var -> out?
}

// NodeStmClearall
iFormulaNodeStmClearall::iFormulaNodeStmClearall(std::shared_ptr<optionmap> g_options) :
    iFormulaNodeStatement(g_options) {
  //  TODO out = {compiler->getSym("all_symbols")};
}

// NodeStmDelete
iFormulaNodeStmDelete::iFormulaNodeStmDelete(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
}

// NodeStmUpdate
iFormulaNodeStmUpdate::iFormulaNodeStmUpdate(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
}

// NodeStmTablecell
iFormulaNodeStmTablecell::iFormulaNodeStmTablecell(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
}

// NodeStmCalccell
iFormulaNodeStmCalccell::iFormulaNodeStmCalccell(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
}

// NodeStmReadfile
iFormulaNodeStmReadfile::iFormulaNodeStmReadfile(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
}

// NodeStmChart
iFormulaNodeStmChart::iFormulaNodeStmChart(std::shared_ptr<optionmap> g_options, std::vector<OUString> formulaParts) :
   iFormulaNodeStatement(g_options, std::move(formulaParts)) {
}

// Node Expression (virtual superclass of Node Ex and Node Eq)
iFormulaNodeExpression::iFormulaNodeExpression(
    GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options,
    std::vector<OUString> formulaParts, const OUString& label,
    const expression& expr, const bool hide
  ) : iFormulaLine(g_options, std::move(l_options), std::move(formulaParts)), _label(label), _expr(expr), _hide(hide), _unitconv(std::move(unitConversions)) {
}

iFormulaLine_ptr iFormulaNodeExpression::clone() const {
  return std::make_shared<iFormulaNodeExpression>(*this);
}

OUString iFormulaNodeExpression::print() const {
  return OU("%%ii ") + (_label.getLength() > 0 ? OU("@") + _label + OU("@ ") : OU("")) + printOptions() + getCommand() + (_hide ? OU("* ") : OU(" ")) + getFormula();
}

// Print an expression with the given options and units
OUString iFormulaNodeExpression::printEx(const expression& e) const {
  MSG_INFO(3,  "printEx() for '" << e << "'" << endline);
  // Create a combined optionmap with all global and local options. Local options have precedence
  optionmap o = options;
  for (const auto& i : *global_options) {
    if (o.find(i.first) == o.end())
      o.emplace(i.first, i.second); // Local option doesn't exist, use global option instead
  }

  expression e_subst = subst_units(e, _unitconv);
  MSG_INFO(3,  "Substituted: '" << e_subst << "'" << endline);

  // Suppress units
  if (getOption(o_suppress_units).value.boolean) {
    remove_units r_units;
    e_subst = r_units(e_subst);
  }

  // Match differentials
  match_differentials match_diffs;
  ex e_matched;
  if (is_a<equation>(e_subst)) {
    // Work-around because for some reason match_diffs() replaces an equation by a relational, losing the modulus
    const equation& eq = ex_to<equation>(e_subst);
    e_matched = dynallocate<equation>(match_diffs(eq.lhs()), match_diffs(eq.rhs()), eq.getop(), eq.getmod());
  } else {
    e_matched = match_diffs(e_subst);
  }

  MSG_INFO(3, "Matched differentials: '" << e_matched << "'" << endline);

  std::ostringstream os;
  o.emplace(o_basefontheight, basefontheight);
  o.emplace(o_fractionlevel, 0);
  imathprint i(os, &o);
  MSG_INFO(3,  "Created imathprint" << endline);

  if (is_a<Unit>(e_matched))
    numeric(1).print(i, 0); // Top level single unit must print with 1 as coefficient
  e_matched.print(i, 0);

  expression preferredUnit;
  if (hasOption(o_units) && !options.at(o_units).value.exvec->empty())
    preferredUnit = options.at(o_units).value.exvec->back();
  else if (!global_options->at(o_units).value.exvec->empty())
    preferredUnit = global_options->at(o_units).value.exvec->back();

  if (!preferredUnit.is_empty() && !getOption(o_suppress_units).value.boolean &&
    (e_matched.is_zero() || (is_a<equation>(e_matched) && ex_to<equation>(e_matched).rhs().is_zero()))) {
    // Special case because GiNaC automatic simplification cancels everything multiplied with zero
    preferredUnit.print(i); // Print only one unit - this is a best guess
  }

  MSG_INFO(3,  "printed on stream" << endline);
  return OUS8(os.str());
} // printEx()

void iFormulaNodeExpression::markError(const OUString& compiledText, const int formulaStart, const int errorStart, const int errorEnd, const OUString& errorMessage)
{
    // Ensure that we are handling a label error
    if (errorStart > compiledText.indexOf(getCommand()))
        return iFormulaLine::markError(compiledText, formulaStart, errorStart, errorEnd, errorMessage);

    _formulaParts.clear();
    _formulaParts.emplace_back("");
    _formulaParts.emplace_back(compiledText.copy(errorStart, errorEnd - errorStart));
    _formulaParts.emplace_back(compiledText.copy(formulaStart));
    _formulaParts.emplace_back(errorMessage);
    error = label_error;
}

std::string iFormulaNodeExpression::getGraphLabel() const {
  if (_label.isEmpty())
    return iFormulaLine::getGraphLabel();
  return "@" + STR(_label) + "@";
}

// NodeText
iFormulaNodeText::iFormulaNodeText(GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options, std::vector<OUString> formulaParts, std::vector<std::shared_ptr<textItem>> textlist)
    : iFormulaNodeExpression(std::move(unitConversions), g_options, std::move(l_options), std::move(formulaParts), OU(""), expression(), false), _textlist(std::move(textlist))
{
  for (const auto& p : _textlist) {
    if (p->isExpression()) {
      auto in1 = collectSymbols(p->getExpression());
      in.insert(in1.begin(), in1.end());
    }
  }
}

iFormulaNodeText::iFormulaNodeText(const iFormulaNodeText& other) : iFormulaNodeExpression(other) {
  for (const auto& t : other._textlist)
    _textlist.emplace_back(t->clone());
}

iFormulaLine_ptr iFormulaNodeText::clone() const {
  return std::make_shared<iFormulaNodeText>(*this);
}

OUString iFormulaNodeText::printFormula() const {
    OUString result = iFormulaLine::printFormula();
    return OUS8(std::regex_replace(STR(result), std::regex("\"_ii_\""), "_ii_"));
}

std::vector<std::vector<OUString>> iFormulaNodeText::display(const Reference<XModel>&) const {
  if (error != no_error)
      return iFormulaLine::display();

  std::vector<std::vector<OUString>> result;
  if (_textlist.empty()) return result;

  std::vector<OUString> line;
  OUString text("");

  for (const auto& textPortion : _textlist) {
    if (textPortion->isNewline()) {
      text += OU(" newline");
      line.emplace_back(text);
      result.emplace_back(line);
      line = std::vector<OUString>();
      text = OU("");
    } else if (textPortion->isOperator()) {
      // Operators go in a column by themselves
      if (text.getLength() > 0)
        line.emplace_back(text);
      line.emplace_back(OU("{}") + textPortion->getText().toAsciiUpperCase() + OU("{}")); // The two empty bracket pairs are required when aligning operators in a matrix
      text = OU("");
    } else if (textPortion->isExpression()) {
      // Expressions and equations go in 1 or 3 columns by themselves
      if (text.getLength() > 0)
        line.emplace_back(text);

      if (is_a<equation>(textPortion->getExpression())) {
        equation eq = ex_to<equation>(textPortion->getExpression());
        line.emplace_back(OU("{alignr ") + printEx(eq.lhs()) + OU("}"));
        line.emplace_back(OU("{}") + OUS8(get_oper(imathprint(), eq.getop(), eq.getmod())).trim() + OU("{}"));
        line.emplace_back(OU("{alignl ") + printEx(eq.rhs()) + OU("}"));
        text = OU("");
      } else {
        line.emplace_back(printEx(textPortion->getExpression()));
        text = OU("");
      }
    } else {
      // Append to previous text
      text += textPortion->getText();
    }
  }

  // Clean up remainders
  if (!text.isEmpty())
    line.emplace_back(text);
  if (!line.empty())
    result.emplace_back(line);

  return result;
}

void iFormulaNodeText::markError(const OUString& compiledText, const int formulaStart, const int errorStart, const int errorEnd, const OUString& errorMessage) {
    iFormulaLine::markError(compiledText, formulaStart, errorStart, errorEnd, errorMessage);
    for (size_t i = 0; i < 3; ++i)
        _formulaParts[i] = OUS8(std::regex_replace(STR(_formulaParts[i]), std::regex("_ii_"), "\"_ii_\"")); // Underscore is starmath subscript token
}

// Node Ex
iFormulaNodeEx::iFormulaNodeEx(
    GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options,
    std::vector<OUString> formulaParts, const OUString& label,
    const expression& expr, const bool hide
  ) : iFormulaNodeExpression(std::move(unitConversions), g_options, std::move(l_options), std::move(formulaParts), label, expr, hide)
{
  in = collectSymbols(_expr);
}

std::vector<std::vector<OUString>> iFormulaNodeEx::display(const Reference<XModel>&) const {
  if (error == no_error && _hide)
      return {};

  OUString what = (autoformat_required() ?
                    printEx(_expr) : // autoformat
                    printFormula()); // preserve user formatting changing decimal separator according to locale

  switch(error)
  {
      case label_error:
        return
        {
            {"newline "},
            {"{}bold color red{(\"" + _formulaParts[1] + "\")}{}" + what, "{}newline "},
            {"color blue{\"" + _formulaParts[3] + "\"}", "newline "}
        };
      case no_error:
        return
        {
            {"{alignl " + what + "}"}
        };
      default:
          return iFormulaLine::display();
  }
}

// Node Value
iFormulaNodeValue::iFormulaNodeValue(
    GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options,
    std::vector<OUString> formulaParts, const OUString& label,
    const expression& expr, const bool hide,
    const expression& lh
  ) : iFormulaNodeExpression(std::move(unitConversions), g_options, std::move(l_options), std::move(formulaParts), label, expr, hide), _lh(lh)
{
  in = collectSymbols(_lh);
}

iFormulaLine_ptr iFormulaNodeValue::clone() const {
  return std::make_shared<iFormulaNodeValue>(*this);
}

// Node Printval
iFormulaNodePrintval::iFormulaNodePrintval(
    GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options,
    std::vector<OUString> formulaParts, const OUString& label,
    const expression& expr, const bool hide,
    const expression& lh,
    const bool algebraic, const bool with
  ) : iFormulaNodeValue(std::move(unitConversions), g_options, std::move(l_options), std::move(formulaParts), label, expr, hide, lh), _algebraic(algebraic), _with(with) {
}

iFormulaLine_ptr iFormulaNodePrintval::clone() const {
  return std::make_shared<iFormulaNodePrintval>(*this);
}

OUString iFormulaNodePrintval::getCommand() const {
  return OU("PRINT") + (_algebraic ? OU("AVAL") : OU("VAL")) + (_with ? OU("WITH") : OU(""));
}

std::vector<std::vector<OUString>> iFormulaNodePrintval::display(const Reference<XModel>&) const {
  if (error != no_error)
      return iFormulaLine::display();

  std::vector<std::vector<OUString>> result;
  std::vector<OUString> line;

  OUString what = (autoformat_required() ?
     printEx(_lh) :
     adjustLocale(replaceString(_formulaParts[(_with ? 1 : 0)], OU("\n%%ii+"), OU(""))));
  line.emplace_back(OU("{alignr ") + what + OU("}"));
  line.emplace_back(OU("{}={}"));
  line.emplace_back(OU("{alignl ") + printEx(_expr) + OU("}")); // The RHS is always calculated and therefore auto-formatted

  result.emplace_back(line);
  return result;
}

// Node Explainval
iFormulaNodeExplainval::iFormulaNodeExplainval(
    GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options,
    std::vector<OUString> formulaParts, const OUString& label,
    const expression& expr, const bool hide,
    const expression& lh,
    const expression& definition, exhashmap<ex> symbols
  ) : iFormulaNodeValue(std::move(unitConversions), g_options, std::move(l_options), std::move(formulaParts), label, expr, hide, lh), _definition(definition), _symbols(std::move(symbols)) {
}

iFormulaLine_ptr iFormulaNodeExplainval::clone() const {
  return std::make_shared<iFormulaNodeExplainval>(*this);
}

std::vector<std::vector<OUString>> iFormulaNodeExplainval::display(const Reference<XModel>&) const {
  if (error != no_error)
      return iFormulaLine::display();

  std::vector<std::vector<OUString>> result;
  std::vector<OUString> line;

  OUString lhs = (autoformat_required() ? printEx(_lh) : adjustLocale(replaceString(_formulaParts[0], OU("\n%%ii+"), OU(""))));
  OUString rhs = printEx(_expr); // The RHS is always calculated and therefore auto-formatted

  // Prepare the definition string
  exmap variables; // Cannot use exhashmap because subs() doesn't accept it
  std::map<std::string, std::string> replacements;
  for (const auto& s : _symbols) {
    std::string newsym = "@@" + ex_to<symbol>(s.first).get_name() + "@@";
    variables.emplace(s.first, symbol(newsym));
    replacements.emplace(newsym, "(" + STR(printEx(s.second)) + ")");
  }
  std::string defstring = STR(printEx(_definition.subs(variables)));
  for (const auto& r : replacements)
    defstring = std::regex_replace(defstring, std::regex(r.first), r.second);

  // Display
  if (_hide) {
    line.emplace_back(OU("{alignr ") + OUS8(defstring) + OU("}"));
    line.emplace_back(OU("{}={}"));
    line.emplace_back(OU("{alignl ") + rhs + OU("}"));
  } else if (_definition.is_equal(_lh)) {
    line.emplace_back(OU("{alignr ") + lhs + OU("}"));
    line.emplace_back(OU("{}={}"));
    line.emplace_back(OUS8(defstring));
    line.emplace_back(OU("{}={}"));
    line.emplace_back(OU("{alignl ") + rhs + OU("}"));
  } else {
    line.emplace_back(OU("{alignr ") + lhs + OU("}"));
    line.emplace_back(OU("{}={}"));
    line.emplace_back(printEx(_definition));
    line.emplace_back(OU("{}={}"));
    line.emplace_back(OUS8(defstring));
    line.emplace_back(OU("{}={}"));
    line.emplace_back(OU("{alignl ") + rhs + OU("}"));
  }

  result.emplace_back(line);
  return result;
}

// Node Eq
iFormulaNodeEq::iFormulaNodeEq(
    GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options,
    std::vector<OUString> formulaParts, const OUString& label,
    const expression& expr, const bool hide
  ) : iFormulaNodeExpression(std::move(unitConversions), g_options, std::move(l_options), std::move(formulaParts), label, expr, hide)
{
  ex lhs = ex_to<equation>(_expr).lhs();
  ex rhs = ex_to<equation>(_expr).rhs();
  if (is_a<symbol>(lhs) || is_a<func>(lhs)) {
    in = collectSymbols(rhs);
    out = {lhs};
  } else if (is_a<symbol>(rhs) || is_a<func>(rhs)) {
    in = collectSymbols(lhs);
    out = {rhs};
  } else {
    in = collectSymbols(lhs);
    auto in2 = collectSymbols(rhs);
    in.insert(in2.begin(), in2.end());
    // out = in; // But we should give priority to direct assignments to a single symbol
  }
}

OUString iFormulaNodeEq::print() const {
    return OU("%%ii @") + _label + OU("@ ") + printOptions() + getCommand() + (_hide ? OU("* ") : OU(" ")) + getFormula();
}

std::vector<std::vector<OUString>> iFormulaNodeEq::display(const Reference<XModel>&) const {
  if (error == no_error && _hide)
      return {};

  const equation& eq = ex_to<equation>(_expr);
  OUString oper = OUS8(get_oper(imathprint(), eq.getop(), eq.getmod())).trim();
  OUString lhs;
  OUString rhs;

   if (autoformat_required()) {
    lhs = printEx(eq.lhs());
    rhs = printEx(eq.rhs());
  } else {
    OUString textEq = printFormula();
    int alignpos = textEq.toAsciiUpperCase().indexOf(oper); // TODO: Formulas with operator signs within stringEx might bring confusion
    lhs = textEq.copy(0, alignpos).trim();
    rhs = textEq.copy(alignpos + oper.getLength()).trim();
  }

  switch(error) {
      case label_error:
        return
        {
            {"newline "},
            {
                "bold color red{(\"" + _formulaParts[1] + "\")}",
                OU("{alignr ") + lhs + OU("}"), OU("{}") + oper + OU("{}"), OU("{alignl ") + rhs + OU("}"),
                "newline "
            },
            {"color blue{\"" + _formulaParts[3] + "\"}", "newline "}
        };
      case no_error:
        return
        {
            {OU("{alignr ") + lhs + OU("}"), OU("{}") + oper + OU("{}"), OU("{alignl ") + rhs + OU("}")}
        };
      default:
        return iFormulaLine::display();
  }
}

// Node Const
iFormulaNodeConst::iFormulaNodeConst(
    GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options,
    std::vector<OUString> formulaParts, const OUString& label,
    const expression& expr, const bool hide
  ) : iFormulaNodeEq(std::move(unitConversions), g_options, std::move(l_options), std::move(formulaParts), label, expr, hide) {
}

// Node Funcdef
iFormulaNodeFuncdef::iFormulaNodeFuncdef(
    GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options,
    std::vector<OUString> formulaParts, const OUString& label,
    const expression& expr, const bool hide
  ) : iFormulaNodeEq(std::move(unitConversions), g_options, std::move(l_options), std::move(formulaParts), label, expr, hide) {
}

// Node Vectordef
iFormulaNodeVectordef::iFormulaNodeVectordef(
    GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options,
    std::vector<OUString> formulaParts, const OUString& label,
    const expression& expr, const bool hide
  ) : iFormulaNodeEq(std::move(unitConversions), g_options, std::move(l_options), std::move(formulaParts), label, expr, hide) {
}

// Node Matrixdef
iFormulaNodeMatrixdef::iFormulaNodeMatrixdef(
    GiNaC::unitvec unitConversions, std::shared_ptr<optionmap> g_options, optionmap l_options,
    std::vector<OUString> formulaParts, const OUString& label,
    const expression& expr, const bool hide
  ) : iFormulaNodeEq(std::move(unitConversions), g_options, std::move(l_options), std::move(formulaParts), label, expr, hide) {
}

