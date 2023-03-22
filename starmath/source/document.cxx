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

#include <sal/config.h>

#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/uno/Any.h>

#include <comphelper/fileformat.h>
#include <comphelper/accessibletexthelper.hxx>
#include <comphelper/string.hxx>
#include <comphelper/processfactory.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>
#include <sal/log.hxx>
#include <unotools/eventcfg.hxx>
#include <sfx2/event.hxx>
#include <sfx2/app.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/docfilt.hxx>
#include <sfx2/msg.hxx>
#include <sfx2/objface.hxx>
#include <sfx2/printer.hxx>
#include <sfx2/request.hxx>
#include <sfx2/viewfrm.hxx>
#include <comphelper/classids.hxx>
#include <sot/formats.hxx>
#include <sot/storage.hxx>
#include <svl/eitem.hxx>
#include <svl/intitem.hxx>
#include <svl/itempool.hxx>
#include <svl/slstitm.hxx>
#include <svl/hint.hxx>
#include <svl/stritem.hxx>
#include <svl/undo.hxx>
#include <svl/whiter.hxx>
#include <vcl/mapmod.hxx>
#include <vcl/virdev.hxx>
#include <tools/mapunit.hxx>
#include <vcl/settings.hxx>

#include <document.hxx>
#include <action.hxx>
#include <dialog.hxx>
#include <format.hxx>
#include <parse.hxx>
#include <starmath.hrc>
#include <strings.hrc>
#include <smmod.hxx>
#include <symbol.hxx>
#include <unomodel.hxx>
#include <utility.hxx>
#include <view.hxx>
#include "mathtype.hxx"
#include "ooxmlexport.hxx"
#include "ooxmlimport.hxx"
#include "rtfexport.hxx"
#include <mathmlimport.hxx>
#include <mathmlexport.hxx>
#include <svx/svxids.hrc>
#include <cursor.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <visitors.hxx>
#include "accessibility.hxx"
#include <cfgitem.hxx>
#include <utility>
#include <oox/mathml/imexport.hxx>
#include <ElementsDockingWindow.hxx>
#include <smediteng.hxx>
#include <editeng/editund2.hxx>

#define ShellClass_SmDocShell
#include <smslots.hxx>

#include <com/sun/star/presentation/XPresentationSupplier.hpp>

using namespace ::com::sun::star;
using namespace ::com::sun::star::accessibility;
using namespace ::com::sun::star::uno;

#include <config_folders.h>
#include <osl/file.hxx>
#include <rtl/bootstrap.hxx>

#include <smim.hrc>
#include <com/sun/star/lang/XMultiComponentFactory.hpp>
#include <com/sun/star/i18n/XLocaleData.hpp>
#include <com/sun/star/util/CloseVetoException.hpp>
#include <logging.hxx>
#include <imath/settingsmanager.hxx>
#include <imath/alignblock.hxx>
#include <imath/funcmgr.hxx>
#include <imath/imathutils.hxx>
#include <imath/imathparse.hxx>

#include <iostream>

namespace
{
    // Taken from embeddedobject TODO Is there a way to use that code directly instead of duplicating it?
    class IFormulaClosePreventer : public ::cppu::WeakImplHelper < css::util::XCloseListener >
    {
        virtual void SAL_CALL queryClosing( const css::lang::EventObject& Source, sal_Bool GetsOwnership ) override;
        virtual void SAL_CALL notifyClosing( const css::lang::EventObject& Source ) override;

        virtual void SAL_CALL disposing( const css::lang::EventObject& Source ) override;
    };

    void SAL_CALL IFormulaClosePreventer::queryClosing(const css::lang::EventObject&, sal_Bool)
    {
        SAL_INFO_LEVEL(3, "starmath.imath", "Vetoing closure of iFormula");
        throw css::util::CloseVetoException();
    }

    void SAL_CALL IFormulaClosePreventer::notifyClosing(const css::lang::EventObject&)
    {
        // just a disaster
        OSL_FAIL("The object can not be prevented from closing!");
    }

    void SAL_CALL IFormulaClosePreventer::disposing(const css::lang::EventObject&)
    {
        // just a disaster
        OSL_FAIL("The object can not be prevented from closing!");
    }
}

SFX_IMPL_SUPERCLASS_INTERFACE(SmDocShell, SfxObjectShell)

void SmDocShell::InitInterface_Impl()
{
    GetStaticInterface()->RegisterPopupMenu("view");
}

void SmDocShell::SetSmSyntaxVersion(sal_Int16 nSmSyntaxVersion)
{
    mnSmSyntaxVersion = nSmSyntaxVersion;
    maParser.reset(starmathdatabase::GetVersionSmParser(mnSmSyntaxVersion));
}

void SmDocShell::SetImSyntaxVersion(sal_uInt32 nImSyntaxVersion)
{
    mnImSyntaxVersion = nImSyntaxVersion;
}

SFX_IMPL_OBJECTFACTORY(SmDocShell, SvGlobalName(SO3_SM_CLASSID), u"smath"_ustr )

void SmDocShell::Notify(SfxBroadcaster&, const SfxHint& rHint)
{
    if (rHint.GetId() == SfxHintId::MathFormatChanged)
    {
        SetFormulaArranged(false);

        mnModifyCount++;     //! see comment for SID_GRAPHIC_SM in SmDocShell::GetState

        Repaint();
    }
}

Reference<XModel> SmDocShell::GetDocumentModel() const
{
    Reference<container::XChild> xModel(GetModel(), UNO_QUERY_THROW);
    Reference<XModel> xParent(xModel->getParent(), UNO_QUERY);

    if (xParent.is())
    {
        Reference<XTextDocument> xTextDoc(xParent, UNO_QUERY);
        if (xTextDoc.is())
        {
            SAL_INFO_LEVEL(1, "starmath.imath", "Found parent text document");
            return xParent;
        }

        Reference<presentation::XPresentationSupplier> xPresDoc(xParent, UNO_QUERY);
        if (xPresDoc.is())
        {
            SAL_INFO_LEVEL(1, "starmath.imath", "Found parent presentation document");
            return xParent;
        }
    }

    SAL_INFO_LEVEL(1, "starmath.imath", "No parent document for formula");
    return GetModel();
}

void SmDocShell::LoadSymbols()
{
    SmModule *pp = SM_MOD();
    pp->GetSymbolManager().Load();
}


OUString SmDocShell::GetComment() const
{
    uno::Reference<document::XDocumentPropertiesSupplier> xDPS(
        GetModel(), uno::UNO_QUERY_THROW);
    uno::Reference<document::XDocumentProperties> xDocProps(
        xDPS->getDocumentProperties());
    return xDocProps->getDescription();
}


void SmDocShell::SetText(const OUString& rBuffer)
{
    if (rBuffer == maText)
        return;

    bool bIsEnabled = IsEnableSetModified();
    if( bIsEnabled )
        EnableSetModified( false );

    maText = rBuffer;
    SetFormulaArranged( false );

    Parse();

    SmViewShell *pViewSh = SmGetActiveView();
    if (pViewSh)
    {
        pViewSh->GetViewFrame().GetBindings().Invalidate(SID_TEXT);
        if ( SfxObjectCreateMode::EMBEDDED == GetCreateMode() )
        {
            // have SwOleClient::FormatChanged() to align the modified formula properly
            // even if the visible area does not change (e.g. when formula text changes from
            // "{a over b + c} over d" to "d over {a over b + c}"
            SfxGetpApp()->NotifyEvent(SfxEventHint( SfxEventHintId::VisAreaChanged, GlobalEventConfig::GetEventName(GlobalEventId::VISAREACHANGED), this));

            Repaint();
        }
        else
            pViewSh->GetGraphicWidget().Invalidate();
    }

    if ( bIsEnabled )
        EnableSetModified( bIsEnabled );
    SetModified();

    // launch accessible event if necessary
    SmGraphicAccessible *pAcc = pViewSh ? pViewSh->GetGraphicWidget().GetAccessible_Impl() : nullptr;
    if (pAcc)
    {
        Any aOldValue, aNewValue;
        if ( comphelper::OCommonAccessibleText::implInitTextChangedEvent( maText, rBuffer, aOldValue, aNewValue ) )
        {
            pAcc->LaunchEvent( AccessibleEventId::TEXT_CHANGED,
                    aOldValue, aNewValue );
        }
    }

    if ( GetCreateMode() == SfxObjectCreateMode::EMBEDDED )
        OnDocumentPrinterChanged(nullptr);
}

void SmDocShell::PreventFormulaClose(const bool prevent)
{
    SAL_INFO_LEVEL(1, "starmath.imath", "SmDocShell::PreventFormulaClose(): " << (prevent ? "on" : "off"));
    const uno::Reference < util::XCloseBroadcaster > xCloseBroadcaster(GetModel(), UNO_QUERY);
    if (!xCloseBroadcaster.is()) return;

    if (prevent)
    {
        if (!m_xIFormulaClosePreventer.is())
        {
            SAL_INFO_LEVEL(1, "starmath.imath", "Adding new close preventer to SmDocShell");
            m_xIFormulaClosePreventer = new IFormulaClosePreventer;
            xCloseBroadcaster->addCloseListener(m_xIFormulaClosePreventer);
        }
    }
    else
    {
        if (m_xIFormulaClosePreventer.is()) {
            xCloseBroadcaster->removeCloseListener(m_xIFormulaClosePreventer);
            m_xIFormulaClosePreventer.clear();
            SAL_INFO_LEVEL(1, "starmath.imath", "Removed close preventer from SmDocShell");
        }
        else
        {
            SAL_INFO_LEVEL(1, "starmath.imath", "Not removing close preventer from SmDocShell because none exists");
        }
    }
}

void SmDocShell::SetImText(const OUString& rBuffer, const bool doCompile)
{
    SAL_INFO_LEVEL(1, "starmath.imath", "SetImText\n'" << rBuffer << "'");
    if (rBuffer == maImText)
        return;

    maImText = rBuffer;

    // Ensure that this formula will not be cleaned out of the OLE cache
    PreventFormulaClose(true);

    if (doCompile && maImText.getLength() > 0)
        Compile();
}

void SmDocShell::SetPreviousFormula(const OUString& aName)
{
    if (mPreviousFormula == aName)
        return;

    mPreviousFormula = aName;
}

void SmDocShell::SetFormat(SmFormat const & rFormat)
{
    maFormat = rFormat;
    SetFormulaArranged( false );
    SetModified();

    mnModifyCount++;     //! see comment for SID_GRAPHIC_SM in SmDocShell::GetState

    // don't use SmGetActiveView since the view shell might not be active (0 pointer)
    // if for example the Basic Macro dialog currently has the focus. Thus:
    SfxViewFrame* pFrm = SfxViewFrame::GetFirst( this );
    while (pFrm)
    {
        pFrm->GetBindings().Invalidate(SID_GRAPHIC_SM);
        pFrm = SfxViewFrame::GetNext( *pFrm, this );
    }
}

OUString const & SmDocShell::GetAccessibleText()
{
    ArrangeFormula();
    if (maAccText.isEmpty())
    {
        OSL_ENSURE( mpTree, "Tree missing" );
        if (mpTree)
        {
            OUStringBuffer aBuf;
            mpTree->GetAccessibleText(aBuf);
            maAccText = aBuf.makeStringAndClear();
        }
    }
    return maAccText;
}

