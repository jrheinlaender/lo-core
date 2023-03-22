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

#include <documentimathdialog.hxx>

#include <sfx2/objsh.hxx>

#include <comphelper/processfactory.hxx>
#include <svtools/ctrltool.hxx>
#include <vcl/svapp.hxx>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/rdf/XNamedGraph.hpp>
#include "com/sun/star/rdf/XDocumentMetadataAccess.hpp"
#include "com/sun/star/rdf/URI.hpp"
#include "com/sun/star/rdf/XLiteral.hpp"
#include "com/sun/star/rdf/Literal.hpp"
#include "com/sun/star/rdf/Statement.hpp"

using namespace ::com::sun::star;

std::unique_ptr<SfxTabPage> SfxDocumentIMathPage::Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* set)
{
    return std::make_unique<SfxDocumentIMathPage>(pPage, pController, *set);
}

SfxDocumentIMathPage::SfxDocumentIMathPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& set)
    : SfxTabPage(pPage, pController, "sfx/ui/documentimathpage.ui", "DocumentIMathPage", &set)
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
}

SfxDocumentIMathPage::~SfxDocumentIMathPage()
{
}

namespace imathutils
{

uno::Reference<rdf::XNamedGraph> getDocumentGraph(const uno::Reference< uno::XComponentContext >& xContext, const uno::Reference< frame::XModel >& xModel, const bool strict)
{
    uno::Reference<rdf::XDocumentMetadataAccess> xDMA(xModel, uno::UNO_QUERY_THROW);
    uno::Reference<rdf::XURI> xType = rdf::URI::create(xContext, "http://jan.rheinlaender.gmx.de/imath/options/v1.0");
    uno::Sequence<uno::Reference<rdf::XURI> > graphNames = xDMA->getMetadataGraphsWithType(xType);
    uno::Reference<rdf::XNamedGraph> xGraph;
    if (graphNames.getLength() > 0)
    {
        // There should only be one single graph
        xGraph = xDMA->getRDFRepository()->getGraph(graphNames[0]);
    }

    bool hasStatements = false;
    if (xGraph.is())
    {
        if (!strict)
            return xGraph;

        uno::Reference<rdf::XResource> docURI(xModel, uno::UNO_QUERY_THROW);
        uno::Reference<rdf::XURI> xPredicate = rdf::URI::create(xContext, "http://jan.rheinlaender.gmx.de/imath/predicates/formatting_o_autoformat");
        uno::Reference<container::XEnumeration> xResult = xGraph->getStatements(docURI, xPredicate, NULL); // All statements must have this document as subject
        if (xResult->hasMoreElements())
            return xGraph;
    }

    return uno::Reference<rdf::XNamedGraph>();
}

OUString getStatement(const uno::Reference< uno::XComponentContext >& xContext, const uno::Reference<frame::XModel>& xModel, const uno::Reference<rdf::XNamedGraph>& xGraph, const OUString& predicate) {
    uno::Reference<rdf::XResource> docURI(xModel, uno::UNO_QUERY_THROW);
    uno::Reference<rdf::XURI> xPredicate = rdf::URI::create(xContext, "http://jan.rheinlaender.gmx.de/imath/predicates/" + predicate);
    uno::Reference<container::XEnumeration> xResult = xGraph->getStatements(docURI, xPredicate, NULL); // All statements must have this document as subject

    if (xResult->hasMoreElements()) {
        uno::Any element = xResult->nextElement();
        rdf::Statement stmt;
        element >>= stmt;
        uno::Reference<rdf::XLiteral> object(stmt.Object, uno::UNO_QUERY_THROW);
        return object->getValue();
    } else {
        return "";
    }
}

void updateStatement(const uno::Reference< uno::XComponentContext >& mxCC, const uno::Reference<frame::XModel>& xModel, const uno::Reference<rdf::XNamedGraph>& xGraph, const OUString& predicate, const OUString& value) {
    uno::Reference<rdf::XResource> docURI(xModel, uno::UNO_QUERY_THROW);
    uno::Reference<rdf::XURI> xPredicate = rdf::URI::create(mxCC, "http://jan.rheinlaender.gmx.de/imath/predicates/" + predicate);
    uno::Reference<rdf::XLiteral> xLit = rdf::Literal::create(mxCC, value);
    uno::Reference<rdf::XNode> xObj(xLit, uno::UNO_QUERY_THROW);

    xGraph->removeStatements(docURI, xPredicate, NULL);
    xGraph->addStatement(docURI, xPredicate, xObj);
}

} // namespace imathutils
using namespace imathutils;

