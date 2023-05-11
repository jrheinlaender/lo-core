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

#include <config_features.h>

#include <officecfg/Office/Common.hxx>
#include <vcl/weld.hxx>
#include <vcl/svapp.hxx>
#include <vcl/syswin.hxx>
#include <vcl/jobset.hxx>
#include <svl/numformat.hxx>
#include <svl/whiter.hxx>
#include <svl/eitem.hxx>
#include <svl/stritem.hxx>
#include <svl/PasswordHelper.hxx>
#include <unotools/moduleoptions.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/docfilt.hxx>
#include <sfx2/notebookbar/SfxNotebookBar.hxx>
#include <sfx2/printer.hxx>
#include <sfx2/linkmgr.hxx>
#include <editeng/flstitem.hxx>
#include <comphelper/lok.hxx>
#include <comphelper/classids.hxx>
#include <basic/sbmod.hxx>
#include <osl/diagnose.h>
#include <node.hxx>
#include <swwait.hxx>
#include <printdata.hxx>
#include <view.hxx>
#include <edtwin.hxx>
#include <PostItMgr.hxx>
#include <wrtsh.hxx>
#include <docsh.hxx>
#include <viewopt.hxx>
#include <wdocsh.hxx>
#include <swmodule.hxx>
#include <globdoc.hxx>
#include <usrpref.hxx>
#include <shellio.hxx>
#include <docstyle.hxx>
#include <doc.hxx>
#include <docfunc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <IDocumentSettingAccess.hxx>
#include <IDocumentLinksAdministration.hxx>
#include <IDocumentDeviceAccess.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <IDocumentStatistics.hxx>
#include <IDocumentState.hxx>
#include <pview.hxx>
#include <srcview.hxx>
#include <ndindex.hxx>
#include <ndole.hxx>
#include <txtftn.hxx>
#include <ftnidx.hxx>
#include <fldbas.hxx>
#include <docary.hxx>
#include <swerror.h>
#include <cmdid.h>
#include <strings.hrc>

#include <unotools/fltrcfg.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/objface.hxx>

#define ShellClass_SwDocShell
#include <sfx2/msg.hxx>
#include <swslots.hxx>
#include <com/sun/star/document/UpdateDocMode.hpp>

#include <com/sun/star/script/XLibraryContainer.hpp>
#include <com/sun/star/document/XDocumentProperties.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/sdb/DatabaseContext.hpp>
#include <com/sun/star/sdb/XDocumentDataSource.hpp>
#include <com/sun/star/uri/UriReferenceFactory.hpp>
#include <com/sun/star/uri/VndSunStarPkgUrlReferenceFactory.hpp>
#include <com/sun/star/frame/XStorable.hpp>
#include <com/sun/star/frame/XComponentLoader.hpp>
#include <com/sun/star/text/XParagraphCursor.hpp>
#include <ooo/vba/XSinkCaller.hpp>

#include <unotextrange.hxx>
#include <unotxdoc.hxx>

#include <dbmgr.hxx>
#include <iodetect.hxx>

#include <comphelper/processfactory.hxx>

#include <unicode/regex.h>
#include <logging.hxx>
#include <imath/imathutils.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::script;
using namespace ::com::sun::star::container;

SFX_IMPL_SUPERCLASS_INTERFACE(SwDocShell, SfxObjectShell)

void SwDocShell::InitInterface_Impl()
{
}


SFX_IMPL_OBJECTFACTORY(SwDocShell, SvGlobalName(SO3_SW_CLASSID), "swriter"  )

bool SwDocShell::InsertGeneratedStream(SfxMedium & rMedium,
        uno::Reference<text::XTextRange> const& xInsertPosition)
{
    SwUnoInternalPaM aPam(*GetDoc()); // must have doc since called from SwView
    if (!::sw::XTextRangeToSwPaM(aPam, xInsertPosition))
        return false;
    // similar to SwView::InsertMedium
    SwReaderPtr pReader;
    Reader *const pRead = StartConvertFrom(rMedium, pReader, nullptr, &aPam);
    if (!pRead)
        return false;
    ErrCodeMsg const nError = pReader->Read(*pRead);
    return ERRCODE_NONE == nError;
}

// Prepare loading
Reader* SwDocShell::StartConvertFrom(SfxMedium& rMedium, SwReaderPtr& rpRdr,
                                    SwCursorShell const *pCursorShell,
                                    SwPaM* pPaM )
{
    bool bAPICall = false;
    if( const SfxBoolItem* pApiItem = rMedium.GetItemSet().GetItemIfSet( FN_API_CALL ) )
        bAPICall = pApiItem->GetValue();

    std::shared_ptr<const SfxFilter> pFlt = rMedium.GetFilter();
    if( !pFlt )
    {
        if(!bAPICall)
        {
            std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(nullptr,
                                                          VclMessageType::Info, VclButtonsType::Ok,
                                                          SwResId(STR_CANTOPEN)));
            xInfoBox->run();
        }
        return nullptr;
    }
    OUString aFileName( rMedium.GetName() );
    Reader* pRead = SwReaderWriter::GetReader( pFlt->GetUserData() );
    if( !pRead )
        return nullptr;

    if( rMedium.IsStorage()
        ? SwReaderType::Storage & pRead->GetReaderType()
        : SwReaderType::Stream & pRead->GetReaderType() )
    {
        if (pPaM)
            rpRdr.reset(new SwReader( rMedium, aFileName, *pPaM ));
        else if (pCursorShell)
            rpRdr.reset(new SwReader( rMedium, aFileName, *pCursorShell->GetCursor() ));
        else
            rpRdr.reset(new SwReader( rMedium, aFileName, m_xDoc.get() ));
    }
    else
        return nullptr;

    // #i30171# set the UpdateDocMode at the SwDocShell
    const SfxUInt16Item* pUpdateDocItem = rMedium.GetItemSet().GetItem(SID_UPDATEDOCMODE, false);
    m_nUpdateDocMode = pUpdateDocItem ? pUpdateDocItem->GetValue() : document::UpdateDocMode::NO_UPDATE;

    if (!pFlt->GetDefaultTemplate().isEmpty())
        pRead->SetTemplateName( pFlt->GetDefaultTemplate() );

    if( pRead == ReadAscii && nullptr != rMedium.GetInStream() &&
        pFlt->GetUserData() == FILTER_TEXT_DLG )
    {
        SwAsciiOptions aOpt;
        if( const SfxStringItem* pItem = rMedium.GetItemSet().GetItemIfSet( SID_FILE_FILTEROPTIONS ) )
            aOpt.ReadUserData( pItem->GetValue() );

        pRead->GetReaderOpt().SetASCIIOpts( aOpt );
    }

    return pRead;
}

// Loading
bool SwDocShell::ConvertFrom( SfxMedium& rMedium )
{
    SwReaderPtr pRdr;
    Reader* pRead = StartConvertFrom(rMedium, pRdr);
    if (!pRead)
      return false; // #129881# return if no reader is found
    tools::SvRef<SotStorage> pStg=pRead->getSotStorageRef(); // #i45333# save sot storage ref in case of recursive calls

    m_xDoc->setDocAccTitle(OUString());
    if (const auto pFrame1 = SfxViewFrame::GetFirst(this))
    {
        if (auto pSysWin = pFrame1->GetWindow().GetSystemWindow())
        {
            pSysWin->SetAccessibleName(OUString());
        }
    }
    SwWait aWait( *this, true );

        // Suppress SfxProgress, when we are Embedded
    SW_MOD()->SetEmbeddedLoadSave(
                            SfxObjectCreateMode::EMBEDDED == GetCreateMode() );

    pRdr->GetDoc().getIDocumentSettingAccess().set(DocumentSettingId::HTML_MODE, dynamic_cast< const SwWebDocShell *>( this ) !=  nullptr);

    // Restore the pool default if reading a saved document.
    m_xDoc->RemoveAllFormatLanguageDependencies();

    ErrCodeMsg nErr = pRdr->Read( *pRead );

    // Maybe put away one old Doc
    if (m_xDoc.get() != &pRdr->GetDoc())
    {
        RemoveLink();
        m_xDoc = &pRdr->GetDoc();

        AddLink();

        if (!m_xBasePool.is())
            m_xBasePool = new SwDocStyleSheetPool( *m_xDoc, SfxObjectCreateMode::ORGANIZER == GetCreateMode() );
    }

    UpdateFontList();
    InitDrawModelAndDocShell(this, m_xDoc ? m_xDoc->getIDocumentDrawModelAccess().GetDrawModel() : nullptr);

    pRdr.reset();

    SW_MOD()->SetEmbeddedLoadSave( false );

    SetError(nErr);
    bool bOk = !nErr.IsError();

    if (bOk && !m_xDoc->IsInLoadAsynchron())
    {
        LoadingFinished();
    }

    pRead->setSotStorageRef(pStg); // #i45333# save sot storage ref in case of recursive calls

    return bOk;
}

