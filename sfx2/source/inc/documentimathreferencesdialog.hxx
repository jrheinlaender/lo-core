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
#ifndef INCLUDED_SFX2_SOURCE_INC_DOCUMENTIMATHREFERENCESDIALOG_HXX
#define INCLUDED_SFX2_SOURCE_INC_DOCUMENTIMATHREFERENCESDIALOG_HXX

#include <map>

#include <sfx2/tabdlg.hxx>

/**
 Tab page for document font settings in the document properties dialog.
*/
class SfxDocumentIMathReferencesPage final : public SfxTabPage
{
public:
    SfxDocumentIMathReferencesPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& set);
    virtual ~SfxDocumentIMathReferencesPage() override;
    static std::unique_ptr<SfxTabPage> Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* set);

private:
    virtual bool FillItemSet(SfxItemSet* set) override;
    virtual void Reset(const SfxItemSet* set) override;

    std::map<OString, std::unique_ptr<weld::CheckButton>> m_referencesCheckboxes;
    std::vector<std::unique_ptr<weld::Button>> m_userreferencesButtons;
    std::vector<std::unique_ptr<weld::Entry>> m_userreferencesEntries;
    std::unique_ptr<weld::Button> m_masterdocumentButton;
    std::unique_ptr<weld::Entry> m_masterdocumentEntry;

    DECL_LINK(UserRefHdl_Impl, weld::Button&, void);
    DECL_LINK(MasterDocHdl_Impl, weld::Button&, void);
};

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