void SmDocShell::Parse()
{
    mpTree.reset();
    ReplaceBadChars();
    mpTree = maParser->Parse(maText);
    mnModifyCount++;     //! see comment for SID_GRAPHIC_SM in SmDocShell::GetState
    SetFormulaArranged( false );
    InvalidateCursor();
    maUsedSymbols = maParser->GetUsedSymbols();
}

OUString SmDocShell::ImInitializeCompiler() {
    SAL_INFO_LEVEL(1, "starmath.imath", "Preparing formula for compilation");

    if (mPreviousFormula.getLength() > 0) {
        // Find previous iFormula from parent document. If this fails, a error message is returned
        SAL_INFO_LEVEL(1, "starmath.imath", "Previous formula is " << mPreviousFormula);

        Reference<container::XChild> xModel(GetModel(), UNO_QUERY_THROW);
        Reference<XModel> xParent(xModel->getParent(), UNO_QUERY_THROW);
        Reference < XComponent > xPreviousFormulaComponent = getObjectByName(xParent, mPreviousFormula);

         if (xPreviousFormulaComponent.is()) {
            Reference< XModel > xPreviousFormula = extractModel(xPreviousFormulaComponent);

            SmModel* pPreviousModel = comphelper::getFromUnoTunnel<SmModel>(xPreviousFormula);
            SmDocShell* pPreviousDocShell = pPreviousModel ? static_cast<SmDocShell*>(pPreviousModel->GetObjectShell()) : nullptr;

            if (pPreviousDocShell != nullptr) {
                mpInitialCompiler = pPreviousDocShell->mpCurrentCompiler;
                mpInitialOptions = pPreviousDocShell->mpCurrentOptions;
                if (mpInitialCompiler != nullptr && mpInitialOptions != nullptr) {
                    SAL_INFO_LEVEL(1, "starmath.imath", "Set initial compiler and options from previous formula");
                    return "";
                } else {
                    if (mpInitialCompiler == nullptr)
                        if (mpInitialOptions == nullptr)
                            return "Compiler and options of previous formula had null value";
                        else
                            return "Compiler of previous formula had null value";
                    else
                        return "Options of previous formula had null value";
                }
            } else {
                return "Previous formula was not usable";
            }
        } else {
            return "Previous formula could not be found in parent document";
        }
    }

    // Stand-alone formula document or first formula in document
    // TODO: Handle case when ImInitialize() is called after options were changed through the UI
    if (mpInitialOptions != nullptr && mpInitialCompiler != nullptr) return ""; // Already initialized
    SAL_INFO_LEVEL(1, "starmath.imath", "Preparing stand-alone formula or first formula in document");
    Reference<XComponentContext> xContext(comphelper::getProcessComponentContext());

    mpInitialOptions = std::make_shared<GiNaC::optionmap>();
    mpInitialCompiler = std::make_shared<eqc>();

    // Get access to the registry that contains the global options
    Reference<XHierarchicalPropertySet> xProperties = getRegistryAccess(xContext, OU("/org.openoffice.Office.iMath/"));

    // Check for stand-alone formula or part of Text / Presentation
    Reference<container::XChild> xChild(GetModel(), UNO_QUERY_THROW);
    Reference<XModel> xParent(xChild->getParent(), UNO_QUERY);
    Reference<XModel> xModel;

    if (!xParent.is())
    {
        SAL_INFO_LEVEL(1, "starmath.imath", "Detected Starmath document");
        xModel = GetBaseModel();
    }
    else
    {
        Reference<XTextDocument> xTextDoc(xParent, UNO_QUERY);
        Reference<presentation::XPresentationSupplier> xPresDoc(xParent, UNO_QUERY); // TODO: Implement functionality for Impress

        if (xTextDoc.is()) {
            SAL_INFO_LEVEL(1, "starmath.imath", "Detected parent Writer document");
            xModel = xParent;
        } else if (xPresDoc.is()) {
            SAL_INFO_LEVEL(1, "starmath.imath", "Detected parent Impress document");
            xModel = xParent;
        } else {
            SAL_WARN("starmath.imath", "Unknown document type");
        }
    }

    // Get access to the RDF graph that contains the document-specific options. Create one if it doesn't exist
    Reference<XNamedGraph> xGraph = getGraph(xContext, xModel);
    if (!xGraph.is()) xGraph = createGraph(xContext, xModel);

    // Get/Set document-specific options
    // 1. If the document contains document-specific options in an RDF graph, these are used
    // 2. Otherwise, the values from the registry are used and also copied to the RDF graph
    //    In other words, only a new document before the first recalc() will use the registry values, to ensure
    //    document display consistency
    // References. These are always document specific
    OUString references = getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_references"), OU("Includes/txt_References"));
    SAL_INFO_LEVEL(1, "starmath.imath", "Found references '" << references << "'");
    OUString include1 = getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_include1"), OU("Includes/txt_Include1"));
    OUString include2 = getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_include2"), OU("Includes/txt_Include2"));
    OUString include3 = getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_include3"), OU("Includes/txt_Include3"));
    SAL_INFO_LEVEL(1, "starmath.imath", "Found user references '" << include1 << "', '" << include2 << "', '" << include3 << "'");

    // Formatting
    // TODO: This will copy all the options from the registry into the local document graph, which is not what we want for multi-formula documents in Writer or Presentation
    // Note: We could mis-use the master document flag to avoid the copying
     Settingsmanager::initializeOptionmap(xContext, xModel, xGraph, xProperties, mpInitialOptions, false);

    // Path to iMath's own include files (references)
    OUString shareFolder;
    // TODO Fix build system to include share/imath into the Windows msi files
    //OUString shareURL("$BRAND_BASE_DIR/" LIBO_SHARE_FOLDER "/imath/references/");
    OUString shareURL("$BRAND_BASE_DIR/" LIBO_SHARE_FOLDER "/calc/");
    rtl::Bootstrap::expandMacros(shareURL);
    osl::FileBase::getSystemPathFromFileURL(shareURL, shareFolder);

    // TODO Try to get rid of the try-catch block because the parser should not throw exceptions at all
    try {
        // Read referenced files
        auto files = splitString(references, ' ');
        files.unique();
        imath::parserParameters pParams;
        pParams.xContext = comphelper::getProcessComponentContext();
        pParams.xDocumentModel = GetDocumentModel();
        pParams.rawtext = OU("");
        pParams.copyPasteActive = false; // TODO: Check if LO still crashes when a formula is changed during a copy+paste action
        pParams.lines = &mLines;
        pParams.compiler = mpInitialCompiler;
        pParams.global_options = mpInitialOptions;
        pParams.cached_results = new std::vector<std::pair<std::string, GiNaC::expression> >();

        for (const auto& f : files) {
            if (f.getLength() > 2)
                pParams.rawtext += OU("%%ii READFILE {\"") + shareFolder + f.copy(2) + OU(".imath") + OU("\"}\n");
        }

        if (!pParams.rawtext.equalsAscii("")) {
            SAL_INFO_LEVEL(0, "starmath.imath", "Reading referenced files\n" << STR(pParams.rawtext));
            if (imath::parse(pParams) != 0) return "Recalculation error in referenced files\n" + pParams.errormessage;
            if (mLines.size() > 0) mpInitialOptions = mLines.back()->getGlobalOptions(); // Options might have been changed by the OPTIONS keyword
        }

        // units must be set AFTER units.imath is read, because the preferred units list might use user-defined units
        // Note that this will delete any preferred units declared in the previous include files (there shouldn't be any!)
        OUString units = OUS8(*mpInitialOptions->at(o_unitstr).value.str); // This was populated in initializeOptionmap()
        mpInitialOptions->emplace(o_units, option(GiNaC::exvector())); // All keys are expected to exist in global_options
        mpInitialOptions->at(o_unitstr).value.str->clear(); // Clear o_unitstr, because it will be populated again

        if (units.getLength() > 0) {
            // Recreate the global units expression vector, since this cannot be stored in the registry
            SAL_INFO_LEVEL(0, "starmath.imath", "Parsing default units\n" << STR(pParams.rawtext));
            pParams.rawtext = OU("%%ii OPTIONS {units={") + units + OU("}}\n");
            pParams.errormessage = "";
            // Result is stored in mpInitialOptions map under the keys o_unit and o_unitstr
            if (imath::parse(pParams) != 0) return "Recalculation error in global units\n" + pParams.errormessage;
            if (mLines.size() > 0) mpInitialOptions = mLines.back()->getGlobalOptions();
        }

        // Read user include files
        // Note: READFILE converts include[1-3] to a system path if necessary
        pParams.rawtext = OU("");
        if (!include1.equalsAscii("") && (std::find(files.begin(), files.end(), include1) == files.end())) {
            pParams.rawtext = OU("%%ii READFILE {\"") + include1 + OU("\"}\n");
            files.emplace_back(include1);
        }
        if (!include2.equalsAscii("") && (std::find(files.begin(), files.end(), include2) == files.end())) {
            pParams.rawtext += OU("%%ii READFILE {\"") + include2 + OU("\"}\n");
            files.emplace_back(include2);
        }
        if (!include3.equalsAscii("") && (std::find(files.begin(), files.end(), include3) == files.end())) {
            pParams.rawtext += OU("%%ii READFILE {\"") + include3 + OU("\"}\n");
            files.emplace_back(include3);
        }

        if (!pParams.rawtext.equalsAscii("")) {
            SAL_INFO_LEVEL(0, "starmath.imath", "Reading user include files\n" << STR(pParams.rawtext));
            pParams.errormessage = "";
            if (imath::parse(pParams) != 0) return "Recalculation error in user include files\n" + pParams.errormessage;
            if (mLines.size() > 0) mpInitialOptions = mLines.back()->getGlobalOptions();
        }
    } catch (Exception &e) {
        // TODO: Show error message to user with parser location
        SAL_WARN_LEVEL(-1, "starmath.imath", "Exception thrown while recalculating iMath include files\n" << e.Message);
        return "Recalculation error in iMath include files\n" + e.Message;
    } catch (std::exception &e) {
        // TODO: Show error message to user with parser location
        SAL_WARN_LEVEL(-1, "starmath.imath", "std::exception thrown while recalculating iMath include files\n" << OUS8(e.what()));
        return "Recalculation error in iMath include files\n" + OUS8(e.what());
    }

    return "";
}