// Saving the Default-Format, Stg present
bool SwDocShell::Save()
{
    //#i3370# remove quick help to prevent saving of autocorrection suggestions
    if (m_pView)
        m_pView->GetEditWin().StopQuickHelp();
    SwWait aWait( *this, true );

    CalcLayoutForOLEObjects();  // format for OLE objects
    // #i62875#
    // reset compatibility flag <DoNotCaptureDrawObjsOnPage>, if possible
    if (m_pWrtShell && m_xDoc &&
        m_xDoc->getIDocumentSettingAccess().get(DocumentSettingId::DO_NOT_CAPTURE_DRAW_OBJS_ON_PAGE) &&
        docfunc::AllDrawObjsOnPage(*m_xDoc))
    {
        m_xDoc->getIDocumentSettingAccess().set(DocumentSettingId::DO_NOT_CAPTURE_DRAW_OBJS_ON_PAGE, false);
    }

    ErrCodeMsg nErr = ERR_SWG_WRITE_ERROR;
    ErrCode nVBWarning = ERRCODE_NONE;
    if( SfxObjectShell::Save() )
    {
        switch( GetCreateMode() )
        {
        case SfxObjectCreateMode::INTERNAL:
            nErr = ERRCODE_NONE;
            break;

        case SfxObjectCreateMode::ORGANIZER:
            {
                WriterRef xWrt;
                ::GetXMLWriter(std::u16string_view(), GetMedium()->GetBaseURL(true), xWrt);
                xWrt->SetOrganizerMode( true );
                SwWriter aWrt( *GetMedium(), *m_xDoc );
                nErr = aWrt.Write( xWrt );
                xWrt->SetOrganizerMode( false );
            }
            break;

        case SfxObjectCreateMode::EMBEDDED:
            // Suppress SfxProgress, if we are Embedded
            SW_MOD()->SetEmbeddedLoadSave( true );
            [[fallthrough]];

        case SfxObjectCreateMode::STANDARD:
        default:
            {
                if (m_xDoc->ContainsMSVBasic())
                {
                    if( SvtFilterOptions::Get().IsLoadWordBasicStorage() )
                        nVBWarning = GetSaveWarningOfMSVBAStorage( static_cast<SfxObjectShell&>(*this) );
                    m_xDoc->SetContainsMSVBasic( false );
                }

                // End TableBox Edit!
                if (m_pWrtShell)
                    m_pWrtShell->EndAllTableBoxEdit();

                WriterRef xWrt;
                ::GetXMLWriter(std::u16string_view(), GetMedium()->GetBaseURL(true), xWrt);

                bool bLockedView(false);
                if (m_pWrtShell)
                {
                    bLockedView = m_pWrtShell->IsViewLocked();
                    m_pWrtShell->LockView( true );    //lock visible section
                }

                SwWriter aWrt( *GetMedium(), *m_xDoc );
                nErr = aWrt.Write( xWrt );

                if (m_pWrtShell)
                    m_pWrtShell->LockView( bLockedView );
            }
            break;
        }
        SW_MOD()->SetEmbeddedLoadSave( false );
    }
    SetError(nErr ? nErr : nVBWarning);

    SfxViewFrame *const pFrame =
        m_pWrtShell ? &m_pWrtShell->GetView().GetViewFrame() : nullptr;
    if( pFrame )
    {
        pFrame->GetBindings().SetState(SfxBoolItem(SID_DOC_MODIFIED, false));
    }
    return !nErr.IsError();
}

SwDocShell::LockAllViewsGuard_Impl::LockAllViewsGuard_Impl(SwViewShell* pViewShell)
{
    if (!pViewShell)
        return;
    for (SwViewShell& rShell : pViewShell->GetRingContainer())
    {
        if (!rShell.IsViewLocked())
        {
            m_aViewWasUnLocked.push_back(&rShell);
            rShell.LockView(true);
        }
    }
}

SwDocShell::LockAllViewsGuard_Impl::~LockAllViewsGuard_Impl()
{
    for (SwViewShell* pShell : m_aViewWasUnLocked)
        pShell->LockView(false);
}

std::unique_ptr<SfxObjectShell::LockAllViewsGuard> SwDocShell::LockAllViews()
{
    return std::make_unique<LockAllViewsGuard_Impl>(GetEditShell());
}

// Save using the Defaultformat
bool SwDocShell::SaveAs( SfxMedium& rMedium )
{
    SwWait aWait( *this, true );
    //#i3370# remove quick help to prevent saving of autocorrection suggestions
    if (m_pView)
        m_pView->GetEditWin().StopQuickHelp();

    //#i91811# mod if we have an active margin window, write back the text
    if (m_pView &&
        m_pView->GetPostItMgr() &&
        m_pView->GetPostItMgr()->HasActiveSidebarWin())
    {
        m_pView->GetPostItMgr()->UpdateDataOnActiveSidebarWin();
    }

    if (m_xDoc->getIDocumentSettingAccess().get(DocumentSettingId::GLOBAL_DOCUMENT) &&
        !m_xDoc->getIDocumentSettingAccess().get(DocumentSettingId::GLOBAL_DOCUMENT_SAVE_LINKS))
        RemoveOLEObjects();

    if (GetMedium())
    {
        // Task 75666 - is the Document imported by our Microsoft-Filters?
        std::shared_ptr<const SfxFilter> pOldFilter = GetMedium()->GetFilter();
        if( pOldFilter &&
            ( pOldFilter->GetUserData() == FILTER_WW8 ||
              pOldFilter->GetUserData() == "CWW6" ||
              pOldFilter->GetUserData() == "WW6" ) )
        {
            // when saving it in our own fileformat, then remove the template
            // name from the docinfo.
            uno::Reference<document::XDocumentPropertiesSupplier> xDPS(
                GetModel(), uno::UNO_QUERY_THROW);
            uno::Reference<document::XDocumentProperties> xDocProps
                = xDPS->getDocumentProperties();
            xDocProps->setTemplateName(OUString());
            xDocProps->setTemplateURL(OUString());
            xDocProps->setTemplateDate(::util::DateTime());
        }
    }

    CalcLayoutForOLEObjects();  // format for OLE objects

    const bool bURLChanged = GetMedium() && GetMedium()->GetURLObject() != rMedium.GetURLObject();
    const SwDBManager* const pMgr = m_xDoc->GetDBManager();
    const bool bHasEmbedded = pMgr && !pMgr->getEmbeddedName().isEmpty();
    bool bSaveDS = bHasEmbedded && bURLChanged;
    if (bSaveDS)
    {
        // Don't save data source in case a temporary is being saved for preview in MM wizard
        if (const SfxBoolItem* pNoEmbDS
            = rMedium.GetItemSet().GetItem(SID_NO_EMBEDDED_DS, false))
            bSaveDS = !pNoEmbDS->GetValue();
    }
    if (bSaveDS)
    {
        // We have an embedded data source definition, need to re-store it,
        // otherwise relative references will break when the new file is in a
        // different directory.

        OUString aURL(GetMedium()->GetURLObject().GetMainURL(INetURLObject::DecodeMechanism::NONE));
        if (aURL.isEmpty())
        {
            // No old URL - is this a new document created from a template with embedded DS?
            // Try to get the template URL to reconstruct the embedded data source URL
            const css::beans::PropertyValues& rArgs = GetMedium()->GetArgs();
            const auto aURLIter = std::find_if(rArgs.begin(), rArgs.end(),
                                               [](const auto& v) { return v.Name == "URL"; });
            if (aURLIter != rArgs.end())
                aURLIter->Value >>= aURL;
        }

        if (!aURL.isEmpty())
        {
            auto xContext(comphelper::getProcessComponentContext());
            auto xUri = css::uri::UriReferenceFactory::create(xContext)->parse(aURL);
            assert(xUri.is());
            xUri = css::uri::VndSunStarPkgUrlReferenceFactory::create(xContext)
                       ->createVndSunStarPkgUrlReference(xUri);
            assert(xUri.is());
            aURL = xUri->getUriReference() + "/"
                   + INetURLObject::encode(pMgr->getEmbeddedName(), INetURLObject::PART_FPATH,
                                           INetURLObject::EncodeMechanism::All);

            bool bCopyTo = GetCreateMode() == SfxObjectCreateMode::EMBEDDED;
            if (!bCopyTo)
            {
                if (const SfxBoolItem* pSaveToItem
                    = rMedium.GetItemSet().GetItem(SID_SAVETO, false))
                    bCopyTo = pSaveToItem->GetValue();
            }

            auto xDatabaseContext = sdb::DatabaseContext::create(xContext);
            uno::Reference<sdb::XDocumentDataSource> xDataSource(xDatabaseContext->getByName(aURL),
                                                                 uno::UNO_QUERY);
            if (xDataSource)
            {
                uno::Reference<frame::XStorable> xStorable(xDataSource->getDatabaseDocument(),
                                                           uno::UNO_QUERY);
                SwDBManager::StoreEmbeddedDataSource(xStorable, rMedium.GetOutputStorage(),
                                                     pMgr->getEmbeddedName(), rMedium.GetName(),
                                                     bCopyTo);
            }
        }
    }

    // #i62875#
    // reset compatibility flag <DoNotCaptureDrawObjsOnPage>, if possible
    if (m_pWrtShell &&
        m_xDoc->getIDocumentSettingAccess().get(DocumentSettingId::DO_NOT_CAPTURE_DRAW_OBJS_ON_PAGE) &&
        docfunc::AllDrawObjsOnPage(*m_xDoc))
    {
        m_xDoc->getIDocumentSettingAccess().set(DocumentSettingId::DO_NOT_CAPTURE_DRAW_OBJS_ON_PAGE, false);
    }

    ErrCodeMsg nErr = ERR_SWG_WRITE_ERROR;
    ErrCode nVBWarning = ERRCODE_NONE;
    uno::Reference < embed::XStorage > xStor = rMedium.GetOutputStorage();
    if( SfxObjectShell::SaveAs( rMedium ) )
    {
        if( GetDoc()->getIDocumentSettingAccess().get(DocumentSettingId::GLOBAL_DOCUMENT) && dynamic_cast< const SwGlobalDocShell *>( this ) ==  nullptr )
        {
            // The document is closed explicitly, but using SfxObjectShellLock is still more correct here
            SfxObjectShellLock xDocSh =
                new SwGlobalDocShell( SfxObjectCreateMode::INTERNAL );
            // the global document can not be a template
            xDocSh->SetupStorage( xStor, SotStorage::GetVersion( xStor ), false );
            xDocSh->DoClose();
        }

        if (m_xDoc->ContainsMSVBasic())
        {
            if( SvtFilterOptions::Get().IsLoadWordBasicStorage() )
                nVBWarning = GetSaveWarningOfMSVBAStorage( static_cast<SfxObjectShell&>(*this) );
            m_xDoc->SetContainsMSVBasic( false );
        }

        if (m_pWrtShell)
        {
            // End TableBox Edit!
            m_pWrtShell->EndAllTableBoxEdit();

            // Remove invalid signatures.
            m_pWrtShell->ValidateAllParagraphSignatures(false);

            m_pWrtShell->ClassifyDocPerHighestParagraphClass();
        }

        // Remember and preserve Modified-Flag without calling the Link
        // (for OLE; after Statement from MM)
        const bool bIsModified = m_xDoc->getIDocumentState().IsModified();
        m_xDoc->GetIDocumentUndoRedo().LockUndoNoModifiedPosition();
        Link<bool,void> aOldOLELnk( m_xDoc->GetOle2Link() );
        m_xDoc->SetOle2Link( Link<bool,void>() );

            // Suppress SfxProgress when we are Embedded
        SW_MOD()->SetEmbeddedLoadSave(
                            SfxObjectCreateMode::EMBEDDED == GetCreateMode() );

        WriterRef xWrt;
        ::GetXMLWriter(std::u16string_view(), rMedium.GetBaseURL(true), xWrt);

        bool bLockedView(false);
        if (m_pWrtShell)
        {
            bLockedView = m_pWrtShell->IsViewLocked();
            m_pWrtShell->LockView( true );    //lock visible section
        }

        SwWriter aWrt( rMedium, *m_xDoc );
        nErr = aWrt.Write( xWrt );

        if (m_pWrtShell)
            m_pWrtShell->LockView( bLockedView );

        if( bIsModified )
        {
            m_xDoc->getIDocumentState().SetModified();
            m_xDoc->GetIDocumentUndoRedo().UnLockUndoNoModifiedPosition();
        }
        m_xDoc->SetOle2Link( aOldOLELnk );

        SW_MOD()->SetEmbeddedLoadSave( false );

        // Increase RSID
        m_xDoc->setRsid( m_xDoc->getRsid() );

        m_xDoc->cleanupUnoCursorTable();
    }
    SetError(nErr ? nErr : nVBWarning);

    return !nErr.IsError();
}

