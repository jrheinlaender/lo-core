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

#include <TransGradientStyle.hxx>

#include <com/sun/star/awt/Gradient2.hpp>

#include <comphelper/documentconstants.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>
#include <sal/log.hxx>
#include <sax/tools/converter.hxx>
#include <tools/color.hxx>
#include <xmloff/namespacemap.hxx>
#include <xmloff/xmlement.hxx>
#include <xmloff/xmlexp.hxx>
#include <xmloff/xmlimp.hxx>
#include <xmloff/xmlnamespace.hxx>
#include <xmloff/xmltkmap.hxx>
#include <xmloff/xmluconv.hxx>

using namespace ::com::sun::star;

using namespace ::xmloff::token;

SvXMLEnumMapEntry<awt::GradientStyle> const pXML_GradientStyle_Enum[] =
{
    { XML_LINEAR,                       awt::GradientStyle_LINEAR },
    { XML_GRADIENTSTYLE_AXIAL,          awt::GradientStyle_AXIAL },
    { XML_GRADIENTSTYLE_RADIAL,         awt::GradientStyle_RADIAL },
    { XML_GRADIENTSTYLE_ELLIPSOID,      awt::GradientStyle_ELLIPTICAL },
    { XML_GRADIENTSTYLE_SQUARE,         awt::GradientStyle_SQUARE },
    { XML_GRADIENTSTYLE_RECTANGULAR,    awt::GradientStyle_RECT },
    { XML_TOKEN_INVALID,                awt::GradientStyle(0) }
};

// Import

XMLTransGradientStyleImport::XMLTransGradientStyleImport( SvXMLImport& rImp )
    : rImport(rImp)
{
}

void XMLTransGradientStyleImport::importXML(
    const uno::Reference< xml::sax::XFastAttributeList >& xAttrList,
    uno::Any& rValue,
    OUString& rStrName )
{
    OUString aDisplayName;

    awt::Gradient2 aGradient;
    aGradient.XOffset = 0;
    aGradient.YOffset = 0;
    aGradient.StartIntensity = 100;
    aGradient.EndIntensity = 100;
    aGradient.Angle = 0;
    aGradient.Border = 0;

    for (auto &aIter : sax_fastparser::castToFastAttributeList( xAttrList ))
    {
        sal_Int32 nTmpValue;

        switch( aIter.getToken() )
        {
        case XML_ELEMENT(DRAW, XML_NAME):
            {
                rStrName = aIter.toString();
            }
            break;
        case XML_ELEMENT(DRAW, XML_DISPLAY_NAME):
            {
                aDisplayName = aIter.toString();
            }
            break;
        case XML_ELEMENT(DRAW, XML_STYLE):
            {
                SvXMLUnitConverter::convertEnum( aGradient.Style, aIter.toView(), pXML_GradientStyle_Enum );
            }
            break;
        case XML_ELEMENT(DRAW, XML_CX):
            ::sax::Converter::convertPercent( nTmpValue, aIter.toView() );
            aGradient.XOffset = sal::static_int_cast< sal_Int16 >(nTmpValue);
            break;
        case XML_ELEMENT(DRAW, XML_CY):
            ::sax::Converter::convertPercent( nTmpValue, aIter.toView() );
            aGradient.YOffset = sal::static_int_cast< sal_Int16 >(nTmpValue);
            break;
        case XML_ELEMENT(DRAW, XML_START):
            {
                sal_Int32 aStartTransparency;
                ::sax::Converter::convertPercent( aStartTransparency, aIter.toView() );

                sal_uInt8 n = sal::static_int_cast< sal_uInt8 >(
                    ( (100 - aStartTransparency) * 255 ) / 100 );

                Color aColor( n, n, n );
                aGradient.StartColor = static_cast<sal_Int32>( aColor );
            }
            break;
        case XML_ELEMENT(DRAW, XML_END):
            {
                sal_Int32 aEndTransparency;
                ::sax::Converter::convertPercent( aEndTransparency, aIter.toView() );

                sal_uInt8 n = sal::static_int_cast< sal_uInt8 >(
                    ( (100 - aEndTransparency) * 255 ) / 100 );

                Color aColor( n, n, n );
                aGradient.EndColor = static_cast<sal_Int32>( aColor );
            }
            break;
        case XML_ELEMENT(DRAW, XML_GRADIENT_ANGLE):
            {
                auto const cmp12(rImport.GetODFVersion().compareTo(ODFVER_012_TEXT));
                bool const bSuccess =
                    ::sax::Converter::convertAngle(aGradient.Angle, aIter.toView(),
                        // tdf#89475 try to detect borked OOo angles
                        (cmp12 < 0) || (cmp12 == 0
                            && (rImport.isGeneratorVersionOlderThan(SvXMLImport::AOO_4x, SvXMLImport::LO_7x)
                                // also for AOO 4.x, assume there won't ever be a 4.2
                                || rImport.getGeneratorVersion() == SvXMLImport::AOO_4x)));
                SAL_INFO_IF(!bSuccess, "xmloff.style", "failed to import draw:angle");
            }
            break;
        case XML_ELEMENT(DRAW, XML_BORDER):
            ::sax::Converter::convertPercent( nTmpValue, aIter.toView() );
            aGradient.Border = sal::static_int_cast< sal_Int16 >(nTmpValue);
            break;

        default:
            XMLOFF_WARN_UNKNOWN("xmloff.style", aIter);
        }
    }

    rValue <<= aGradient;

    if( !aDisplayName.isEmpty() )
    {
        rImport.AddStyleDisplayName( XmlStyleFamily::SD_GRADIENT_ID, rStrName,
                                     aDisplayName );
        rStrName = aDisplayName;
    }
}

