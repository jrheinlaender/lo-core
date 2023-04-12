/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#pragma once

#include <rtl/strbuf.hxx>
#include <sfx2/docfac.hxx>
#include <sfx2/objsh.hxx>
#include <svl/lstner.hxx>
#include <svl/itempool.hxx>
#include <sax/fshelper.hxx>
#include <unotools/lingucfg.hxx>
#include <oox/core/filterbase.hxx>
#include <oox/export/utils.hxx>

#include <com/sun/star/util/XCloseListener.hpp>
#include <com/sun/star/frame/XModel.hpp>

#include "format.hxx"
#include "node.hxx"
#include "parsebase.hxx"
#include "smdllapi.hxx"
#include "mathml/iterator.hxx"

#include <imath/eqc.hxx>
#include <imath/iFormulaLine.hxx>
typedef std::list<iFormulaLine_ptr>::iterator iFormulaLine_it;

class SfxPrinter;
class Printer;
class SmCursor;

namespace oox::formulaimport { class XmlStream; }

#define STAROFFICE_XML  "StarOffice XML (Math)"
inline constexpr OUString MATHML_XML = u"MathML XML (Math)"_ustr;

namespace imath { class smathparser; }

// Announce to Flex the prototype we want for lexing function. This must match the declaration of lex_param at the top of smathparser.yxx
# define YY_DECL imath::smathparser::token::yytokentype imathlex (imath::smathparser::semantic_type* yylval, \
              imath::smathparser::location_type* yylloc, std::shared_ptr<eqc> compiler, \
              unsigned include_level)

namespace smathlexer {
  /// Parser/lexer handling routines
  void scan_begin(const std::string& input);
  void scan_end();
  bool begin_include(const std::string &fname);
  bool finish_include();
};

/* Access to printer should happen through this class only
 * ==========================================================================
 *
 * The printer can belong to the document or the OLE-Container. If the document
 * is an OLE-Document the printer generally belongs to the container too.
 * But the container maybe works with a different MapUnit than the server.
 * Referring to the MapMode the printer will be accordingly adjusted in the
 * constructor and restored in the destructor. This brings that this class
 * is always allowed to exists only a short time (e.g. while painting).
 * The control whether the printer is self-generated, gotten from the server
 * or is NULL then, is taken by the DocShell in the method GetPrt(), for
 * which the access is friend of the DocShell too.
 */

class SmDocShell;
class EditEngine;
class SmEditEngine;
class SmPrintUIOptions;

class SmPrinterAccess
{
    VclPtr<Printer> pPrinter;
    VclPtr<OutputDevice> pRefDev;
public:
    explicit SmPrinterAccess( SmDocShell &rDocShell );
    ~SmPrinterAccess();
    Printer* GetPrinter()  { return pPrinter.get(); }
    OutputDevice* GetRefDev()  { return pRefDev.get(); }
};

class SM_DLLPUBLIC SmDocShell final : public SfxObjectShell, public SfxListener
{
    friend class SmPrinterAccess;
    friend class SmCursor;

    OUString            maText;
    OUString            maImText;
    SmFormat            maFormat;
    OUString            maAccText;
    SvtLinguOptions     maLinguOptions;
    std::unique_ptr<SmTableNode> mpTree;
    SmMlElement* m_pMlElementTree;
    rtl::Reference<SfxItemPool> mpEditEngineItemPool;
    std::unique_ptr<SmEditEngine> mpEditEngine;
    std::unique_ptr<SmEditEngine> mpImEditEngine;
    VclPtr<SfxPrinter>  mpPrinter;       //q.v. comment to SmPrinter Access!
    VclPtr<Printer>     mpTmpPrinter;    //ditto
    sal_uInt16          mnModifyCount;
    bool                mbFormulaArranged;
    sal_Int16           mnSmSyntaxVersion;
    sal_Int32          mnImSyntaxVersion;
    std::unique_ptr<AbstractSmParser> maParser;
    std::unique_ptr<SmCursor> mpCursor;
    std::set< OUString >    maUsedSymbols;   // to export used symbols only when saving


    virtual void Notify(SfxBroadcaster& rBC, const SfxHint& rHint) override;

    bool        WriteAsMathType3( SfxMedium& );

    virtual void        Draw(OutputDevice *pDevice,
                             const JobSetup & rSetup,
                             sal_uInt16 nAspect,
                             bool bOutputForScreen) override;

    virtual void        FillClass(SvGlobalName* pClassName,
                                  SotClipboardFormatId*  pFormat,
                                  OUString* pFullTypeName,
                                  sal_Int32 nFileFormat,
                                  bool bTemplate = false ) const override;

    virtual void        OnDocumentPrinterChanged( Printer * ) override;
    virtual bool        InitNew( const css::uno::Reference< css::embed::XStorage >& xStorage ) override;
    virtual bool        Load( SfxMedium& rMedium ) override;
    virtual bool        Save() override;
    virtual bool        SaveAs( SfxMedium& rMedium ) override;