// Save all Formats
static SwSrcView* lcl_GetSourceView( SwDocShell const * pSh )
{
    // are we in SourceView?
    SfxViewFrame* pVFrame = SfxViewFrame::GetFirst( pSh );
    SfxViewShell* pViewShell = pVFrame ? pVFrame->GetViewShell() : nullptr;
    return dynamic_cast<SwSrcView*>( pViewShell );
}

bool SwDocShell::ConvertTo( SfxMedium& rMedium )
{
    std::shared_ptr<const SfxFilter> pFlt = rMedium.GetFilter();
    if( !pFlt )
        return false;

    WriterRef xWriter;
    SwReaderWriter::GetWriter( pFlt->GetUserData(), rMedium.GetBaseURL( true ), xWriter );
    if( !xWriter.is() )
    {   // Filter not available
        std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(nullptr,
                                                      VclMessageType::Info, VclButtonsType::Ok,
                                                      SwResId(STR_DLLNOTFOUND)));
        xInfoBox->run();
        return false;
    }

    //#i3370# remove quick help to prevent saving of autocorrection suggestions
    if (m_pView)
        m_pView->GetEditWin().StopQuickHelp();

    //#i91811# mod if we have an active margin window, write back the text
    if (m_pView &&
        m_pView->GetPostItMgr() &&
        m_pView->GetPostItMgr()->HasActiveSidebarWin())
    {
        m_pView->GetPostItMgr()->UpdateDataOnActiveSidebarWin();
    }

    ErrCode nVBWarning = ERRCODE_NONE;

    if (m_xDoc->ContainsMSVBasic())
    {
        bool bSave = pFlt->GetUserData() == "CWW8"
             && SvtFilterOptions::Get().IsLoadWordBasicStorage();

        if ( bSave )
        {
            tools::SvRef<SotStorage> xStg = new SotStorage( rMedium.GetOutStream(), false );
            OSL_ENSURE( !xStg->GetError(), "No storage available for storing VBA macros!" );
            if ( !xStg->GetError() )
            {
                nVBWarning = SaveOrDelMSVBAStorage( static_cast<SfxObjectShell&>(*this), *xStg, bSave, "Macros" );
                xStg->Commit();
                m_xDoc->SetContainsMSVBasic( true );
            }
        }
    }

    // End TableBox Edit!
    if (m_pWrtShell)
        m_pWrtShell->EndAllTableBoxEdit();

    if( pFlt->GetUserData() == "HTML" )
    {
#if HAVE_FEATURE_SCRIPTING
        if( !officecfg::Office::Common::Filter::HTML::Export::Basic::get()
            && officecfg::Office::Common::Filter::HTML::Export::Warning::get()
            && HasBasic() )
        {
            uno::Reference< XLibraryContainer > xLibCont = GetBasicContainer();
            uno::Reference< XNameAccess > xLib;
            const Sequence<OUString> aNames = xLibCont->getElementNames();
            for(const OUString& rName : aNames)
            {
                Any aLib = xLibCont->getByName(rName);
                aLib >>= xLib;
                if(xLib.is())
                {
                    Sequence<OUString> aModNames = xLib->getElementNames();
                    if(aModNames.hasElements())
                    {
                        SetError(WARN_SWG_HTML_NO_MACROS);
                        break;
                    }
                }
            }
        }
#endif
    }

    // #i76360# Update document statistics
    if ( !rMedium.IsSkipImages() )
        m_xDoc->getIDocumentStatistics().UpdateDocStat( false, true );

    CalcLayoutForOLEObjects();  // format for OLE objects
    // #i62875#
    // reset compatibility flag <DoNotCaptureDrawObjsOnPage>, if possible
    if (m_pWrtShell &&
        m_xDoc->getIDocumentSettingAccess().get(DocumentSettingId::DO_NOT_CAPTURE_DRAW_OBJS_ON_PAGE) &&
        docfunc::AllDrawObjsOnPage(*m_xDoc))
    {
        m_xDoc->getIDocumentSettingAccess().set(DocumentSettingId::DO_NOT_CAPTURE_DRAW_OBJS_ON_PAGE, false);
    }

    if( xWriter->IsStgWriter() &&
        ( pFlt->GetUserData() == FILTER_XML ||
          pFlt->GetUserData() == FILTER_XMLV ||
          pFlt->GetUserData() == FILTER_XMLVW ) )
    {
        // determine the own Type
        sal_uInt8 nMyType = 0;
        if( dynamic_cast< const SwWebDocShell *>( this ) !=  nullptr )
            nMyType = 1;
        else if( dynamic_cast< const SwGlobalDocShell *>( this ) !=  nullptr )
            nMyType = 2;

        // determine the desired Type
        sal_uInt8 nSaveType = 0;
        SotClipboardFormatId nSaveClipId = pFlt->GetFormat();
        if( SotClipboardFormatId::STARWRITERWEB_8 == nSaveClipId ||
            SotClipboardFormatId::STARWRITERWEB_60 == nSaveClipId ||
            SotClipboardFormatId::STARWRITERWEB_50 == nSaveClipId ||
            SotClipboardFormatId::STARWRITERWEB_40 == nSaveClipId )
            nSaveType = 1;
        else if( SotClipboardFormatId::STARWRITERGLOB_8 == nSaveClipId ||
                 SotClipboardFormatId::STARWRITERGLOB_8_TEMPLATE == nSaveClipId ||
                 SotClipboardFormatId::STARWRITERGLOB_60 == nSaveClipId ||
                 SotClipboardFormatId::STARWRITERGLOB_50 == nSaveClipId ||
                 SotClipboardFormatId::STARWRITERGLOB_40 == nSaveClipId )
            nSaveType = 2;

        // Change Flags of the Document accordingly
        bool bIsHTMLModeSave = GetDoc()->getIDocumentSettingAccess().get(DocumentSettingId::HTML_MODE);
        bool bIsGlobalDocSave = GetDoc()->getIDocumentSettingAccess().get(DocumentSettingId::GLOBAL_DOCUMENT);
        bool bIsGlblDocSaveLinksSave = GetDoc()->getIDocumentSettingAccess().get(DocumentSettingId::GLOBAL_DOCUMENT_SAVE_LINKS);
        if( nMyType != nSaveType )
        {
            GetDoc()->getIDocumentSettingAccess().set(DocumentSettingId::HTML_MODE, 1 == nSaveType);
            GetDoc()->getIDocumentSettingAccess().set(DocumentSettingId::GLOBAL_DOCUMENT, 2 == nSaveType);
            if( 2 != nSaveType )
                GetDoc()->getIDocumentSettingAccess().set(DocumentSettingId::GLOBAL_DOCUMENT_SAVE_LINKS, false);
        }

        // if the target format is storage based, then the output storage must be already created
        if ( rMedium.IsStorage() )
        {
            // set MediaType on target storage
            // (MediaType will be queried during SaveAs)
            try
            {
                // TODO/MBA: testing
                uno::Reference < beans::XPropertySet > xSet( rMedium.GetStorage(), uno::UNO_QUERY );
                if ( xSet.is() )
                    xSet->setPropertyValue("MediaType", uno::Any( SotExchange::GetFormatMimeType( nSaveClipId ) ) );
            }
            catch (const uno::Exception&)
            {
            }
        }

        // Now normally save the Document
        bool bRet = SaveAs( rMedium );

        if( nMyType != nSaveType )
        {
            GetDoc()->getIDocumentSettingAccess().set(DocumentSettingId::HTML_MODE, bIsHTMLModeSave );
            GetDoc()->getIDocumentSettingAccess().set(DocumentSettingId::GLOBAL_DOCUMENT, bIsGlobalDocSave);
            GetDoc()->getIDocumentSettingAccess().set(DocumentSettingId::GLOBAL_DOCUMENT_SAVE_LINKS, bIsGlblDocSaveLinksSave);
        }

        return bRet;
    }

    if( pFlt->GetUserData() == FILTER_TEXT_DLG &&
        (m_pWrtShell || !::lcl_GetSourceView(this)))
    {
        SwAsciiOptions aOpt;
        OUString sItemOpt;
        if( const SfxStringItem* pItem = rMedium.GetItemSet().GetItemIfSet( SID_FILE_FILTEROPTIONS ) )
            sItemOpt = pItem->GetValue();
        if(!sItemOpt.isEmpty())
            aOpt.ReadUserData( sItemOpt );

        xWriter->SetAsciiOptions( aOpt );
    }

        // Suppress SfxProgress when we are Embedded
    SW_MOD()->SetEmbeddedLoadSave(
                            SfxObjectCreateMode::EMBEDDED == GetCreateMode());

    // Span Context in order to suppress the Selection's View
    ErrCodeMsg nErrno;
    const OUString aFileName( rMedium.GetName() );

    bool bSelection = false;
    if (m_pWrtShell)
    {
        const SfxBoolItem* pSelectionItem = rMedium.GetItemSet().GetItemIfSet(SID_SELECTION);
        bSelection = pSelectionItem && pSelectionItem->GetValue();
    }

    // No View, so the whole Document! (unless SID_SELECTION explicitly set)
    if (m_pWrtShell && (!Application::IsHeadlessModeEnabled() || bSelection))
    {

        SwWait aWait( *this, true );
        // #i106906#
        const bool bFormerLockView = m_pWrtShell->IsViewLocked();
        m_pWrtShell->LockView( true );
        m_pWrtShell->StartAllAction();
        m_pWrtShell->Push();
        SwWriter aWrt( rMedium, *m_pWrtShell, !bSelection );
        nErrno = aWrt.Write( xWriter, &aFileName );
        //JP 16.05.97: In case the SFX revokes the View while saving
        if (m_pWrtShell)
        {
            m_pWrtShell->Pop(SwCursorShell::PopMode::DeleteCurrent);
            m_pWrtShell->EndAllAction();
            // #i106906#
            m_pWrtShell->LockView( bFormerLockView );
        }
    }
    else
    {
        // are we in SourceView?
        SwSrcView* pSrcView = ::lcl_GetSourceView( this );
        if( pSrcView )
        {
            pSrcView->SaveContentTo(rMedium);
            nErrno = ERRCODE_NONE;
        }
        else
        {
            SwWriter aWrt( rMedium, *m_xDoc );
            nErrno = aWrt.Write( xWriter, &aFileName );
        }
    }

    SW_MOD()->SetEmbeddedLoadSave( false );
    SetError(nErrno ? nErrno : nVBWarning);
    if( !rMedium.IsStorage() )
        rMedium.CloseOutStream();

    return ! nErrno.IsError();
}

