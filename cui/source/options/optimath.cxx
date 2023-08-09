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

#include "optimath.hxx"
#include <sfx2/filedlghelper.hxx>
#include <sfx2/strings.hrc>
#include <sfx2/sfxresid.hxx>
#include <osl/file.hxx>
#include <vcl/svapp.hxx>
#include <svtools/ctrltool.hxx>
#include <officecfg/Office/iMath.hxx>
#include <com/sun/star/ui/dialogs/TemplateDescription.hpp>
// Note: Only because of this dependency it is necessary to add imath to Library_swui.mk
#include <imath/msgdriver.hxx> // TODO: Only required to set debug level for imath library

using namespace ::com::sun::star;

namespace {
    const std::vector<rtl::OString> referenceNames(
    {
        "00init", "01units", "02siunits", "03siunits_abbrev", "04engunits",
        "05engunits_abbrev", "06impunits", "07impunits_abbrev", "08siprefixes", "09siprefixes_abbrev",
        "99substitutions"
    });
}

SvxIMathOptionsPage::SvxIMathOptionsPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& rSet)
    : SfxTabPage(pPage, pController, "modules/swriter/ui/optimathpage.ui", "OptIMathPage", &rSet)
    , m_autorenumberduplicateCheckbox(m_xBuilder->weld_check_button("O_Autorenumberduplicate"))
    , m_debuglevelSpinbutton(m_xBuilder->weld_spin_button("I_Debuglevel"))
    , m_externaleditorEntry(m_xBuilder->weld_entry("txt_Externaleditor"))
    , m_autoformatCheckbox(m_xBuilder->weld_check_button("formatting_o_autoformat"))
    , m_autoalignCheckbox (m_xBuilder->weld_check_button("formatting_o_autoalign"))
    , m_autochainCheckbox (m_xBuilder->weld_check_button("formatting_o_autochain"))
    , m_autofractionCheckbox(m_xBuilder->weld_check_button("formatting_o_autofraction"))
    , m_minimumtextsizeSpinbutton(m_xBuilder->weld_spin_button("formatting_i_minimumtextsize"))
    , m_autotextmodeCheckbox(m_xBuilder->weld_check_button("formatting_o_autotextmode"))
    , m_formulafontCombobox(m_xBuilder->weld_combo_box("formatting_txt_formulafont"))
    , m_preferredunitsEntry(m_xBuilder->weld_entry("formatting_txt_preferredunits"))
    , m_suppressunitsCheckbox(m_xBuilder->weld_check_button("formatting_o_suppress_units"))
    , m_precisionSpinbutton(m_xBuilder->weld_spin_button("formatting_i_precision"))
    , m_fixedpointCheckbox(m_xBuilder->weld_check_button("formatting_o_fixedpoint"))
    , m_highsclimitSpinbutton(m_xBuilder->weld_spin_button("formatting_i_highsclimit"))
    , m_lowsclimitSpinbutton(m_xBuilder->weld_spin_button("formatting_i_lowsclimit"))
    , m_inhibitunderflowCheckbox(m_xBuilder->weld_check_button("internal_o_underflow"))
    , m_evalfrealrootsCheckbox(m_xBuilder->weld_check_button("internal_o_evalf_real_roots"))
    , m_allowimplicitCheckbox(m_xBuilder->weld_check_button("formatting_o_implicitmul"))
    , m_showlabelsCheckbox(m_xBuilder->weld_check_button("formatting_o_showlabels"))
    , m_echoformulasCheckbox(m_xBuilder->weld_check_button("miscellaneous_o_echoformula"))
{
    m_formulafontCombobox->make_sorted();
    m_formulafontCombobox->set_size_request(1, -1);

    for (const auto& n : referenceNames)
        m_referencesCheckboxes.emplace(n, m_xBuilder->weld_check_button(n));

    for (int r = 1; r <= 3; ++r)
    {
        m_userreferencesButtons.emplace_back(m_xBuilder->weld_button(rtl::OString("btn_reference" + rtl::OString::number(r))));
        m_userreferencesButtons.back()->connect_clicked( LINK( this, SvxIMathOptionsPage, UserRefHdl_Impl ) );
        m_userreferencesEntries.emplace_back(m_xBuilder->weld_entry(rtl::OString("txt_reference" + rtl::OString::number(r))));
    }
}