    Printer             *GetPrt();
    OutputDevice*       GetRefDev();

    void                SetFormulaArranged(bool bVal) { mbFormulaArranged = bVal; }

    virtual bool        ConvertFrom(SfxMedium &rMedium) override;

    /** Called whenever the formula is changed
     * Deletes the current cursor
     */
    void                InvalidateCursor();

public:
    SFX_DECL_INTERFACE(SFX_INTERFACE_SMA_START+SfxInterfaceId(1))

    SFX_DECL_OBJECTFACTORY();

private:
    /// SfxInterface initializer.
    static void InitInterface_Impl();

public:
    explicit SmDocShell( SfxModelFlags i_nSfxCreationFlags );
    virtual     ~SmDocShell() override;

    virtual bool        ConvertTo( SfxMedium &rMedium ) override;

    // For unit tests, not intended to use in other context
    void SetGreekCharStyle(sal_Int16 nVal) { maFormat.SetGreekCharStyle(nVal); }

    static void LoadSymbols();
    static void SaveSymbols();

    void        ArrangeFormula();

    //Access for the View. This access is not for the OLE-case!
    //and for the communication with the SFX!
    //All internal printer uses should work with the SmPrinterAccess only
    bool        HasPrinter() const { return mpPrinter != nullptr; }
    SfxPrinter *GetPrinter()    { GetPrt(); return mpPrinter; }
    void        SetPrinter( SfxPrinter * );

    OUString GetComment() const;

    // to replace chars that can not be saved with the document...
    void        ReplaceBadChars();

    void        UpdateText();
    void        UpdateImText();
    void        SetText(const OUString& rBuffer);
    const OUString&  GetText() const { return maText; }
    void        SetImText(const OUString& rBuffer, const bool doCompile = true);
    const OUString&  GetImText() const { return maImText; }
    void        SetFormat(SmFormat const & rFormat);
    const SmFormat&  GetFormat() const { return maFormat; }

    void            Parse();
    AbstractSmParser* GetParser() { return maParser.get(); }
    const SmTableNode *GetFormulaTree() const  { return mpTree.get(); }
    void            SetFormulaTree(SmTableNode *pTree) { mpTree.reset(pTree); }
    sal_Int16      GetSmSyntaxVersion() const { return mnSmSyntaxVersion; }
    sal_Int32      GetImSyntaxVersion() const { return mnImSyntaxVersion; }
    void            SetSmSyntaxVersion(sal_Int16 nSmSyntaxVersion);
    void            SetImSyntaxVersion(sal_Int32 nImSyntaxVersion);

    // iMath internal properties
    OUString        GetImTypeFirstLine() const { return maImTypeFirstLine; }
    OUString        GetImTypeLastLine() const { return maImTypeLastLine; }
    bool            GetImHidden() const { return mImHidden; }
    void            SetImHidden(const bool h);
    OUString        GetImExprFirstLhs() const { return maImExprFirstLhs; }
    OUString        GetImExprLastLhs() const { return maImExprLastLhs; }
    Sequence<OUString> GetImLabels() const { return mImLabels; }

    void            Compile(); // run iCompiler on the maImText

    const std::set< OUString > &    GetUsedSymbols() const  { return maUsedSymbols; }

    OUString const & GetAccessibleText();

    EditEngine &    GetEditEngine();
    EditEngine &    GetImEditEngine();

    void        DrawFormula(OutputDevice &rDev, Point &rPosition, bool bDrawSelection = false);
    Size        GetSize();

    void        Repaint();

    virtual SfxUndoManager *GetUndoManager () override;

    static SfxItemPool& GetPool();

    void        Execute( SfxRequest& rReq );
    void        GetState(SfxItemSet &);

    virtual void SetVisArea (const tools::Rectangle & rVisArea) override;
    virtual void SetModified(bool bModified = true) override;

    /** Get a cursor for modifying this document
     * @remarks Don't store this reference, a new cursor may be made...
     */
    SmCursor&   GetCursor();
    /** True, if cursor have previously been requested and thus
     * has some sort of position.
     */
    bool        HasCursor() const;

    void writeFormulaOoxml(const ::sax_fastparser::FSHelperPtr& pSerializer,
            oox::core::OoxmlVersion version,
            oox::drawingml::DocumentType documentType,
            const sal_Int8 nAlign);
    void writeFormulaRtf(OStringBuffer& rBuffer, rtl_TextEncoding nEncoding);
    void readFormulaOoxml( oox::formulaimport::XmlStream& stream );

    void UpdateEditEngineDefaultFonts();

    SmMlElement* GetMlElementTree()  { return m_pMlElementTree; }
    void SetMlElementTree(SmMlElement* pMlElementTree) {
        mathml::SmMlIteratorFree(m_pMlElementTree);
        m_pMlElementTree = pMlElementTree;
    }