// Hands off
// do not yet activate, must deliver TRUE
bool SwDocShell::SaveCompleted( const uno::Reference < embed::XStorage >& xStor  )
{
    bool bRet = SfxObjectShell::SaveCompleted( xStor );
    if( bRet )
    {
        // Do not decide until here, whether Saving was successful or not
        if( IsModified() )
            m_xDoc->getIDocumentState().SetModified();
        else
            m_xDoc->getIDocumentState().ResetModified();
    }

    if (m_pOLEChildList)
    {
        bool bResetModified = IsEnableSetModified();
        if( bResetModified )
            EnableSetModified( false );

        uno::Sequence < OUString > aNames = m_pOLEChildList->GetObjectNames();
        for( sal_Int32 n = aNames.getLength(); n; n-- )
        {
            if (!m_pOLEChildList->MoveEmbeddedObject(aNames[n-1], GetEmbeddedObjectContainer()))
            {
                OSL_FAIL("Copying of objects didn't work!" );
            }
        }

        m_pOLEChildList.reset();
        if( bResetModified )
            EnableSetModified();
    }
    return bRet;
}

// Draw()-Override for OLE2 (Sfx)
void SwDocShell::Draw( OutputDevice* pDev, const JobSetup& rSetup,
                               sal_uInt16 nAspect, bool bOutputForScreen )
{
    //fix #25341# Draw should not affect the Modified
    bool bResetModified = IsEnableSetModified();
    if ( bResetModified )
        EnableSetModified( false );

    // When there is a JobSetup connected to the Document, we copy it to
    // reconnect it after PrtOle2. We don't use an empty JobSetup because
    // that would only lead to questionable results after expensive
    // reformatting (Preview!)
    std::unique_ptr<JobSetup> pOrig;
    if ( !rSetup.GetPrinterName().isEmpty() && ASPECT_THUMBNAIL != nAspect )
    {
        const JobSetup* pCurrentJobSetup = m_xDoc->getIDocumentDeviceAccess().getJobsetup();
        if( pCurrentJobSetup )         // then we copy that
            pOrig.reset(new JobSetup( *pCurrentJobSetup ));
        m_xDoc->getIDocumentDeviceAccess().setJobsetup( rSetup );
    }

    tools::Rectangle aRect( nAspect == ASPECT_THUMBNAIL ?
            GetVisArea( nAspect ) : GetVisArea( ASPECT_CONTENT ) );

    pDev->Push();
    pDev->SetFillColor();
    pDev->SetLineColor();
    pDev->SetBackground();
    const bool bWeb = dynamic_cast< const SwWebDocShell *>( this ) !=  nullptr;
    SwPrintData aOpts;
    SwViewShell::PrtOle2(m_xDoc.get(), SW_MOD()->GetUsrPref(bWeb), aOpts, *pDev, aRect, bOutputForScreen);
    pDev->Pop();

    if( pOrig )
    {
        m_xDoc->getIDocumentDeviceAccess().setJobsetup( *pOrig );
    }
    if ( bResetModified )
        EnableSetModified();
}

void SwDocShell::SetVisArea( const tools::Rectangle &rRect )
{
    tools::Rectangle aRect( rRect );
    if (m_pView)
    {
        Size aSz( m_pView->GetDocSz() );
        aSz.AdjustWidth(DOCUMENTBORDER ); aSz.AdjustHeight(DOCUMENTBORDER );
        tools::Long nMoveX = 0, nMoveY = 0;
        if ( aRect.Right() > aSz.Width() )
            nMoveX = aSz.Width() - aRect.Right();
        if ( aRect.Bottom() > aSz.Height() )
            nMoveY = aSz.Height() - aRect.Bottom();
        aRect.Move( nMoveX, nMoveY );
        nMoveX = aRect.Left() < 0 ? -aRect.Left() : 0;
        nMoveY = aRect.Top()  < 0 ? -aRect.Top()  : 0;
        aRect.Move( nMoveX, nMoveY );

        // Calls SfxInPlaceObject::SetVisArea()!
        m_pView->SetVisArea( aRect );
    }
    else
        SfxObjectShell::SetVisArea( aRect );
}

tools::Rectangle SwDocShell::GetVisArea( sal_uInt16 nAspect ) const
{
    if ( nAspect == ASPECT_THUMBNAIL )
    {
        // Preview: set VisArea to the first page.
        SwNodeIndex aIdx( m_xDoc->GetNodes().GetEndOfExtras(), 1 );
        SwContentNode* pNd = m_xDoc->GetNodes().GoNext( &aIdx );

        const SwRect aPageRect = pNd->FindPageFrameRect();
        if (aPageRect.IsEmpty())
            return tools::Rectangle();
        tools::Rectangle aRect(aPageRect.SVRect());

        // tdf#81219 sanitize - nobody is interested in a thumbnail where's
        // nothing visible
        if (aRect.GetHeight() > 2*aRect.GetWidth())
            aRect.SetSize(Size(aRect.GetWidth(), 2*aRect.GetWidth()));
        else if (aRect.GetWidth() > 2*aRect.GetHeight())
            aRect.SetSize(Size(2*aRect.GetHeight(), aRect.GetHeight()));

        return aRect;
    }
    return SfxObjectShell::GetVisArea( nAspect );
}

Printer *SwDocShell::GetDocumentPrinter()
{
    return m_xDoc->getIDocumentDeviceAccess().getPrinter( false );
}

OutputDevice* SwDocShell::GetDocumentRefDev()
{
    return m_xDoc->getIDocumentDeviceAccess().getReferenceDevice( false );
}

void SwDocShell::OnDocumentPrinterChanged( Printer * pNewPrinter )
{
    if ( pNewPrinter )
        GetDoc()->getIDocumentDeviceAccess().setJobsetup( pNewPrinter->GetJobSetup() );
    else
        GetDoc()->getIDocumentDeviceAccess().setPrinter( nullptr, true, true );
}

// #i20883# Digital Signatures and Encryption
HiddenInformation SwDocShell::GetHiddenInformationState( HiddenInformation nStates )
{
    // get global state like HiddenInformation::DOCUMENTVERSIONS
    HiddenInformation nState = SfxObjectShell::GetHiddenInformationState( nStates );

    if ( nStates & HiddenInformation::RECORDEDCHANGES )
    {
        if ( !GetDoc()->getIDocumentRedlineAccess().GetRedlineTable().empty() )
            nState |= HiddenInformation::RECORDEDCHANGES;
    }
    if ( nStates & HiddenInformation::NOTES )
    {
        OSL_ENSURE( GetWrtShell(), "No SwWrtShell, no information" );
        if(GetWrtShell() && GetWrtShell()->GetFieldType(SwFieldIds::Postit, OUString())->HasHiddenInformationNotes())
                nState |= HiddenInformation::NOTES;
    }

    return nState;
}

