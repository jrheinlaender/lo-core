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

#include <map>
#include <vcl/weld.hxx>
#include <sfx2/tabdlg.hxx>

class SvxIMathOptionsPage final : public SfxTabPage
{
private:
    std::unique_ptr<weld::CheckButton> m_autorenumberduplicateCheckbox;
    std::unique_ptr<weld::SpinButton>  m_debuglevelSpinbutton;
    std::unique_ptr<weld::Entry>       m_externaleditorEntry;

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
    std::unique_ptr<weld::SpinButton>  m_lowsclimitSpinbutton;
    std::unique_ptr<weld::CheckButton> m_inhibitunderflowCheckbox;
    std::unique_ptr<weld::CheckButton> m_evalfrealrootsCheckbox;
    std::unique_ptr<weld::CheckButton> m_allowimplicitCheckbox;
    std::unique_ptr<weld::CheckButton> m_showlabelsCheckbox;
    std::unique_ptr<weld::CheckButton> m_echoformulasCheckbox;

    std::map<OString, std::unique_ptr<weld::CheckButton>> m_referencesCheckboxes;
    std::vector<std::unique_ptr<weld::Button>> m_userreferencesButtons;
    std::vector<std::unique_ptr<weld::Entry>> m_userreferencesEntries;

    DECL_LINK(UserRefHdl_Impl, weld::Button&, void);

public:
    SvxIMathOptionsPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& rSet);
    virtual ~SvxIMathOptionsPage() override;

    static std::unique_ptr<SfxTabPage> Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* rAttrSet);

    virtual bool        FillItemSet( SfxItemSet* rSet ) override;
    virtual void        Reset( const SfxItemSet* rSet ) override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