void SmDocShell::Compile()
{
    if (maImText.equalsAscii("")) return; // empty iFormula

    if (mPreviousFormula.equalsAscii("_IMATH_UNDEFINED_")) return; // Partly-initialized formula (see SwWrtShell::InsertOleObject())

    if (mImBlocked) {
        SAL_WARN_LEVEL(-1, "starmath.imath", "iMath cannot be used because an iMath extension is still installed");
        return;
    }
    SAL_INFO_LEVEL(1, "starmath.imath", "SmDocShell::Compile()\n'" << maImText << "'");

    OUString error = ImInitializeCompiler();
    if (error.getLength() > 0) {
        // TODO: Publish it somewhere
        SAL_WARN_LEVEL(-1, "starmath.imath", error);
        return;
    }

    // Important settings for the compiler. Note: Initialization must do without them, since no mpInitialOptions are available before initialization...
    GiNaC::imathprint::decimalpoint = mDecimalSeparator;
    //setlocale(LC_NUMERIC, "C"); // Ensure printf() always uses decimal points! TODO Why is that important?
    // Inhibit floating point underflow exceptions?
    set_inhibit_floating_point_underflow(mpInitialOptions->at(o_underflow).value.boolean);
    SAL_INFO_LEVEL(1, "starmath.imath", "Inhibit floating point underflow exception: " << (get_inhibit_floating_point_underflow() ? "true" : "false"));
    // Evaluate odd negative roots to the positive real value?
    GiNaC::expression::evalf_real_roots_flag = (mpInitialOptions->at(o_evalf_real_roots).value.boolean);

    // Save old outgoing dependencies
    std::set<GiNaC::expression, GiNaC::expr_is_less> oldOutDep;
    for (const auto& l : mLines) oldOutDep.merge(l->getOut());
    SAL_INFO_LEVEL(1, "starmath.imath", "This formula had old outgoing dependencies for '" << makeSymbolString(oldOutDep) << "'");

    // Prepare compiler. Note: Since currentCompiler is a shared_ptr, the old data will automatically get cleaned up when the last reference is released
    mpCurrentCompiler = std::make_shared<eqc>(*mpInitialCompiler); // Takes a deep copy TODO: Reduce the amount of data copied, e.g. by copy-on-write semantics in the eqc private data structures

    // TODO Try to get rid of the try-catch block because the parser should not throw exceptions at all. Requires complete rework of exceptions in imath library
    try {
        mLines.clear();

        imath::parserParameters pParams;
        pParams.xContext = comphelper::getProcessComponentContext();
        pParams.xDocumentModel = GetDocumentModel();
        pParams.rawtext = OU("");
        pParams.copyPasteActive = false;
        pParams.lines = &mLines;
        pParams.compiler = mpCurrentCompiler;
        pParams.global_options = mpInitialOptions; // mpInitialOptions are not modified by the parser, copy is taken when OPTIONS keyword is encountered
        pParams.cached_results = new std::vector<std::pair<std::string, GiNaC::expression> >();
        // Add %%ii in front of every line
        // TODO: Change parser to make this unnecessary
        sal_Int32 idx = 0;

        do {
            OUString line = maImText.getToken(0, '\n', idx);
            if (line.getLength() > 0)
                pParams.rawtext += "%%ii " + line + OU("\n");
        } while (idx >= 0);

        if (imath::parse(pParams) != 0) {
            pParams.errormessage = "Syntax error\n" + pParams.errormessage;
            SAL_WARN_LEVEL(-1, "starmath.imath", pParams.errormessage);
        } else if (mLines.size() > 0) {
            mpCurrentOptions = mLines.back()->getGlobalOptions();

            SAL_INFO_LEVEL(0, "starmath.imath", "Printing " << mLines.size() << " lines");
            for (const auto& i : mLines)
                SAL_INFO_LEVEL(0, "starmath.imath", i->printFormula());

            addResultLines();
            OUString result;

            for (const auto& i : mLines)
                if (i->getSelectionType() == formulaTypeResult)
                    result += i->print() + OU("\n");

            SetText(result);

            // Update properties
            // Note: This must happen after print() because the displayedLhs is created in the iFormulaLine::display() method
            maImTypeFirstLine = "";
            maImTypeLastLine = "";
            mImHidden = true;
            maImExprFirstLhs = "";
            maImExprLastLhs = "";
            std::vector<OUString> tempLabels; // Sequence does not appear to support appending of elements

            for (const auto& l : mLines)
            {
                if (l->getSelectionType() == formulaTypeEquation)
                {
                    maImTypeFirstLine = "equation";
                    iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(l);
                    maImExprFirstLhs = expr->getDisplayedLhs();
                    tempLabels.push_back(expr->getLabel());
                    break;
                }
                else if (l->getSelectionType() == formulaTypeExpression)
                {
                    maImTypeFirstLine = "expression";
                    iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(l);
                    maImExprFirstLhs = expr->getDisplayedLhs();
                    if (expr->getLabel().getLength() > 0)
                        tempLabels.push_back(expr->getLabel());
                    break;
                }
            }

            mImLabels = Sequence<OUString>(tempLabels.size());

            for (size_t l = 0; l < tempLabels.size(); ++l)
                mImLabels.getArray()[l] = tempLabels[l];

            for (auto line = mLines.rbegin(); line != mLines.rend(); ++line)
            {
                if ((*line)->getSelectionType() == formulaTypeEquation)
                {
                    maImTypeLastLine = "equation";
                    iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(*line);
                    maImExprLastLhs = expr->getDisplayedLhs();
                    break;
                }
                else if ((*line)->getSelectionType() == formulaTypeExpression)
                {
                    maImTypeLastLine = "expression";
                    iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(*line);
                    maImExprLastLhs = expr->getDisplayedLhs();
                    break;
                }
            }

            for (const auto& l : mLines)
            {
                iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(l);

                if (expr != nullptr && !expr->getHide())
                    mImHidden = false; // If one line is not hidden, the whole formula counts as not hidden
            }

            // Update dependencies
            // TODO: Currently dependency tracking in iFormulaLine.cxx works on the compilation result, thus VAL(z) does not depend on z if it expands to a numeric value
            std::set<GiNaC::expression, GiNaC::expr_is_less> inDep, outDep;
            OUString inDepStr, outDepStr;

            for (const auto& l : mLines)
            {
                if (l->dependencyType() == depRecalc)
                {
                    inDepStr = "all formulas";
                    outDepStr = "all formulas";
                    break;
                }

                for (const auto& dep : l->getIn())
                    if (outDep.find(dep) == outDep.end()) // Avoid bogus incoming dependencies in multi-line formulas
                        inDep.insert(dep);
                outDep.merge(l->getOut());
            }

            if (outDepStr.getLength() == 0)
            {
                // Outgoing dependencies that have been removed will also influence the following formulas - thus they must be inserted again
                // TODO: The way this is currently implemented means that outgoing dependencies will NEVER be removed at all!
                for (const auto& oldDep : oldOutDep) {
                    bool found = false;
                    if (!GiNaC::is_a<GiNaC::symbol>(oldDep)) continue;

                    for (const auto& dep : outDep) {
                        if (GiNaC::is_a<GiNaC::symbol>(dep) && GiNaC::ex_to<GiNaC::symbol>(oldDep).get_name() == GiNaC::ex_to<GiNaC::symbol>(dep).get_name()) {
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        SAL_INFO_LEVEL(1, "starmath.imath", "Outgoing dependency on '" << GiNaC::ex_to<GiNaC::symbol>(oldDep).get_name() << "' was removed");
                        outDep.insert(mpCurrentCompiler->getsym(GiNaC::ex_to<GiNaC::symbol>(oldDep).get_name()));
                    }
                }

                inDepStr = makeSymbolString(inDep);
                outDepStr = makeSymbolString(outDep);
            }

            SAL_INFO_LEVEL(1, "starmath.imath", "This formula depends on '" << inDepStr << "'");
            SAL_INFO_LEVEL(1, "starmath.imath", "This formula modifies '" << outDepStr << "'");
            SetIFormulaDependencyIn(inDepStr);
            SetIFormulaDependencyOut(outDepStr);
        }
    } catch (Exception &e) {
        error = "Compilation error\n" + e.Message;
        SAL_WARN_LEVEL(-1, "starmath.imath", "Exception thrown while compiling user input\n" << STR(e.Message));
    } catch (std::exception &e) {
        error = OU("Compilation error\n") + OUS8(e.what());
        SAL_WARN_LEVEL(-1, "starmath.imath", "std::exception thrown while compiling user input\n" << e.what());
    }

    if (!error.isEmpty()) {
        if (!mLines.empty())
            mpCurrentOptions = mLines.back()->getGlobalOptions();
        else
            mpCurrentOptions = mpInitialOptions;
        // TODO: Show error in imath edit window, not in formula object
        SetText(replaceString(error, "\n", "\nnewline\n"));
    }

    //setlocale(LC_NUMERIC, ""); // Reset to system locale
    SAL_INFO_LEVEL(0, "starmath.imath", "Recalculation finished" << endline);
}

void SmDocShell::SetImHidden(const bool h)
{
    if (mImHidden == h) return;

    OUString oldImText = maImText;
    maImText = OU("");
    //unsigned basefontheight = getFormulaProperty<unsigned>(extractModel(obj), OU("BaseFontHeight"));

    for (auto& l : mLines)
    {
        iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(l);

        if (expr != nullptr && expr->getHide() != h)
            expr->setHide(h);

        if (l->getSelectionType() == formulaTypeResult) continue;

        //i->setBasefontHeight(basefontheight);
        OUString lineText = l->print();

        if (maImText.getLength() > 0)
            maImText += "\n";
        maImText += lineText.copy(4).trim(); // Drop leading %%ii
    }

    SAL_INFO_LEVEL(2, "starmath.imath", "Rebuilt ImText: '" << maImText << "'");

    if (!maImText.equals(oldImText))
    {
        if (h)
            SetText("");
        else
            Compile();
    }

    mImHidden = h;
}

void SmDocShell::ArrangeFormula()
{
    if (mbFormulaArranged)
        return;

    // Only for the duration of the existence of this object the correct settings
    // at the printer are guaranteed!
    SmPrinterAccess  aPrtAcc(*this);
    OutputDevice* pOutDev = aPrtAcc.GetRefDev();

    SAL_WARN_IF( !pOutDev, "starmath", "!! SmDocShell::ArrangeFormula: reference device missing !!");

    // if necessary get another OutputDevice for which we format
    if (!pOutDev)
    {
        if (SmViewShell *pView = SmGetActiveView())
            pOutDev = &pView->GetGraphicWidget().GetDrawingArea()->get_ref_device();
        else
        {
            pOutDev = &SM_MOD()->GetDefaultVirtualDev();
            pOutDev->SetMapMode( MapMode(SmMapUnit()) );
        }
    }
    OSL_ENSURE(pOutDev->GetMapMode().GetMapUnit() == SmMapUnit(),
               "Sm : wrong MapMode");

    const SmFormat &rFormat = GetFormat();
    mpTree->Prepare(rFormat, *this, 0);

    pOutDev->Push(vcl::PushFlags::TEXTLAYOUTMODE | vcl::PushFlags::TEXTLANGUAGE);

    // We want the device to always be LTR, we handle RTL formulas ourselves.
    bool bOldRTL = pOutDev->IsRTLEnabled();
    pOutDev->EnableRTL(false);

    // For RTL formulas, we want the brackets to be mirrored.
    bool bRTL = GetFormat().IsRightToLeft();
    pOutDev->SetLayoutMode(bRTL ? vcl::text::ComplexTextLayoutFlags::BiDiRtl
                                : vcl::text::ComplexTextLayoutFlags::Default);

    // Numbers should not be converted, for now.
    pOutDev->SetDigitLanguage( LANGUAGE_ENGLISH );

    mpTree->Arrange(*pOutDev, rFormat);

    pOutDev->EnableRTL(bOldRTL);
    pOutDev->Pop();

    SetFormulaArranged(true);

    // invalidate accessible text
    maAccText.clear();
}

// Note: Mostly copied from iFormula.cxx
bool SmDocShell::align_makes_sense() const {
  // If there are at least two operator signs in two different lines, aligning makes sense
  bool have_operator = false;
  unsigned count = 0;

  for (const auto& i : mLines) {
    iExpression_ptr pExpr = std::dynamic_pointer_cast<iFormulaNodeExpression>(i);
    if ((pExpr != nullptr) && !pExpr->getHide())
      count += pExpr->countLinesWithOperators(have_operator);
    if (count > 1) return true; // Avoid unnecessary iterations
  }

  if (have_operator) count++; // Final line does not need newline
  return count > 1;
} // align_makes_sense()

void SmDocShell::addResultLines() {
  SAL_INFO_LEVEL(2, "starmath.imath", "SmDocShell::addResultLines" << endline);
  // Don't try to align one-line iFormulas!
  bool do_not_align = !align_makes_sense();
  SAL_INFO_LEVEL(3, "starmath.imath", "do_not_align = " << (do_not_align ? "true" : "false") << endline);

  // Collects all the lines that should be aligned to one another
  alignblock a;

  // Insert result lines where appropriate
  bool hasResult = false;
  OUString resultText;
  OUString prev_lhs = OU(""); // LHS of previous equation, for chaining
  unsigned basefontheight = sal_Int16(SmRoundFraction(Sm100th_mmToPts(GetFormat().GetBaseSize().Height()))); // TODO: getFormulaUnsignedProperty(fModel, OU("BaseFontHeight"))

  for (iFormulaLine_it i = mLines.begin(); i != mLines.end();) {
    SAL_INFO_LEVEL(3, "starmath.imath",  "Line type = " << (*i)->getSelectionType() << endline);
    // Echo iFormula text
    if ((*i)->getOption(o_echoformula).value.boolean == true) {
      if ((*i)->getSelectionType() != formulaTypeComment && (*i)->getSelectionType() != formulaTypeEmptyLine && (*i)->getSelectionType() != formulaTypeResult) {
        OUString rtext = (*i)->print();
        rtext = replaceString(rtext, OU("\""), OU("\\\""));
        rtext = replaceString(rtext, OU("\n%%ii+"), OU("\" newline\"%%ii+"));
        rtext = OU("\"") + rtext + OU("\" newline{}");

        if (i != mLines.begin()) {
          iFormulaLine_it prev_it = i;
          --prev_it;
          if (prev_it != mLines.begin()) --prev_it;
          if ((*prev_it)->getFormula().lastIndexOf(OU("\" newline{}")) < 0)
            rtext = OU("{} newline ") + rtext;
        }

        i = mLines.emplace(i, std::make_shared<iFormulaNodeResult>(rtext));
        SAL_INFO_LEVEL(3, "starmath.imath", "Created echo line" << endline);
        ++i;
      }
    }

    // Display the result of the line
    (*i)->setBasefontHeight(basefontheight);
    if ((*i)->isDisplayable()) {
      if ((*i)->getSelectionType() == formulaTypeChart) {
        // A valid xModel is only required for the CHART statement
        Reference<container::XChild> xModel(GetModel(), UNO_QUERY_THROW);
        Reference<XModel> xParent(xModel->getParent(), UNO_QUERY_THROW);
        Reference<XTextDocument> xTextDoc(xParent, UNO_QUERY);
        if (xTextDoc.is()) {
            (*i)->display(xParent, resultText, prev_lhs, a, do_not_align); // For stand-alone formulas ignore CHART statement // TODO Implement for charts in presentations
        }
        hasResult = false; // CHART is displayable but has no textual result
      } else {
        (*i)->display(Reference< XModel>(), resultText, prev_lhs, a, do_not_align);
        hasResult = true;
      }
      iExpression_ptr pExpr = std::dynamic_pointer_cast<iFormulaNodeExpression>(*i);
      if (pExpr != nullptr && !(pExpr->getHide() && pExpr->getDisplayedLhs().getLength() == 0))
        prev_lhs = pExpr->getDisplayedLhs();
      SAL_INFO_LEVEL(3, "starmath.imath", "Line is displayable and has " << (hasResult ? "a" : "no") << " textual result" << endline);
    }

    // Point iterator to next element because emplace moves everything backwards
    ++i;

    // The following possibilities exist
    // 1. We are not aligning (alignblock is empty). Insert the resultLine
    // 2. We started a new alignblock. Neither block nor resultLine is inserted
    // 3. We continued an existing alignblock. Neither block nor resultLine is inserted
    // 4. We finished an alignblock. Insert the alignblock, and the resultLine, too
    // 5. We have processed the last line. Insert the alignblock
    // After emplacing lines, ensure that the iterator points to the line after the newly created line
    if (a.isEmpty()) { // Case 1.
      SAL_INFO_LEVEL(3, "starmath.imath", "Not aligning this line. There is " << (hasResult ? "a" : "no") << " textual result" << endline);
      if (hasResult) {
        i = mLines.emplace(i, std::make_shared<iFormulaNodeResult>(resultText));
        ++i;
        resultText = OU("");
        hasResult = false;
      }
    } else if (a.isFinished()) { // Case 4.
      SAL_INFO_LEVEL(3, "starmath.imath", "Finishing alignblock with a result line" << endline);
      i = mLines.emplace(i, std::make_shared<iFormulaNodeResult>(a.print()));
      ++i;
      if (hasResult) {
        SAL_INFO_LEVEL(3, "starmath.imath", "... and inserting new result line" << endline);
        i = mLines.emplace(i, std::make_shared<iFormulaNodeResult>(resultText));
        ++i;
        resultText = OU("");
        hasResult = false;
      }
      a.clear();
    } else if (i == mLines.end()) { // Case 5.
      SAL_INFO_LEVEL(3, "starmath.imath", "Reached last line, inserting alignblock in a result line" << endline);
      a.finish();
      mLines.emplace_back(std::make_shared<iFormulaNodeResult>(a.print()));
      i = mLines.end();
      a.clear();
    }
  }
}

void SmDocShell::UpdateEditEngineDefaultFonts()
{
    SmEditEngine::setSmItemPool(mpEditEngineItemPool.get(), maLinguOptions);
}

EditEngine& SmDocShell::GetEditEngine()
{
    if (!mpEditEngine)
    {
        //!
        //! see also SmEditWindow::DataChanged !
        //!
        mpEditEngineItemPool = EditEngine::CreatePool();
        SmEditEngine::setSmItemPool(mpEditEngineItemPool.get(), maLinguOptions);
        mpEditEngine.reset( new SmEditEngine( mpEditEngineItemPool.get() ) );
        mpEditEngine->EraseVirtualDevice();

        // set initial text if the document already has some...
        // (may be the case when reloading a doc)
        OUString aTxt( GetText() );
        if (!aTxt.isEmpty())
            mpEditEngine->SetText( aTxt );
        mpEditEngine->ClearModifyFlag();
    }
    return *mpEditEngine;
}

EditEngine& SmDocShell::GetImEditEngine()
{
    if (!mpImEditEngine)
    {
        //!
        //! see also SmEditWindow::DataChanged !
        //!
        mpEditEngineItemPool = EditEngine::CreatePool();
        SmEditEngine::setSmItemPool(mpEditEngineItemPool.get(), maLinguOptions);
        mpImEditEngine.reset( new SmEditEngine( mpEditEngineItemPool.get() ) );
        mpImEditEngine->EraseVirtualDevice();

        // set initial text if the document already has some...
        // (may be the case when reloading a doc)
        OUString aTxt( GetImText() );
        if (!aTxt.isEmpty())
            mpImEditEngine->SetText( aTxt );
        mpImEditEngine->ClearModifyFlag();
    }
    return *mpImEditEngine;
}


void SmDocShell::DrawFormula(OutputDevice &rDev, Point &rPosition, bool bDrawSelection)
{
    if (!mpTree)
        Parse();
    OSL_ENSURE(mpTree, "Sm : NULL pointer");

    ArrangeFormula();

    bool bRTL = GetFormat().IsRightToLeft();

    // Problem: What happens to WYSIWYG? While we're active inplace, we don't have a reference
    // device and aren't aligned to that either. So now there can be a difference between the
    // VisArea (i.e. the size within the client) and the current size.
    // Idea: The difference could be adapted with SmNod::SetSize (no long-term solution)

    rPosition.AdjustX(maFormat.GetDistance( DIS_LEFTSPACE ) );
    rPosition.AdjustY(maFormat.GetDistance( DIS_TOPSPACE  ) );

    Point aPosition(rPosition);
    if (bRTL && rDev.GetOutDevType() != OUTDEV_WINDOW)
        aPosition.AdjustX(GetSize().Width()
                          - maFormat.GetDistance(DIS_LEFTSPACE)
                          - maFormat.GetDistance(DIS_RIGHTSPACE));

    //! in case of high contrast-mode (accessibility option!)
    //! the draw mode needs to be set to default, because when embedding
    //! Math for example in Calc in "a over b" the fraction bar may not
    //! be visible else. More generally: the FillColor may have been changed.
    DrawModeFlags nOldDrawMode = DrawModeFlags::Default;
    bool bRestoreDrawMode = false;
    if (OUTDEV_WINDOW == rDev.GetOutDevType() &&
        rDev.GetOwnerWindow()->GetSettings().GetStyleSettings().GetHighContrastMode())
    {
        nOldDrawMode = rDev.GetDrawMode();
        rDev.SetDrawMode( DrawModeFlags::Default );
        bRestoreDrawMode = true;
    }

    rDev.Push(vcl::PushFlags::TEXTLAYOUTMODE | vcl::PushFlags::TEXTLANGUAGE);

    // We want the device to always be LTR, we handle RTL formulas ourselves.
    bool bOldRTL = rDev.IsRTLEnabled();
    if (rDev.GetOutDevType() == OUTDEV_WINDOW)
        rDev.EnableRTL(bRTL);
    else
        rDev.EnableRTL(false);

    auto nLayoutFlags = vcl::text::ComplexTextLayoutFlags::Default;
    if (bRTL)
    {
        // For RTL formulas, we want the brackets to be mirrored.
        nLayoutFlags |= vcl::text::ComplexTextLayoutFlags::BiDiRtl;
        if (rDev.GetOutDevType() == OUTDEV_WINDOW)
            nLayoutFlags |= vcl::text::ComplexTextLayoutFlags::TextOriginLeft;
    }

    rDev.SetLayoutMode(nLayoutFlags);

    // Numbers should not be converted, for now.
    rDev.SetDigitLanguage( LANGUAGE_ENGLISH );

    //Set selection if any
    if(mpCursor && bDrawSelection){
        mpCursor->AnnotateSelection();
        SmSelectionDrawingVisitor(rDev, mpTree.get(), aPosition);
    }

    //Drawing using visitor
    SmDrawingVisitor(rDev, aPosition, mpTree.get(), GetFormat());

    rDev.EnableRTL(bOldRTL);
    rDev.Pop();

    if (bRestoreDrawMode)
        rDev.SetDrawMode( nOldDrawMode );
}

Size SmDocShell::GetSize()
{
    Size aRet;

    if (!mpTree)
        Parse();

    if (mpTree)
    {
        ArrangeFormula();
        aRet = mpTree->GetSize();

        if ( !aRet.Width() || aRet.Width() == 1 )
            aRet.setWidth( 2000 );
        else
            aRet.AdjustWidth(maFormat.GetDistance( DIS_LEFTSPACE ) +
                             maFormat.GetDistance( DIS_RIGHTSPACE ) );
        if ( !aRet.Height() )
            aRet.setHeight( 1000 );
        else
            aRet.AdjustHeight(maFormat.GetDistance( DIS_TOPSPACE ) +
                             maFormat.GetDistance( DIS_BOTTOMSPACE ) );
    }

    return aRet;
}

void SmDocShell::InvalidateCursor(){
    mpCursor.reset();
}

SmCursor& SmDocShell::GetCursor(){
    if(!mpCursor)
        mpCursor.reset(new SmCursor(mpTree.get(), this));
    return *mpCursor;
}

bool SmDocShell::HasCursor() const { return mpCursor != nullptr; }

SmPrinterAccess::SmPrinterAccess( SmDocShell &rDocShell )
{
    pPrinter = rDocShell.GetPrt();
    if ( pPrinter )
    {
        pPrinter->Push( vcl::PushFlags::MAPMODE );
        if ( SfxObjectCreateMode::EMBEDDED == rDocShell.GetCreateMode() )
        {
            // if it is an embedded object (without its own printer)
            // we change the MapMode temporarily.
            //!If it is a document with its own printer the MapMode should
            //!be set correct (once) elsewhere(!), in order to avoid numerous
            //!superfluous pushing and popping of the MapMode when using
            //!this class.

            const MapUnit eOld = pPrinter->GetMapMode().GetMapUnit();
            if ( SmMapUnit() != eOld )
            {
                MapMode aMap( pPrinter->GetMapMode() );
                aMap.SetMapUnit( SmMapUnit() );
                Point aTmp( aMap.GetOrigin() );
                aTmp.setX( OutputDevice::LogicToLogic( aTmp.X(), eOld, SmMapUnit() ) );
                aTmp.setY( OutputDevice::LogicToLogic( aTmp.Y(), eOld, SmMapUnit() ) );
                aMap.SetOrigin( aTmp );
                pPrinter->SetMapMode( aMap );
            }
        }
    }
    pRefDev = rDocShell.GetRefDev();
    if ( !pRefDev || pPrinter.get() == pRefDev.get() )
        return;

    pRefDev->Push( vcl::PushFlags::MAPMODE );
    if ( SfxObjectCreateMode::EMBEDDED != rDocShell.GetCreateMode() )
        return;

    // if it is an embedded object (without its own printer)
    // we change the MapMode temporarily.
    //!If it is a document with its own printer the MapMode should
    //!be set correct (once) elsewhere(!), in order to avoid numerous
    //!superfluous pushing and popping of the MapMode when using
    //!this class.

    const MapUnit eOld = pRefDev->GetMapMode().GetMapUnit();
    if ( SmMapUnit() != eOld )
    {
        MapMode aMap( pRefDev->GetMapMode() );
        aMap.SetMapUnit( SmMapUnit() );
        Point aTmp( aMap.GetOrigin() );
        aTmp.setX( OutputDevice::LogicToLogic( aTmp.X(), eOld, SmMapUnit() ) );
        aTmp.setY( OutputDevice::LogicToLogic( aTmp.Y(), eOld, SmMapUnit() ) );
        aMap.SetOrigin( aTmp );
        pRefDev->SetMapMode( aMap );
    }
}

SmPrinterAccess::~SmPrinterAccess()
{
    if ( pPrinter )
        pPrinter->Pop();
    if ( pRefDev && pRefDev != pPrinter )
        pRefDev->Pop();
}

Printer* SmDocShell::GetPrt()
{
    if (SfxObjectCreateMode::EMBEDDED == GetCreateMode())
    {
        // Normally the server provides the printer. But if it doesn't provide one (e.g. because
        // there is no connection) it still can be the case that we know the printer because it
        // has been passed on by the server in OnDocumentPrinterChanged and being kept temporarily.
        Printer* pPrt = GetDocumentPrinter();
        if (!pPrt && mpTmpPrinter)
            pPrt = mpTmpPrinter;
        return pPrt;
    }
    else if (!mpPrinter)
    {
        auto pOptions = std::make_unique<SfxItemSetFixed<
                SID_PRINTTITLE, SID_PRINTZOOM,
                SID_NO_RIGHT_SPACES, SID_SAVE_ONLY_USED_SYMBOLS,
                SID_AUTO_CLOSE_BRACKETS, SID_SMEDITWINDOWZOOM,
                SID_INLINE_EDIT_ENABLE, SID_INLINE_EDIT_ENABLE>>(GetPool());
        SmModule *pp = SM_MOD();
        pp->GetConfig()->ConfigToItemSet(*pOptions);
        mpPrinter = VclPtr<SfxPrinter>::Create(std::move(pOptions));
        mpPrinter->SetMapMode(MapMode(SmMapUnit()));
    }
    return mpPrinter;
}

OutputDevice* SmDocShell::GetRefDev()
{
    if (SfxObjectCreateMode::EMBEDDED == GetCreateMode())
    {
        OutputDevice* pOutDev = GetDocumentRefDev();
        if (pOutDev)
            return pOutDev;
    }

    return GetPrt();
}

void SmDocShell::SetPrinter( SfxPrinter *pNew )
{
    mpPrinter.disposeAndClear();
    mpPrinter = pNew;    //Transfer ownership
    mpPrinter->SetMapMode( MapMode(SmMapUnit()) );
    SetFormulaArranged(false);
    Repaint();
}

void SmDocShell::OnDocumentPrinterChanged( Printer *pPrt )
{
    mpTmpPrinter = pPrt;
    SetFormulaArranged(false);
    Size aOldSize = GetVisArea().GetSize();
    Repaint();
    if( aOldSize != GetVisArea().GetSize() && !maText.isEmpty() )
        SetModified();
    mpTmpPrinter = nullptr;
}

void SmDocShell::Repaint()
{
    bool bIsEnabled = IsEnableSetModified();
    if (bIsEnabled)
        EnableSetModified( false );

    SetFormulaArranged(false);

    Size aVisSize = GetSize();
    SetVisAreaSize(aVisSize);
    if (SmViewShell* pViewSh = SmGetActiveView())
        pViewSh->GetGraphicWidget().Invalidate();

    if (bIsEnabled)
        EnableSetModified(bIsEnabled);
}

std::string SmDocShell::mDecimalSeparator = "";
bool SmDocShell::mImBlocked = false;

void SmDocShell::ImStaticInitialization() {
    static bool imInitialized = false;
    if (imInitialized) return;

    // Ensure iMath extension is not installed
    // TODO: Put this in SmModule::SmModule() but how to get the MessageDialog to appear?!
    Reference<XComponentContext> xContext = comphelper::getProcessComponentContext();
    OUString iMathExtLocation = getPackageLocation(xContext, "de.gmx.rheinlaender.jan.imath");
    if (iMathExtLocation.getLength() > 0) {
        SAL_WARN_LEVEL(-1, "starmath.imath", "ERROR: iMath extension found");
        std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(nullptr, VclMessageType::Error, VclButtonsType::Ok, SmResId(RID_STR_IMATHEXTENSIONFOUND)));
        xInfoBox->run();
        mImBlocked = true; // This will block execution of ::Compile() to avoid problems with CLN and GiNaC
        return;
    }

    MSG_INFO(0, "SmDocShell::SmDocShell with iMath version=" << SM_MOD()->GetConfig()->GetDefaultImSyntaxVersion());
    SvtLinguConfig().GetOptions(maLinguOptions);

    SetPool(&SfxGetpApp()->GetPool());

    SmModule *pp = SM_MOD();
    maFormat = pp->GetConfig()->GetStandardFormat();

    StartListening(maFormat);
    StartListening(*pp->GetConfig());

    SetBaseModel(new SmModel(this));
    SetSmSyntaxVersion(mnSmSyntaxVersion);

    SetMapUnit(SmMapUnit());

    // Find decimal separator character from the Office locale and store it for iMath compilation
    // TODO: Re-initialize if the locale is changed?
    Reference<lang::XMultiComponentFactory> xMCF = xContext->getServiceManager();
    OUString ooLocale = getLocaleName(xContext);
    Reference<i18n::XLocaleData> xld(xMCF->createInstanceWithContext(OU("com.sun.star.i18n.LocaleData"), xContext), UNO_QUERY_THROW);
    // TODO: Can't we pass the ooLocale string directly somehow?
    int dashpos = ooLocale.indexOfAsciiL("-",1);
    OUString ooLocale1, ooLocale2;
    if (dashpos > 0) {
        ooLocale1 = ooLocale.copy(0, dashpos);
        ooLocale2 = ooLocale.copy(dashpos + 1);
    } else {
        // Not all locales appear to return a full string, e.g. just "de" is returned on my German installation
        ooLocale1 = ooLocale;
        ooLocale2 = OU("");
    }

    mDecimalSeparator = STR(xld->getLocaleItem(lang::Locale(ooLocale1, ooLocale2, OU(""))).decimalSeparator);

    imInitialized = true;
}