void SwDocShell::GetState(SfxItemSet& rSet)
{
    SfxWhichIter aIter(rSet);
    sal_uInt16  nWhich  = aIter.FirstWhich();

    while (nWhich)
    {
        switch (nWhich)
        {
        case SID_PRINTPREVIEW:
        {
            bool bDisable = IsInPlaceActive();
            // Disable "multiple layout"
            if ( !bDisable )
            {
                SfxViewFrame *pTmpFrame = SfxViewFrame::GetFirst(this);
                while (pTmpFrame)     // Look for Preview
                {
                    if ( auto pSwView = dynamic_cast<SwView*>( pTmpFrame->GetViewShell() ) )
                        if (pSwView->GetWrtShell().GetViewOptions()->getBrowseMode())
                        {
                            bDisable = true;
                            break;
                        }
                    pTmpFrame = SfxViewFrame::GetNext(*pTmpFrame, this);
                }
            }
            // End of disabled "multiple layout"
            if ( bDisable )
                rSet.DisableItem( SID_PRINTPREVIEW );
            else
            {
                SfxBoolItem aBool( SID_PRINTPREVIEW, false );
                if( dynamic_cast<SwPagePreview*>( SfxViewShell::Current())  )
                    aBool.SetValue( true );
                rSet.Put( aBool );
            }
        }
        break;
        case SID_AUTO_CORRECT_DLG:
            if ( comphelper::LibreOfficeKit::isActive() )
                rSet.DisableItem( SID_AUTO_CORRECT_DLG );
        break;
        case SID_SOURCEVIEW:
        {
            SfxViewShell* pCurrView = GetView() ? static_cast<SfxViewShell*>(GetView())
                                        : SfxViewShell::Current();
            bool bSourceView = dynamic_cast<SwSrcView*>( pCurrView ) !=  nullptr;
            rSet.Put(SfxBoolItem(SID_SOURCEVIEW, bSourceView));
        }
        break;
        case SID_HTML_MODE:
            rSet.Put(SfxUInt16Item(SID_HTML_MODE, ::GetHtmlMode(this)));
        break;

        case FN_ABSTRACT_STARIMPRESS:
        case FN_OUTLINE_TO_IMPRESS:
            {
                SvtModuleOptions aMOpt;
                if (!aMOpt.IsImpress() || GetObjectShell()->isExportLocked())
                    rSet.DisableItem( nWhich );
            }
            [[fallthrough]];
        case FN_ABSTRACT_NEWDOC:
        case FN_OUTLINE_TO_CLIPBOARD:
            {
                if ( GetDoc()->GetNodes().GetOutLineNds().empty() )
                    rSet.DisableItem( nWhich );
            }
            break;
        case SID_BROWSER_MODE:
        case FN_PRINT_LAYOUT:
            {
                bool bState = GetDoc()->getIDocumentSettingAccess().get(DocumentSettingId::BROWSE_MODE);
                if(FN_PRINT_LAYOUT == nWhich)
                    bState = !bState;
                rSet.Put( SfxBoolItem( nWhich, bState));
            }
            break;

        case FN_NEW_GLOBAL_DOC:
            if (dynamic_cast<const SwGlobalDocShell*>(this) != nullptr
                || GetObjectShell()->isExportLocked())
                rSet.DisableItem( nWhich );
            break;

        case FN_NEW_HTML_DOC:
            if (dynamic_cast<const SwWebDocShell*>(this) != nullptr
                || GetObjectShell()->isExportLocked())
                rSet.DisableItem( nWhich );
            break;

        case FN_OPEN_FILE:
            if( dynamic_cast< const SwWebDocShell *>( this ) !=  nullptr )
                rSet.DisableItem( nWhich );
            break;

        case SID_ATTR_YEAR2000:
            {
                const SvNumberFormatter* pFormatr = m_xDoc->GetNumberFormatter(false);
                rSet.Put( SfxUInt16Item( nWhich,
                        static_cast< sal_uInt16 >(
                        pFormatr ? pFormatr->GetYear2000()
                              : officecfg::Office::Common::DateFormat::TwoDigitYear::get())) );
            }
            break;
        case SID_ATTR_CHAR_FONTLIST:
        {
            rSet.Put( SvxFontListItem(m_pFontList.get(), SID_ATTR_CHAR_FONTLIST) );
        }
        break;
        case SID_MAIL_PREPAREEXPORT:
        {
            //check if linked content or possibly hidden content is available
            //m_xDoc->UpdateFields( NULL, false );
            sfx2::LinkManager& rLnkMgr = m_xDoc->getIDocumentLinksAdministration().GetLinkManager();
            const ::sfx2::SvBaseLinks& rLnks = rLnkMgr.GetLinks();
            bool bRet = false;
            if( !rLnks.empty() )
                bRet = true;
            else
            {
                //sections with hidden flag, hidden character attribute, hidden paragraph/text or conditional text fields
                bRet = m_xDoc->HasInvisibleContent();
            }
            rSet.Put( SfxBoolItem( nWhich, bRet ) );
        }
        break;
        case SID_NOTEBOOKBAR:
        {
            SfxViewShell* pViewShell = GetView()? GetView(): SfxViewShell::Current();
            bool bVisible = sfx2::SfxNotebookBar::StateMethod(pViewShell->GetViewFrame().GetBindings(),
                                                              u"modules/swriter/ui/");
            rSet.Put( SfxBoolItem( SID_NOTEBOOKBAR, bVisible ) );
        }
        break;
        case FN_REDLINE_ACCEPT_ALL:
        case FN_REDLINE_REJECT_ALL:
        {
            if (GetDoc()->getIDocumentRedlineAccess().GetRedlineTable().empty() ||
                HasChangeRecordProtection()) // tdf#128229 Disable Accept / Reject all if redlines are password protected
                rSet.DisableItem(nWhich);
        }
        break;

        default: OSL_ENSURE(false,"You cannot get here!");

        }
        nWhich = aIter.NextWhich();
    }
}

// OLE-Hdls
IMPL_LINK( SwDocShell, Ole2ModifiedHdl, bool, bNewStatus, void )
{
    if (m_pWrtShell)
    {
        SwOLENode* pOLENode = nullptr;
        if (!m_pWrtShell->IsTableMode())
        {
            pOLENode = m_pWrtShell->GetCursor()->GetPointNode().GetOLENode();
        }
        if (pOLENode)
        {
            if (pOLENode->GetOLEObj().IsProtected())
            {
                return;
            }
        }
    }

    if( IsEnableSetModified() )
        SetModified( bNewStatus );
}

// return Pool here, because virtual
SfxStyleSheetBasePool*  SwDocShell::GetStyleSheetPool()
{
    return m_xBasePool.get();
}

sfx2::StyleManager* SwDocShell::GetStyleManager()
{
    return m_pStyleManager.get();
}

void SwDocShell::SetView(SwView* pVw)
{
    SetViewShell_Impl(pVw);
    m_pView = pVw;
    if (m_pView)
    {
        m_pWrtShell = &m_pView->GetWrtShell();

        // Set view-specific redline author.
        const OUString& rRedlineAuthor = m_pView->GetRedlineAuthor();
        if (!rRedlineAuthor.isEmpty())
            SW_MOD()->SetRedlineAuthor(m_pView->GetRedlineAuthor());
    }
    else
        m_pWrtShell = nullptr;
}

template<typename T> T getFormulaProperty(const Reference< XComponent >& xFormulaComp, const OUString& propertyName)
{
    if ( xFormulaComp.is() )
    {
        Reference< XModel > xFormulaModel = extractModel(xFormulaComp);
        if ( xFormulaModel.is() )
        {
            uno::Reference < beans::XPropertySet > xFormulaProps( xFormulaModel, uno::UNO_QUERY );
            if ( xFormulaProps.is() )
            {
                uno::Any aText = xFormulaProps->getPropertyValue(propertyName);

                T result = T();
                aText >>= result;
                return result;
            }
        }
    }

    return T();
}

void setFormulaProperty(const Reference< XComponent >& xFormulaComp, const OUString& propertyName, const uno::Any& value)
{
    if ( xFormulaComp.is() )
    {
        Reference< XModel > xFormulaModel = extractModel(xFormulaComp);
        if ( xFormulaModel.is() )
        {
            uno::Reference < beans::XPropertySet > xFormulaProps( xFormulaModel, uno::UNO_QUERY );
            if ( xFormulaProps.is() )
            {
                xFormulaProps->setPropertyValue(propertyName, value);
            }
        }
    }
}

void updateFormatting(const Reference< XComponent >& xFormulaComp)
{
    if (getFormulaProperty<OUString>(xFormulaComp, "iFormulaPendingAction") == "checktextmode") // TODO There must be a better way to communicate with the starmath object
    {
        Reference< XTextContent > xTextContent(xFormulaComp, UNO_QUERY);
        if (xTextContent.is())
        {
            bool textExistsInParagraph = false;
            Reference< XText > xDocumentText = xTextContent->getAnchor()->getText();
            Reference< text::XParagraphCursor > xCursor(xDocumentText->createTextCursorByRange(xTextContent->getAnchor()->getEnd()), UNO_QUERY_THROW);

            // Look for non-whitespace to the left and right of the anchor point, inside the paragraph where the anchor point is
            xCursor->gotoStartOfParagraph(true);
            if (xCursor->getString().trim().getLength() > 0)
            {
                textExistsInParagraph = true; // Text exists before the formula
            }
            else
            {
                xCursor->gotoEndOfParagraph (true);
                textExistsInParagraph = (xCursor->getString().trim().getLength() > 0);
            }

            if (textExistsInParagraph)
                setFormulaProperty(xFormulaComp, "IsTextMode", uno::makeAny(textExistsInParagraph));
            SAL_INFO_LEVEL(2, "sw.imath", "Set text mode to " << (getFormulaProperty<bool>(xFormulaComp, "IsTextMode") ? "true" : "false"));
        }
    }

    // TODO Should happen in textsh.cxx at insertion of a new iFormula object, but has no effect there
    Reference< XTextContent > xTextContent(xFormulaComp, UNO_QUERY);
    if (xTextContent.is())
    {
        Reference < XPropertySet > xPropertySet (xTextContent, UNO_QUERY);
        if (xPropertySet.is())
        {
            xPropertySet->setPropertyValue(OU("LeftMargin"), uno::makeAny(sal_Int16(0)));
            xPropertySet->setPropertyValue(OU("RightMargin"), uno::makeAny(sal_Int16(0)));
        }
    }
}

void SwDocShell::CheckIFormulaNumber(const Reference< XComponent > xFormulaComp)
{
    OUString formula = getFormulaProperty<OUString>(xFormulaComp, "iFormula");
    auto bpos = formula.indexOf("@");
    if (bpos < 0) return;

    auto epos = formula.indexOf("@", bpos + 1);
    if (epos < 0) return;

    unsigned label = formula.copy(bpos + 1, epos).toInt32() + 1;
    if (label > m_nextIFormulaNumber)
        m_nextIFormulaNumber = label;

    SAL_INFO_LEVEL(2, "sw.imath", "Set next iFormula number to " << label);
}

