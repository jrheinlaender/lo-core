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

#include <logging.hxx>

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

    const uno::Reference < embed::XEmbeddedObject > xObj( GetShell().GetOleRef() );
    if (xObj.is())
    {
        Reference < lang::XComponent > formulaComponent(xObj->getComponent(), UNO_QUERY);
        OUString newFormulaText;

        if (formulaComponent.is())
        {
            Reference < beans::XPropertySet > fPS(formulaComponent, UNO_QUERY);
            if (fPS.is())
            {
                fPS->getPropertyValue("iFormula") >>= newFormulaText;
                newFormulaText = newFormulaText.trim();
            }
        }
        SAL_INFO_LEVEL(1, "sw.imath", "SwOleShell::Activate() found formula text\n'" << newFormulaText << "'");

        // Note: Activate() is called AFTER the user has finished editing
        mFormulaTextChanged = !mIFormulaText.equals(newFormulaText);

        if (!mFormulaTextChanged)
            SAL_INFO_LEVEL(1, "sw.imath", "Formula text is unchanged");
        else
            mIFormulaText = newFormulaText;
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
        SAL_INFO_LEVEL(1, "sw.imath", "Shell for Math object '" << mIFormulaName << "' was deactivated");

        if (!mFormulaTextChanged)
        {
            SAL_INFO_LEVEL(1, "sw.imath", "Not recalculating dependent iFormulas because formula text is unchanged");
        }
        else if (mIFormulaText.getLength() > 0)
        {
            // Notify document that dependent iFormulas need to be recompiled
            SAL_INFO_LEVEL(1, "sw.imath", "Recalculating dependent iFormulas");
            GetShell().GetDoc()->GetDocShell()->RecalculateDependentIFormulas(mIFormulaName);
        }
        else
        {
            SAL_INFO_LEVEL(1, "sw.imath", "iFormula became empty, removing...");
            GetShell().GetDoc()->GetDocShell()->RemoveIFormula(mIFormulaName); // Note: This will recalculate dependent iFormulas after the deletion
        }
    }
}

SwOleShell::SwOleShell(SwView &_rView) :
    SwFrameShell(_rView)

{
    SetName("Object");
    SfxShell::SetContextName(vcl::EnumContext::GetContextName(vcl::EnumContext::Context::OLE));
    mIFormulaName = "";
    mIFormulaText = "";
    mFormulaTextChanged = false;
    SAL_INFO_LEVEL(1, "sw.imath", "SwOleShell::SwOleShell()");

    // Set iFormula name and text
    const uno::Reference < embed::XEmbeddedObject > xObj( GetShell().GetOleRef() );
    if (xObj.is()) {
        SvGlobalName aCLSID( xObj->getClassID() );
        if ( SotExchange::IsMath( aCLSID ) )
        {
            mIFormulaName = GetShell().GetFlyName();
            SAL_INFO_LEVEL(1, "sw.imath", "Shell Math object name set to '" << mIFormulaName << "'");

            Reference < lang::XComponent > formulaComponent(xObj->getComponent(), UNO_QUERY);
            if (!formulaComponent.is())
            {
                // Note: Sequence when inserting a formula from clipboard
                // Constructor - Activate - user removes focus - Deactivate
                SAL_INFO_LEVEL(1, "sw.imath", "Pasted math object, triggering compile");
                GetShell().GetDoc()->GetDocShell()->UpdatePreviousIFormulaLinks(); // TODO It would be sufficient to update the links of this and the immediately following formula
                formulaComponent = Reference < lang::XComponent >(xObj->getComponent(), UNO_QUERY); // try again (we must extract the formula text into mIFormulaText)
                if ( formulaComponent.is() )
                {
                    Reference < beans::XPropertySet > xFormulaProps(formulaComponent, uno::UNO_QUERY );
                    if ( xFormulaProps.is() )
                    {
                        xFormulaProps->setPropertyValue("iFormulaPendingAction", uno::makeAny(OUString("compile")));
                    }
                }
                GetShell().GetDoc()->GetDocShell()->RecalculateDependentIFormulas(mIFormulaName);
            }

            if (formulaComponent.is())
            {
                Reference < beans::XPropertySet > xFormulaProps(formulaComponent, UNO_QUERY);
                if (xFormulaProps.is())
                {
                    xFormulaProps->getPropertyValue("iFormula") >>= mIFormulaText;
                    mIFormulaText = mIFormulaText.trim();
                    SAL_INFO_LEVEL(1, "sw.imath", "Shell Math object old text set to\n'" << mIFormulaText << "'");

                    OUString previousIFormula;
                    xFormulaProps->getPropertyValue("PreviousIFormula") >>= previousIFormula;
                    SAL_INFO_LEVEL(1, "sw.imath", "Previous iFormula is '" << previousIFormula << "'");
                }
            }
        }
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