SmDocShell::SmDocShell( SfxModelFlags i_nSfxCreationFlags )
    : SfxObjectShell(i_nSfxCreationFlags)
    , m_pMlElementTree(nullptr)
    , mpPrinter(nullptr)
    , mpTmpPrinter(nullptr)
    , mnModifyCount(0)
    , mbFormulaArranged(false)
    , mnSmSyntaxVersion(SM_MOD()->GetConfig()->GetDefaultSmSyntaxVersion())
    , mnImSyntaxVersion(SM_MOD()->GetConfig()->GetDefaultImSyntaxVersion())
    , mPreviousFormula("")
    , mIFormulaDependencyIn("")
    , mIFormulaDependencyOut("")
    , mpInitialOptions(nullptr)
    , mpInitialCompiler(nullptr)
    , mpCurrentOptions(nullptr)
    , mpCurrentCompiler(nullptr)
    , maImTypeFirstLine("")
    , maImTypeLastLine("")
    , mImHidden(false)
    , maImExprFirstLhs("")
    , maImExprLastLhs("")
{
    ImStaticInitialization();
    SAL_INFO_LEVEL(0, "starmath.imath", "SmDocShell::SmDocShell with iMath version=" << mnImSyntaxVersion);

    SvtLinguConfig().GetOptions(maLinguOptions);

    SetPool(&SfxGetpApp()->GetPool());

    SmModule *pp = SM_MOD();
    maFormat = pp->GetConfig()->GetStandardFormat();

    StartListening(maFormat);
    StartListening(*pp->GetConfig());

    SetBaseModel(new SmModel(this));
    SetSmSyntaxVersion(mnSmSyntaxVersion);
    SetImSyntaxVersion(mnImSyntaxVersion);
}