void SwDocShell::UpdatePreviousIFormulaLinks()
{
    SAL_INFO_LEVEL(1, "sw.imath", "SwDocShell::UpdatePreviousIFormulaLinks()");

    // Note: Unfortunately, this does not provide the objects in textual order
    // std::unique_ptr<SwOLENodes> pNodes = SwContentNode::CreateOLENodesArray( *GetDoc()->GetDfltGrfFormatColl(), false );
    // Note: (*pNodes)[i]->GetOLEObj().GetCurrentPersistName() returns a different name (the difference occurs when the user edits the object name through the GUI)
    auto pDoc = comphelper::getFromUnoTunnel<SwXTextDocument>(GetModel());
    Reference< text::XText > xText(pDoc->getText(), UNO_QUERY);
    if (xText.is())
    {
        unsigned count = 0;
        // TODO: Implement progress bar with
        // ::StartProgress( STR_STATSTR_SWGPRTOLENOTIFY, 0, m_IFormulaNames.size(), this);
        // ::SetProgressState( count, this );
        // ::EndProgress( this );
        // Note: Requires #include <mdiexp.hxx>
        m_IFormulaNames.clear();
        Reference< task::XStatusIndicator > xStatusIndicator;
        orderXText(xText, m_IFormulaNames, count, xStatusIndicator);
        OUString previousFormulaName = "";

        for (const auto& fn : m_IFormulaNames)
        {
            SAL_INFO_LEVEL(1, "sw.imath", "Updating formula '" << fn << "', previous formula is '" << previousFormulaName << "'");
            Reference< XComponent > xFormulaComp = getObjectByName(GetModel(), fn);

            setFormulaProperty(xFormulaComp, "PreviousIFormula", uno::makeAny(previousFormulaName));

            // Note: Empty iFormulas are included in the chain of previous equations, because when a new iFormula is inserted it starts off as an empty formula
            // The links are updated first, then the formula text is set and compiled. See textsh.cxx FN_IMATH_INSERT_CREATE etc.
            previousFormulaName = fn;
        }
    }
}

// #i59688#
// linked graphics are now loaded on demand.
// Thus, loading of linked graphics no longer needed and necessary for
// the load of document being finished.
void SwDocShell::LoadingFinished()
{
    SAL_INFO_LEVEL(1, "sw.imath", "SwDocShell::LoadingFinished()");

    // Load master document if defined
    // TODO See sw/source/core/doc/rdfhelper.cxx
    Reference<XComponentContext> xContext(comphelper::getProcessComponentContext());
    Reference<XNamedGraph> xGraph = getGraph(xContext, GetModel());

    if (xGraph.is() && hasStatement(xContext, GetModel(), xGraph, OU("masterdocument")))
    {
        OUString masterDocURL = getStatementString(xContext, GetModel(), xGraph, OU("masterdocument"));

        if (masterDocURL.getLength() > 0)
        {
            Reference<XStorable> xDocumentStorable(GetModel(), UNO_QUERY_THROW);
            OUString documentURL = xDocumentStorable->getLocation();
            masterDocURL = makeURLFor(masterDocURL, documentURL, xContext); // Handle relative URL. TODO Use GetModel()->getArgs() PropertyValue 'DocumentBaseURL' ?
            SAL_INFO_LEVEL(1, "sw.imath", "Master document URL is '" << masterDocURL << "'");
            // TODO Show some kind of progress message to the user

            // Check if the user has already opened the master document
            Reference< XDesktop > xDesktop(xContext->getServiceManager()->createInstanceWithContext("com.sun.star.frame.Desktop", xContext), UNO_QUERY_THROW);
            Reference< XEnumerationAccess > xLoadedDocsEnumAccess = xDesktop->getComponents();
            Reference< XEnumeration > xDocsEnum = xLoadedDocsEnumAccess->createEnumeration();

            while (xDocsEnum->hasMoreElements()) {
                Any docModel = xDocsEnum->nextElement();
                docModel >>= m_xMasterDocument;
                xDocumentStorable = Reference<XStorable>(m_xMasterDocument, UNO_QUERY);
                if (!xDocumentStorable.is())
                    continue;

                SAL_INFO_LEVEL(1, "sw.imath", "Checking for master document: '" << xDocumentStorable->getLocation() << "'");
                if (m_xMasterDocument.is() && (xDocumentStorable->getLocation() == masterDocURL))
                {
                    m_masterDocumentWasLoaded = true;
                    SAL_INFO_LEVEL(1, "sw.imath", "Found master document");
                    break;
                }
            }

            if (!m_xMasterDocument.is())
            {
                try
                {
                    Sequence< PropertyValue > args(2);
                    auto pArgs = args.getArray();
                    PropertyValue hidden;
                    hidden.Name = "Hidden";
                    hidden.Value = makeAny(true);
                    pArgs[0] = hidden;
                    PropertyValue ro;
                    ro.Name = "ReadOnly";
                    ro.Value = makeAny(true);
                    pArgs[1] = ro;

                    Reference< XComponentLoader > xComponentLoader(xDesktop, UNO_QUERY_THROW);
                    m_xMasterDocument = Reference< XModel >(xComponentLoader->loadComponentFromURL(masterDocURL, "_default", 0, args), UNO_QUERY);
                    m_masterDocumentWasLoaded = false;
                    SAL_INFO_LEVEL(1, "sw.imath", "Loaded master document '" << masterDocURL << "'");
                }
                catch (Exception&) { }
            }

            if (!m_xMasterDocument.is())
            {
                SAL_WARN_LEVEL(1, "sw.imath", "Failed to load master document '" << masterDocURL << "'. Continuing without");
                // TODO: Inform the user with a message dialog
            }

            // Note: If the master document is outdated, it will be updated automatically, even if it is loaded read-only. Of course the changes will be lost when the document is closed
        }
    }

    // Update iFormulas to avoid problems if document was edited with non-iMath Office
    UpdatePreviousIFormulaLinks();

    // Trigger the necessary initial Compile() of the formulas
    // TODO Implement progress bar
    for (const auto& fn : m_IFormulaNames)
    {
        SAL_INFO_LEVEL(1, "sw.imath", "Compiling formula '" << fn << "'");
        Reference< XComponent > xFormulaComp = getObjectByName(GetModel(), fn);

        // Set previous iFormula from the master document for the first formula in this document
        if (fn == *m_IFormulaNames.begin() && m_xMasterDocument.is())
        {
            SwXTextDocument* pMasterDocument = comphelper::getFromUnoTunnel<SwXTextDocument>(m_xMasterDocument);
            Reference<XStorable> xStorable(m_xMasterDocument, UNO_QUERY);

            if (pMasterDocument != nullptr && xStorable.is())
            {
                setFormulaProperty(xFormulaComp, "iFormulaMasterDocument", uno::makeAny(xStorable->getLocation()));
                SwDocShell* pMasterDocumentShell = static_cast<SwDocShell*>(pMasterDocument->GetObjectShell());

                if (!pMasterDocumentShell->m_IFormulaNames.empty())
                {
                    setFormulaProperty(xFormulaComp, "PreviousIFormula", uno::makeAny(pMasterDocumentShell->m_IFormulaNames.back()));
                    SAL_INFO_LEVEL(2, "sw.imath", "Set previous formula '" << pMasterDocumentShell->m_IFormulaNames.back() << "' in master document '" << xStorable->getLocation() << "'");
                }
            }
        }

        setFormulaProperty(xFormulaComp, "iFormulaPendingAction", uno::makeAny(OUString("compile")));
        // TODO: Do we need to give time for the compilation?
        // TODO: If the update leads to a changed formula size, then the formula will appear distorted because the frame does not adjust automatically

        CheckIFormulaNumber(xFormulaComp);
        updateFormatting(xFormulaComp);
    }

    // #i38810#
    // Original fix fails after integration of cws xmlsec11:
    // interface <SfxObjectShell::EnableSetModified(..)> no longer works, because
    // <SfxObjectShell::FinishedLoading(..)> doesn't care about its status and
    // enables the document modification again.
    // Thus, manual modify the document, if it's modified and its links are updated
    // before <FinishedLoading(..)> is called.
    const bool bHasDocToStayModified( m_xDoc->getIDocumentState().IsModified() && m_xDoc->getIDocumentLinksAdministration().LinksUpdated() );

    FinishedLoading();
    SfxViewFrame* pVFrame = SfxViewFrame::GetFirst(this);
    if(pVFrame)
    {
        SfxViewShell* pShell = pVFrame->GetViewShell();
        if(auto pSrcView = dynamic_cast<SwSrcView*>( pShell) )
            pSrcView->Load(this);
    }

    // #i38810#
    if ( bHasDocToStayModified && !m_xDoc->getIDocumentState().IsModified() )
    {
        m_xDoc->getIDocumentState().SetModified();
    }
}