void SfxDocumentIMathPage::Reset( const SfxItemSet* )
{
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

    bool autoformat = false;
    bool autoalign  = false;
    bool autochain  = false;
    bool autofraction = false;
    unsigned mintextsize = 0;
    bool autotextmode = false;
    OUString formulafont = "";
    OUString preferredunits = "";
    bool suppressunits = false;
    int precision = 0;
    bool fixedpoint = false;
    int highsclimit = 0;
    int lowsclimit = 0;
    bool underflow = false;
    bool realroots = false;
    bool implicitmul = false;
    bool showlabels = false;
    bool echoformula = false;

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
                auto frame = m_xBuilder->weld_frame("appearanceFrame");
                frame->set_visible(false);
                frame = m_xBuilder->weld_frame("formulaFontFrame");
                frame->set_visible(false);
                frame = m_xBuilder->weld_frame("unitsFrame");
                frame->set_visible(false);
                frame = m_xBuilder->weld_frame("numbersFrame");
                frame->set_visible(false);
                frame = m_xBuilder->weld_frame("calculationFrame");
                frame->set_visible(false);
                auto label = m_xBuilder->weld_label("informationContent");
                label->set_visible(true);
                auto checkbox = m_xBuilder->weld_check_button("formatting_o_showlabels");
                checkbox->set_visible(false);
                checkbox = m_xBuilder->weld_check_button("miscellaneous_o_echoformula");
                checkbox->set_visible(false);
            }
            else
            {
                auto label = m_xBuilder->weld_label("informationContent");
                label->set_visible(false);
                autoformat = getStatement(xContext, xModel, xGraph, "formatting_o_autoformat").equalsAscii("true");
                autoalign  = getStatement(xContext, xModel, xGraph, "formatting_o_autoalign").equalsAscii("true");
                autochain  = getStatement(xContext, xModel, xGraph, "formatting_o_autochain").equalsAscii("true");
                autofraction = getStatement(xContext, xModel, xGraph, "formatting_o_autofraction").equalsAscii("true");
                mintextsize = getStatement(xContext, xModel, xGraph, "formatting_i_minimumtextsize").toInt64();
                autotextmode = getStatement(xContext, xModel, xGraph, "formatting_o_autotextmode").equalsAscii("true");
                formulafont = getStatement(xContext, xModel, xGraph, "formatting_txt_formulafont");
                preferredunits = getStatement(xContext, xModel, xGraph, "formatting_txt_preferredunits");
                suppressunits = getStatement(xContext, xModel, xGraph, "formatting_o_suppress_units").equalsAscii("true");
                precision = getStatement(xContext, xModel, xGraph, "formatting_i_precision").toInt64();
                fixedpoint  = getStatement(xContext, xModel, xGraph, "formatting_o_fixedpoint").equalsAscii("true");
                highsclimit  = getStatement(xContext, xModel, xGraph, "formatting_i_highsclimit").toInt64();
                lowsclimit = getStatement(xContext, xModel, xGraph, "formatting_i_lowsclimit").toInt64();
                underflow = getStatement(xContext, xModel, xGraph, "internal_o_underflow").equalsAscii("true");
                realroots = getStatement(xContext, xModel, xGraph, "internal_o_evalf_real_roots").equalsAscii("true");
                implicitmul = getStatement(xContext, xModel, xGraph, "formatting_o_implicitmul").equalsAscii("true");
                showlabels = getStatement(xContext, xModel, xGraph, "formatting_o_showlabels").equalsAscii("true");
                echoformula = getStatement(xContext, xModel, xGraph, "miscellaneous_o_echoformula").equalsAscii("true");
            }
        }
        catch( uno::Exception& )
        {
        }
    }
    m_autoformatCheckbox->set_active(autoformat);
    m_autoalignCheckbox->set_active(autoalign);
    m_autochainCheckbox->set_active(autochain);
    m_autofractionCheckbox->set_active(autofraction);
    m_minimumtextsizeSpinbutton->set_value(mintextsize);
    m_autotextmodeCheckbox->set_active(autotextmode);
    m_formulafontCombobox->set_active_text(formulafont);
    m_preferredunitsEntry->set_text(preferredunits);
    m_suppressunitsCheckbox->set_active(suppressunits);
    m_precisionSpinbutton->set_value(precision);
    m_fixedpointCheckbox->set_active(fixedpoint);
    m_highsclimitSpinbutton->set_value(highsclimit);
    m_lowsclimitSpinbutton->set_value(lowsclimit);
    m_inhibitunderflowCheckbox->set_active(underflow);
    m_evalfrealrootsCheckbox->set_active(realroots);
    m_allowimplicitCheckbox->set_active(implicitmul);
    m_showlabelsCheckbox->set_active(showlabels);
    m_echoformulasCheckbox->set_active(echoformula);
}