SmDocShell::~SmDocShell()
{
    SmModule *pp = SM_MOD();

    EndListening(maFormat);
    EndListening(*pp->GetConfig());

    mpCursor.reset();
    mpEditEngine.reset();
    mpImEditEngine.reset();
    mpEditEngineItemPool.clear();
    mpPrinter.disposeAndClear();

    mathml::SmMlIteratorFree(m_pMlElementTree);
    SAL_INFO_LEVEL(1, "starmath.imath", "Destroyed SmDocShell");
}

bool SmDocShell::ConvertFrom(SfxMedium &rMedium)
{
    bool     bSuccess = false;
    const OUString& rFltName = rMedium.GetFilter()->GetFilterName();

    OSL_ENSURE( rFltName != STAROFFICE_XML, "Wrong filter!");

    if ( rFltName == MATHML_XML )
    {
        if (mpTree)
        {
            mpTree.reset();
            InvalidateCursor();
        }
        rtl::Reference<SmModel> xModel(dynamic_cast<SmModel*>(GetModel().get()));
        SmXMLImportWrapper aEquation(xModel);
        aEquation.useHTMLMLEntities(true);
        bSuccess = ( ERRCODE_NONE == aEquation.Import(rMedium) );
    }
    else
    {
        SvStream *pStream = rMedium.GetInStream();
        if ( pStream )
        {
            if ( SotStorage::IsStorageFile( pStream ) )
            {
                tools::SvRef<SotStorage> aStorage = new SotStorage( pStream, false );
                if ( aStorage->IsStream("Equation Native") )
                {
                    // is this a MathType Storage?
                    OUStringBuffer aBuffer;
                    MathType aEquation(aBuffer);
                    bSuccess = aEquation.Parse( aStorage.get() );
                    if ( bSuccess )
                    {
                        maText = aBuffer.makeStringAndClear();
                        Parse();
                    }
                }
            }
        }
    }

    if ( GetCreateMode() == SfxObjectCreateMode::EMBEDDED )
    {
        SetFormulaArranged( false );
        Repaint();
    }

    FinishedLoading();
    return bSuccess;
}


