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
#ifndef INCLUDED_SFX2_SOURCE_INC_DOCUMENTIMATHDIALOG_HXX
#define INCLUDED_SFX2_SOURCE_INC_DOCUMENTIMATHDIALOG_HXX

#include <sfx2/tabdlg.hxx>

/**
 Tab page for document font settings in the document properties dialog.
*/
class SfxDocumentIMathPage final : public SfxTabPage
{
public:
    SfxDocumentIMathPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& set);
    virtual ~SfxDocumentIMathPage() override;
    static std::unique_ptr<SfxTabPage>
    Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* set);

private:
    virtual bool FillItemSet(SfxItemSet* set) override;
    virtual void Reset(const SfxItemSet* set) override;

    std::unique_ptr<weld::CheckButton> m_autoformatCheckbox;
    std::unique_ptr<weld::CheckButton> m_autoalignCheckbox;
    std::unique_ptr<weld::CheckButton> m_autochainCheckbox;
    std::unique_ptr<weld::CheckButton> m_autofractionCheckbox;
    std::unique_ptr<weld::SpinButton>  m_minimumtextsizeSpinbutton;
    std::unique_ptr<weld::CheckButton> m_autotextmodeCheckbox;
    std::unique_ptr<weld::ComboBox>    m_formulafontCombobox;
    std::unique_ptr<weld::Entry>       m_preferredunitsEntry;
    std::unique_ptr<weld::CheckButton> m_suppressunitsCheckbox;
    std::unique_ptr<weld::SpinButton>  m_precisionSpinbutton;
    std::unique_ptr<weld::CheckButton> m_fixedpointCheckbox;
    std::unique_ptr<weld::SpinButton>  m_highsclimitSpinbutton;
    std::unique_ptr<weld::SpinButton> m_lowsclimitSpinbutton;
    std::unique_ptr<weld::CheckButton> m_inhibitunderflowCheckbox;
    std::unique_ptr<weld::CheckButton> m_evalfrealrootsCheckbox;
    std::unique_ptr<weld::CheckButton> m_allowimplicitCheckbox;
    std::unique_ptr<weld::CheckButton> m_showlabelsCheckbox;
    std::unique_ptr<weld::CheckButton> m_echoformulasCheckbox;
};

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
