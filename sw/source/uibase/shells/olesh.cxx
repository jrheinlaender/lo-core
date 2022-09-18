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

#include <sfx2/objface.hxx>
#include <vcl/EnumContext.hxx>
#include <tools/globname.hxx>
#include <sot/exchange.hxx>

#include <view.hxx>
#include <frmsh.hxx>
#include <olesh.hxx>
#include <wrtsh.hxx>
#include <flyfrm.hxx>

#include <sfx2/sidebar/SidebarController.hxx>

#define ShellClass_SwOleShell
#include <sfx2/msg.hxx>
#include <swslots.hxx>

#include <com/sun/star/embed/XEmbeddedObject.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>

using namespace css::uno;
using namespace sfx2::sidebar;

namespace {

bool inChartOrMathContext(const SwView& rViewShell)
{
    SidebarController* pSidebar = SidebarController::GetSidebarControllerForView(&rViewShell);
    if (pSidebar)
        return pSidebar->hasChartOrMathContextCurrently();

    return false;
}

} // anonymous namespace

SFX_IMPL_INTERFACE(SwOleShell, SwFrameShell)

void SwOleShell::InitInterface_Impl()
{
    GetStaticInterface()->RegisterPopupMenu("oleobject");

    GetStaticInterface()->RegisterObjectBar(SFX_OBJECTBAR_OBJECT, SfxVisibilityFlags::Invisible, ToolbarId::Ole_Toolbox);
}

void SwOleShell::Activate(bool bMDI)
{
    if(!inChartOrMathContext(GetView()))
        SwFrameShell::Activate(bMDI);
    else
    {
        // Avoid context changes for chart/math during activation / deactivation.
        const bool bIsContextBroadcasterEnabled (SfxShell::SetContextBroadcasterEnabled(false));

        SwFrameShell::Activate(bMDI);

        SfxShell::SetContextBroadcasterEnabled(bIsContextBroadcasterEnabled);
    }
}

void SwOleShell::Deactivate(bool bMDI)
{
    if(!inChartOrMathContext(GetView()))
        SwFrameShell::Deactivate(bMDI);
    else
    {
        // Avoid context changes for chart/math during activation / deactivation.
        const bool bIsContextBroadcasterEnabled (SfxShell::SetContextBroadcasterEnabled(false));

        SwFrameShell::Deactivate(bMDI);

        SfxShell::SetContextBroadcasterEnabled(bIsContextBroadcasterEnabled);
    }

    // Note: Sequence for double-clicking a formula without prior selection is:
    // Double click - Constructor - User editing - Activate - User removes focus from formula - Deactivate
    // The last deactivation happens when the focus leaves the formula
    // If the formula is first selected and then double clicked
    // Click - Constructor - Activate - Double click - Deactivate - User editing - Activate - User removes focus from formula - Deactivate
    if (mIFormulaName.getLength() > 0)
    {
        SAL_INFO("sw.imath", "Shell for Math object '" << mIFormulaName << "' was deactivated");

        // Notify document that dependent iFormulas need to be recompiled
        bool textChanged = GetShell().GetDoc()->GetDocShell()->RecalculateDependentIFormulas(mIFormulaName, mIFormulaText);
        if (textChanged)
            mIFormulaText = ""; // Clear text so that new text will be set at next Activate() call
        // Otherwise keep name and text so that repeated activation of the same formula can be handled
    }
}

SwOleShell::SwOleShell(SwView &_rView) :
    SwFrameShell(_rView)

{
    SetName("Object");
    SfxShell::SetContextName(vcl::EnumContext::GetContextName(vcl::EnumContext::Context::OLE));
    mIFormulaName = "";
    mIFormulaText = "";
    SAL_INFO("sw.imath", "SwOleShell::SwOleShell()");

    // Set iFormula name and text
    const uno::Reference < embed::XEmbeddedObject > xObj( GetShell().GetOleRef() );
    if (xObj.is()) {
        SvGlobalName aCLSID( xObj->getClassID() );
        if ( SotExchange::IsMath( aCLSID ) )
        {
            mIFormulaName = GetShell().GetFlyName();
            SAL_INFO("sw.imath", "Shell Math object name set to '" << mIFormulaName << "'");

            Reference < lang::XComponent > formulaComponent(xObj->getComponent(), UNO_QUERY);
            if (formulaComponent.is())
            {
                Reference < beans::XPropertySet > fPS(formulaComponent, UNO_QUERY);
                if (fPS.is())
                {
                    Any fTextAny;
                    fTextAny = fPS->getPropertyValue("iFormula");
                    fTextAny >>= mIFormulaText;
                    SAL_INFO("sw.imath", "Shell Math object old text set to\n" << mIFormulaText);

                    fTextAny = fPS->getPropertyValue("PreviousIFormula");
                    OUString previousIFormula;
                    fTextAny >>= previousIFormula;
                    if (previousIFormula.equalsAscii("_IMATH_UNDEFINED_"))
                    {
                        SAL_INFO("sw.imath", "New math object, triggering compile");
                        GetShell().GetDoc()->GetDocShell()->UpdatePreviousIFormulaLinks();
                        mIFormulaText = ""; // This will force recalculation of all dependent formulas because a formula text change is detected
                        // Note: The immediately following formula is recompiled by UpdatePreviousIFormulaLinks() because the PreviousIFormula property changes
                    }
                }
            }
        }
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
