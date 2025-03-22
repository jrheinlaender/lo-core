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
#include <sfx2/dinfdlg.hxx>
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
#include <imath/funcmgr.hxx>
#include <imath/imathutils.hxx>
#include <imath/imathparse.hxx>

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

void SmDocShell::SetImSyntaxVersion(sal_Int32 nImSyntaxVersion)
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

Reference<XModel> SmDocShell::GetDocumentModel(OUString& documentType) const
{
    documentType = "SmDoc";
    Reference<container::XChild> xModel(GetModel(), UNO_QUERY);
    if (!xModel.is())
        return GetModel();

    Reference<XModel> xParent(xModel->getParent(), UNO_QUERY);

    if (xParent.is())
    {
        Reference<XTextDocument> xTextDoc(xParent, UNO_QUERY);
        if (xTextDoc.is())
        {
            SAL_INFO_LEVEL(1, "starmath.imath", "Found parent text document");
            documentType = "XTextDocument";
            return xParent;
        }

        Reference<presentation::XPresentationSupplier> xPresDoc(xParent, UNO_QUERY);
        if (xPresDoc.is())
        {
            SAL_INFO_LEVEL(1, "starmath.imath", "Found parent presentation document");
            documentType = "XPresentationSupplier";
            return xParent;
        }
    }

    SAL_INFO_LEVEL(1, "starmath.imath", "No parent document for formula");
    return GetModel();
}
Reference<XModel> SmDocShell::GetDocumentModel() const
{
    OUString documentType;
    return GetDocumentModel(documentType);
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
    /*
     * This method is disabled for now because it breaks insertion of charts as soon as the number of
     * formula objects in the document exceeds the officecfg::Office::Common::Cache::Writer::OLE_Objects value
     * Instead we adjust that value in sw/source/uibase/app/docsh.cxx so that no formulas are ever closed
    SAL_INFO_LEVEL(1, "starmath.imath", "SmDocShell::PreventFormulaClose(): " << (prevent ? "on" : "off"));
    const uno::Reference < util::XCloseBroadcaster > xCloseBroadcaster(GetModel(), UNO_QUERY);
    if (!xCloseBroadcaster.is()) return;

    if (prevent)
    {
        if (!m_xIFormulaClosePreventer.is())
        {
            SAL_INFO_LEVEL(2, "starmath.imath", "Adding new close preventer to SmDocShell");
            m_xIFormulaClosePreventer = new IFormulaClosePreventer;
            xCloseBroadcaster->addCloseListener(m_xIFormulaClosePreventer);
        }
    }
    else
    {
        if (m_xIFormulaClosePreventer.is()) {
            xCloseBroadcaster->removeCloseListener(m_xIFormulaClosePreventer);
            m_xIFormulaClosePreventer.clear();
            SAL_INFO_LEVEL(2, "starmath.imath", "Removed close preventer from SmDocShell");
        }
        else
        {
            SAL_INFO_LEVEL(2, "starmath.imath", "Not removing close preventer from SmDocShell because none exists");
        }
    }
    */

    SetIFormulaPendingAction("");
}

void SmDocShell::SetImText(const OUString& rBuffer, const bool doCompile)
{
    if (rBuffer == maImText)
        return;

    maImText = rBuffer;

    if (doCompile && maImText.getLength() > 0)
    {
        bool bIsEnabled = IsEnableSetModified();
        if( bIsEnabled )
            EnableSetModified( false );

        Compile();

        if ( bIsEnabled )
            EnableSetModified( bIsEnabled );
        SetModified();
    }
}