bool SfxDocumentIMathPage::FillItemSet( SfxItemSet* )
{
    bool autoformat = m_autoformatCheckbox->get_active();
    bool autoalign  = m_autoalignCheckbox->get_active();
    bool autochain  = m_autochainCheckbox->get_active();
    bool autofraction = m_autofractionCheckbox->get_active();
    unsigned mintextsize = m_minimumtextsizeSpinbutton->get_value();
    bool autotextmode = m_autotextmodeCheckbox->get_active();
    OUString formulafont = m_formulafontCombobox->get_active_text();
    OUString preferredunits = m_preferredunitsEntry->get_text();
    bool suppressunits = m_suppressunitsCheckbox->get_active();
    int precision = m_precisionSpinbutton->get_value();
    bool fixedpoint = m_fixedpointCheckbox->get_active();
    int highsclimit = m_highsclimitSpinbutton->get_value();
    int lowsclimit = m_lowsclimitSpinbutton->get_value();
    bool underflow = m_inhibitunderflowCheckbox->get_active();
    bool realroots = m_evalfrealrootsCheckbox->get_active();
    bool implicitmul = m_allowimplicitCheckbox->get_active();
    bool showlabels = m_showlabelsCheckbox->get_active();
    bool echoformula = m_echoformulasCheckbox->get_active();

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
                updateStatement(xContext, xModel, xGraph, "formatting_o_autoformat", autoformat ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "formatting_o_autoalign",  autoalign  ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "formatting_o_autochain",  autochain  ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "formatting_o_autofraction", autofraction ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "formatting_i_minimumtextsize", OUString::number(mintextsize));
                updateStatement(xContext, xModel, xGraph, "formatting_o_autotextmode", autotextmode ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "formatting_txt_formulafont", formulafont);
                updateStatement(xContext, xModel, xGraph, "formatting_txt_preferredunits", preferredunits);
                updateStatement(xContext, xModel, xGraph, "formatting_o_suppress_units", suppressunits ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "formatting_i_precision", OUString::number(precision));
                updateStatement(xContext, xModel, xGraph, "formatting_o_fixedpoint", fixedpoint ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "formatting_i_highsclimit", OUString::number(highsclimit));
                updateStatement(xContext, xModel, xGraph, "formatting_i_lowsclimit", OUString::number(lowsclimit));
                updateStatement(xContext, xModel, xGraph, "internal_o_underflow", underflow ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "internal_o_evalf_real_roots", realroots ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "formatting_o_implicitmul", implicitmul ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "formatting_o_showlabels", showlabels ? OUString("true") : OUString("false"));
                updateStatement(xContext, xModel, xGraph, "miscellaneous_o_echoformula", echoformula ? OUString("true") : OUString("false"));
            }
        }
        catch( uno::Exception& )
        {
        }
    }
    return false;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