bool SmDocShell::InitNew( const uno::Reference < embed::XStorage >& xStorage )
{
    bool bRet = false;
    if ( SfxObjectShell::InitNew( xStorage ) )
    {
        bRet = true;
        SetVisArea(tools::Rectangle(Point(0, 0), Size(2000, 1000)));
    }
    return bRet;
}


bool SmDocShell::Load( SfxMedium& rMedium )
{
    bool bRet = false;
    if( SfxObjectShell::Load( rMedium ))
    {
        uno::Reference < embed::XStorage > xStorage = GetMedium()->GetStorage();
        if (xStorage->hasByName("content.xml") && xStorage->isStreamElement("content.xml"))
        {
            // is this a fabulous math package ?
            rtl::Reference<SmModel> xModel(dynamic_cast<SmModel*>(GetModel().get()));
            SmXMLImportWrapper aEquation(xModel);
            auto nError = aEquation.Import(rMedium);
            bRet = ERRCODE_NONE == nError;
            SetError(nError);
        }
    }

    if ( GetCreateMode() == SfxObjectCreateMode::EMBEDDED )
    {
        SetFormulaArranged( false );
        Repaint();
    }

    FinishedLoading();
    return bRet;
}


bool SmDocShell::Save()
{
    //! apply latest changes if necessary
    UpdateText();

    if ( SfxObjectShell::Save() )
    {
        if (!mpTree)
            Parse();
        if( mpTree )
            ArrangeFormula();

        Reference<css::frame::XModel> xModel(GetModel());
        SmXMLExportWrapper aEquation(xModel);
        aEquation.SetFlat(false);
        return aEquation.Export(*GetMedium());
    }

    return false;
}

/*
 * replace bad characters that can not be saved. (#i74144)
 * */
void SmDocShell::ReplaceBadChars()
{
    bool bReplace = false;

    if (!mpEditEngine)
        return;

    OUStringBuffer aBuf( mpEditEngine->GetText() );

    for (sal_Int32 i = 0;  i < aBuf.getLength();  ++i)
    {
        if (aBuf[i] < ' ' && aBuf[i] != '\r' && aBuf[i] != '\n' && aBuf[i] != '\t')
        {
            aBuf[i] = ' ';
            bReplace = true;
        }
    }

    if (bReplace)
        maText = aBuf.makeStringAndClear();

    bReplace = false;

    if (!mpImEditEngine)
        return;

    aBuf = mpImEditEngine->GetText();

    for (sal_Int32 i = 0;  i < aBuf.getLength();  ++i)
    {
        if (aBuf[i] < ' ' && aBuf[i] != '\r' && aBuf[i] != '\n' && aBuf[i] != '\t')
        {
            aBuf[i] = ' ';
            bReplace = true;
        }
    }

     if (bReplace)
        maImText = aBuf.makeStringAndClear();
}


void SmDocShell::UpdateText()
{
    if (mpEditEngine && mpEditEngine->IsModified())
    {
        OUString aEngTxt( mpEditEngine->GetText() );
        if (GetText() != aEngTxt)
            SetText( aEngTxt );
    }
}

void SmDocShell::UpdateImText()
{
    if (mpImEditEngine && mpImEditEngine->IsModified())
    {
        OUString aEngTxt( mpImEditEngine->GetText() );
        if (GetImText() != aEngTxt)
            SetImText( aEngTxt );
    }
}


bool SmDocShell::SaveAs( SfxMedium& rMedium )
{
    bool bRet = false;

    //! apply latest changes if necessary
    UpdateText();

    if ( SfxObjectShell::SaveAs( rMedium ) )
    {
        if (!mpTree)
            Parse();
        if( mpTree )
            ArrangeFormula();

        Reference<css::frame::XModel> xModel(GetModel());
        SmXMLExportWrapper aEquation(xModel);
        aEquation.SetFlat(false);
        bRet = aEquation.Export(rMedium);
    }
    return bRet;
}

bool SmDocShell::ConvertTo( SfxMedium &rMedium )
{
    bool bRet = false;
    std::shared_ptr<const SfxFilter> pFlt = rMedium.GetFilter();
    if( pFlt )
    {
        if( !mpTree )
            Parse();
        if( mpTree )
            ArrangeFormula();

        const OUString& rFltName = pFlt->GetFilterName();
        if(rFltName == STAROFFICE_XML)
        {
            Reference<css::frame::XModel> xModel(GetModel());
            SmXMLExportWrapper aEquation(xModel);
            aEquation.SetFlat(false);
            bRet = aEquation.Export(rMedium);
        }
        else if(rFltName == MATHML_XML)
        {
            Reference<css::frame::XModel> xModel(GetModel());
            SmXMLExportWrapper aEquation(xModel);
            aEquation.SetFlat(true);
            aEquation.SetUseHTMLMLEntities(true);
            bRet = aEquation.Export(rMedium);
        }
        else if (pFlt->GetFilterName() == "MathType 3.x")
            bRet = WriteAsMathType3( rMedium );
    }
    return bRet;
}

void SmDocShell::writeFormulaOoxml(
        ::sax_fastparser::FSHelperPtr const& pSerializer,
        oox::core::OoxmlVersion const version,
        oox::drawingml::DocumentType const documentType,
        const sal_Int8 nAlign)
{
    if( !mpTree )
        Parse();
    if( mpTree )
        ArrangeFormula();
    SmOoxmlExport aEquation(mpTree.get(), version, documentType);
    if(documentType == oox::drawingml::DOCUMENT_DOCX)
        aEquation.ConvertFromStarMath( pSerializer, nAlign);
    else
        aEquation.ConvertFromStarMath(pSerializer, oox::FormulaImExportBase::eFormulaAlign::INLINE);
}

void SmDocShell::writeFormulaRtf(OStringBuffer& rBuffer, rtl_TextEncoding nEncoding)
{
    if (!mpTree)
        Parse();
    if (mpTree)
        ArrangeFormula();
    SmRtfExport aEquation(mpTree.get());
    aEquation.ConvertFromStarMath(rBuffer, nEncoding);
}

void SmDocShell::readFormulaOoxml( oox::formulaimport::XmlStream& stream )
{
    SmOoxmlImport aEquation( stream );
    SetText( aEquation.ConvertToStarMath());
}