// Export

XMLTransGradientStyleExport::XMLTransGradientStyleExport( SvXMLExport& rExp )
    : rExport(rExp)
{
}

void XMLTransGradientStyleExport::exportXML(
    const OUString& rStrName,
    const uno::Any& rValue )
{
    awt::Gradient2 aGradient;

    if( rStrName.isEmpty() )
        return;

    if( !(rValue >>= aGradient) )
        return;

    OUString aStrValue;
    OUStringBuffer aOut;

    // Style
    if( !SvXMLUnitConverter::convertEnum( aOut, aGradient.Style, pXML_GradientStyle_Enum ) )
        return;

    // Name
    bool bEncoded = false;
    rExport.AddAttribute( XML_NAMESPACE_DRAW, XML_NAME,
                          rExport.EncodeStyleName( rStrName,
                                                    &bEncoded ) );
    if( bEncoded )
        rExport.AddAttribute( XML_NAMESPACE_DRAW, XML_DISPLAY_NAME,
                                rStrName );

    aStrValue = aOut.makeStringAndClear();
    rExport.AddAttribute( XML_NAMESPACE_DRAW, XML_STYLE, aStrValue );

    // Center x/y
    if( aGradient.Style != awt::GradientStyle_LINEAR &&
        aGradient.Style != awt::GradientStyle_AXIAL   )
    {
        ::sax::Converter::convertPercent(aOut, aGradient.XOffset);
        aStrValue = aOut.makeStringAndClear();
        rExport.AddAttribute( XML_NAMESPACE_DRAW, XML_CX, aStrValue );

        ::sax::Converter::convertPercent(aOut, aGradient.YOffset);
        aStrValue = aOut.makeStringAndClear();
        rExport.AddAttribute( XML_NAMESPACE_DRAW, XML_CY, aStrValue );
    }

    // Transparency start
    Color aColor(ColorTransparency, aGradient.StartColor);
    sal_Int32 aStartValue = 100 - static_cast<sal_Int32>(((aColor.GetRed() + 1) * 100) / 255);
    ::sax::Converter::convertPercent( aOut, aStartValue );
    aStrValue = aOut.makeStringAndClear();
    rExport.AddAttribute( XML_NAMESPACE_DRAW, XML_START, aStrValue );

    // Transparency end
    aColor = Color(ColorTransparency, aGradient.EndColor);
    sal_Int32 aEndValue = 100 - static_cast<sal_Int32>(((aColor.GetRed() + 1) * 100) / 255);
    ::sax::Converter::convertPercent( aOut, aEndValue );
    aStrValue = aOut.makeStringAndClear();
    rExport.AddAttribute( XML_NAMESPACE_DRAW, XML_END, aStrValue );

    // Angle
    if( aGradient.Style != awt::GradientStyle_RADIAL )
    {
        ::sax::Converter::convertAngle(aOut, aGradient.Angle, rExport.getSaneDefaultVersion());
        aStrValue = aOut.makeStringAndClear();
        rExport.AddAttribute( XML_NAMESPACE_DRAW, XML_GRADIENT_ANGLE, aStrValue );
    }

    // Border
    ::sax::Converter::convertPercent( aOut, aGradient.Border );
    aStrValue = aOut.makeStringAndClear();
    rExport.AddAttribute( XML_NAMESPACE_DRAW, XML_BORDER, aStrValue );

    // ctor writes start tag. End-tag is written by destructor at block end.
    SvXMLElementExport rElem( rExport,
                              XML_NAMESPACE_DRAW, XML_OPACITY,
                              true, false );

    // Write child elements <loext:opacity-stop>
    // Do not export in standard ODF 1.3 or older.
    if ((rExport.getSaneDefaultVersion() & SvtSaveOptions::ODFSVER_EXTENDED) == 0)
        return;
    sal_Int32 nCount = aGradient.ColorStops.getLength();
    if (nCount == 0)
        return;
    double fPreviousOffset = 0.0;
    for (auto& aCandidate : aGradient.ColorStops)
    {
        // Attribute svg:offset. Make sure offsets are increasing.
        double fOffset = std::clamp<double>(aCandidate.StopOffset, 0.0, 1.0);
        if (fOffset < fPreviousOffset)
            fOffset = fPreviousOffset;
        rExport.AddAttribute(XML_NAMESPACE_SVG, XML_OFFSET, OUString::number(fOffset));
        fPreviousOffset = fOffset;

        // Attribute svg:stop-opacity, data type zeroToOneDecimal
        rendering::RGBColor aDecimalColor = aCandidate.StopColor;
        // transparency is encoded as gray, 1.0 corresponds to full transparent
        double fOpacity = std::clamp<double>(1.0 - aDecimalColor.Red, 0.0, 1.0);
        rExport.AddAttribute(XML_NAMESPACE_SVG, XML_STOP_OPACITY, OUString::number(fOpacity));

        // write opacity stop element
        SvXMLElementExport aStopElement(rExport, XML_NAMESPACE_LO_EXT, XML_OPACITY_STOP, true, true);
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