SvxIMathOptionsPage::~SvxIMathOptionsPage()
{
}

// TODO Code duplicated from sfx2/source/dialog/documentimathreferencesdialog.cxx
IMPL_LINK(SvxIMathOptionsPage, UserRefHdl_Impl, weld::Button&, rButton, void)
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

std::unique_ptr<SfxTabPage> SvxIMathOptionsPage::Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* rAttrSet)
{
    return std::make_unique<SvxIMathOptionsPage>(pPage, pController, *rAttrSet);
}

bool SvxIMathOptionsPage::FillItemSet( SfxItemSet* )
{
    bool bRet = false;

    auto xChanges = comphelper::ConfigurationChanges::create();

    officecfg::Office::iMath::Miscellaneous::O_Autorenumberduplicate::set(m_autorenumberduplicateCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Miscellaneous::I_Debuglevel::set(m_debuglevelSpinbutton->get_value(), xChanges);
    msg::info().setlevel(m_debuglevelSpinbutton->get_value());
    SAL_INFO("starmath.imath", "Set debug level to " << OUString::number(m_debuglevelSpinbutton->get_value()));
    // officecfg::Office::iMath::Miscellaneous::txt_Externaleditor::set(m_externaleditorEntry->get_text(), xChanges); TODO Implement

    officecfg::Office::iMath::Formatting::O_Autoformat::set(m_autoformatCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Formatting::O_Autoalign::set(m_autoalignCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Formatting::O_Autochain::set(m_autochainCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Formatting::O_Autofraction::set(m_autofractionCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Formatting::I_Minimumtextsize::set(m_minimumtextsizeSpinbutton->get_value(), xChanges);
    officecfg::Office::iMath::Formatting::O_Autotextmode::set(m_autotextmodeCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Formatting::txt_FormulaFont::set(m_formulafontCombobox->get_active_text(), xChanges);
    officecfg::Office::iMath::Formatting::txt_PreferredUnits::set(m_preferredunitsEntry->get_text(), xChanges);
    officecfg::Office::iMath::Formatting::O_Suppress_Units::set(m_suppressunitsCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Formatting::I_Precision::set(m_precisionSpinbutton->get_value(), xChanges);
    officecfg::Office::iMath::Formatting::O_Fixedpoint::set(m_fixedpointCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Formatting::I_Highsclimit::set(m_highsclimitSpinbutton->get_value(), xChanges);
    officecfg::Office::iMath::Formatting::I_Lowsclimit::set(m_lowsclimitSpinbutton->get_value(), xChanges);
    officecfg::Office::iMath::Internal::O_Underflow::set(m_inhibitunderflowCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Internal::O_Evalf_Real_Roots::set(m_evalfrealrootsCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Formatting::O_Implicitmul::set(m_allowimplicitCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Formatting::O_ShowLabels::set(m_showlabelsCheckbox->get_active(), xChanges);
    officecfg::Office::iMath::Miscellaneous::O_Echoformula::set(m_echoformulasCheckbox->get_active(), xChanges);

    OString referenceString;

    for (const auto& n : referenceNames)
        if (m_referencesCheckboxes.at(n)->get_active())
            referenceString += n + OString(' ');

    officecfg::Office::iMath::Includes::txt_References::set(OUString::fromUtf8(referenceString), xChanges);
    officecfg::Office::iMath::Includes::txt_Include1::set(m_userreferencesEntries.at(0)->get_text(), xChanges);
    officecfg::Office::iMath::Includes::txt_Include2::set(m_userreferencesEntries.at(1)->get_text(), xChanges);
    officecfg::Office::iMath::Includes::txt_Include3::set(m_userreferencesEntries.at(2)->get_text(), xChanges);

    xChanges->commit();

    bRet = true;

    return bRet;
}

void SvxIMathOptionsPage::Reset( const SfxItemSet* )
{
    m_autorenumberduplicateCheckbox->set_active(officecfg::Office::iMath::Miscellaneous::O_Autorenumberduplicate::get());
    m_debuglevelSpinbutton->set_value(officecfg::Office::iMath::Miscellaneous::I_Debuglevel::get());
    //m_externaleditorEntry->set_text(officecfg::Office::iMath::Miscellaneous::txt_Externaleditor::get()); TODO Implement

    m_formulafontCombobox->freeze();
    m_formulafontCombobox->clear();

    FontList aFntLst(Application::GetDefaultDevice());
    sal_uInt16 nFontCount = aFntLst.GetFontNameCount();
    for (sal_uInt16 i = 0; i < nFontCount; ++i)
    {
        const FontMetric& rFontMetric = aFntLst.GetFontName(i);
        m_formulafontCombobox->append_text(rFontMetric.GetFamilyName());
    }

    m_formulafontCombobox->thaw();

    m_autoformatCheckbox->set_active(officecfg::Office::iMath::Formatting::O_Autoformat::get());
    m_autoalignCheckbox->set_active(officecfg::Office::iMath::Formatting::O_Autoalign::get());
    m_autochainCheckbox->set_active(officecfg::Office::iMath::Formatting::O_Autochain::get());
    m_autofractionCheckbox->set_active(officecfg::Office::iMath::Formatting::O_Autofraction::get());
    m_minimumtextsizeSpinbutton->set_value(officecfg::Office::iMath::Formatting::I_Minimumtextsize::get());
    m_autotextmodeCheckbox->set_active(officecfg::Office::iMath::Formatting::O_Autotextmode::get());
    m_formulafontCombobox->set_active_text(officecfg::Office::iMath::Formatting::txt_FormulaFont::get());
    m_preferredunitsEntry->set_text(officecfg::Office::iMath::Formatting::txt_PreferredUnits::get());
    m_suppressunitsCheckbox->set_active(officecfg::Office::iMath::Formatting::O_Suppress_Units::get());
    m_precisionSpinbutton->set_value(officecfg::Office::iMath::Formatting::I_Precision::get());
    m_fixedpointCheckbox->set_active(officecfg::Office::iMath::Formatting::O_Fixedpoint::get());
    m_highsclimitSpinbutton->set_value(officecfg::Office::iMath::Formatting::I_Highsclimit::get());
    m_lowsclimitSpinbutton->set_value(officecfg::Office::iMath::Formatting::I_Lowsclimit::get());
    m_inhibitunderflowCheckbox->set_active(officecfg::Office::iMath::Internal::O_Underflow::get());
    m_evalfrealrootsCheckbox->set_active(officecfg::Office::iMath::Internal::O_Evalf_Real_Roots::get());
    m_allowimplicitCheckbox->set_active(officecfg::Office::iMath::Formatting::O_Implicitmul::get());
    m_showlabelsCheckbox->set_active(officecfg::Office::iMath::Formatting::O_ShowLabels::get());
    m_echoformulasCheckbox->set_active(officecfg::Office::iMath::Miscellaneous::O_Echoformula::get());

    for (const auto& n : referenceNames)
        m_referencesCheckboxes.at(n)->set_active(false);

    OUString referenceString = officecfg::Office::iMath::Includes::txt_References::get();
    if (referenceString.getLength() > 0)
    {
        sal_Int32 nIdx = 0;
        OUString token;

        while ((token = referenceString.getToken(0, ' ', nIdx)).getLength() > 0) {
            if (std::find(referenceNames.begin(), referenceNames.end(), token.toUtf8()) != referenceNames.end())
                m_referencesCheckboxes.at(token.toUtf8())->set_active(true);
        }
    }

    m_userreferencesEntries.at(0)->set_text(officecfg::Office::iMath::Includes::txt_Include1::get());
    m_userreferencesEntries.at(1)->set_text(officecfg::Office::iMath::Includes::txt_Include2::get());
    m_userreferencesEntries.at(2)->set_text(officecfg::Office::iMath::Includes::txt_Include3::get());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