void SmDocShell::Execute(SfxRequest& rReq)
{
    switch (rReq.GetSlot())
    {
        case SID_TEXTMODE:
        {
            SmFormat aOldFormat  = GetFormat();
            SmFormat aNewFormat( aOldFormat );
            aNewFormat.SetTextmode(!aOldFormat.IsTextmode());

            SfxUndoManager *pTmpUndoMgr = GetUndoManager();
            if (pTmpUndoMgr)
                pTmpUndoMgr->AddUndoAction(
                    std::make_unique<SmFormatAction>(this, aOldFormat, aNewFormat));

            SetFormat( aNewFormat );
            Repaint();
        }
        break;

        case SID_AUTO_REDRAW :
        {
            SmModule *pp = SM_MOD();
            bool bRedraw = pp->GetConfig()->IsAutoRedraw();
            pp->GetConfig()->SetAutoRedraw(!bRedraw);
        }
        break;

        case SID_LOADSYMBOLS:
            LoadSymbols();
        break;

        case SID_SAVESYMBOLS:
            SaveSymbols();
        break;

        case SID_FONT:
        {
            // get device used to retrieve the FontList
            OutputDevice *pDev = GetPrinter();
            if (!pDev || pDev->GetFontFaceCollectionCount() == 0)
                pDev = &SM_MOD()->GetDefaultVirtualDev();
            OSL_ENSURE (pDev, "device for font list missing" );

            SmFontTypeDialog aFontTypeDialog(rReq.GetFrameWeld(), pDev);

            SmFormat aOldFormat  = GetFormat();
            aFontTypeDialog.ReadFrom( aOldFormat );
            if (aFontTypeDialog.run() == RET_OK)
            {
                SmFormat aNewFormat( aOldFormat );

                aFontTypeDialog.WriteTo(aNewFormat);
                SfxUndoManager *pTmpUndoMgr = GetUndoManager();
                if (pTmpUndoMgr)
                    pTmpUndoMgr->AddUndoAction(
                        std::make_unique<SmFormatAction>(this, aOldFormat, aNewFormat));

                SetFormat( aNewFormat );
                Repaint();
            }
        }
        break;

        case SID_FONTSIZE:
        {
            SmFontSizeDialog aFontSizeDialog(rReq.GetFrameWeld());

            SmFormat aOldFormat  = GetFormat();
            aFontSizeDialog.ReadFrom( aOldFormat );
            if (aFontSizeDialog.run() == RET_OK)
            {
                SmFormat aNewFormat( aOldFormat );

                aFontSizeDialog.WriteTo(aNewFormat);

                SfxUndoManager *pTmpUndoMgr = GetUndoManager();
                if (pTmpUndoMgr)
                    pTmpUndoMgr->AddUndoAction(
                        std::make_unique<SmFormatAction>(this, aOldFormat, aNewFormat));

                SetFormat( aNewFormat );
                Repaint();
            }
        }
        break;

        case SID_DISTANCE:
        {
            SmDistanceDialog aDistanceDialog(rReq.GetFrameWeld());

            SmFormat aOldFormat  = GetFormat();
            aDistanceDialog.ReadFrom( aOldFormat );
            if (aDistanceDialog.run() == RET_OK)
            {
                SmFormat aNewFormat( aOldFormat );

                aDistanceDialog.WriteTo(aNewFormat);

                SfxUndoManager *pTmpUndoMgr = GetUndoManager();
                if (pTmpUndoMgr)
                    pTmpUndoMgr->AddUndoAction(
                        std::make_unique<SmFormatAction>(this, aOldFormat, aNewFormat));

                SetFormat( aNewFormat );
                Repaint();
            }
        }
        break;

        case SID_ALIGN:
        {
            SmAlignDialog aAlignDialog(rReq.GetFrameWeld());

            SmFormat aOldFormat  = GetFormat();
            aAlignDialog.ReadFrom( aOldFormat );
            if (aAlignDialog.run() == RET_OK)
            {
                SmFormat aNewFormat( aOldFormat );

                aAlignDialog.WriteTo(aNewFormat);

                SmModule *pp = SM_MOD();
                SmFormat aFmt( pp->GetConfig()->GetStandardFormat() );
                aAlignDialog.WriteTo( aFmt );
                pp->GetConfig()->SetStandardFormat( aFmt );

                SfxUndoManager *pTmpUndoMgr = GetUndoManager();
                if (pTmpUndoMgr)
                    pTmpUndoMgr->AddUndoAction(
                        std::make_unique<SmFormatAction>(this, aOldFormat, aNewFormat));

                SetFormat( aNewFormat );
                Repaint();
            }
        }
        break;

        case SID_TEXT:
        {
            const SfxStringItem& rItem = rReq.GetArgs()->Get(SID_TEXT);
            if (GetText() != rItem.GetValue())
                SetText(rItem.GetValue());
        }
        break;

        case SID_ITEXT:
        {
            const SfxStringItem& rItem = static_cast<const SfxStringItem&>(rReq.GetArgs()->Get(SID_ITEXT));
            if (GetImText() != rItem.GetValue())
                SetImText(rItem.GetValue());
        }
        break;

        case SID_UNDO:
        case SID_REDO:
        {
            SfxUndoManager* pTmpUndoMgr = GetUndoManager();
            if( pTmpUndoMgr )
            {
                sal_uInt16 nId = rReq.GetSlot(), nCnt = 1;
                const SfxItemSet* pArgs = rReq.GetArgs();
                const SfxPoolItem* pItem;
                if( pArgs && SfxItemState::SET == pArgs->GetItemState( nId, false, &pItem ))
                    nCnt = static_cast<const SfxUInt16Item*>(pItem)->GetValue();

                bool (SfxUndoManager::*fnDo)();

                size_t nCount;
                if( SID_UNDO == rReq.GetSlot() )
                {
                    nCount = pTmpUndoMgr->GetUndoActionCount();
                    fnDo = &SfxUndoManager::Undo;
                }
                else
                {
                    nCount = pTmpUndoMgr->GetRedoActionCount();
                    fnDo = &SfxUndoManager::Redo;
                }

                try
                {
                    for( ; nCnt && nCount; --nCnt, --nCount )
                        (pTmpUndoMgr->*fnDo)();
                }
                catch( const Exception& )
                {
                    DBG_UNHANDLED_EXCEPTION("starmath");
                }
            }
            Repaint();
            UpdateText();
            SfxViewFrame* pFrm = SfxViewFrame::GetFirst( this );
            while( pFrm )
            {
                SfxBindings& rBind = pFrm->GetBindings();
                rBind.Invalidate(SID_UNDO);
                rBind.Invalidate(SID_REDO);
                rBind.Invalidate(SID_REPEAT);
                rBind.Invalidate(SID_CLEARHISTORY);
                pFrm = SfxViewFrame::GetNext( *pFrm, this );
            }
        }
        break;
    }

    rReq.Done();
}


void SmDocShell::GetState(SfxItemSet &rSet)
{
    SfxWhichIter aIter(rSet);

    for (sal_uInt16 nWh = aIter.FirstWhich();  0 != nWh;  nWh = aIter.NextWhich())
    {
        switch (nWh)
        {
        case SID_TEXTMODE:
            rSet.Put(SfxBoolItem(SID_TEXTMODE, GetFormat().IsTextmode()));
            break;

        case SID_DOCTEMPLATE :
            rSet.DisableItem(SID_DOCTEMPLATE);
            break;

        case SID_AUTO_REDRAW :
            {
                SmModule  *pp = SM_MOD();
                bool       bRedraw = pp->GetConfig()->IsAutoRedraw();

                rSet.Put(SfxBoolItem(SID_AUTO_REDRAW, bRedraw));
            }
            break;

        case SID_MODIFYSTATUS:
            {
                sal_Unicode cMod = ' ';
                if (IsModified())
                    cMod = '*';
                rSet.Put(SfxStringItem(SID_MODIFYSTATUS, OUString(cMod)));
            }
            break;

        case SID_TEXT:
            rSet.Put(SfxStringItem(SID_TEXT, GetText()));
            break;

        case SID_ITEXT:
            rSet.Put(SfxStringItem(SID_ITEXT, GetImText()));
            break;

        case SID_GRAPHIC_SM:
            //! very old (pre UNO) and ugly hack to invalidate the SmGraphicWidget.
            //! If mnModifyCount gets changed then the call below will implicitly notify
            //! SmGraphicController::StateChanged and there the window gets invalidated.
            //! Thus all the 'mnModifyCount++' before invalidating this slot.
            rSet.Put(SfxInt16Item(SID_GRAPHIC_SM, mnModifyCount));
            break;

        case SID_UNDO:
        case SID_REDO:
            {
                SfxViewFrame* pFrm = SfxViewFrame::GetFirst( this );
                if( pFrm )
                    pFrm->GetSlotState( nWh, nullptr, &rSet );
                else
                    rSet.DisableItem( nWh );
            }
            break;

        case SID_GETUNDOSTRINGS:
        case SID_GETREDOSTRINGS:
            {
                SfxUndoManager* pTmpUndoMgr = GetUndoManager();
                if( pTmpUndoMgr )
                {
                    OUString(SfxUndoManager::*fnGetComment)( size_t, bool const ) const;

                    size_t nCount;
                    if( SID_GETUNDOSTRINGS == nWh )
                    {
                        nCount = pTmpUndoMgr->GetUndoActionCount();
                        fnGetComment = &SfxUndoManager::GetUndoActionComment;
                    }
                    else
                    {
                        nCount = pTmpUndoMgr->GetRedoActionCount();
                        fnGetComment = &SfxUndoManager::GetRedoActionComment;
                    }
                    if (nCount)
                    {
                        OUStringBuffer aBuf;
                        for (size_t n = 0; n < nCount; ++n)
                        {
                            aBuf.append((pTmpUndoMgr->*fnGetComment)( n, SfxUndoManager::TopLevel ));
                            aBuf.append('\n');
                        }

                        SfxStringListItem aItem( nWh );
                        aItem.SetString( aBuf.makeStringAndClear() );
                        rSet.Put( aItem );
                    }
                }
                else
                    rSet.DisableItem( nWh );
            }
            break;
        }
    }
}


SfxUndoManager *SmDocShell::GetUndoManager()
{
    if (!mpEditEngine)
        GetEditEngine();
    return &mpEditEngine->GetUndoManager();
}


void SmDocShell::SaveSymbols()
{
    SmModule *pp = SM_MOD();
    pp->GetSymbolManager().Save();
}


void SmDocShell::Draw(OutputDevice *pDevice,
                      const JobSetup &,
                      sal_uInt16 /*nAspect*/,
                      bool /*bOutputForScreen*/)
{
    pDevice->IntersectClipRegion(GetVisArea());
    Point atmppoint;
    DrawFormula(*pDevice, atmppoint);
}

SfxItemPool& SmDocShell::GetPool()
{
    return SfxGetpApp()->GetPool();
}

void SmDocShell::SetVisArea(const tools::Rectangle & rVisArea)
{
    tools::Rectangle aNewRect(rVisArea);

    aNewRect.SetPos(Point());

    if (aNewRect.IsWidthEmpty())
        aNewRect.SetRight( 2000 );
    if (aNewRect.IsHeightEmpty())
        aNewRect.SetBottom( 1000 );

    bool bIsEnabled = IsEnableSetModified();
    if ( bIsEnabled )
        EnableSetModified( false );

    //TODO/LATER: it's unclear how this interacts with the SFX code
    // If outplace editing, then don't resize the OutplaceWindow. But the
    // ObjectShell has to resize.
    bool bUnLockFrame;
    if( GetCreateMode() == SfxObjectCreateMode::EMBEDDED && !IsInPlaceActive() && GetFrame() )
    {
        GetFrame()->LockAdjustPosSizePixel();
        bUnLockFrame = true;
    }
    else
        bUnLockFrame = false;

    SfxObjectShell::SetVisArea( aNewRect );

    if( bUnLockFrame )
        GetFrame()->UnlockAdjustPosSizePixel();

    if ( bIsEnabled )
        EnableSetModified( bIsEnabled );
}


void SmDocShell::FillClass(SvGlobalName* pClassName,
                           SotClipboardFormatId*  pFormat,
                           OUString* pFullTypeName,
                           sal_Int32 nFileFormat,
                           bool bTemplate /* = false */) const
{
    if (nFileFormat == SOFFICE_FILEFORMAT_60 )
    {
        *pClassName     = SvGlobalName(SO3_SM_CLASSID_60);
        *pFormat        = SotClipboardFormatId::STARMATH_60;
        *pFullTypeName  = SmResId(STR_MATH_DOCUMENT_FULLTYPE_CURRENT);
    }
    else if (nFileFormat == SOFFICE_FILEFORMAT_8 )
    {
        *pClassName     = SvGlobalName(SO3_SM_CLASSID_60);
        *pFormat        = bTemplate ? SotClipboardFormatId::STARMATH_8_TEMPLATE : SotClipboardFormatId::STARMATH_8;
        *pFullTypeName  = SmResId(STR_MATH_DOCUMENT_FULLTYPE_CURRENT);
    }
}

void SmDocShell::SetModified(bool bModified)
{
    if( IsEnableSetModified() )
    {
        SfxObjectShell::SetModified( bModified );
        Broadcast(SfxHint(SfxHintId::DocChanged));
    }
}

bool SmDocShell::WriteAsMathType3( SfxMedium& rMedium )
{
    OUStringBuffer aTextAsBuffer(maText);
    MathType aEquation(aTextAsBuffer, mpTree.get());
    return aEquation.ConvertFromStarMath( rMedium );
}

void SmDocShell::SetRightToLeft(bool bRTL)
{
    SmFormat aOldFormat = GetFormat();
    if (aOldFormat.IsRightToLeft() == bRTL)
        return;

    SmFormat aNewFormat(aOldFormat);
    aNewFormat.SetRightToLeft(bRTL);

    SfxUndoManager* pTmpUndoMgr = GetUndoManager();
    if (pTmpUndoMgr)
        pTmpUndoMgr->AddUndoAction(
            std::make_unique<SmFormatAction>(this, aOldFormat, aNewFormat));

    SetFormat(aNewFormat);
    Repaint();
}

static Size GetTextLineSize(OutputDevice const& rDevice, const OUString& rLine)
{
    Size aSize(rDevice.GetTextWidth(rLine), rDevice.GetTextHeight());
    const tools::Long nTabPos = rLine.isEmpty() ? 0 : rDevice.approximate_digit_width() * 8;

    if (nTabPos)
    {
        aSize.setWidth(0);
        sal_Int32 nPos = 0;
        do
        {
            if (nPos > 0)
                aSize.setWidth(((aSize.Width() / nTabPos) + 1) * nTabPos);

            const OUString aText = rLine.getToken(0, '\t', nPos);
            aSize.AdjustWidth(rDevice.GetTextWidth(aText));
        } while (nPos >= 0);
    }

    return aSize;
}

