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

#include <documentimathreferencesdialog.hxx>

#include <sfx2/objsh.hxx>
#include <sfx2/filedlghelper.hxx>
#include <sfx2/strings.hrc>
#include <sfx2/sfxresid.hxx>

#include <comphelper/processfactory.hxx>
#include <svtools/ctrltool.hxx>
#include <vcl/svapp.hxx>
#include <osl/file.hxx>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/rdf/XNamedGraph.hpp>
#include <com/sun/star/rdf/XDocumentMetadataAccess.hpp>
#include <com/sun/star/rdf/URI.hpp>
#include <com/sun/star/rdf/XLiteral.hpp>
#include <com/sun/star/rdf/Literal.hpp>
#include <com/sun/star/rdf/Statement.hpp>
#include <com/sun/star/ui/dialogs/TemplateDescription.hpp>

using namespace ::com::sun::star;
using namespace ::com::sun::star::ui::dialogs;

// TODO Defined in documentimathdialog.cxx, put it in some common file
namespace imathutils
{
    extern uno::Reference<rdf::XNamedGraph> getDocumentGraph(const uno::Reference< uno::XComponentContext >& xContext, const uno::Reference< frame::XModel >& xModel, const bool strict);
    extern OUString getStatement(const uno::Reference< uno::XComponentContext >& xContext, const uno::Reference<frame::XModel>& xModel, const uno::Reference<rdf::XNamedGraph>& xGraph, const OUString& predicate);
    extern void updateStatement(const uno::Reference< uno::XComponentContext >& mxCC, const uno::Reference<frame::XModel>& xModel, const uno::Reference<rdf::XNamedGraph>& xGraph, const OUString& predicate, const OUString& value);
}
using namespace imathutils;

std::unique_ptr<SfxTabPage> SfxDocumentIMathReferencesPage::Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* set)
{
    return std::make_unique<SfxDocumentIMathReferencesPage>(pPage, pController, *set);
}

namespace {
    const std::vector<rtl::OString> referenceNames(
    {
        "00init", "01units", "02siunits", "03siunits_abbrev", "04engunits",
        "05engunits_abbrev", "06impunits", "07impunits_abbrev", "08siprefixes", "09siprefixes_abbrev",
        "99substitutions"
    });
}

SfxDocumentIMathReferencesPage::SfxDocumentIMathReferencesPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& set)
    : SfxTabPage(pPage, pController, "sfx/ui/documentimathreferencespage.ui", "DocumentIMathReferencesPage", &set)
    , m_masterdocumentButton(m_xBuilder->weld_button("btn_masterdoc"))
    , m_masterdocumentEntry(m_xBuilder->weld_entry("txt_masterdoc"))
{
    for (const auto& n : referenceNames)
        m_referencesCheckboxes.emplace(n, m_xBuilder->weld_check_button(n));

    for (int r = 1; r <= 3; ++r)
    {
        m_userreferencesButtons.emplace_back(m_xBuilder->weld_button(rtl::OString("btn_reference" + rtl::OString::number(r))));
        m_userreferencesButtons.back()->connect_clicked( LINK( this, SfxDocumentIMathReferencesPage, UserRefHdl_Impl ) );
        m_userreferencesEntries.emplace_back(m_xBuilder->weld_entry(rtl::OString("txt_reference" + rtl::OString::number(r))));
    }

    m_masterdocumentButton->connect_clicked( LINK( this, SfxDocumentIMathReferencesPage, MasterDocHdl_Impl ) );
}

SfxDocumentIMathReferencesPage::~SfxDocumentIMathReferencesPage()
{
}

IMPL_LINK(SfxDocumentIMathReferencesPage, UserRefHdl_Impl, weld::Button&, rButton, void)
{
    sfx2::FileDialogHelper aDlg(ui::dialogs::TemplateDescription::FILEOPEN_SIMPLE, FileDialogFlags::NONE, SfxResId(STR_IMATH_USRREF_HEADLINE), u"imath", OUString(), uno::Sequence<OUString>(), nullptr );
    aDlg.SetTitle( SfxResId( STR_IMATH_USRREF_TITLE ) );

    OUString sFolder;
    for (const auto& r : m_userreferencesEntries)
        if (r->get_text().getLength() > 0)
        {
            osl::FileBase::getFileURLFromSystemPath(m_userreferencesEntries.at(0)->get_text(), sFolder);
            break;
        }
    if (!sFolder.isEmpty())
        aDlg.SetDisplayDirectory( sFolder );

    if ( aDlg.Execute() == ERRCODE_NONE )
    {
        OUString sURL = aDlg.GetPath();
        OUString sFile;
        if (osl::FileBase::getSystemPathFromFileURL(sURL, sFile) == osl::FileBase::E_None)
        {
            OString entryName = rButton.get_buildable_name();
            unsigned entryNumber = entryName.copy(entryName.getLength() - 1).toUInt64();

            if (entryNumber < 3)
                m_userreferencesEntries.at(entryNumber)->set_text(sFile);
        }
        else
        {
            OUString sMsg( SfxResId( STR_IMATH_CANNOTCONVERTURL_ERR ) );
            sMsg = sMsg.replaceFirst( "%1", sURL );
            std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(nullptr, VclMessageType::Warning, VclButtonsType::Ok, sMsg));
            xBox->run();
        }
    }
}