void SmDocShell::SetPreviousFormula(const OUString& aName)
{
    // Ensure that this formula will not be cleaned out of the OLE cache
    PreventFormulaClose(true); // This must happen as soon as possible, to avoid the formula dying in the middle of Compile()

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

OUString compileIncludes(const std::list<OUString>& files, imath::parserParameters& pParams, const OUString& folder, std::list<iFormulaLine_ptr>& mLines, std::shared_ptr<GiNaC::optionmap>& initialOptions)
{
    // Read all files in one parser run
    for (const auto& f : files) {
        if (f.getLength() > 2)
            pParams.rawtext += OU("%%ii READFILE {\"") + folder + f.copy(2) + OU(".imath") + OU("\"}\n");
    }

    if (!pParams.rawtext.isEmpty()) {
        SAL_INFO_LEVEL(0, "starmath.imath", "Reading referenced files\n" << STR(pParams.rawtext));
        pParams.lines.clear();
        imath::imathparse parser;
        if (parser.parse(pParams) != 0)
        {
            SAL_WARN_LEVEL(-1, "starmath.imath", "Parser error: " << mLines.back()->getErrorMessage());
            OUString error("");
            for (const auto& l : mLines)
                if (l->hasError())
                    error += l->getErrorMessage() + "\n";
            return error;
        }
        if (mLines.size() > 0)
            initialOptions = mLines.back()->getGlobalOptions(); // Options might have been changed by the OPTIONS keyword
    }

    return {};
}

// Order of initialization
// =======================
// mPreviousFormula is not empty, and mIFormulaMasterDocument is empty
//    Get compiler and options from previous formula in current document
//
// a) Stand-alone math formula
//    Initialize options
//    Read iMath references (at least init.imath)
//    Initialize units
//    Read user-defined include files
//
// b) First formula in text or presentation document, without master document
//    Initialize options
//    Read iMath references (at least init.imath)
//    Initialize units
//    Read user-defined include files
//
// c) First formula in text or presentation document, with master document
//    Note that only the first formula in a document will have the mIFormulaMasterDocument property set
//    Case b) has already run on the master document
//    Get compiler and options from previous formula in master document
//    Read iMath references that have not already been read in master document
//    Initialize units for current document
//    Read user-defined include files

OUString SmDocShell::ImInitializeCompiler() {
    SAL_INFO_LEVEL(1, "starmath.imath", "Preparing formula for compilation");

    // Get access to the registry that contains the global options
    Reference<XComponentContext> xContext(comphelper::getProcessComponentContext());
    Reference<XHierarchicalPropertySet> xProperties = getRegistryAccess(xContext, OU("/org.openoffice.Office.iMath/"));

    std::list<OUString> masterDocFiles;

    if (!mPreviousFormula.isEmpty()) {
        // Find previous iFormula from parent document. If this fails, a error message is returned
        SAL_INFO_LEVEL(1, "starmath.imath", "Previous formula is " << mPreviousFormula << (mIFormulaMasterDocument.isEmpty() ? OUString() : " in master document " + mIFormulaMasterDocument));
        Reference<XModel> xParent;

        // Find master document if there is one
        if (!mIFormulaMasterDocument.isEmpty())
        {
            SAL_INFO_LEVEL(1, "starmath.imath", "Searching for master document '" << mIFormulaMasterDocument << "'");
            // Note: We assume that the parent document has already loaded the master document
            Reference<XDesktop> xDesktop(xContext->getServiceManager()->createInstanceWithContext("com.sun.star.frame.Desktop", xContext), UNO_QUERY_THROW);
            Reference<container::XEnumerationAccess> xLoadedDocsEnumAccess = xDesktop->getComponents();
            Reference<container::XEnumeration> xDocsEnum = xLoadedDocsEnumAccess->createEnumeration();

            while (xDocsEnum->hasMoreElements()) {
                Any docModel = xDocsEnum->nextElement();
                docModel >>= xParent;
                if (!xParent.is()) continue;

                Reference<XStorable> xDocumentStorable(xParent, UNO_QUERY);
                if (!xDocumentStorable.is()) continue;

                SAL_INFO_LEVEL(1, "starmath.imath", "Checking for master document: '" << xDocumentStorable->getLocation() << "'");
                if (xDocumentStorable->getLocation() == mIFormulaMasterDocument)
                {
                    SAL_INFO_LEVEL(1, "starmath.imath", "Found master document");
                    Reference<XNamedGraph> xGraph = getGraph(xContext, xParent);
                    if (xGraph.is())
                    {
                        masterDocFiles = splitString(getTextProperty(xContext, xParent, xGraph, xProperties, OU("includes_txt_references"), OU("Includes/txt_References")), ' ');
                        masterDocFiles.emplace_back(getTextProperty(xContext, xParent, xGraph, xProperties, OU("includes_txt_include1"), OU("Includes/txt_Include1")));
                        masterDocFiles.emplace_back(getTextProperty(xContext, xParent, xGraph, xProperties, OU("includes_txt_include2"), OU("Includes/txt_Include2")));
                        masterDocFiles.emplace_back(getTextProperty(xContext, xParent, xGraph, xProperties, OU("includes_txt_include3"), OU("Includes/txt_Include3")));
                        masterDocFiles.unique();
                    }
                    break;
                }
            }

            if (masterDocFiles.empty())
                return OUString("Master document ") + mIFormulaMasterDocument + " could not be found"; // Note: We rely on at lest 00init reference to exist
        }

        if (!xParent.is())
        {
            SAL_INFO_LEVEL(2, "starmath.imath", "Searching for parent document");
            Reference<container::XChild> xModel(GetModel(), UNO_QUERY);
            if (xModel.is())
                xParent = Reference<XModel>(xModel->getParent(), UNO_QUERY);
        }

        if (!xParent.is())
            return "Parent document with previous formula could not be found";

        Reference < XComponent > xPreviousFormulaComponent = getObjectByName(xParent, mPreviousFormula);
        if (xPreviousFormulaComponent.is()) {
            SAL_INFO_LEVEL(2, "starmath.imath", "Found previous formula in parent document");
            Reference< XModel > xPreviousFormula = extractModel(xPreviousFormulaComponent);

            SmModel* pPreviousModel = comphelper::getFromUnoTunnel<SmModel>(xPreviousFormula);
            SmDocShell* pPreviousDocShell = pPreviousModel ? static_cast<SmDocShell*>(pPreviousModel->GetObjectShell()) : nullptr;

            if (pPreviousDocShell != nullptr) {
                mpInitialCompiler = pPreviousDocShell->mpCurrentCompiler;
                mpInitialOptions = pPreviousDocShell->mpCurrentOptions;
                if (mpInitialCompiler != nullptr && mpInitialOptions != nullptr) {
                    SAL_INFO_LEVEL(1, "starmath.imath", "Set initial compiler and options from previous formula");
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

    // Check for stand-alone formula or part of Text / Presentation
    OUString documentType;
    Reference<XModel> xParent = GetDocumentModel(documentType);
    Reference<XModel> xModel;

    if (documentType.equalsAscii("SmDoc"))
    {
        SAL_INFO_LEVEL(1, "starmath.imath", "Detected Starmath document");
        xModel = GetBaseModel(); // GetDocumentModel() uses GetModel() instead
    }
    else
        xModel = xParent;

    // Get access to the RDF graph that contains the document-specific options. Create one if it doesn't exist
    // TODO In stand-alone Math the graph does not get saved with the document. Why?
    Reference<XNamedGraph> xGraph = getGraph(xContext, xModel);
    if (!xGraph.is())
        xGraph = createGraph(xContext, xModel);

    // Path to iMath's own include files (references)
    OUString shareFolder;
    // TODO Fix build system to include share/imath into the Windows msi files
    //OUString shareURL("$BRAND_BASE_DIR/" LIBO_SHARE_FOLDER "/imath/references/");
    OUString shareURL("$BRAND_BASE_DIR/" LIBO_SHARE_FOLDER "/calc/");
    rtl::Bootstrap::expandMacros(shareURL);
    osl::FileBase::getSystemPathFromFileURL(shareURL, shareFolder);

    // TODO: Handle case when ImInitialize() is called after options were changed through the UI
    if (mpInitialOptions != nullptr && mpInitialCompiler != nullptr)
    {
        if (!mIFormulaMasterDocument.isEmpty())
        {
            // Read additional references and includes in the sub-document of the master document
            auto files = splitString(getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_references"), OU("Includes/txt_References")), ' ');
            files.emplace_back(getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_include1"), OU("Includes/txt_Include1")));
            files.emplace_back(getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_include2"), OU("Includes/txt_Include2")));
            files.emplace_back(getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_include3"), OU("Includes/txt_Include3")));
            files.unique();
            for (const auto& mFile : masterDocFiles)
            {
                auto it = std::find(files.begin(), files.end(), mFile);
                if (it != files.end())
                    files.erase(it);
            }

            // Update option map with options from sub-document
            Settingsmanager::initializeOptionmap(xContext, xModel, xGraph, xProperties, mpInitialOptions, true);

            if (files.empty())
                return ""; // Nothing to be done

            try
            {
                mpInitialCompiler = std::make_shared<eqc>(*mpInitialCompiler); // Take a deep copy because we must not modify the currentCompiler of the previous formula
                imath::parserParameters pParams(mLines);
                pParams.xContext = comphelper::getProcessComponentContext();
                pParams.xDocumentModel = GetDocumentModel();
                pParams.rawtext = OU("");
                pParams.copyPasteActive = false; // TODO: Check if LO still crashes when a formula is changed during a copy+paste action
                pParams.compiler = mpInitialCompiler;
                pParams.global_options = mpInitialOptions;
                pParams.cached_results = nullptr; // Cached results are not useful for include files

                OUString error = compileIncludes(files, pParams, shareFolder, mLines, mpInitialOptions);

                if (!error.isEmpty())
                    return error;
            }
            catch (Exception &e)
            {
                SAL_WARN_LEVEL(-1, "starmath.imath", "Parser exception: " << e.Message);
                return e.Message;
            }
            catch (std::exception &e)
            {
                SAL_WARN_LEVEL(-1, "starmath.imath", "Parser exception: " << e.what());
                return OUS8(e.what());
            }
        }

        return "";
    }

    // Stand-alone formula document or first formula in document
    SAL_INFO_LEVEL(1, "starmath.imath", "Preparing stand-alone formula or first formula in document");

    mpInitialOptions = std::make_shared<GiNaC::optionmap>();
    mpInitialCompiler = std::make_shared<eqc>();

    // Get/Set document-specific options
    // 1. If the document contains document-specific options in an RDF graph, these are used
    // 2. Otherwise, the values from the registry are used and also copied to the RDF graph
    //    In other words, only a new document before the first recalc() will use the registry values, to ensure
    //    document display consistency
    // References. These are always document specific
    OUString references = getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_references"), OU("Includes/txt_References"));
    SAL_INFO_LEVEL(1, "starmath.imath", "Found references '" << references << "'");
    if (references.isEmpty())
    {
        SAL_WARN_LEVEL(1, "starmath.imath", "Empty references found in document, assuming init.imath must be loaded");
        references = "00init";
    }
    OUString include1 = getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_include1"), OU("Includes/txt_Include1"));
    OUString include2 = getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_include2"), OU("Includes/txt_Include2"));
    OUString include3 = getTextProperty(xContext, xModel, xGraph, xProperties, OU("includes_txt_include3"), OU("Includes/txt_Include3"));
    SAL_INFO_LEVEL(1, "starmath.imath", "Found user references '" << include1 << "', '" << include2 << "', '" << include3 << "'");

    // Formatting
    Settingsmanager::initializeOptionmap(xContext, xModel, xGraph, xProperties, mpInitialOptions, false);

    imath::parserParameters pParams(mLines);

    try {
        // Read referenced files
        auto files = splitString(references, ' ');
        files.unique();
        pParams.xContext = comphelper::getProcessComponentContext();
        pParams.xDocumentModel = GetDocumentModel();
        pParams.rawtext = OU("");
        pParams.copyPasteActive = false; // TODO: Check if LO still crashes when a formula is changed during a copy+paste action
        pParams.compiler = mpInitialCompiler;
        pParams.global_options = mpInitialOptions;
        pParams.cached_results = nullptr; // Cached results are not useful for include files

        // Read all files in one parser run
        OUString error = compileIncludes(files, pParams, shareFolder, mLines, mpInitialOptions);
        if (!error.isEmpty())
            return error;

        // units must be set AFTER units.imath is read, because the preferred units list might use user-defined units
        // Note that this will delete any preferred units declared in the previous include files (there shouldn't be any!)
        OUString units = OUS8(*mpInitialOptions->at(o_unitstr).value.str); // This was populated in initializeOptionmap()
        mpInitialOptions->emplace(o_units, option(GiNaC::exvector())); // All keys are expected to exist in global_options
        mpInitialOptions->at(o_unitstr).value.str->clear(); // Clear o_unitstr, because it will be populated again

        if (units.getLength() > 0) {
            // Recreate the global units expression vector, since this cannot be stored in the registry
            SAL_INFO_LEVEL(0, "starmath.imath", "Parsing default units\n" << STR(pParams.rawtext));
            pParams.rawtext = OU("%%ii OPTIONS {units={") + units + OU("}}\n");
            pParams.lines.clear();
            imath::imathparse parser;
            // Result is stored in mpInitialOptions map under the keys o_unit and o_unitstr
            if (parser.parse(pParams) != 0) {
                SAL_WARN_LEVEL(-1, "starmath.imath", "Parser error: " << mLines.back()->getErrorMessage());
                return mLines.back()->getErrorMessage();
            }
            if (mLines.size() > 0)
                mpInitialOptions = mLines.back()->getGlobalOptions();
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
            pParams.lines.clear();
            imath::imathparse parser;
            if (parser.parse(pParams) != 0)
            {
                SAL_WARN_LEVEL(-1, "starmath.imath", "Parser error: " << mLines.back()->getErrorMessage());
                error = "";
                for (const auto& l : mLines)
                    if (l->hasError())
                        error += l->getErrorMessage() + "\n";
                return error;
            }
            if (mLines.size() > 0)
                mpInitialOptions = mLines.back()->getGlobalOptions();
        }
    } catch (Exception &e) {
        SAL_WARN_LEVEL(-1, "starmath.imath", "Parser exception: " << e.Message);
        return e.Message;
    } catch (std::exception &e) {
        SAL_WARN_LEVEL(-1, "starmath.imath", "Parser exception: " << e.what());
        return OUS8(e.what());
    }

    return "";
}

sal_Int64 SmDocShell::lastTempLabel = 0;

void SmDocShell::Compile()
{
    if (mImBlocked) {
        SAL_WARN_LEVEL(-1, "starmath.imath", "iMath cannot be used because an iMath extension is still installed");
        return;
    }

    SAL_INFO_LEVEL(1, "starmath.imath", "SmDocShell::Compile()\n'" << maImText << "'");

    OUString initError = ImInitializeCompiler();
    if (initError.getLength() > 0) {
        SAL_WARN_LEVEL(0, "starmath.imath", initError);
        // TODO: Publish it somewhere
        mLines.clear();
        mpCurrentCompiler = mpInitialCompiler;
        mpCurrentOptions = mpInitialOptions;
        return;
    }

    if (maImText.isEmpty())
    {
        SAL_INFO_LEVEL(1, "starmath.imath", "Empty formula, aborting compile");
        mpCurrentCompiler = mpInitialCompiler;
        mpCurrentOptions = mpInitialOptions;
        return; // empty Math formula
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
    SAL_INFO_LEVEL(2, "starmath.imath", "This formula had old outgoing dependencies for '" << makeSymbolString(oldOutDep) << "'");

    // Prepare compiler. Note: Since mpCurrentCompiler is a shared_ptr, the old data will automatically get cleaned up when the last reference is released
    mpCurrentCompiler = std::make_shared<eqc>(*mpInitialCompiler); // Takes a deep copy TODO: Reduce the amount of data copied, e.g. by copy-on-write semantics in the eqc private data structures
    mLines.clear();

    imath::parserParameters pParams(mLines);
    pParams.xContext = comphelper::getProcessComponentContext();
    pParams.xDocumentModel = GetDocumentModel();
    pParams.copyPasteActive = false;
    pParams.compiler = mpCurrentCompiler;
    pParams.cached_results = nullptr; // TODO: Cached results not used yet
    mpCurrentOptions = mpInitialOptions; // mpInitialOptions are not modified by the parser, copy is taken when OPTIONS keyword is encountered

    // Compile line-by-line, creating error lines if necessary but handling all lines
    sal_Int32 idx = 0;

    do
    {
        OUString line = maImText.getToken(0, '\n', idx);
        if (line.getLength() == 0)
            continue;
        pParams.rawtext = "%%ii " + line + OU("\n"); // TODO: Change parser to make this unnecessary
        pParams.global_options = mpCurrentOptions;
        bool reset_auto_renumber = false;

        try
        {
            bool try_again;

            do
            {
                imath::imathparse parser;
                int parseResult = parser.parse(pParams);
                try_again = false;

                if (parseResult > 0)
                {
                    SAL_WARN_LEVEL(-1, "starmath.imath", "Parser error: " << mLines.back()->getErrorMessage());

                    if (mLines.back()->getErrorMessage().indexOfAsciiL("Duplicate label:", 16) >= 0)
                    {
                        MSG_INFO(0, "Found duplication error" << endline);
                    }
                }
            } while (try_again);

            pParams.updateFormulas.clear();
        }
        catch (Exception &e)
        {
            SAL_WARN_LEVEL(-1, "starmath.imath", "Parser exception: " << e.Message);
            mLines.emplace_back(std::make_shared<iFormulaNodeError>(mpCurrentOptions, pParams.rawtext.trim()));
            mLines.back()->markError(pParams.rawtext, 5, 5, pParams.rawtext.getLength(), e.Message);
        }
        catch (std::exception &e)
        {
            SAL_WARN_LEVEL(-1, "starmath.imath", "Parser exception: " << e.what());
            mLines.emplace_back(std::make_shared<iFormulaNodeError>(mpCurrentOptions, pParams.rawtext.trim()));
            mLines.back()->markError(pParams.rawtext, 5, 5, pParams.rawtext.getLength(), OUS(e.what()));
        }

        if (reset_auto_renumber)
        {
            std::shared_ptr<comphelper::ConfigurationChanges> batch(comphelper::ConfigurationChanges::create());
            officecfg::Office::iMath::Miscellaneous::O_Autorenumberduplicate::set(false, batch);
            batch->commit();
        }

        if (mLines.empty())
            continue; // If the echo option is turned on, then a formula might start with a generated line (%%gg)

        mpCurrentOptions = mLines.back()->getGlobalOptions();
    } while (idx >= 0);

    if (mLines.size() > 0) {
        SAL_INFO_LEVEL(0, "starmath.imath", "Printing " << mLines.size() << " lines");
        for (const auto& i : mLines)
            SAL_INFO_LEVEL(0, "starmath.imath", i->printFormula());

        bool hasAlignment = addResultLines();

        // Reduce column spacing for aligned equations. Note: This also reduces spaces in "normal" matrices
        if (hasAlignment)
        {
            SmFormat aOldFormat  = GetFormat();
            SmFormat aNewFormat( aOldFormat );
            aNewFormat.SetDistance(DIS_MATRIXCOL, sal_Int16(10));
            SetFormat( aNewFormat );
        }

        OUString result;
        for (const auto& i : mLines)
            if (typeid(*i) == typeid(iFormulaNodeResult))
                result += i->print() + OU("\n");

        SetText(result); // This takes care of marking the document as modified etc.

        // Update properties
        // Note: This must happen after print() because the displayedLhs is created in the iFormulaLine::display() method
        maImTypeFirstLine = "";
        maImTypeLastLine = "";
        mImHidden = true;
        std::shared_ptr<iFormulaLine> firstLine; // First expression or equation (of multi-line formula)
        std::shared_ptr<iFormulaLine> lastLine; // Last expression or equation  (of multi-line formula)
        std::vector<OUString> tempLabels; // Sequence does not appear to support appending of elements

        for (const auto& l : mLines)
        {
            if (typeid(*l) == typeid(iFormulaNodeEq))
            {
                maImTypeFirstLine = "equation";
                iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(l);
                tempLabels.push_back(expr->getLabel());
                firstLine = l;
                break;
            }
            else if (typeid(*l) == typeid(iFormulaNodeEx) || typeid(*l) == typeid(iFormulaNodeConst))
            {
                maImTypeFirstLine = "expression";
                iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(l);
                if (expr->getLabel().getLength() > 0)
                    tempLabels.push_back(expr->getLabel());
                firstLine = l;
                break;
            }
        }

        mImLabels = Sequence<OUString>(tempLabels.size());

        for (size_t l = 0; l < tempLabels.size(); ++l)
            mImLabels.getArray()[l] = tempLabels[l];

        for (auto line = mLines.rbegin(); line != mLines.rend(); ++line)
        {
            if (typeid(**line) == typeid(iFormulaNodeEq))
            {
                maImTypeLastLine = "equation";
                lastLine = *line;
                break;
            }
            else if (typeid(**line) == typeid(iFormulaNodeEx) || typeid(**line) == typeid(iFormulaNodeConst))
            {
                maImTypeLastLine = "expression";
                lastLine = *line;
                break;
            }
        }

        for (const auto& l : mLines)
        {
            iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(l);

            if (expr != nullptr && !expr->getHide())
                mImHidden = false; // If one line is not hidden, the whole formula counts as not hidden
        }

        // Set text mode if wished for and applicable
        if (lastLine != nullptr && lastLine->getOption(o_autotextmode).value.boolean)
            SetIFormulaPendingAction("checktextmode");

        /* Note: This works but has no effect
            * SmDocShell *pDocSh = static_cast < SmDocShell * > (GetObjectShell());
             * SmFormat aFormat = pDocSh->GetFormat();
             * aFormat.SetTextmode(true); //checkTextmodeFormula(XTextContent)));
             * pDocSh->SetFormat( aFormat );
            * pDocSh->SetVisArea( tools::Rectangle( Point(0, 0), pDocSh->GetSize() ) );
            */

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

    //setlocale(LC_NUMERIC, ""); // Reset to system locale
    if (GetIFormulaPendingAction() == "compile")
        SetIFormulaPendingAction("");
    SAL_INFO_LEVEL(0, "starmath.imath", "Recalculation finished" << endline);
}

void SmDocShell::SetImHidden(const bool h)
{
    if (mImHidden == h) return;

    unsigned basefontheight = o3tl::convert(GetFormat().GetBaseSize().Height(), SmO3tlLengthUnit(), o3tl::Length::pt);

    for (auto& l : mLines)
    {
        iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(l);

        if (expr != nullptr && expr->getHide() != h)
            expr->setHide(h);

        if (typeid(*l) == typeid(iFormulaNodeResult)) continue;

        l->setBasefontHeight(basefontheight);
    }

    SAL_INFO_LEVEL(2, "starmath.imath", "Set formula hidden");
    mImHidden = h;
    UpdateGuiText();
}

const GiNaC::expression& SmDocShell::GetUnit(const OUString& unitname) const
{
    return mpCurrentCompiler->getUnit(STR(unitname));
}

std::vector<std::string> SmDocShell::GetAllUnitNames(iFormulaLine_ptr pLine) const {
    std::vector<std::string> result;

    if (mpInitialCompiler != nullptr)
        result = mpInitialCompiler->getUnitnames();

    bool unitAdded = false;
    for (const auto& l : mLines)
    {
        if (l == pLine)
            break;

        if (typeid(*l) == typeid(iFormulaNodeStmUnitdef))
        {
            auto line = std::dynamic_pointer_cast<iFormulaNodeStmUnitdef>(l);
            result.push_back(STR(line->getUnitname()));
            unitAdded = true;
        }
    }

    if (unitAdded)
        std::sort(result.begin(), result.end());

    return result;
}

std::vector<std::string> SmDocShell::GetAllLabels(iFormulaLine_ptr pLine) const {
    std::vector<std::string> result;

    if (mpInitialCompiler != nullptr)
        result = mpInitialCompiler->getLabels();

    for (const auto& l : mLines) {
        if (l == pLine)
            break;

        if (l->isExpression())
        {
            auto line = std::dynamic_pointer_cast<iFormulaNodeExpression>(l);
            result.push_back(STR(line->getLabel()));
        }
    }

    std::sort(result.begin(), result.end());

    return result;
}

void SmDocShell::insertFormulaLineBefore(const iFormulaLine_ptr& pLine, iFormulaLine_ptr pNewLine)
{
    if (pLine == nullptr)
    {
        mLines.push_back(std::move(pNewLine));
        return;
    }

    auto it = std::find(mLines.begin(), mLines.end(), pLine);
    if (it == mLines.end())
        return;

    mLines.insert(it, std::move(pNewLine));

    // Note: Not calling this because all cases that use this method call it anysway
    //UpdateGuiText();
}

void SmDocShell::eraseFormulaLine(const iFormulaLine_ptr& pLine)
{
    auto it = std::find(mLines.begin(), mLines.end(), pLine);
    if (it == mLines.end())
        return;

    mLines.erase(it);

    UpdateGuiText();
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

bool SmDocShell::addResultLines()
{
    SAL_INFO_LEVEL(2, "starmath.imath", "SmDocShell::addResultLines");
    // Check if we must reserve space for labels
    bool showlabels = false;
    for (const auto& line_it : mLines)
    {
        iExpression_ptr p_expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(line_it);
        if (p_expr != nullptr && !p_expr->getHide() && line_it->getOption(o_showlabels).value.boolean)
        {
            showlabels = true;
            break;
        }
    }
    SAL_INFO_LEVEL(3, "starmath.imath", "showlabels = " << (showlabels ? "true" : "false"));

    // Find parent text document, if there is one
    // A valid xModel is only required for the CHART statement
    OUString documentType;
    Reference<XModel> xParent = GetDocumentModel(documentType);
    if (!documentType.equalsAscii("XTextDocument"))
        xParent.clear();

    unsigned basefontheight = o3tl::convert(GetFormat().GetBaseSize().Height(), SmO3tlLengthUnit(), o3tl::Length::pt);
    std::vector<std::vector<OUString>> resultMatrix; // Collect all result lines in a sort of matrix
    std::vector<OUString> currentMatrixLine;
    std::size_t columns(0);
    bool autoalign(false);
    bool autochain(false);
    OUString lineLabel("");
    OUString exLabel("");
    OUString previousLhs("");

    for (auto line_it = mLines.begin(); line_it != mLines.end();)
    {
        SAL_INFO_LEVEL(4, "starmath.imath", "Processing formula line");
        const auto& line = *line_it;
        iExpression_ptr p_expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(line);

        // Create result lines
        line->setBasefontHeight(basefontheight);
        std::vector<std::vector<OUString>> displayLines = line->display(xParent);

        if (currentMatrixLine.empty())
        {
            // Extract options. Options remain valid until the next newline, thus we extract them only if we are starting a new result line
            autoalign = line->getOption(o_eqalign).value.boolean;
            autochain = line->getOption(o_eqchain).value.boolean;

            // Extract label. This label is shown at the beginning of the result line in a column by itself
            lineLabel = (p_expr != nullptr && line->getOption(o_showlabels).value.boolean) ? p_expr->getLabel() : "";
            if (!lineLabel.isEmpty())
                lineLabel = OU("\"(") + lineLabel + OU(")\"~");
            else
                lineLabel = OU("{}");

            SAL_INFO_LEVEL(3, "starmath.imath", "Extracted line label '" << lineLabel << "'");
        }
        else
        {
            // Extract label for expression/equation in the middle of a result line
            exLabel = (p_expr != nullptr && line->getOption(o_showlabels).value.boolean) ? p_expr->getLabel() : OU("");
            SAL_INFO_LEVEL(3, "starmath.imath", "Extracted in-line label '" << exLabel << "'");
        }

        // The error markup won't fit into the auto-aligned columns
        if (line->hasError())
            autoalign = false;

        // Move iterator to next formula line
        ++line_it;

        // Concatenate result lines such that each result line ends with a newline
        // Note: newlines are only ever found at the end of a resultLine
        for (auto displayLine_it = displayLines.begin(); displayLine_it != displayLines.end(); )
        {
            auto& displayLine = *displayLine_it;
            SAL_INFO_LEVEL(4, "starmath.imath", "Processing display line");
            /*for (const auto& col : displayLine)
                std::cout << "'" << col << "' ";
            std::cout << std::endl;*/

            if (displayLine.empty())
            {
                ++displayLine_it;
                continue;
            }

            // Option showlabels=true was used in an expression/equation in the middle of a line
            if (!exLabel.isEmpty() && displayLine.empty())
            {
                displayLine.front() = OU("{alignr \"(") + exLabel + OU(")\"}~") + displayLine.front();
                exLabel = OU("");
            }

            if (currentMatrixLine.empty())
                std::swap(currentMatrixLine, displayLine);
            else
                currentMatrixLine.insert(currentMatrixLine.end(), displayLine.begin(), displayLine.end());

            // Move iterator to next result line
            ++displayLine_it;

            // Finish the result line either at a new line or after processing all formula lines
            bool hasNewline = currentMatrixLine.back().trim().equalsAsciiL("newline", 7);
            if (hasNewline)
            {
                SAL_INFO_LEVEL(4, "starmath.imath", OUString("Finishing result line") + (hasNewline ? OUString(" with newline") : OUString("")));
                // Check for repeated left-hand-side
                if (autochain && previousLhs.equals(currentMatrixLine.front()))
                    currentMatrixLine.front() = OU("{}");
                else
                    previousLhs = currentMatrixLine.front();

                if (hasNewline && currentMatrixLine.size() > 1) {
                    // Add label if asked for
                    if (showlabels)
                        currentMatrixLine.emplace(currentMatrixLine.begin(), lineLabel);

                    // Mark request for automatic alignment
                    if (hasNewline)
                        currentMatrixLine.back() = (autoalign ? "y" : "n"); // Overwrite the newline because it is unnecessary
                    else
                        currentMatrixLine.emplace_back(autoalign ? "y" : "n"); // This might be the case for the last formula line

                    // Count columns
                    columns = std::max(columns, currentMatrixLine.size() - 1); // But don't count alignment marker

                    // Write line to matrix
                    resultMatrix.emplace_back(std::vector<OUString>());
                    std::swap(resultMatrix.back(), currentMatrixLine);
                }
                else
                {
                    // Empty line, e.g. hidden line
                    currentMatrixLine.clear();
                }
            }
        }

        // Add echo line after current formula line if asked for (note that the iterator has already been incremented and emplace() inserts the new element before it)
        if (line->getOption(o_echoformula).value.boolean == true)
        {
            if (typeid(*line) != typeid(iFormulaNodeComment) && typeid(*line) != typeid(iFormulaNodeEmptyLine) && typeid(*line) != typeid(iFormulaNodeResult))
            {
                OUString rtext = line->print();
                rtext = replaceString(rtext, OU("\""), OU("\\\""));
                rtext = replaceString(rtext, OU("\n%%ii+"), OU("\" newline\"%%ii+"));
                rtext = OU("\"") + rtext + OU("\" newline{}");

                line_it = mLines.emplace(line_it, std::make_shared<iFormulaNodeResult>(rtext));
                ++line_it;
                SAL_INFO_LEVEL(3, "starmath.imath", "Created echo line");
            }
        }
    }

    // Finish remaining line
    if (!currentMatrixLine.empty()) {
        // TODO This replicates code from above, but it is difficult to know in advance when a line should be finished
        // e.g. if CLEAREQUATIONS is used in the last formula line, then that is empty and the displayline_it loop will not run at all
        if (autochain && previousLhs.equals(currentMatrixLine.front()))
            currentMatrixLine.front() = OU("{}");
        if (showlabels)
            currentMatrixLine.emplace(currentMatrixLine.begin(), lineLabel);
        currentMatrixLine.emplace_back(autoalign ? "y" : "n");
        columns = std::max(columns, currentMatrixLine.size() - 1);

        // Write line to matrix
        resultMatrix.emplace_back(std::vector<OUString>());
        std::swap(resultMatrix.back(), currentMatrixLine);
    }

    // Add all result lines at the end
    bool insideBlock = false;
    bool hasAlignment = false;

    for (auto resultLine_it = resultMatrix.begin(); resultLine_it != resultMatrix.end(); )
    {
        SAL_INFO_LEVEL(4, "starmath.imath", "Result matrix row");
        auto& resultLine = *resultLine_it;

        // Check for automatic alignment, and ignore the setting if there is only a single result line
        autoalign = (resultLine.back() == OU("y")) && (resultMatrix.size() > 1);

        if (autoalign)
        {
            if (!insideBlock)
            {
                // Start new alignment block
                mLines.emplace_back(std::make_shared<iFormulaNodeResult>(OU("MATRIX {")));
                insideBlock = true;
                hasAlignment = true; // There is (at least) one aligned block in the result lines
            }
        }
        else
        {
            if (insideBlock)
            {
                // Finish alignment block
                mLines.emplace_back(std::make_shared<iFormulaNodeResult>(OU("} newline")));
                insideBlock = false;
            }
        }

        resultLine.pop_back(); // Remove alignment marker

        OUString textLine("");
        for (auto part_it = resultLine.begin(); part_it != resultLine.end(); )
        {
            textLine += *part_it;
            ++part_it;
            if (part_it != resultLine.end() && autoalign)
                textLine += OU(" # ");
        }

        // Add empty columns as required
        if (autoalign)
        {
            size_t col = resultLine.size();
            while (columns > col++)
                textLine += OU(" # {}");
        }

        // Move iterator to next line
        ++resultLine_it;

        if (resultLine_it != resultMatrix.end())
        {
            if (resultLine_it->back() == OU("y") && autoalign)
                textLine += OU(" ##"); // Continue aligned matrix
            else
                textLine += OU(" newline"); // Finish normal line
        }

        SAL_INFO_LEVEL(4, "starmath.imath", "Adding result line " << textLine);
        mLines.emplace_back(std::make_shared<iFormulaNodeResult>(textLine));
    }

    if (insideBlock)
        mLines.emplace_back(std::make_shared<iFormulaNodeResult>(OU("}")));

    return hasAlignment;
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

    // Find decimal separator character from the Office locale and store it for iMath compilation
    // TODO: Re-initialize if the locale is changed?
    // TODO: utl::ConfigManager::getUILocale()
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
    , mIFormulaMasterDocument("")
    , mIFormulaPendingAction("")
    , mpInitialOptions(nullptr)
    , mpInitialCompiler(nullptr)
    , mpCurrentOptions(nullptr)
    , mpCurrentCompiler(nullptr)
    , maImTypeFirstLine("")
    , maImTypeLastLine("")
    , mImHidden(false)
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
    SAL_INFO_LEVEL(3, "starmath.imath", "Destroyed SmDocShell"); // Note: ~SmDocShell() is only called after the IFormulaClosePreventer has been removed
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

std::shared_ptr<SfxDocumentInfoDialog> SmDocShell::CreateDocumentInfoDialog(weld::Window* pParent, const SfxItemSet &rSet)
{
    std::shared_ptr<SfxDocumentInfoDialog> xDlg = std::make_shared<SfxDocumentInfoDialog>(pParent, rSet);

    SmDocShell* pDocSh = static_cast<SmDocShell*>( SfxObjectShell::Current());
    if( pDocSh == this )
    {
        xDlg->AddIMathTabPage();
        xDlg->AddIMathReferencesTabPage();
    }
    return xDlg;
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

void SmDocShell::UpdateGuiText()
{
    OUString newFormula;

    for (const auto& line : mLines)
    {
        if (typeid(*line) == typeid(iFormulaNodeResult))  continue;
        newFormula += line->print().copy(5) + "\n";
    }

    if (GetImText() != newFormula)
        SetImText( newFormula );
    else
        SAL_INFO_LEVEL(0, "starmath.imath", "Formula unchanged, not updating GUI");
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

// Methods changing iFormulaLines:
// SetImHidden() calls UpdateGuiText()
// insertFormulaLineBefore() currently does not call UpdateGuiText()
// eraseFormulaLine() calls UpdateGuiText()
// ImGuiWindow MousePressHdl IMGUIWINDOW_COL_HIDE and IMGUIWINDOW_COL_LABEL_HIDE calls UpdateGuiText()
// ImGuiWindow EditedEntryHdl calls UpdateGuiText() directly or indirectly via eraseFormulaLine()
// ImGuiWindow KeyReleaseHdl calls insertFormulaLineBefore() and UpdateGuiText()
// ImGuiOptionsDialog multiple handlers call UpdateGuiText()
// ImEditWindow constructor calls Compile(): Hack to compile the formula immediately after it was openend

// Update chain when an iFormulaLine was changed:
// UpdateGuiText() rebuilds ImText and calls SetImText()
// SetImText() blocks SetModified(), calls Compile() and then calls SetModified()
// Compile() rebuilds the iFormulaLines and calls SetText()
// SetText() blocks SetModified(), calls Parse() and then calls SetFormulaArranged() and SetModified()
// SetModified() broadcasts DocChanged
// SmViewShell::Notify() catches the DocChanged broadcast and calls ImGuiWindow::ResetModel()
// ImGuiWindow::ResetModel() sets the formula line pointer for the ImGuiOptionsDialog
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