    void SetRightToLeft(bool bRTL);

    void Impl_Print(OutputDevice& rOutDev, const SmPrintUIOptions& rPrintUIOptions,
                    tools::Rectangle aOutRect);

// iMath ==========================================================================================
public:
    /// Access the document model. Either starmath or writer or impress, depending on whether the formula is stand-alone or inside a document
    css::uno::Reference<css::frame::XModel> GetDocumentModel() const;

    /// Set/Get for previous iFormula
    void SetPreviousFormula(const OUString& aName);
    OUString GetPreviousFormula() const  { return mPreviousFormula; }

    /// Set/Get list of symbols on which this formula depends, and list of symbols which it modifies
    void SetIFormulaDependencyIn(const OUString& aDep) { mIFormulaDependencyIn = aDep; }
    void SetIFormulaDependencyOut(const OUString& aDep) { mIFormulaDependencyOut = aDep; }
    OUString GetIFormulaDependencyIn() const  { return mIFormulaDependencyIn; }
    OUString GetIFormulaDependencyOut() const  { return mIFormulaDependencyOut; }

    /// Set/Get the master document URL
    void SetIFormulaMasterDocument(const OUString& aUrl) { mIFormulaMasterDocument = aUrl; }
    OUString GetIFormulaMasterDocument() const { return mIFormulaMasterDocument; }

    /// Prevent the document from being closed
    // Note: By default, the OLE cache is set to 20 objects in /org.openoffice.Office.Common/Cache/Writer/OLE_Objects
    //       If a office document has more formulas, older ones will be closed. This is bad for performance
    //       and also gives headaches about persisting the mpCurrentCompiler
    void PreventFormulaClose(const bool prevent);

private:
    /// Name of the previous iFormula (OLE mode), empty if the formula is stand-alone or the first formula in a document
    OUString mPreviousFormula;
    /// Names of variables and functions which this formula depends on
    OUString mIFormulaDependencyIn;
    /// Names of variables and functions which this formula creates or modifies
    OUString mIFormulaDependencyOut;
    /// URL of the master document (if any)
    OUString mIFormulaMasterDocument;

    /**
     * Initial options for this formula.
     * Stand-alone (Math) mode: Initialized on first call to Compile() from document options and registry
     * OLE (Writer/Impress) mode: Pointer to mpCurrentOptions of previous formula
     * OLE mode: This is a chain of pointers, unless a formula with the OPTIONS keyword is processed. In this
     * case the options are copied and modified. The modified pointer is passed to the following formulas
     **/
    std::shared_ptr<GiNaC::optionmap> mpInitialOptions;
    /**
     * Initial compiler for this formula.
     * Stand-alone (Math) mode: Initialized on first call to Compile() by reading include files and processing default options
     * OLE (Writer/Impress) mode: Pointer to mpCurrentCompiler of previous formula
     * OLE mode: Before compilation, a copy is taken into the formula's currentCompiler
     **/
    std::shared_ptr<eqc> mpInitialCompiler;

    /// Modified options of this formula after compilation. If nothing is modified, remains same pointer as initialOptions
    std::shared_ptr<GiNaC::optionmap> mpCurrentOptions;
    /// Modified compiler of ths formula after compilation
    std::shared_ptr<eqc> mpCurrentCompiler;

    /// Initialize options and compiler from previous iFormula (if there is one), document options and registry. Must be repeated whenever document options are changed through the UI
    // Return error message or empty string if successful
    // TODO: Update on UI changes not implemented yet
    OUString ImInitializeCompiler();

    /// The parsed formula text split into lines
    std::list<iFormulaLine_ptr> mLines;
    /// Add result lines to the list of iFormulaLines
    void addResultLines();
    /// Count the number of lines of a certain type
    bool align_makes_sense() const;

    /// Initialize members once for the lifetime of the class
    static void ImStaticInitialization();

    /// Internal iMath is blocked because an iMath extension is still installed
    static bool mImBlocked;
    /// Decimal separator character(s)
    static std::string mDecimalSeparator;

    /// Prevent the document from being closed
    css::uno::Reference< css::util::XCloseListener > m_xIFormulaClosePreventer;

    /// Internal properties for communicating with parent documents
    /// Type of the first line in the (multiline) formula: equation, expression, other
    OUString maImTypeFirstLine;
    /// Type of the last line in the (multiline) formula: equation, expression, other
    OUString maImTypeLastLine;
    /// True if the iFormula (all lines of it) is hidden, i.e. no text is displayed
    bool mImHidden;
    /// Left-hand side of the first line of the formula that contains an expression or equation (for merging formulas)
    OUString maImExprFirstLhs;
    /// Left-hand side of the last line of the formula that contains an expression or equation (for merging formulas)
    OUString maImExprLastLhs;
    /// List of all equation labels in this formula
    Sequence<OUString> mImLabels;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