void SwDocShell::RecalculateDependentIFormulas(const OUString& formulaName, const OUString& useDependencies)
{
    if (formulaName.getLength() == 0)
    {
        // Forced recalculation of all formulas
        OUString previousFormulaName = "";

        for (const auto& fName : m_IFormulaNames)
        {
            Reference< XComponent > xFormulaComp = getObjectByName(GetModel(), fName);
            setFormulaProperty(xFormulaComp, "PreviousIFormula", uno::makeAny(previousFormulaName));
            previousFormulaName = fName;

            if (getFormulaProperty<OUString>(xFormulaComp, "iFormula").getLength() > 0)
            {
                SAL_INFO_LEVEL(1, "sw.imath", "Triggering compile on " << fName);
                setFormulaProperty(xFormulaComp, "iFormulaPendingAction", uno::makeAny(OUString("compile")));
                CheckIFormulaNumber(xFormulaComp);
                updateFormatting(xFormulaComp); // Update formula properties autotextmode, margin
            }
        }

        return;
    }

    Reference< XComponent > xFormulaComp = getObjectByName(GetModel(), formulaName);

    // Extract required formula properties
    OUString formulaText = getFormulaProperty<OUString>(xFormulaComp, "iFormula");

    if (formulaText.getLength() == 0)
    {
        // Note: This warning is triggered also when a formula object has been deleted
        SAL_WARN_LEVEL(1, "sw.imath", "RecalculateDependentIFormulas() could not read the iFormula properties or iFormula text is empty");
        return;
    }

    if (useDependencies.getLength() == 0)
        SAL_INFO_LEVEL(1, "sw.imath", "Recalculating formulas that depend on '" << formulaName << "'");
    else
        SAL_INFO_LEVEL(1, "sw.imath", "Recalculating formulas from '" << formulaName << "' that depend on '" << useDependencies << "'");

    auto it = std::find(m_IFormulaNames.begin(), m_IFormulaNames.end(), formulaName);
    if (it == m_IFormulaNames.end())
    {
        // New iFormula, probably inserted by Copy+Paste operation, this case is not caught by SwOleShell::SwOleShell because the XComponent does not appear to exist (yet)
        SAL_INFO_LEVEL(1, "sw.imath", "Formula is not contained in list, updating list");
        UpdatePreviousIFormulaLinks();
        it = std::find(m_IFormulaNames.begin(), m_IFormulaNames.end(), formulaName);
        if (it == m_IFormulaNames.end())
        {
            SAL_WARN("sw.imath", "Error, new formula object was not inserted into list of iFormula names");
            return;
        }
    }
    else
    {
        /*
         * This check is disabled for now because dependency tracking is still very imperfect
        // Check if any formulas depend on this formula
        OUString modifiedSymbols = (useDependencies.getLength() == 0)
            ? getFormulaProperty<OUString>(xFormulaComp, "iFormulaDependencyOut")
            : useDependencies;

        if (modifiedSymbols.getLength() == 0)
        {
            SAL_INFO_LEVEL(1, "sw.imath", "No symbols are modified, recalculation is not required");
            return;
        }
        */
    }

    updateFormatting(xFormulaComp); // Update formula properties autotextmode, margin
    OUString previousFormulaName = *it;
    ++it; // Skip this formula, it was compiled already (at text change or previous iFormula link change)

    while (it != m_IFormulaNames.end())
    {
        xFormulaComp = getObjectByName(GetModel(), *it);
        // Update previous iFormula property to catch the case where an empty Math object is inserted and later edited on the iFormula tab
        setFormulaProperty(xFormulaComp, "PreviousIFormula", uno::makeAny(previousFormulaName));
        previousFormulaName = *it;

        if (getFormulaProperty<OUString>(xFormulaComp, "iFormula").getLength() > 0)
        {
            SAL_INFO_LEVEL(1, "sw.imath", "Triggering compile on " << *it);
            setFormulaProperty(xFormulaComp, "iFormulaPendingAction", uno::makeAny(OUString("compile")));
            CheckIFormulaNumber(xFormulaComp);
            updateFormatting(xFormulaComp); // Update formula properties autotextmode, margin
        }

        /*
         * TODO: This does not work yet, because there is a linear chain of mpInitialCompiler/mpCurrentCompiler in starmath objects, so we cannot skip any of them
         * If this is implemented, the dependency string "all formulas" for statements must be handled
        // Add modified symbols of previous formula
        sal_Int32 idx = 0;
        do
        {
            OUString token = modifiedSymbols.getToken(0, ',', idx);
            if (token.getLength() > 0)
                symbolSet.insert(token);
        }
        while (idx >= 0);

        // Check this formula
        xFormulaComp = getObjectByName(GetModel(), *it);
        OUString dependencies = getFormulaProperty(xFormulaComp, "iFormulaDependencyIn");
        for (const auto& s: symbolSet)
        {
            if (dependencies.indexOf(s) >= 0)
            {
                SAL_INFO_LEVEL(1, "sw.imath", "Recalculating " << *it << " because it depends on " << s);
                setFormulaProperty(xFormulaComp, "iFormula", getFormulaProperty(xFormulaComp, "iFormula") + " ");
                break;
            }
        }

        // Prepare for next iteration
        modifiedSymbols = getFormulaProperty(xFormulaComp, "iFormulaDependencyOut");
        */

        ++it;
    }


    return;
}

void SwDocShell::RemoveIFormula(const OUString& formulaName) {
    SAL_INFO_LEVEL(1, "sw.imath", "SwDocShell::RemoveIFormula '" << formulaName << "'");
    auto formulaIterator = std::find(m_IFormulaNames.begin(), m_IFormulaNames.end(), formulaName);
    if (formulaIterator == m_IFormulaNames.end()) return; // See SwUndoFlyBase::DelFly() why this can happen

    SAL_INFO_LEVEL(1, "sw.imath", "Removing iFormula " << formulaName);
    Reference< XComponent > xFormulaComp = getObjectByName(GetModel(), *formulaIterator);
    OUString previousName = getFormulaProperty<OUString>(xFormulaComp, "PreviousIFormula");
    OUString removedDependencies = getFormulaProperty<OUString>(xFormulaComp, "iFormulaDependencyOut");
    setFormulaProperty(xFormulaComp, "iFormulaPendingAction", uno::makeAny(OUString("delete"))); // This will remove the IFormulaClosePreventer instance, after this xFormulaComp may become invalid at any time!

    std::list< OUString >::iterator next_it = m_IFormulaNames.end();

    while (formulaIterator != m_IFormulaNames.end()) {
        // Erase all occurrences of the name (there might be more than one because of UPDATE keyword usage)
        next_it = m_IFormulaNames.erase(formulaIterator);
        formulaIterator = std::find(next_it, m_IFormulaNames.end(), formulaName);
    }

    if (next_it != m_IFormulaNames.end()) {
        xFormulaComp = getObjectByName(GetModel(), *next_it);
        setFormulaProperty(xFormulaComp, "PreviousIFormula", uno::makeAny(previousName));
        SAL_INFO_LEVEL(1, "sw.imath", "Updating previous formula of " << *next_it << " to '" << previousName << "'");
        setFormulaProperty(xFormulaComp, "iFormulaPendingAction", uno::makeAny(OUString("compile"))); // Trigger compile
        RecalculateDependentIFormulas(*next_it, removedDependencies);
    }
}

void SwDocShell::MergeIFormula(const OUString& formulaName)
{
    SAL_INFO_LEVEL(1, "sw.imath", "SwDocShell::MergeIFormula '" << formulaName << "'");

    // Find formula
    Reference< XComponent > xFormulaComp = getObjectByName(GetModel(), formulaName);
    if (!xFormulaComp.is()) return;

    // Find previous formula
    OUString previousFormulaName = getFormulaProperty<OUString>(xFormulaComp, "PreviousIFormula");
    if (previousFormulaName.getLength() == 0) return;
    Reference< XComponent > xPreviousFormulaComp = getObjectByName(GetModel(), previousFormulaName);
    if (!xPreviousFormulaComp.is()) return;

    // Save formula's properties
    OUString formulaText = getFormulaProperty<OUString>(xFormulaComp, "iFormula");

    // Find text to interject between the two formulas
    OUString interText = "\nTEXT newline\n";

    if (!getFormulaProperty<bool>(xPreviousFormulaComp, "ImIsHidden"))
    {
        OUString prevLast = getFormulaProperty<OUString>(xPreviousFormulaComp, "ImTypeLastLine");
        OUString thisFirst = getFormulaProperty<OUString>(xFormulaComp, "ImTypeFirstLine");
        SAL_INFO_LEVEL(2, "sw.imath", "Found " << prevLast << " followed by " << thisFirst);

        if (prevLast == "equation" && thisFirst == "expression")
        {
            interText = OU("\nTEXT =\n");
        }
        else if (prevLast == "equation" && thisFirst == "equation")
        {
            if (getFormulaProperty<OUString>(xPreviousFormulaComp, "ImExpressionLastLhs") == getFormulaProperty<OUString>(xPreviousFormulaComp, "ImExpressionFirstLhs"))
            {
                // Check intermediate text
                interText = getInterText(Reference< XTextContent >(xPreviousFormulaComp, UNO_QUERY_THROW), Reference< XTextContent >(xFormulaComp, UNO_QUERY_THROW));

                if (interText.indexOfAsciiL("\n", 1) < 0 && interText.trim().getLength() == 0)
                    interText = OU("\n");
            }
        }
        else if (prevLast == "expression" && thisFirst == "expression")
        {
            interText = getInterText(Reference< XTextContent >(xPreviousFormulaComp, UNO_QUERY_THROW), Reference< XTextContent >(xFormulaComp, UNO_QUERY_THROW));

            if (interText.indexOfAsciiL("\n", 1) < 0 && interText.trim().getLength() == 0)
                interText = OU("\nTEXT =\n");
        }
        else
        {
            interText = OU("\n");
        }
    }
    else
    {
        interText = OU("\n");
    }

    // Update previous formula
    setFormulaProperty(xPreviousFormulaComp, "iFormula", uno::makeAny(getFormulaProperty<OUString>(xPreviousFormulaComp, "iFormula") + interText + formulaText));

    // Delete formula
    RemoveIFormula(formulaName);
    deleteFormula(GetModel(), xFormulaComp); // TODO Use writer-internal methods (if they exist)
}

void SwDocShell::HideIFormula(const OUString& formulaName, const bool hide)
{
    SAL_INFO_LEVEL(1, "sw.imath", "SwDocShell::HideIFormula '" << formulaName << "'");

    // Find formula
    Reference< XComponent > xFormulaComp = getObjectByName(GetModel(), formulaName);
    if (!xFormulaComp.is()) return;

    if (getFormulaProperty<bool>(xFormulaComp, "ImIsHidden") == hide) return;

    setFormulaProperty(xFormulaComp, "ImIsHidden", uno::makeAny(hide));
}