static Size GetTextSize(OutputDevice const& rDevice, std::u16string_view rText,
                        tools::Long MaxWidth)
{
    Size aSize;
    Size aTextSize;
    if (rText.empty())
        return aTextSize;

    sal_Int32 nPos = 0;
    do
    {
        OUString aLine(o3tl::getToken(rText, 0, '\n', nPos));
        aLine = aLine.replaceAll("\r", "");

        aSize = GetTextLineSize(rDevice, aLine);

        if (aSize.Width() > MaxWidth)
        {
            do
            {
                OUString aText;
                sal_Int32 m = aLine.getLength();
                sal_Int32 nLen = m;

                for (sal_Int32 n = 0; n < nLen; n++)
                {
                    sal_Unicode cLineChar = aLine[n];
                    if ((cLineChar == ' ') || (cLineChar == '\t'))
                    {
                        aText = aLine.copy(0, n);
                        if (GetTextLineSize(rDevice, aText).Width() < MaxWidth)
                            m = n;
                        else
                            break;
                    }
                }

                aText = aLine.copy(0, m);
                aLine = aLine.replaceAt(0, m, u"");
                aSize = GetTextLineSize(rDevice, aText);
                aTextSize.AdjustHeight(aSize.Height());
                aTextSize.setWidth(std::clamp(aSize.Width(), aTextSize.Width(), MaxWidth));

                aLine = comphelper::string::stripStart(aLine, ' ');
                aLine = comphelper::string::stripStart(aLine, '\t');
                aLine = comphelper::string::stripStart(aLine, ' ');
            } while (!aLine.isEmpty());
        }
        else
        {
            aTextSize.AdjustHeight(aSize.Height());
            aTextSize.setWidth(std::max(aTextSize.Width(), aSize.Width()));
        }
    } while (nPos >= 0);

    return aTextSize;
}

static void DrawTextLine(OutputDevice& rDevice, const Point& rPosition, const OUString& rLine)
{
    Point aPoint(rPosition);
    const tools::Long nTabPos = rLine.isEmpty() ? 0 : rDevice.approximate_digit_width() * 8;

    if (nTabPos)
    {
        sal_Int32 nPos = 0;
        do
        {
            if (nPos > 0)
                aPoint.setX(((aPoint.X() / nTabPos) + 1) * nTabPos);

            OUString aText = rLine.getToken(0, '\t', nPos);
            rDevice.DrawText(aPoint, aText);
            aPoint.AdjustX(rDevice.GetTextWidth(aText));
        } while (nPos >= 0);
    }
    else
        rDevice.DrawText(aPoint, rLine);
}

static void DrawText(OutputDevice& rDevice, const Point& rPosition, std::u16string_view rText,
                     sal_uInt16 MaxWidth)
{
    if (rText.empty())
        return;

    Point aPoint(rPosition);
    Size aSize;

    sal_Int32 nPos = 0;
    do
    {
        OUString aLine(o3tl::getToken(rText, 0, '\n', nPos));
        aLine = aLine.replaceAll("\r", "");
        aSize = GetTextLineSize(rDevice, aLine);
        if (aSize.Width() > MaxWidth)
        {
            do
            {
                OUString aText;
                sal_Int32 m = aLine.getLength();
                sal_Int32 nLen = m;

                for (sal_Int32 n = 0; n < nLen; n++)
                {
                    sal_Unicode cLineChar = aLine[n];
                    if ((cLineChar == ' ') || (cLineChar == '\t'))
                    {
                        aText = aLine.copy(0, n);
                        if (GetTextLineSize(rDevice, aText).Width() < MaxWidth)
                            m = n;
                        else
                            break;
                    }
                }
                aText = aLine.copy(0, m);
                aLine = aLine.replaceAt(0, m, u"");

                DrawTextLine(rDevice, aPoint, aText);
                aPoint.AdjustY(aSize.Height());

                aLine = comphelper::string::stripStart(aLine, ' ');
                aLine = comphelper::string::stripStart(aLine, '\t');
                aLine = comphelper::string::stripStart(aLine, ' ');
            } while (GetTextLineSize(rDevice, aLine).Width() > MaxWidth);

            // print the remaining text
            if (!aLine.isEmpty())
            {
                DrawTextLine(rDevice, aPoint, aLine);
                aPoint.AdjustY(aSize.Height());
            }
        }
        else
        {
            DrawTextLine(rDevice, aPoint, aLine);
            aPoint.AdjustY(aSize.Height());
        }
    } while (nPos >= 0);
}

void SmDocShell::Impl_Print(OutputDevice& rOutDev, const SmPrintUIOptions& rPrintUIOptions,
                tools::Rectangle aOutRect)
{
    const bool bIsPrintTitle = rPrintUIOptions.getBoolValue(PRTUIOPT_TITLE_ROW, true);
    const bool bIsPrintFrame = rPrintUIOptions.getBoolValue(PRTUIOPT_BORDER, true);
    const bool bIsPrintFormulaText = rPrintUIOptions.getBoolValue(PRTUIOPT_FORMULA_TEXT, true);
    SmPrintSize ePrintSize(static_cast<SmPrintSize>(
        rPrintUIOptions.getIntValue(PRTUIOPT_PRINT_FORMAT, PRINT_SIZE_NORMAL)));
    const sal_uInt16 nZoomFactor
        = static_cast<sal_uInt16>(rPrintUIOptions.getIntValue(PRTUIOPT_PRINT_SCALE, 100));

    rOutDev.Push();
    rOutDev.SetLineColor(COL_BLACK);

    // output text on top
    if (bIsPrintTitle)
    {
        Size aSize600(0, 600);
        Size aSize650(0, 650);
        vcl::Font aFont(FAMILY_DONTKNOW, aSize600);

        aFont.SetAlignment(ALIGN_TOP);
        aFont.SetWeight(WEIGHT_BOLD);
        aFont.SetFontSize(aSize650);
        aFont.SetColor(COL_BLACK);
        rOutDev.SetFont(aFont);

        Size aTitleSize(GetTextSize(rOutDev, GetTitle(), aOutRect.GetWidth() - 200));

        aFont.SetWeight(WEIGHT_NORMAL);
        aFont.SetFontSize(aSize600);
        rOutDev.SetFont(aFont);

        Size aDescSize(GetTextSize(rOutDev, GetComment(), aOutRect.GetWidth() - 200));

        if (bIsPrintFrame)
            rOutDev.DrawRect(tools::Rectangle(
                aOutRect.TopLeft(), Size(aOutRect.GetWidth(), 100 + aTitleSize.Height() + 200
                                                                  + aDescSize.Height() + 100)));
        aOutRect.AdjustTop(200);

        // output title
        aFont.SetWeight(WEIGHT_BOLD);
        aFont.SetFontSize(aSize650);
        rOutDev.SetFont(aFont);
        Point aPoint(aOutRect.Left() + (aOutRect.GetWidth() - aTitleSize.Width()) / 2,
                     aOutRect.Top());
        DrawText(rOutDev, aPoint, GetTitle(),
                 sal::static_int_cast<sal_uInt16>(aOutRect.GetWidth() - 200));
        aOutRect.AdjustTop(aTitleSize.Height() + 200);

        // output description
        aFont.SetWeight(WEIGHT_NORMAL);
        aFont.SetFontSize(aSize600);
        rOutDev.SetFont(aFont);
        aPoint.setX(aOutRect.Left() + (aOutRect.GetWidth() - aDescSize.Width()) / 2);
        aPoint.setY(aOutRect.Top());
        DrawText(rOutDev, aPoint, GetComment(),
                 sal::static_int_cast<sal_uInt16>(aOutRect.GetWidth() - 200));
        aOutRect.AdjustTop(aDescSize.Height() + 300);
    }

    // output text on bottom
    if (bIsPrintFormulaText)
    {
        vcl::Font aFont(FAMILY_DONTKNOW, Size(0, 600));
        aFont.SetAlignment(ALIGN_TOP);
        aFont.SetColor(COL_BLACK);

        // get size
        rOutDev.SetFont(aFont);

        Size aSize(GetTextSize(rOutDev, GetText(), aOutRect.GetWidth() - 200));

        aOutRect.AdjustBottom(-(aSize.Height() + 600));

        if (bIsPrintFrame)
            rOutDev.DrawRect(tools::Rectangle(
                aOutRect.BottomLeft(), Size(aOutRect.GetWidth(), 200 + aSize.Height() + 200)));

        Point aPoint(aOutRect.Left() + (aOutRect.GetWidth() - aSize.Width()) / 2,
                     aOutRect.Bottom() + 300);
        DrawText(rOutDev, aPoint, GetText(),
                 sal::static_int_cast<sal_uInt16>(aOutRect.GetWidth() - 200));
        aOutRect.AdjustBottom(-200);
    }

    if (bIsPrintFrame)
        rOutDev.DrawRect(aOutRect);

    aOutRect.AdjustTop(100);
    aOutRect.AdjustLeft(100);
    aOutRect.AdjustBottom(-100);
    aOutRect.AdjustRight(-100);

    Size aSize(GetSize());

    MapMode OutputMapMode;
    switch (ePrintSize)
    {
        case PRINT_SIZE_NORMAL:
            OutputMapMode = MapMode(SmMapUnit());
            break;

        case PRINT_SIZE_SCALED:
            if (!aSize.IsEmpty())
            {
                sal_uInt16 nZ
                    = std::min(o3tl::convert(aOutRect.GetWidth(), 100, aSize.Width()),
                               o3tl::convert(aOutRect.GetHeight(), 100, aSize.Height()));
                if (bIsPrintFrame && nZ > MINZOOM)
                    nZ -= 10;
                Fraction aFraction(std::clamp(nZ, MINZOOM, MAXZOOM), 100);

                OutputMapMode = MapMode(SmMapUnit(), Point(), aFraction, aFraction);
            }
            else
                OutputMapMode = MapMode(SmMapUnit());
            break;

        case PRINT_SIZE_ZOOMED:
        {
            Fraction aFraction(nZoomFactor, 100);

            OutputMapMode = MapMode(SmMapUnit(), Point(), aFraction, aFraction);
            break;
        }
    }

    aSize = OutputDevice::LogicToLogic(aSize, OutputMapMode, MapMode(SmMapUnit()));

    Point aPos(aOutRect.Left() + (aOutRect.GetWidth() - aSize.Width()) / 2,
               aOutRect.Top() + (aOutRect.GetHeight() - aSize.Height()) / 2);

    aPos = OutputDevice::LogicToLogic(aPos, MapMode(SmMapUnit()), OutputMapMode);
    aOutRect = OutputDevice::LogicToLogic(aOutRect, MapMode(SmMapUnit()), OutputMapMode);

    rOutDev.SetMapMode(OutputMapMode);
    rOutDev.SetClipRegion(vcl::Region(aOutRect));
    DrawFormula(rOutDev, aPos);
    rOutDev.SetClipRegion();

    rOutDev.Pop();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
