/***************************************************************************
    iFormulaLine.hxx  -  iFormulaLine - header file
    internal representation of an smath formula line in a text document
                             -------------------
    begin                : Sun Feb 5 2011
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
#ifndef _IFORMULALINE_HXX
#define _IFORMULALINE_HXX

#ifdef INSIDE_SM
#include <imath/imathutils.hxx>
#include <imath/option.hxx>
#include <imath/printing.hxx>
#else
#include "imathutils.hxx"
#include "option.hxx"
#include "printing.hxx"
#endif

// Mapping of formula types to the index in the list box of the edit formula dialog
enum formulaType {
  formulaTypeEquation = 0,
  formulaTypeExpression = 1,
  formulaTypeConstant = 2,
  formulaTypeFunctionDeclaration = 3,
  formulaTypeFunctionDefinition = 4,
  formulaTypeVectorDeclaration = 5,
  formulaTypeMatrixDeclaration = 6,
  formulaTypeUnit = 7,
  formulaTypeOptions = 8,
  formulaTypeClearall = 9,
  formulaTypeDelete = 10,
  formulaTypeChart = 11,
  formulaTypeText = 12,
  formulaTypePrintval = 13,
  formulaTypeExplainval = 14,
  formulaTypePrefix = 15,
  formulaTypePosvar = 16,
  formulaTypeRealvar = 17,
  formulaTypeTablecell = 18,
  formulaTypeCalccell = 19,
  formulaTypeReadfile = 20,
  formulaTypeComment = 21, // Note: Unused for now
  formulaTypeResult = 22, // Note: Unused for now
  formulaTypeEmptyLine = 23, // Note: Unused for now
  formulaTypeNamespace = 24,
  formulaTypeUpdate = 25,
  formulaTypeError = 26
};

enum depType {
  depNone,
  depIn, // Node depends on other nodes
  depOut, // Node influences other nodes
  depInOut, // Both
  depRecalc, // Change in this node requires recalculation of all dependent nodes
  depRedisplay // Change in this node requires re-displaying all dependent nodes
};

/// Class hierarchy for storing user text
class IMATH_DLLPUBLIC iFormulaLine;
class IMATH_DLLPUBLIC iFormulaNodeExpression;
typedef std::shared_ptr<iFormulaLine> iFormulaLine_ptr;
typedef std::shared_ptr<iFormulaNodeExpression> iExpression_ptr;

class IMATH_DLLPUBLIC textItem {
public:
  textItem(const OUString& t, const bool op = false) : _text(t.trim()), _expr(GiNaC::_expr0), _newline(t.trim().equalsAsciiL("newline", 7)), _operator(op), _expression(false) {}
  textItem(const std::string& t, const bool op = false) : _text(OUS8(t).trim()), _expr(GiNaC::_expr0), _newline(OUS8(t).trim().equalsAsciiL("newline", 7)), _operator(op), _expression(false) {}
  textItem(const GiNaC::expression& e) : _text(OU("")), _expr(e), _newline(false), _operator(false), _expression(false) {}
  textItem(const textItem&) = default; // Required to avoid compiler warning
  ~textItem() {}; // Required to avoid compiler warning
  std::shared_ptr<textItem> clone() const { return std::make_shared<textItem>(*this); }
  bool isNewline() const { return _newline; }
  bool isOperator() const { return _operator; }
  bool isExpression() const { return _expression; }
  OUString getText() const { return _text; }
  GiNaC::expression getExpression() const { return _expr; }
private:
  OUString _text;
  GiNaC::expression _expr;
  bool _newline;
  bool _operator;
  bool _expression;
};

/// Stores one text line of an iFormula with its type
// Possible line formats:
// for iStatement:
// %%ii command formula
// for iText:
// %%ii options TEXT textlist
// for iExpression:
// %%ii label options command hide formula
// for iEquation: (note that lhs is also set)
// %%ii label options command hide formula
// for iResult:
// text %%gg
// for iComment:
// %% text
class IMATH_DLLPUBLIC iFormulaLine {
public:
  // Constructors
  iFormulaLine(std::vector<OUString>&& formulaParts);
  iFormulaLine(std::shared_ptr<GiNaC::optionmap> g_options);
  iFormulaLine(std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options, std::vector<OUString>&& formulaParts);
  iFormulaLine(const iFormulaLine&) = default;
  virtual ~iFormulaLine() {
#ifdef DEBUG_CONSTR_DESTR
    MSG_INFO(3, "Destructing iFormulaLine" << endline);
#endif
};
  // Deep copy
  virtual iFormulaLine_ptr clone() const;

  // Move and convert. This can only work if a matching constructor for T2 exists
  template<typename T1, typename T2> static std::shared_ptr<T2> move(const std::shared_ptr<iFormulaLine>& source) {
    auto pT1 = std::dynamic_pointer_cast<T1>(source);
    return std::make_shared<T2>(std::move(*pT1));
  }

  // Textual output
  /// Print the formula as input by the user, without applying any formatting except decimal separator according to locale
  OUString printFormula() const;
  OUString getFormula() const;
  void setFormula(const OUString& f);
  void setFormula(std::vector<OUString>&& formulaParts);
  void addFormulaPart(const OUString& f);

  /// Return the message if the line has an error
  OUString getErrorMessage() const;

  /// Build the raw text of the line from its components
  virtual OUString print() const;

  /// Display the formula on the controller (charts) or return a list of display lines
  virtual std::vector<std::vector<OUString>> display(const Reference<XModel>& xModel = Reference<XModel>()) const;

  // Properties
  /// Set and get the line's global options
  void setGlobalOptions(std::shared_ptr<GiNaC::optionmap> g_options) { global_options = g_options; }
  std::shared_ptr<GiNaC::optionmap> getGlobalOptions() const { return global_options; }
  // Set and get the line's base font height
  void setBasefontHeight(const unsigned h) { basefontheight = h; }
  unsigned getBasefontHeight() const { return basefontheight; }
  /// Return true if the line has local options
  bool hasOptions() const { return options.size() > 0; }
  /// Return the iFormula command
  virtual OUString getCommand() const { return OU(""); }
  /// Return true if this type of line can have (global or local) options defined (e.g. TEXT, EQDEF, EXDEF)
  virtual bool canHaveOptions() const { return false; }
  /// Returns true if this type of line is an expression or equation
  virtual bool isExpression() const { return false; }
  /// Returns true if this type of node is displayable (e.g. not a comment)
  virtual bool isDisplayable() const { return true; }

  /// For filling dialogs
  virtual formulaType getSelectionType() const { return formulaTypeEquation; };

  // Option management
  /// Check if the option exists for this line
  inline bool hasOption(const option_name o) const { return options.find(o) != options.end(); }
  /// Get the value of an option for this line
  const option& getOption(const option_name o, const bool force_global = false) const;
  /// Set the value of an option for this line if its value differs from the global option
  void setOption(const option_name name, const option& o);
  /// Force the value of an option for this line even if its value is the same as the global option
  void setOptionForce(const option_name name, const option& o);

  /// Print the options
  OUString printOptions() const;

  /// Force automatic formatting of this line by inserting the forceautoformat=true option
  void force_autoformat(const bool value);

  /// Is raw formatting possible or must we format the result line ourselves?
  sal_Bool autoformat_required() const;

  /// Error position
  void markError(const OUString& compiledText, const int errorStart, const int errorEnd, const OUString& errorMessage);
  bool hasError() const { return error; }

  // Dependency management
  virtual depType dependencyType() const { return depNone; }
  // Output the dependencies (ancestors and children) of this iFormula in graphviz dot format
  virtual std::string getGraphLabel() const;
  std::set<GiNaC::expression, GiNaC::expr_is_less> getIn() const { return in; }
  std::set<GiNaC::expression, GiNaC::expr_is_less> getOut() const { return out; }

protected:
  /// the iFormula options specified globally for this line
  std::shared_ptr<GiNaC::optionmap> global_options;

  /// the iFormula options specified for this line only
  GiNaC::optionmap options;

  /// The parts of the formula, as entered verbatim by the user
  std::vector<OUString> _formulaParts;

  /// The base font height of the iFormula and the TextContent object
  unsigned basefontheight;

  /// Changed status of this line
  bool changed;

  /// Error status of this line
  bool error;

  /// Dependency tracking
  std::set<GiNaC::expression, GiNaC::expr_is_less> in, out;
};

class IMATH_DLLPUBLIC iFormulaNodeComment : public iFormulaLine {
public:
  iFormulaNodeComment(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);

  virtual OUString print() const override;

  virtual formulaType getSelectionType() const override { return formulaTypeComment; }
  virtual bool isDisplayable() const override { return false; }
};

class IMATH_DLLPUBLIC iFormulaNodeEmptyLine : public iFormulaLine {
public:
  iFormulaNodeEmptyLine(std::shared_ptr<GiNaC::optionmap> g_options);

  virtual OUString print() const override { return OU(""); }

  virtual formulaType getSelectionType() const override { return formulaTypeEmptyLine; }
  virtual bool isDisplayable() const override { return false; }
};

class IMATH_DLLPUBLIC iFormulaNodeResult : public iFormulaLine {
public:
  iFormulaNodeResult(const OUString& text);

  virtual OUString print() const override;

  virtual formulaType getSelectionType() const override { return formulaTypeResult; }
  virtual bool isDisplayable() const override { return false; }
};

class IMATH_DLLPUBLIC iFormulaNodeError : public iFormulaLine {
public:
  iFormulaNodeError(std::shared_ptr<GiNaC::optionmap> g_options, const OUString& compiledText);
  virtual OUString getCommand() const override { return OU("ERROR"); }

  virtual OUString print() const override;

  virtual formulaType getSelectionType() const override { return formulaTypeError; }
  virtual bool isDisplayable() const override { return true; }
};

class IMATH_DLLPUBLIC iFormulaNodeStatement : public iFormulaLine {
public:
  iFormulaNodeStatement(std::shared_ptr<GiNaC::optionmap> g_options);
  iFormulaNodeStatement(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  iFormulaNodeStatement(const iFormulaNodeStatement& other) = delete;
  iFormulaNodeStatement(iFormulaNodeStatement&& other) noexcept = default;
  virtual ~iFormulaNodeStatement() {};

  virtual OUString print() const override;

  virtual formulaType getSelectionType() const override = 0;
  virtual bool canHaveOptions() const { return true; } // Actually only option echo is relevant
  virtual bool isDisplayable() const override { return false; }
  virtual depType dependencyType() const override { return depRecalc; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmNamespace : public iFormulaNodeStatement {
public:
  iFormulaNodeStmNamespace(std::shared_ptr<GiNaC::optionmap> g_options, const OUString& cmd, std::vector<OUString>&& formulaParts);
  virtual OUString getCommand() const override { return (isBegin ? OU("BEGIN") : OU("END")); }
  virtual formulaType getSelectionType() const override { return formulaTypeNamespace; }
private:
  bool isBegin;
};

class IMATH_DLLPUBLIC iFormulaNodeStmOptions : public iFormulaNodeStatement {
public:
  iFormulaNodeStmOptions(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  virtual OUString getCommand() const override { return OU("OPTIONS"); }
  virtual formulaType getSelectionType() const override { return formulaTypeOptions; }
  // dependencyType could be depRedisplay for all options except realroots
};

class IMATH_DLLPUBLIC iFormulaNodeStmFunction : public iFormulaNodeStatement {
public:
  iFormulaNodeStmFunction(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts, const GiNaC::expression& f);
  virtual OUString getCommand() const override { return OU("FUNCTION"); }
  virtual formulaType getSelectionType() const override { return formulaTypeFunctionDeclaration; }
  // dependencyType recalc e.g. if function name was changed
};

class IMATH_DLLPUBLIC iFormulaNodeStmUnitdef : public iFormulaNodeStatement {
public:
  iFormulaNodeStmUnitdef(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  virtual OUString getCommand() const override { return OU("UNITDEF"); }
  virtual formulaType getSelectionType() const override { return formulaTypeUnit; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmPrefixdef : public iFormulaNodeStatement {
public:
  iFormulaNodeStmPrefixdef(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  virtual OUString getCommand() const override { return OU("PREFIXDEF"); }
  virtual formulaType getSelectionType() const override { return formulaTypePrefix; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmVectordef : public iFormulaNodeStatement {
public:
  iFormulaNodeStmVectordef(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  iFormulaNodeStmVectordef(const iFormulaNodeStatement& other) = delete;
  iFormulaNodeStmVectordef(iFormulaNodeStatement&& other) : iFormulaNodeStatement(std::move(other)) {}
  virtual OUString getCommand() const override { return OU("VECTORDEF"); }
  virtual formulaType getSelectionType() const override { return formulaTypeVectorDeclaration; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmMatrixdef : public iFormulaNodeStatement {
public:
  iFormulaNodeStmMatrixdef(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  iFormulaNodeStmMatrixdef(const iFormulaNodeStatement& other) = delete;
  iFormulaNodeStmMatrixdef(iFormulaNodeStatement&& other) : iFormulaNodeStatement(std::move(other)) {}
  virtual OUString getCommand() const override { return OU("MATRIXDEF"); }
  virtual formulaType getSelectionType() const override { return formulaTypeMatrixDeclaration; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmRealvardef : public iFormulaNodeStatement {
public:
  iFormulaNodeStmRealvardef(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  iFormulaNodeStmRealvardef(const iFormulaNodeStatement& other) = delete;
  iFormulaNodeStmRealvardef(iFormulaNodeStatement&& other) : iFormulaNodeStatement(std::move(other)) {}
  virtual OUString getCommand() const override { return OU("REALVARDEF"); }
  virtual formulaType getSelectionType() const override { return formulaTypeRealvar; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmPosvardef : public iFormulaNodeStatement {
public:
  iFormulaNodeStmPosvardef(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  iFormulaNodeStmPosvardef(const iFormulaNodeStatement& other) = delete;
  iFormulaNodeStmPosvardef(iFormulaNodeStatement&& other) : iFormulaNodeStatement(std::move(other)) {}
  virtual OUString getCommand() const override { return OU("POSVARDEF"); }
  virtual formulaType getSelectionType() const override { return formulaTypePosvar; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmClearall : public iFormulaNodeStatement {
public:
  iFormulaNodeStmClearall(std::shared_ptr<GiNaC::optionmap> g_options);
  virtual OUString getCommand() const override { return OU("CLEAREQUATIONS"); }
  virtual formulaType getSelectionType() const override { return formulaTypeClearall; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmDelete : public iFormulaNodeStatement {
public:
  iFormulaNodeStmDelete(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  virtual OUString getCommand() const override { return OU("DELETE"); }
  virtual formulaType getSelectionType() const override { return formulaTypeDelete; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmUpdate : public iFormulaNodeStatement {
public:
  iFormulaNodeStmUpdate(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  virtual OUString getCommand() const override { return OU("UPDATE"); }
  virtual formulaType getSelectionType() const override { return formulaTypeUpdate; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmTablecell : public iFormulaNodeStatement {
public:
  iFormulaNodeStmTablecell(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  virtual OUString getCommand() const override { return OU("SETTABLECELL"); }
  virtual formulaType getSelectionType() const override { return formulaTypeTablecell; }
  virtual depType dependencyType() const override { return depIn; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmCalccell : public iFormulaNodeStatement {
public:
  iFormulaNodeStmCalccell(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  virtual OUString getCommand() const override { return OU("SETCALCCELLS"); }
  virtual formulaType getSelectionType() const override { return formulaTypeCalccell; }
  virtual depType dependencyType() const override { return depIn; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmReadfile : public iFormulaNodeStatement {
public:
  iFormulaNodeStmReadfile(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  virtual OUString getCommand() const override { return OU("READFILE"); }
  virtual formulaType getSelectionType() const override { return formulaTypeReadfile; }
};

class IMATH_DLLPUBLIC iFormulaNodeStmChart : public iFormulaNodeStatement {
public:
  iFormulaNodeStmChart(std::shared_ptr<GiNaC::optionmap> g_options, std::vector<OUString>&& formulaParts);
  virtual OUString getCommand() const override { return OU("CHART"); }
  virtual formulaType getSelectionType() const override { return formulaTypeChart; }
};

class IMATH_DLLPUBLIC iFormulaNodeExpression : public iFormulaLine {
public:
  iFormulaNodeExpression(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, const OUString& label,
    const GiNaC::expression& expr, const bool hide
  );
  iFormulaNodeExpression(const iFormulaNodeExpression& other) = default;
  iFormulaNodeExpression(iFormulaNodeExpression&& other) noexcept = default;
  virtual iFormulaLine_ptr clone() const override;

  OUString getLabel() const { return _label; }
  void setLabel(const OUString& label) { _label = label; }
  bool getHide() const { return _hide; }
  void setHide(const bool hide) { _hide = hide; }
  GiNaC::expression getExpression() const { return _expr; }

  virtual OUString print() const override;
  /// Print an expression with options and units of the node
  OUString printEx(const GiNaC::expression& e) const;

  virtual bool canHaveOptions() const override { return true; }
  virtual bool isExpression() const override { return true; }

  virtual std::string getGraphLabel() const override;
  virtual depType dependencyType() const override { return depIn; }
protected:
  OUString _label;
  GiNaC::expression _expr;
  bool _hide;
  GiNaC::unitvec _unitconv;
};

class IMATH_DLLPUBLIC iFormulaNodeText : public iFormulaNodeExpression {
public:
  iFormulaNodeText(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, std::vector<std::shared_ptr<textItem>>&& textlist);
  iFormulaNodeText(const iFormulaNodeText& other);
  virtual iFormulaLine_ptr clone() const override;

  virtual std::vector<std::vector<OUString>> display(const Reference<XModel>&) const override;

  virtual OUString getCommand() const { return OU("TEXT"); }
  virtual bool isExpression() const override { return false; }
  virtual formulaType getSelectionType() const override { return formulaTypeText; }

private:
  /// List of arbitrary user text, operators, quoted strings, newlines and expressions
  std::vector<std::shared_ptr<textItem>> _textlist;
};

class IMATH_DLLPUBLIC iFormulaNodeEx : public iFormulaNodeExpression {
public:
  iFormulaNodeEx(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, const OUString& label,
    const GiNaC::expression& expr, const bool hide
  );
  iFormulaNodeEx(const iFormulaNodeExpression& other) = delete;
  iFormulaNodeEx(iFormulaNodeExpression&& other) : iFormulaNodeExpression(std::move(other)) {}

  virtual std::vector<std::vector<OUString>> display(const Reference<XModel>&) const override;

  virtual OUString getCommand() const override { return OU("EXDEF"); }
  virtual formulaType getSelectionType() const override { return formulaTypeExpression; }
};

class IMATH_DLLPUBLIC iFormulaNodeValue : public iFormulaNodeExpression {
public:
  iFormulaNodeValue(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, const OUString& label,
    const GiNaC::expression& expr, const bool hide,
    const GiNaC::expression& lh
  );
  iFormulaNodeValue(const iFormulaNodeExpression& other) = delete;
  iFormulaNodeValue(iFormulaNodeExpression&& other) : iFormulaNodeExpression(std::move(other)) {}
  virtual iFormulaLine_ptr clone() const override;

  virtual bool canHaveOptions() const override { return true; }
  virtual bool isExpression() const { return true; }

protected:
  GiNaC::expression _lh;
};

class IMATH_DLLPUBLIC iFormulaNodePrintval : public iFormulaNodeValue {
public:
  iFormulaNodePrintval(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, const OUString& label,
    const GiNaC::expression& expr, const bool hide,
    const GiNaC::expression& lh,
    const bool algebraic = false, const bool with = false
  );
  iFormulaNodePrintval(const iFormulaNodeEx& other) = delete;
  iFormulaNodePrintval(iFormulaNodeEx&& other) : iFormulaNodeValue(std::move(other)) { _lh = _expr; _label = OU(""); }
  virtual iFormulaLine_ptr clone() const override;

  virtual std::vector<std::vector<OUString>> display(const Reference<XModel>&) const override;

  virtual OUString getCommand() const override;
  virtual formulaType getSelectionType() const override { return formulaTypePrintval; }
private:
  bool _algebraic;
  bool _with;
};

class IMATH_DLLPUBLIC iFormulaNodeExplainval : public iFormulaNodeValue {
public:
  iFormulaNodeExplainval(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, const OUString& label,
    const GiNaC::expression& expr, const bool hide,
    const GiNaC::expression& lh,
    const GiNaC::expression& definition, GiNaC::exhashmap<GiNaC::ex>&& symbols
  );
  virtual iFormulaLine_ptr clone() const override;

  virtual std::vector<std::vector<OUString>> display(const Reference<XModel>&) const override;

  virtual OUString getCommand() const override { return OU("EXPLAINVAL"); }
  virtual formulaType getSelectionType() const override { return formulaTypeExplainval; }

private:
  GiNaC::expression _definition;
  GiNaC::exhashmap<GiNaC::ex> _symbols;
};

class IMATH_DLLPUBLIC iFormulaNodeEq : public iFormulaNodeExpression {
public:
  iFormulaNodeEq(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, const OUString& label,
    const GiNaC::expression& expr, const bool hide
  );
  iFormulaNodeEq(const iFormulaNodeEq& other) = delete;
  iFormulaNodeEq(iFormulaNodeEq&& other) : iFormulaNodeExpression(std::move(other)) {}
  virtual ~iFormulaNodeEq() {};

  virtual OUString print() const override;
  virtual std::vector<std::vector<OUString>> display(const Reference<XModel>&) const override;

  virtual OUString getCommand() const override { return OU("EQDEF"); }
  virtual formulaType getSelectionType() const override { return formulaTypeEquation; }
  virtual bool canHaveOptions() const override { return true; }
  virtual bool isExpression() const { return true; }
  virtual depType dependencyType() const override { return depInOut; }
};

class IMATH_DLLPUBLIC iFormulaNodeConst : public iFormulaNodeEq {
public:
  iFormulaNodeConst(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, const OUString& label,
    const GiNaC::expression& expr, const bool hide
  );
  iFormulaNodeConst(const iFormulaNodeEq& other) = delete;
  iFormulaNodeConst(iFormulaNodeEq&& other) : iFormulaNodeEq(std::move(other)) {}

  virtual OUString getCommand() const override { return OU("CONSTDEF"); }
  virtual formulaType getSelectionType() const override { return formulaTypeConstant; }
};

class IMATH_DLLPUBLIC iFormulaNodeFuncdef : public iFormulaNodeEq {
public:
  iFormulaNodeFuncdef(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, const OUString& label,
    const GiNaC::expression& expr, const bool hide
  );
  iFormulaNodeFuncdef(const iFormulaNodeEq& other) = delete;
  iFormulaNodeFuncdef(iFormulaNodeEq&& other) : iFormulaNodeEq(std::move(other)) {}

  virtual OUString getCommand() const override { return OU("FUNCDEF"); }
  virtual formulaType getSelectionType() const override { return formulaTypeFunctionDefinition; }
};

class IMATH_DLLPUBLIC iFormulaNodeVectordef : public iFormulaNodeEq {
public:
  iFormulaNodeVectordef(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, const OUString& label,
    const GiNaC::expression& expr, const bool hide
  );
  iFormulaNodeVectordef(const iFormulaNodeEq& other) = delete;
  iFormulaNodeVectordef(iFormulaNodeEq&& other) : iFormulaNodeEq(std::move(other)) {}

  virtual OUString getCommand() const override { return OU("VECTORDEF"); }
};

class IMATH_DLLPUBLIC iFormulaNodeMatrixdef : public iFormulaNodeEq {
public:
  iFormulaNodeMatrixdef(
    const GiNaC::unitvec&& unitConversions, std::shared_ptr<GiNaC::optionmap> g_options, GiNaC::optionmap&& l_options,
    std::vector<OUString>&& formulaParts, const OUString& label,
    const GiNaC::expression& expr, const bool hide
  );
  iFormulaNodeMatrixdef(const iFormulaNodeEq& other) = delete;
  iFormulaNodeMatrixdef(iFormulaNodeEq&& other) : iFormulaNodeEq(std::move(other)) {}

  virtual OUString getCommand() const override { return OU("MATRIXDEF"); }
};

#endif // _IFORMULALINE_HXX