IMPL_LINK_NOARG(SfxDocumentIMathReferencesPage, MasterDocHdl_Impl, weld::Button&, void)
{
    sfx2::FileDialogHelper aDlg(ui::dialogs::TemplateDescription::FILEOPEN_SIMPLE, FileDialogFlags::NONE, SfxResId(STR_IMATH_MASTERDOC_HEADLINE), u"odm", OUString(), uno::Sequence<OUString>(), nullptr );
    aDlg.SetTitle( SfxResId( STR_IMATH_MASTERDOC_TITLE ) );

    OUString sFolder;
    if (m_masterdocumentEntry->get_text().getLength() > 0)
        osl::FileBase::getFileURLFromSystemPath(m_masterdocumentEntry->get_text(), sFolder);
    if (!sFolder.isEmpty())
        aDlg.SetDisplayDirectory( sFolder );

    if ( aDlg.Execute() == ERRCODE_NONE )
    {
        OUString sURL = aDlg.GetPath();
        OUString sFile;
        if (osl::FileBase::getSystemPathFromFileURL(sURL, sFile) == osl::FileBase::E_None)
        {
            m_masterdocumentEntry->set_text(sFile);
        }
        else
        {
            OUString sMsg( SfxResId( STR_IMATH_CANNOTCONVERTURL_ERR ) );
            sMsg = sMsg.replaceFirst( "%1", sURL );
            std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(nullptr, VclMessageType::Warning, VclButtonsType::Ok, sMsg));
            xBox->run();
        }
    }
}

void SfxDocumentIMathReferencesPage::Reset( const SfxItemSet* )
{
    std::map<OString, bool> references;
    for (const auto& n : referenceNames)
        references.emplace(n, false);
    std::vector<OUString> userreferencesNames;
    for (unsigned r = 0; r < 3; ++r)
        userreferencesNames.emplace_back("");
    OUString masterdoc = "";

    SfxObjectShell* pDocSh = SfxObjectShell::Current();
    if (pDocSh)
    {
        try
        {
            uno::Reference< uno::XComponentContext > xContext = comphelper::getProcessComponentContext();
            uno::Reference< frame::XModel > xModel = pDocSh->GetModel();
            uno::Reference<rdf::XNamedGraph> xGraph = getDocumentGraph(xContext, xModel, true);

            if (!xGraph.is())
            {
                auto frame = m_xBuilder->weld_frame("generalFrame");
                frame->set_visible(false);
                frame = m_xBuilder->weld_frame("unitsFrame");
                frame->set_visible(false);
                frame = m_xBuilder->weld_frame("userdefinedFrame");
                frame->set_visible(false);
                frame = m_xBuilder->weld_frame("masterdocFrame");
                frame->set_visible(false);
                frame = m_xBuilder->weld_frame("informationFrame");
                frame->set_visible(true);
            }
            else
            {
                auto frame = m_xBuilder->weld_frame("informationFrame");
                frame->set_visible(false);

                OUString referenceString = getStatement(xContext, xModel, xGraph, "includes_txt_references");
                if (referenceString.getLength() > 0)
                {
                    sal_Int32 nIdx = 0;
                    OUString token;

                    while ((token = referenceString.getToken(0, ' ', nIdx)).getLength() > 0) {
                        if (references.find(token.toUtf8()) != references.end())
                            references.at(token.toUtf8()) = true;
                    }
                }

                for (unsigned r = 1; r <= 3; ++r)
                    userreferencesNames.at(r-1) = getStatement(xContext, xModel, xGraph, OUString("includes_txt_include" + OUString::number(r)));

                masterdoc = getStatement(xContext, xModel, xGraph, "masterdocument");
            }
        }
        catch( uno::Exception& )
        {
        }
    }

    for (const auto& r : references)
        m_referencesCheckboxes.at(r.first)->set_active(r.second);
    for (unsigned r = 0; r < 3; ++r)
        m_userreferencesEntries.at(r)->set_text(userreferencesNames.at(r));
    m_masterdocumentEntry->set_text(masterdoc);
}

bool SfxDocumentIMathReferencesPage::FillItemSet( SfxItemSet* )
{
    std::map<OString, bool> references;
    for (const auto& n : referenceNames)
        references.emplace(n, m_referencesCheckboxes.at(n)->get_active());
    std::vector<OUString> userreferencesNames;
    for (unsigned r = 0; r < 3; ++r)
        userreferencesNames.emplace_back(m_userreferencesEntries.at(r)->get_text());
    OUString masterdoc = m_masterdocumentEntry->get_text();

    SfxObjectShell* pDocSh = SfxObjectShell::Current();
    if ( pDocSh )
    {
        try
        {
            uno::Reference< uno::XComponentContext > xContext = comphelper::getProcessComponentContext();
            uno::Reference< frame::XModel > xModel = pDocSh->GetModel();
            uno::Reference<rdf::XNamedGraph> xGraph = getDocumentGraph(xContext, xModel, false);

            if (xGraph.is())
            {
                OString referenceString;
                for (const auto& r : references)
                    if (r.second)
                        referenceString += r.first + OString(' ');
                updateStatement(xContext, xModel, xGraph, "includes_txt_references", OUString::fromUtf8(referenceString));

                for (unsigned r = 1; r <= 3; ++r)
                    updateStatement(xContext, xModel, xGraph, OUString("includes_txt_include") + OUString::number(r), userreferencesNames.at(r-1));

                updateStatement(xContext, xModel, xGraph, "masterdocument", masterdoc);

                // TODO The document should be recalculated from the beginning if something was changed: SmDocShell::ImInitializeCompiler()
            }
        }
        catch( uno::Exception& )
        {
        }
    }
    return false;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