void SwDocShell::RenumberIFormulas()
{
    // TODO Create a status indicator
    SAL_INFO_LEVEL(2, "sw.imath", "Renumbering formulas");
    m_nextIFormulaNumber = 1;

    // Handle master document
    if (m_xMasterDocument.is())
    {
        SwXTextDocument* pMasterDocument = comphelper::getFromUnoTunnel<SwXTextDocument>(m_xMasterDocument);

        if (pMasterDocument != nullptr)
        {
            SwDocShell* pMasterDocumentShell = static_cast<SwDocShell*>(pMasterDocument->GetObjectShell());
            m_nextIFormulaNumber = pMasterDocumentShell->m_nextIFormulaNumber + 1;
        }
    }

    std::map<icu::UnicodeString, OUString> mapping;

    for (const auto& i : m_IFormulaNames)
    {
        // get the formula
        Reference< XComponent > xFormulaComp = getObjectByName(GetModel(), i);
        if (!xFormulaComp.is()) continue;

        icu::UnicodeString formulaText = getFormulaProperty<OUString>(xFormulaComp, "iFormula").getStr();
        if (formulaText.length() == 0) continue;

        OUString result = OU("");
        UErrorCode status = U_ZERO_ERROR;
        icu::RegexMatcher labelRegex("@[0-9]+(_[0-9]+)*@", formulaText, 0, status);
        int pos = 0;

        while (labelRegex.find())
        {
            icu::UnicodeString l = labelRegex.group(status).tempSubString(1, labelRegex.group(status).length() - 2);
            OUString newlabel;

            if (mapping.find(l) != mapping.end())
            {
                // found a label inside an equation that was used previously
                newlabel = mapping.at(l);
            }
            else
            {
                // Note: duplicate equation labels will remain duplicate
                newlabel = OUString::number(m_nextIFormulaNumber++);
                mapping[l] = newlabel;
            }

            SAL_INFO_LEVEL(2, "sw.imath", "Found label '" << OUString(l.getTerminatedBuffer()) << "' at position " << labelRegex.start(status) << ", replacing with '" << newlabel << "'");
            result += OUString(formulaText.tempSubString(pos, labelRegex.start(status) - pos).getTerminatedBuffer()) + "@" + newlabel + "@";
            pos = labelRegex.end(status);
        }

        result += OUString(formulaText.tempSubString(pos).getTerminatedBuffer());
        SAL_INFO_LEVEL(2,  "sw.imath", "iFormula with replacements: '" << result << "'");

        if (!result.equals(getFormulaProperty<OUString>(xFormulaComp, "iFormula")))
            setFormulaProperty(xFormulaComp, "iFormula", uno::makeAny(result));
        else
            setFormulaProperty(xFormulaComp, "iFormulaPendingAction", uno::makeAny(OUString("compile"))); // We must always compile, to update the chain of eqc objects
    }

    SAL_INFO_LEVEL(2, "sw.imath", "Finished renumbering formulas");
}

// a Transfer is cancelled (is called from SFX)
void SwDocShell::CancelTransfers()
{
    // Cancel all links from LinkManager
    m_xDoc->getIDocumentLinksAdministration().GetLinkManager().CancelTransfers();
    SfxObjectShell::CancelTransfers();
}

SwEditShell * SwDocShell::GetEditShell()
{
    return m_pWrtShell;
}

SwFEShell* SwDocShell::GetFEShell()
{
    return m_pWrtShell;
}

void SwDocShell::RemoveOLEObjects()
{
    SwIterator<SwContentNode,SwFormatColl> aIter( *m_xDoc->GetDfltGrfFormatColl() );
    for( SwContentNode* pNd = aIter.First(); pNd; pNd = aIter.Next() )
    {
        SwOLENode* pOLENd = pNd->GetOLENode();
        if( pOLENd && ( pOLENd->IsOLEObjectDeleted() ||
                        pOLENd->IsInGlobalDocSection() ) )
        {
            if (!m_pOLEChildList)
                m_pOLEChildList.reset( new comphelper::EmbeddedObjectContainer );

            OUString aObjName = pOLENd->GetOLEObj().GetCurrentPersistName();
            GetEmbeddedObjectContainer().MoveEmbeddedObject( aObjName, *m_pOLEChildList );
        }
    }
}

// When a document is loaded, SwDoc::PrtOLENotify is called to update
// the sizes of math objects. However, for objects that do not have a
// SwFrame at this time, only a flag is set (bIsOLESizeInvalid) and the
// size change takes place later, while calculating the layout in the
// idle handler. If this document is saved now, it is saved with invalid
// sizes. For this reason, the layout has to be calculated before a document is
// saved, but of course only id there are OLE objects with bOLESizeInvalid set.
void SwDocShell::CalcLayoutForOLEObjects()
{
    if (!m_pWrtShell)
        return;

    if (m_pView && m_pView->GetIPClient())
    {
        // We have an active OLE edit: allow link updates, so an up to date replacement graphic can
        // be created.
        comphelper::EmbeddedObjectContainer& rEmbeddedObjectContainer = getEmbeddedObjectContainer();
        rEmbeddedObjectContainer.setUserAllowsLinkUpdate(true);
    }

    SwIterator<SwContentNode,SwFormatColl> aIter( *m_xDoc->GetDfltGrfFormatColl() );
    for( SwContentNode* pNd = aIter.First(); pNd; pNd = aIter.Next() )
    {
        SwOLENode* pOLENd = pNd->GetOLENode();
        if( pOLENd && pOLENd->IsOLESizeInvalid() )
        {
            m_pWrtShell->CalcLayout();
            break;
        }
    }
}

// #i42634# Overwrites SfxObjectShell::UpdateLinks
// This new function is necessary to trigger update of links in docs
// read by the binary filter:
void SwDocShell::UpdateLinks()
{
    GetDoc()->getIDocumentLinksAdministration().UpdateLinks();
    // #i50703# Update footnote numbers
    SwTextFootnote::SetUniqueSeqRefNo( *GetDoc() );
    SwNodeIndex aTmp( GetDoc()->GetNodes() );
    GetDoc()->GetFootnoteIdxs().UpdateFootnote( aTmp.GetNode() );
}

uno::Reference< frame::XController >
                                SwDocShell::GetController()
{
    css::uno::Reference< css::frame::XController > aRet;
    // #i82346# No view in page preview
    if ( GetView() )
        aRet = GetView()->GetController();
    return aRet;
}

static const char* s_EventNames[] =
{
    "OnPageCountChange",
    "OnMailMerge",
    "OnMailMergeFinished",
    "OnFieldMerge",
    "OnFieldMergeFinished",
    "OnLayoutFinished"
};
sal_Int32 const s_nEvents(SAL_N_ELEMENTS(s_EventNames));

Sequence< OUString >    SwDocShell::GetEventNames()
{
    Sequence< OUString > aRet = SfxObjectShell::GetEventNames();
    sal_Int32 nLen = aRet.getLength();
    aRet.realloc(nLen + 6);
    OUString* pNames = aRet.getArray();
    pNames[nLen++] = GetEventName(0);
    pNames[nLen++] = GetEventName(1);
    pNames[nLen++] = GetEventName(2);
    pNames[nLen++] = GetEventName(3);
    pNames[nLen++] = GetEventName(4);
    pNames[nLen]   = GetEventName(5);

    return aRet;
}

OUString SwDocShell::GetEventName( sal_Int32 nIndex )
{
    if (nIndex < s_nEvents)
    {
        return OUString::createFromAscii(s_EventNames[nIndex]);
    }
    return OUString();
}

const ::sfx2::IXmlIdRegistry* SwDocShell::GetXmlIdRegistry() const
{
    return m_xDoc ? &m_xDoc->GetXmlIdRegistry() : nullptr;
}

bool SwDocShell::IsChangeRecording() const
{
    if (!m_pWrtShell)
        return false;
    return bool(m_pWrtShell->GetRedlineFlags() & RedlineFlags::On);
}

bool SwDocShell::HasChangeRecordProtection() const
{
    if (!m_pWrtShell)
        return false;
    return m_pWrtShell->getIDocumentRedlineAccess().GetRedlinePassword().hasElements();
}

void SwDocShell::SetChangeRecording( bool bActivate, bool bLockAllViews )
{
    RedlineFlags nOn = bActivate ? RedlineFlags::On : RedlineFlags::NONE;
    RedlineFlags nMode = m_pWrtShell->GetRedlineFlags();
    if (bLockAllViews)
    {
        // tdf#107870: prevent jumping to cursor
        auto aViewGuard(LockAllViews());
        m_pWrtShell->SetRedlineFlagsAndCheckInsMode( (nMode & ~RedlineFlags::On) | nOn );
    }
    else
    {
        m_pWrtShell->SetRedlineFlagsAndCheckInsMode( (nMode & ~RedlineFlags::On) | nOn );
    }
}

void SwDocShell::SetProtectionPassword( const OUString &rNewPassword )
{
    const SfxAllItemSet aSet( GetPool() );

    IDocumentRedlineAccess& rIDRA = m_pWrtShell->getIDocumentRedlineAccess();
    Sequence< sal_Int8 > aPasswd = rIDRA.GetRedlinePassword();
    const SfxBoolItem* pRedlineProtectItem = aSet.GetItemIfSet(FN_REDLINE_PROTECT, false);
    if (pRedlineProtectItem
        && pRedlineProtectItem->GetValue() == aPasswd.hasElements())
        return;

    if (!rNewPassword.isEmpty())
    {
        // when password protection is applied change tracking must always be active
        SetChangeRecording( true );

        Sequence< sal_Int8 > aNewPasswd;
        SvPasswordHelper::GetHashPassword( aNewPasswd, rNewPassword );
        rIDRA.SetRedlinePassword( aNewPasswd );
    }
    else
    {
        rIDRA.SetRedlinePassword( Sequence< sal_Int8 >() );
    }
}

bool SwDocShell::GetProtectionHash( /*out*/ css::uno::Sequence< sal_Int8 > &rPasswordHash )
{
    bool bRes = false;

    const SfxAllItemSet aSet( GetPool() );

    IDocumentRedlineAccess& rIDRA = m_pWrtShell->getIDocumentRedlineAccess();
    const Sequence< sal_Int8 >& aPasswdHash( rIDRA.GetRedlinePassword() );
    const SfxBoolItem* pRedlineProtectItem = aSet.GetItemIfSet(FN_REDLINE_PROTECT, false);
    if (pRedlineProtectItem
        && pRedlineProtectItem->GetValue() == aPasswdHash.hasElements())
        return false;
    rPasswordHash = aPasswdHash;
    bRes = true;

    return bRes;
}

void SwDocShell::RegisterAutomationDocumentEventsCaller(css::uno::Reference< ooo::vba::XSinkCaller > const& xCaller)
{
    mxAutomationDocumentEventsCaller = xCaller;
}

void SwDocShell::CallAutomationDocumentEventSinks(const OUString& Method, css::uno::Sequence< css::uno::Any >& Arguments)
{
    if (mxAutomationDocumentEventsCaller.is())
        mxAutomationDocumentEventsCaller->CallSinks(Method, Arguments);
}

void SwDocShell::RegisterAutomationDocumentObject(css::uno::Reference< ooo::vba::word::XDocument > const& xDocument)
{
    mxAutomationDocumentObject = xDocument;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
