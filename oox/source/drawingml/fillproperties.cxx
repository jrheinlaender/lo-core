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

#include <drawingml/fillproperties.hxx>

#include <iterator>

#include <comphelper/propertyvalue.hxx>
#include <drawingml/graphicproperties.hxx>
#include <vcl/graph.hxx>
#include <vcl/BitmapFilter.hxx>
#include <vcl/BitmapMonochromeFilter.hxx>
#include <docmodel/uno/UnoComplexColor.hxx>
#include <basegfx/utils/gradienttools.hxx>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/awt/Gradient2.hpp>
#include <com/sun/star/text/GraphicCrop.hpp>
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/drawing/BitmapMode.hpp>
#include <com/sun/star/drawing/ColorMode.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/RectanglePoint.hpp>
#include <com/sun/star/graphic/XGraphicTransformer.hpp>
#include <oox/helper/graphichelper.hxx>
#include <oox/drawingml/drawingmltypes.hxx>
#include <oox/drawingml/shapepropertymap.hxx>
#include <drawingml/hatchmap.hxx>
#include <oox/token/namespaces.hxx>
#include <oox/token/properties.hxx>
#include <oox/token/tokens.hxx>
#include <osl/diagnose.h>
#include <sal/log.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::drawing;
using namespace ::com::sun::star::graphic;

using ::com::sun::star::uno::Reference;
using ::com::sun::star::uno::Exception;
using ::com::sun::star::uno::UNO_QUERY_THROW;
using ::com::sun::star::geometry::IntegerRectangle2D;

namespace oox::drawingml {

namespace {

Reference< XGraphic > lclCheckAndApplyDuotoneTransform(const BlipFillProperties& aBlipProps, uno::Reference<graphic::XGraphic> const & xGraphic,
                                                       const GraphicHelper& rGraphicHelper, const ::Color nPhClr)
{
    if (aBlipProps.maDuotoneColors[0].isUsed() && aBlipProps.maDuotoneColors[1].isUsed())
    {
        ::Color nColor1 = aBlipProps.maDuotoneColors[0].getColor( rGraphicHelper, nPhClr );
        ::Color nColor2 = aBlipProps.maDuotoneColors[1].getColor( rGraphicHelper, nPhClr );

        uno::Reference<graphic::XGraphicTransformer> xTransformer(aBlipProps.mxFillGraphic, uno::UNO_QUERY);
        if (xTransformer.is())
            return xTransformer->applyDuotone(xGraphic, sal_Int32(nColor1), sal_Int32(nColor2));
    }
    return xGraphic;
}

Reference< XGraphic > lclRotateGraphic(uno::Reference<graphic::XGraphic> const & xGraphic, Degree10 nRotation)
{
    ::Graphic aGraphic(xGraphic);
    ::Graphic aReturnGraphic;

    assert (aGraphic.GetType() == GraphicType::Bitmap);

    BitmapEx aBitmapEx(aGraphic.GetBitmapEx());
    const ::Color& aColor = ::Color(0x00);
    aBitmapEx.Rotate(nRotation, aColor);
    aReturnGraphic = ::Graphic(aBitmapEx);
    aReturnGraphic.setOriginURL(aGraphic.getOriginURL());

    return aReturnGraphic.GetXGraphic();
}

using Quotients = std::tuple<double, double, double, double>;
Quotients getQuotients(geometry::IntegerRectangle2D aRelRect, double hDiv, double vDiv)
{
    return { aRelRect.X1 / hDiv, aRelRect.Y1 / vDiv, aRelRect.X2 / hDiv, aRelRect.Y2 / vDiv };
}

// ECMA-376 Part 1 20.1.8.55 srcRect (Source Rectangle)
std::optional<Quotients> CropQuotientsFromSrcRect(geometry::IntegerRectangle2D aSrcRect)
{
    aSrcRect.X1 = std::max(aSrcRect.X1, sal_Int32(0));
    aSrcRect.X2 = std::max(aSrcRect.X2, sal_Int32(0));
    aSrcRect.Y1 = std::max(aSrcRect.Y1, sal_Int32(0));
    aSrcRect.Y2 = std::max(aSrcRect.Y2, sal_Int32(0));
    if (aSrcRect.X1 + aSrcRect.X2 >= 100'000 || aSrcRect.Y1 + aSrcRect.Y2 >= 100'000)
        return {}; // Cropped everything
    return getQuotients(aSrcRect, 100'000.0, 100'000.0);
}

// ECMA-376 Part 1 20.1.8.30 fillRect (Fill Rectangle)
std::optional<Quotients> CropQuotientsFromFillRect(geometry::IntegerRectangle2D aFillRect)
{
    aFillRect.X1 = std::min(aFillRect.X1, sal_Int32(0));
    aFillRect.X2 = std::min(aFillRect.X2, sal_Int32(0));
    aFillRect.Y1 = std::min(aFillRect.Y1, sal_Int32(0));
    aFillRect.Y2 = std::min(aFillRect.Y2, sal_Int32(0));
    // Negative divisor and negative relative offset give positive value wanted in lclCropGraphic
    return getQuotients(aFillRect, -100'000.0 + aFillRect.X1 + aFillRect.X2,
                        -100'000.0 + aFillRect.Y1 + aFillRect.Y2);
}

// Crops a piece of the bitmap. lclCropGraphic doesn't handle growing.
Reference<XGraphic> lclCropGraphic(uno::Reference<graphic::XGraphic> const& xGraphic,
                                   std::optional<Quotients> quotients)
{
    ::Graphic aGraphic(xGraphic);
    assert (aGraphic.GetType() == GraphicType::Bitmap);

    BitmapEx aBitmapEx;
    if (quotients)
    {
        aBitmapEx = aGraphic.GetBitmapEx();

        const Size bmpSize = aBitmapEx.GetSizePixel();
        const auto& [qx1, qy1, qx2, qy2] = *quotients;
        const tools::Long l = std::round(bmpSize.Width() * qx1);
        const tools::Long t = std::round(bmpSize.Height() * qy1);
        const tools::Long r = std::round(bmpSize.Width() * qx2);
        const tools::Long b = std::round(bmpSize.Height() * qy2);

        aBitmapEx.Crop({ l, t, bmpSize.Width() - r - 1, bmpSize.Height() - b - 1 });
    }

    ::Graphic aReturnGraphic(aBitmapEx);
    aReturnGraphic.setOriginURL(aGraphic.getOriginURL());

    return aReturnGraphic.GetXGraphic();
}

Reference< XGraphic > lclMirrorGraphic(uno::Reference<graphic::XGraphic> const & xGraphic, bool bFlipH, bool bFlipV)
{
    ::Graphic aGraphic(xGraphic);
    ::Graphic aReturnGraphic;

    assert (aGraphic.GetType() == GraphicType::Bitmap);

    BitmapEx aBitmapEx(aGraphic.GetBitmapEx());
    BmpMirrorFlags nMirrorFlags = BmpMirrorFlags::NONE;

    if(bFlipH)
        nMirrorFlags |= BmpMirrorFlags::Horizontal;
    if(bFlipV)
        nMirrorFlags |= BmpMirrorFlags::Vertical;

    aBitmapEx.Mirror(nMirrorFlags);

    aReturnGraphic = ::Graphic(aBitmapEx);
    aReturnGraphic.setOriginURL(aGraphic.getOriginURL());

    return aReturnGraphic.GetXGraphic();
}

Reference< XGraphic > lclGreysScaleGraphic(uno::Reference<graphic::XGraphic> const & xGraphic)
{
    ::Graphic aGraphic(xGraphic);
    ::Graphic aReturnGraphic;

    assert (aGraphic.GetType() == GraphicType::Bitmap);

    BitmapEx aBitmapEx(aGraphic.GetBitmapEx());
    aBitmapEx.Convert(BmpConversion::N8BitGreys);

    aReturnGraphic = ::Graphic(aBitmapEx);
    aReturnGraphic.setOriginURL(aGraphic.getOriginURL());

    return aReturnGraphic.GetXGraphic();
}

/// Applies the graphic Black&White (Monochrome) effect with the imported threshold
Reference<XGraphic> lclApplyBlackWhiteEffect(const BlipFillProperties& aBlipProps,
                                             const uno::Reference<graphic::XGraphic>& xGraphic)
{
    const auto& oBiLevelThreshold = aBlipProps.moBiLevelThreshold;
    if (oBiLevelThreshold.has_value())
    {
        sal_uInt8 nThreshold
            = static_cast<sal_uInt8>(oBiLevelThreshold.value() * 255 / MAX_PERCENT);

        ::Graphic aGraphic(xGraphic);
        ::Graphic aReturnGraphic;

        BitmapEx aBitmapEx(aGraphic.GetBitmapEx());
        AlphaMask aMask(aBitmapEx.GetAlphaMask());

        BitmapEx aTmpBmpEx(aBitmapEx.GetBitmap());
        BitmapFilter::Filter(aTmpBmpEx, BitmapMonochromeFilter{ nThreshold });

        aReturnGraphic = ::Graphic(BitmapEx(aTmpBmpEx.GetBitmap(), aMask));
        aReturnGraphic.setOriginURL(aGraphic.getOriginURL());
        return aReturnGraphic.GetXGraphic();
    }
    return xGraphic;
}

Reference< XGraphic > lclCheckAndApplyChangeColorTransform(const BlipFillProperties &aBlipProps, uno::Reference<graphic::XGraphic> const & xGraphic,
                                                           const GraphicHelper& rGraphicHelper, const ::Color nPhClr)
{
    if( aBlipProps.maColorChangeFrom.isUsed() && aBlipProps.maColorChangeTo.isUsed() )
    {
        ::Color nFromColor = aBlipProps.maColorChangeFrom.getColor( rGraphicHelper, nPhClr );
        ::Color nToColor = aBlipProps.maColorChangeTo.getColor( rGraphicHelper, nPhClr );
        if ( (nFromColor != nToColor) || aBlipProps.maColorChangeTo.hasTransparency() )
        {
            sal_Int16 nToTransparence = aBlipProps.maColorChangeTo.getTransparency();
            sal_Int8 nToAlpha = static_cast< sal_Int8 >( (100 - nToTransparence) * 2.55 );

            sal_uInt8 nTolerance = 9;
            Graphic aGraphic{ xGraphic };
            if( aGraphic.IsGfxLink() )
            {
                // tdf#149670: Try to guess tolerance depending on image format
                switch (aGraphic.GetGfxLink().GetType())
                {
                    case GfxLinkType::NativeJpg:
                        nTolerance = 15;
                        break;
                    case GfxLinkType::NativePng:
                    case GfxLinkType::NativeTif:
                        nTolerance = 1;
                        break;
                    case GfxLinkType::NativeBmp:
                        nTolerance = 0;
                        break;
                    default:
                        break;
                }
            }

            uno::Reference<graphic::XGraphicTransformer> xTransformer(aBlipProps.mxFillGraphic, uno::UNO_QUERY);
            if (xTransformer.is())
                return xTransformer->colorChange(xGraphic, sal_Int32(nFromColor), nTolerance, sal_Int32(nToColor), nToAlpha);
        }
    }
    return xGraphic;
}

uno::Reference<graphic::XGraphic> applyBrightnessContrast(uno::Reference<graphic::XGraphic> const & xGraphic, sal_Int32 brightness, sal_Int32 contrast)
{
    uno::Reference<graphic::XGraphicTransformer> xTransformer(xGraphic, uno::UNO_QUERY);
    if (xTransformer.is())
        return xTransformer->applyBrightnessContrast(xGraphic, brightness, contrast, true);
    return xGraphic;
}

BitmapMode lclGetBitmapMode( sal_Int32 nToken )
{
    OSL_ASSERT((nToken & sal_Int32(0xFFFF0000))==0);
    switch( nToken )
    {
        case XML_tile:      return BitmapMode_REPEAT;
        case XML_stretch:   return BitmapMode_STRETCH;
    }

    // tdf#128596 Default value is XML_tile for MSO.
    return BitmapMode_REPEAT;
}

RectanglePoint lclGetRectanglePoint( sal_Int32 nToken )
{
    OSL_ASSERT((nToken & sal_Int32(0xFFFF0000))==0);
    switch( nToken )
    {
        case XML_tl:    return RectanglePoint_LEFT_TOP;
        case XML_t:     return RectanglePoint_MIDDLE_TOP;
        case XML_tr:    return RectanglePoint_RIGHT_TOP;
        case XML_l:     return RectanglePoint_LEFT_MIDDLE;
        case XML_ctr:   return RectanglePoint_MIDDLE_MIDDLE;
        case XML_r:     return RectanglePoint_RIGHT_MIDDLE;
        case XML_bl:    return RectanglePoint_LEFT_BOTTOM;
        case XML_b:     return RectanglePoint_MIDDLE_BOTTOM;
        case XML_br:    return RectanglePoint_RIGHT_BOTTOM;
    }
    return RectanglePoint_LEFT_TOP;
}

awt::Size lclGetOriginalSize( const GraphicHelper& rGraphicHelper, const Reference< XGraphic >& rxGraphic )
{
    awt::Size aSizeHmm( 0, 0 );
    try
    {
        Reference< beans::XPropertySet > xGraphicPropertySet( rxGraphic, UNO_QUERY_THROW );
        if( xGraphicPropertySet->getPropertyValue( "Size100thMM" ) >>= aSizeHmm )
        {
            if( !aSizeHmm.Width && !aSizeHmm.Height )
            {   // MAPMODE_PIXEL USED :-(
                awt::Size aSourceSizePixel( 0, 0 );
                if( xGraphicPropertySet->getPropertyValue( "SizePixel" ) >>= aSourceSizePixel )
                    aSizeHmm = rGraphicHelper.convertScreenPixelToHmm( aSourceSizePixel );
            }
        }
    }
    catch( Exception& )
    {
    }
    return aSizeHmm;
}

} // namespace

void GradientFillProperties::assignUsed( const GradientFillProperties& rSourceProps )
{
    if( !rSourceProps.maGradientStops.empty() )
        maGradientStops = rSourceProps.maGradientStops;
    assignIfUsed( moFillToRect, rSourceProps.moFillToRect );
    assignIfUsed( moTileRect, rSourceProps.moTileRect );
    assignIfUsed( moGradientPath, rSourceProps.moGradientPath );
    assignIfUsed( moShadeAngle, rSourceProps.moShadeAngle );
    assignIfUsed( moShadeFlip, rSourceProps.moShadeFlip );
    assignIfUsed( moShadeScaled, rSourceProps.moShadeScaled );
    assignIfUsed( moRotateWithShape, rSourceProps.moRotateWithShape );
}

void PatternFillProperties::assignUsed( const PatternFillProperties& rSourceProps )
{
    maPattFgColor.assignIfUsed( rSourceProps.maPattFgColor );
    maPattBgColor.assignIfUsed( rSourceProps.maPattBgColor );
    assignIfUsed( moPattPreset, rSourceProps.moPattPreset );
}

void BlipFillProperties::assignUsed( const BlipFillProperties& rSourceProps )
{
    if(rSourceProps.mxFillGraphic.is())
        mxFillGraphic = rSourceProps.mxFillGraphic;
    assignIfUsed( moBitmapMode, rSourceProps.moBitmapMode );
    assignIfUsed( moFillRect, rSourceProps.moFillRect );
    assignIfUsed( moTileOffsetX, rSourceProps.moTileOffsetX );
    assignIfUsed( moTileOffsetY, rSourceProps.moTileOffsetY );
    assignIfUsed( moTileScaleX, rSourceProps.moTileScaleX );
    assignIfUsed( moTileScaleY, rSourceProps.moTileScaleY );
    assignIfUsed( moTileAlign, rSourceProps.moTileAlign );
    assignIfUsed( moTileFlip, rSourceProps.moTileFlip );
    assignIfUsed( moRotateWithShape, rSourceProps.moRotateWithShape );
    assignIfUsed( moColorEffect, rSourceProps.moColorEffect );
    assignIfUsed( moBrightness, rSourceProps.moBrightness );
    assignIfUsed( moContrast, rSourceProps.moContrast );
    assignIfUsed( moBiLevelThreshold, rSourceProps.moBiLevelThreshold );
    maColorChangeFrom.assignIfUsed( rSourceProps.maColorChangeFrom );
    maColorChangeTo.assignIfUsed( rSourceProps.maColorChangeTo );
    maDuotoneColors[0].assignIfUsed( rSourceProps.maDuotoneColors[0] );
    maDuotoneColors[1].assignIfUsed( rSourceProps.maDuotoneColors[1] );
    maEffect.assignUsed( rSourceProps.maEffect );
    assignIfUsed(moAlphaModFix, rSourceProps.moAlphaModFix);
}

void FillProperties::assignUsed( const FillProperties& rSourceProps )
{
    assignIfUsed( moFillType, rSourceProps.moFillType );
    maFillColor.assignIfUsed( rSourceProps.maFillColor );
    assignIfUsed( moUseBgFill, rSourceProps.moUseBgFill );
    maGradientProps.assignUsed( rSourceProps.maGradientProps );
    maPatternProps.assignUsed( rSourceProps.maPatternProps );
    maBlipProps.assignUsed( rSourceProps.maBlipProps );
}

Color FillProperties::getBestSolidColor() const
{
    Color aSolidColor;
    if( moFillType.has_value() ) switch( moFillType.value() )
    {
        case XML_solidFill:
            aSolidColor = maFillColor;
        break;
        case XML_gradFill:
            if( !maGradientProps.maGradientStops.empty() )
            {
                GradientFillProperties::GradientStopMap::const_iterator aGradientStop =
                    maGradientProps.maGradientStops.begin();
                if (maGradientProps.maGradientStops.size() > 2)
                    ++aGradientStop;
                aSolidColor = aGradientStop->second;
            }
        break;
        case XML_pattFill:
            aSolidColor = maPatternProps.maPattBgColor.isUsed() ? maPatternProps.maPattBgColor : maPatternProps.maPattFgColor;
        break;
    }
    return aSolidColor;
}

void FillProperties::pushToPropMap(ShapePropertyMap& rPropMap, const GraphicHelper& rGraphicHelper,
                                   sal_Int32 nShapeRotation, ::Color nPhClr,
                                   const css::awt::Size& rSize, sal_Int16 nPhClrTheme, bool bFlipH,
                                   bool bFlipV, bool bIsCustomShape) const
{
    if( !moFillType.has_value() )
        return;

    FillStyle eFillStyle = FillStyle_NONE;
    OSL_ASSERT((moFillType.value() & sal_Int32(0xFFFF0000))==0);
    switch( moFillType.value() )
    {
        case XML_noFill:
        {
            eFillStyle = FillStyle_NONE;
            rPropMap.setProperty(ShapeProperty::FillUseSlideBackground, moUseBgFill.value_or(false));
        }
        break;

        case XML_solidFill:
            if( maFillColor.isUsed() )
            {
                ::Color aFillColor = maFillColor.getColor(rGraphicHelper, nPhClr);
                rPropMap.setProperty(ShapeProperty::FillColor, aFillColor);
                if( maFillColor.hasTransparency() )
                    rPropMap.setProperty( ShapeProperty::FillTransparency, maFillColor.getTransparency() );

                model::ComplexColor aComplexColor;
                if (aFillColor == nPhClr)
                {
                    aComplexColor.setSchemeColor(model::convertToThemeColorType(nPhClrTheme));
                    rPropMap.setProperty(PROP_FillComplexColor, model::color::createXComplexColor(aComplexColor));
                }
                else
                {
                    aComplexColor.setSchemeColor(model::convertToThemeColorType(maFillColor.getSchemeColorIndex()));
                    if (maFillColor.getLumMod() != 10000)
                        aComplexColor.addTransformation({model::TransformationType::LumMod, maFillColor.getLumMod()});
                    if (maFillColor.getLumOff() != 0)
                        aComplexColor.addTransformation({model::TransformationType::LumOff, maFillColor.getLumOff()});
                    if (maFillColor.getTintOrShade() > 0)
                        aComplexColor.addTransformation({model::TransformationType::Tint, maFillColor.getTintOrShade()});
                    if (maFillColor.getTintOrShade() < 0)
                    {
                        sal_Int16 nShade = o3tl::narrowing<sal_Int16>(-maFillColor.getTintOrShade());
                        aComplexColor.addTransformation({model::TransformationType::Shade, nShade});
                    }
                    rPropMap.setProperty(PROP_FillComplexColor, model::color::createXComplexColor(aComplexColor));
                }

                eFillStyle = FillStyle_SOLID;
            }
        break;

        case XML_gradFill:
            // do not create gradient struct if property is not supported...
            if( rPropMap.supportsProperty( ShapeProperty::FillGradient ) )
            {
                // use awt::Gradient2, prepare ColorStops
                awt::Gradient2 aGradient;
                assert(aGradient.ColorStops.get() && "cid#1524676 aGradient.ColorStops._pSequence won't be null here");
                basegfx::ColorStops aColorStops;
                basegfx::ColorStops aTransparencyStops;
                bool bContainsTransparency(false);

                // set defaults
                aGradient.Angle = 900;
                aGradient.StartIntensity = 100;
                aGradient.EndIntensity = 100;
                aGradient.Style = awt::GradientStyle_LINEAR;

                // convert to ColorStops, check for contained transparency
                for (const auto& rCandidate : maGradientProps.maGradientStops)
                {
                    const ::Color aColor(rCandidate.second.getColor(rGraphicHelper, nPhClr));
                    aColorStops.emplace_back(rCandidate.first, aColor.getBColor());
                    bContainsTransparency = bContainsTransparency || rCandidate.second.hasTransparency();
                }

                // if we have transparency, convert to ColorStops
                if (bContainsTransparency)
                {
                    for (const auto& rCandidate : maGradientProps.maGradientStops)
                    {
                        const double fTrans(rCandidate.second.getTransparency() * (1.0/100.0));
                        aTransparencyStops.emplace_back(rCandidate.first, basegfx::BColor(fTrans, fTrans, fTrans));
                    }
                }

                // "rotate with shape" set to false -> do not rotate
                if (!maGradientProps.moRotateWithShape.value_or(true))
                {
                    nShapeRotation = 0;
                }

                if (maGradientProps.moGradientPath.has_value())
                {
                    IntegerRectangle2D aFillToRect = maGradientProps.moFillToRect.value_or( IntegerRectangle2D( 0, 0, MAX_PERCENT, MAX_PERCENT ) );
                    sal_Int32 nCenterX = (MAX_PERCENT + aFillToRect.X1 - aFillToRect.X2) / 2;
                    aGradient.XOffset = getLimitedValue<sal_Int16, sal_Int32>(
                        nCenterX / PER_PERCENT, 0, 100);
                    sal_Int32 nCenterY = (MAX_PERCENT + aFillToRect.Y1 - aFillToRect.Y2) / 2;
                    aGradient.YOffset = getLimitedValue<sal_Int16, sal_Int32>(
                        nCenterY / PER_PERCENT, 0, 100);

                    if( maGradientProps.moGradientPath.value() == XML_circle )
                    {
                        // Style should be radial at least when the horizontal center is at 50%.
                        // Otherwise import as a linear gradient, because it is the most similar to the MSO radial style.
                        // aGradient.Style = awt::GradientStyle_LINEAR;
                        if( aGradient.XOffset == 100 && aGradient.YOffset == 100 )
                            aGradient.Angle = 450;
                        else if( aGradient.XOffset == 0 && aGradient.YOffset == 100 )
                            aGradient.Angle = 3150;
                        else if( aGradient.XOffset == 100 && aGradient.YOffset == 0 )
                            aGradient.Angle = 1350;
                        else if( aGradient.XOffset == 0 && aGradient.YOffset == 0 )
                            aGradient.Angle = 2250;
                        else
                            aGradient.Style = awt::GradientStyle_RADIAL;
                    }
                    else
                    {
                        aGradient.Style = awt::GradientStyle_RECT;
                    }

                    basegfx::utils::reverseColorStops(aColorStops);
                    basegfx::utils::reverseColorStops(aTransparencyStops);
                }
                else if (!maGradientProps.maGradientStops.empty())
                {
                    // aGradient.Style = awt::GradientStyle_LINEAR;
                    sal_Int32 nShadeAngle = maGradientProps.moShadeAngle.value_or( 0 );
                    // Adjust for flips
                    if ( bFlipH )
                        nShadeAngle = 180*60000 - nShadeAngle;
                    if ( bFlipV )
                        nShadeAngle = -nShadeAngle;
                    const sal_Int32 nDmlAngle = nShadeAngle + nShapeRotation;
                    // convert DrawingML angle (in 1/60000 degrees) to API angle (in 1/10 degrees)
                    aGradient.Angle = static_cast< sal_Int16 >( (8100 - (nDmlAngle / (PER_DEGREE / 10))) % 3600 );
                }

                // set ColorStops using UNO API
                basegfx::utils::fillColorStopSequenceFromColorStops(aGradient.ColorStops, aColorStops);

                // for compatibility, still set StartColor/EndColor
                // NOTE: All code after adapting to multi color gradients works
                //       using the ColorSteps, so in principle Start/EndColor might
                //       be either
                //        (a) ignored consequently everywhere or
                //        (b) be set/added consequently everywhere
                //       since this is - in principle - redundant data.
                //       Be aware that e.g. cases like DrawingML::EqualGradients
                //       and others would have to be identified and adapted (!)
                //       Since awt::Gradient2 is UNO API data there might
                //       be cases where just awt::Gradient is transferred, so (b)
                //       is far better backwards compatible and thus more safe, so
                //       all changes will make use of additionally using/setting
                //       these additionally, but will only make use of the given
                //       ColorSteps if these are not empty, assuming that these
                //       already contain Start/EndColor.
                //       In principle that redundancy and that it is conflict-free
                //       could even be checked and asserted, but consequently using
                //       (b) methodically should be safe.
                aGradient.StartColor = static_cast<sal_Int32>(::Color(aColorStops.front().getStopColor()));
                aGradient.EndColor = static_cast<sal_Int32>(::Color(aColorStops.back().getStopColor()));

                // push gradient or named gradient to property map
                if (rPropMap.setProperty(ShapeProperty::FillGradient, aGradient))
                {
                    eFillStyle = FillStyle_GRADIENT;
                }

                // push gradient transparency to property map if it exists
                if (!aTransparencyStops.empty())
                {
                    basegfx::utils::fillColorStopSequenceFromColorStops(aGradient.ColorStops, aTransparencyStops);
                    rPropMap.setProperty(ShapeProperty::GradientTransparency, aGradient);
                }
            }
        break;

        case XML_blipFill:
            // do not start complex graphic transformation if property is not supported...
            if (maBlipProps.mxFillGraphic.is() && rPropMap.supportsProperty(ShapeProperty::FillBitmap))
            {
                uno::Reference<graphic::XGraphic> xGraphic = lclCheckAndApplyDuotoneTransform(maBlipProps, maBlipProps.mxFillGraphic, rGraphicHelper, nPhClr);
                // TODO: "rotate with shape" is not possible with our current core

                if (xGraphic.is())
                {
                    if (maBlipProps.moColorEffect.value_or(XML_TOKEN_INVALID) == XML_grayscl)
                        xGraphic = lclGreysScaleGraphic(xGraphic);

                    if (rPropMap.supportsProperty(ShapeProperty::FillBitmapName) &&
                        rPropMap.setProperty(ShapeProperty::FillBitmapName, xGraphic))
                    {
                        eFillStyle = FillStyle_BITMAP;
                    }
                    else if (rPropMap.setProperty(ShapeProperty::FillBitmap, xGraphic))
                    {
                        eFillStyle = FillStyle_BITMAP;
                    }
                }

                // set other bitmap properties, if bitmap has been inserted into the map
                if( eFillStyle == FillStyle_BITMAP )
                {
                    // bitmap mode (single, repeat, stretch)
                    BitmapMode eBitmapMode = lclGetBitmapMode( maBlipProps.moBitmapMode.value_or( XML_TOKEN_INVALID ) );

                    // additional settings for repeated bitmap
                    if( eBitmapMode == BitmapMode_REPEAT )
                    {
                        // anchor position inside bitmap
                        RectanglePoint eRectPoint = lclGetRectanglePoint( maBlipProps.moTileAlign.value_or( XML_tl ) );
                        rPropMap.setProperty( ShapeProperty::FillBitmapRectanglePoint, eRectPoint );

                        awt::Size aOriginalSize = lclGetOriginalSize(rGraphicHelper, maBlipProps.mxFillGraphic);
                        if( (aOriginalSize.Width > 0) && (aOriginalSize.Height > 0) )
                        {
                            // size of one bitmap tile (given as 1/1000 percent of bitmap size), convert to 1/100 mm
                            double fScaleX = maBlipProps.moTileScaleX.value_or( MAX_PERCENT ) / static_cast< double >( MAX_PERCENT );
                            sal_Int32 nFillBmpSizeX = getLimitedValue< sal_Int32, double >( aOriginalSize.Width * fScaleX, 1, SAL_MAX_INT32 );
                            rPropMap.setProperty( ShapeProperty::FillBitmapSizeX, nFillBmpSizeX );
                            double fScaleY = maBlipProps.moTileScaleY.value_or( MAX_PERCENT ) / static_cast< double >( MAX_PERCENT );
                            sal_Int32 nFillBmpSizeY = getLimitedValue< sal_Int32, double >( aOriginalSize.Height * fScaleY, 1, SAL_MAX_INT32 );
                            rPropMap.setProperty( ShapeProperty::FillBitmapSizeY, nFillBmpSizeY );

                            awt::Size aBmpSize(nFillBmpSizeX, nFillBmpSizeY);
                            // offset of the first bitmap tile (given as EMUs), convert to percent
                            sal_Int16 nTileOffsetX = getDoubleIntervalValue< sal_Int16 >(std::round(maBlipProps.moTileOffsetX.value_or( 0 ) / 3.6 / aBmpSize.Width), 0, 100 );
                            rPropMap.setProperty( ShapeProperty::FillBitmapOffsetX, nTileOffsetX );
                            sal_Int16 nTileOffsetY = getDoubleIntervalValue< sal_Int16 >(std::round(maBlipProps.moTileOffsetY.value_or( 0 ) / 3.6 / aBmpSize.Height), 0, 100 );
                            rPropMap.setProperty( ShapeProperty::FillBitmapOffsetY, nTileOffsetY );
                        }
                    }
                    else if ( eBitmapMode == BitmapMode_STRETCH && maBlipProps.moFillRect.has_value() )
                    {
                        geometry::IntegerRectangle2D aFillRect( maBlipProps.moFillRect.value() );
                        awt::Size aOriginalSize( rGraphicHelper.getOriginalSize( xGraphic ) );
                        if ( aOriginalSize.Width && aOriginalSize.Height )
                        {
                            text::GraphicCrop aGraphCrop( 0, 0, 0, 0 );
                            if ( aFillRect.X1 )
                                aGraphCrop.Left = static_cast< sal_Int32 >( ( static_cast< double >( aOriginalSize.Width ) * aFillRect.X1 ) / 100000 );
                            if ( aFillRect.Y1 )
                                aGraphCrop.Top = static_cast< sal_Int32 >( ( static_cast< double >( aOriginalSize.Height ) * aFillRect.Y1 ) / 100000 );
                            if ( aFillRect.X2 )
                                aGraphCrop.Right = static_cast< sal_Int32 >( ( static_cast< double >( aOriginalSize.Width ) * aFillRect.X2 ) / 100000 );
                            if ( aFillRect.Y2 )
                                aGraphCrop.Bottom = static_cast< sal_Int32 >( ( static_cast< double >( aOriginalSize.Height ) * aFillRect.Y2 ) / 100000 );

                            bool bHasCropValues = aGraphCrop.Left != 0 || aGraphCrop.Right !=0 || aGraphCrop.Top != 0 || aGraphCrop.Bottom != 0;
                            // Negative GraphicCrop values means "crop" here.
                            bool bNeedCrop = aGraphCrop.Left <= 0 && aGraphCrop.Right <= 0 && aGraphCrop.Top <= 0 && aGraphCrop.Bottom <= 0;

                            if (bHasCropValues)
                            {
                                if (bIsCustomShape && bNeedCrop)
                                {
                                    // Physically crop the image
                                    // In this case, don't set the PROP_GraphicCrop because that
                                    // would lead to applying the crop twice after roundtrip
                                    xGraphic = lclCropGraphic(xGraphic, CropQuotientsFromFillRect(aFillRect));
                                    if (rPropMap.supportsProperty(ShapeProperty::FillBitmapName))
                                        rPropMap.setProperty(ShapeProperty::FillBitmapName, xGraphic);
                                    else
                                        rPropMap.setProperty(ShapeProperty::FillBitmap, xGraphic);
                                }
                                else if ((aFillRect.X1 != 0 && aFillRect.X2 != 0
                                          && aFillRect.X1 != aFillRect.X2)
                                         || (aFillRect.Y1 != 0 && aFillRect.Y2 != 0
                                             && aFillRect.Y1 != aFillRect.Y2))
                                {
                                    rPropMap.setProperty(PROP_GraphicCrop, aGraphCrop);
                                }
                                else
                                {
                                    double nL = aFillRect.X1 / static_cast<double>(MAX_PERCENT);
                                    double nT = aFillRect.Y1 / static_cast<double>(MAX_PERCENT);
                                    double nR = aFillRect.X2 / static_cast<double>(MAX_PERCENT);
                                    double nB = aFillRect.Y2 / static_cast<double>(MAX_PERCENT);

                                    sal_Int32 nSizeX;
                                    if (nL || nR)
                                        nSizeX = rSize.Width * (1 - (nL + nR));
                                    else
                                        nSizeX = rSize.Width;
                                    rPropMap.setProperty(ShapeProperty::FillBitmapSizeX, nSizeX);

                                    sal_Int32 nSizeY;
                                    if (nT || nB)
                                        nSizeY = rSize.Height * (1 - (nT + nB));
                                    else
                                        nSizeY = rSize.Height;
                                    rPropMap.setProperty(ShapeProperty::FillBitmapSizeY, nSizeY);

                                    RectanglePoint eRectPoint;
                                    if (!aFillRect.X1 && aFillRect.X2)
                                    {
                                        if (!aFillRect.Y1 && aFillRect.Y2)
                                            eRectPoint = lclGetRectanglePoint(XML_tl);
                                        else if (aFillRect.Y1 && !aFillRect.Y2)
                                            eRectPoint = lclGetRectanglePoint(XML_bl);
                                        else
                                            eRectPoint = lclGetRectanglePoint(XML_l);
                                    }
                                    else if (aFillRect.X1 && !aFillRect.X2)
                                    {
                                        if (!aFillRect.Y1 && aFillRect.Y2)
                                            eRectPoint = lclGetRectanglePoint(XML_tr);
                                        else if (aFillRect.Y1 && !aFillRect.Y2)
                                            eRectPoint = lclGetRectanglePoint(XML_br);
                                        else
                                            eRectPoint = lclGetRectanglePoint(XML_r);
                                    }
                                    else
                                    {
                                        if (!aFillRect.Y1 && aFillRect.Y2)
                                            eRectPoint = lclGetRectanglePoint(XML_t);
                                        else if (aFillRect.Y1 && !aFillRect.Y2)
                                            eRectPoint = lclGetRectanglePoint(XML_b);
                                        else
                                            eRectPoint = lclGetRectanglePoint(XML_ctr);
                                    }
                                    rPropMap.setProperty(ShapeProperty::FillBitmapRectanglePoint, eRectPoint);
                                    eBitmapMode = BitmapMode_NO_REPEAT;
                                }
                            }
                        }
                    }
                    rPropMap.setProperty(ShapeProperty::FillBitmapMode, eBitmapMode);
                }

                if (maBlipProps.moAlphaModFix.has_value())
                    rPropMap.setProperty(ShapeProperty::FillTransparency, static_cast<sal_Int16>(100 - (maBlipProps.moAlphaModFix.value() / PER_PERCENT)));
            }
        break;

        case XML_pattFill:
        {
            if( rPropMap.supportsProperty( ShapeProperty::FillHatch ) )
            {
                Color aColor( maPatternProps.maPattFgColor );
                if( aColor.isUsed() && maPatternProps.moPattPreset.has_value() )
                {
                    eFillStyle = FillStyle_HATCH;
                    rPropMap.setProperty( ShapeProperty::FillHatch, createHatch( maPatternProps.moPattPreset.value(), aColor.getColor( rGraphicHelper, nPhClr ) ) );
                    if( aColor.hasTransparency() )
                        rPropMap.setProperty( ShapeProperty::FillTransparency, aColor.getTransparency() );

                    // Set background color for hatch
                    if(maPatternProps.maPattBgColor.isUsed())
                    {
                        aColor = maPatternProps.maPattBgColor;
                        rPropMap.setProperty( ShapeProperty::FillBackground, aColor.getTransparency() != 100 );
                        rPropMap.setProperty( ShapeProperty::FillColor, aColor.getColor( rGraphicHelper, nPhClr ) );
                    }
                }
                else if ( maPatternProps.maPattBgColor.isUsed() )
                {
                    aColor = maPatternProps.maPattBgColor;
                    rPropMap.setProperty( ShapeProperty::FillColor, aColor.getColor( rGraphicHelper, nPhClr ) );
                    if( aColor.hasTransparency() )
                        rPropMap.setProperty( ShapeProperty::FillTransparency, aColor.getTransparency() );
                    eFillStyle = FillStyle_SOLID;
                }
            }
        }
        break;

        case XML_grpFill:
            // todo
            eFillStyle = FillStyle_NONE;
        break;
    }

    // set final fill style property
    rPropMap.setProperty( ShapeProperty::FillStyle, eFillStyle );
}

void GraphicProperties::pushToPropMap( PropertyMap& rPropMap, const GraphicHelper& rGraphicHelper, bool bFlipH, bool bFlipV) const
{
    sal_Int16 nBrightness = getLimitedValue< sal_Int16, sal_Int32 >( maBlipProps.moBrightness.value_or( 0 ) / PER_PERCENT, -100, 100 );
    sal_Int16 nContrast = getLimitedValue< sal_Int16, sal_Int32 >( maBlipProps.moContrast.value_or( 0 ) / PER_PERCENT, -100, 100 );
    ColorMode eColorMode = ColorMode_STANDARD;

    switch( maBlipProps.moColorEffect.value_or( XML_TOKEN_INVALID ) )
    {
        case XML_biLevel:   eColorMode = ColorMode_MONO;    break;
        case XML_grayscl:   eColorMode = ColorMode_GREYS;   break;
    }

    if (maBlipProps.mxFillGraphic.is())
    {
        // created transformed graphic
        uno::Reference<graphic::XGraphic> xGraphic = lclCheckAndApplyChangeColorTransform(maBlipProps, maBlipProps.mxFillGraphic, rGraphicHelper, API_RGB_TRANSPARENT);
        xGraphic = lclCheckAndApplyDuotoneTransform(maBlipProps, xGraphic, rGraphicHelper, API_RGB_TRANSPARENT);

        if( eColorMode == ColorMode_MONO )
        {
            // ColorMode_MONO is the same with MSO's biLevel with 50000 (50%) threshold,
            // when threshold isn't 50000 bake the effect instead.
            if( maBlipProps.moBiLevelThreshold != 50000 )
            {
                xGraphic = lclApplyBlackWhiteEffect(maBlipProps, xGraphic);
                eColorMode = ColorMode_STANDARD;
            }
        }

        if (eColorMode == ColorMode_STANDARD && nBrightness == 70 && nContrast == -70)
        {
            // map MSO 'washout' to our Watermark colormode
            eColorMode = ColorMode_WATERMARK;
            nBrightness = 0;
            nContrast = 0;
        }
        else if( nBrightness != 0 && nContrast != 0 )
        {
            // MSO uses a different algorithm for contrast+brightness, LO applies contrast before brightness,
            // while MSO apparently applies half of brightness before contrast and half after. So if only
            // contrast or brightness need to be altered, the result is the same, but if both are involved,
            // there's no way to map that, so just force a conversion of the image.
            xGraphic = applyBrightnessContrast( xGraphic, nBrightness, nContrast );
            nBrightness = 0;
            nContrast = 0;
        }

        // cropping
        if ( maBlipProps.moClipRect.has_value() )
        {
            geometry::IntegerRectangle2D oClipRect( maBlipProps.moClipRect.value() );
            awt::Size aOriginalSize( rGraphicHelper.getOriginalSize( xGraphic ) );
            if ( aOriginalSize.Width && aOriginalSize.Height )
            {
                text::GraphicCrop aGraphCrop( 0, 0, 0, 0 );
                if ( oClipRect.X1 )
                    aGraphCrop.Left = rtl::math::round( ( static_cast< double >( aOriginalSize.Width ) * oClipRect.X1 ) / 100000 );
                if ( oClipRect.Y1 )
                    aGraphCrop.Top = rtl::math::round( ( static_cast< double >( aOriginalSize.Height ) * oClipRect.Y1 ) / 100000 );
                if ( oClipRect.X2 )
                    aGraphCrop.Right = rtl::math::round( ( static_cast< double >( aOriginalSize.Width ) * oClipRect.X2 ) / 100000 );
                if ( oClipRect.Y2 )
                    aGraphCrop.Bottom = rtl::math::round( ( static_cast< double >( aOriginalSize.Height ) * oClipRect.Y2 ) / 100000 );
                rPropMap.setProperty(PROP_GraphicCrop, aGraphCrop);

                bool bHasCropValues = aGraphCrop.Left != 0 || aGraphCrop.Right !=0 || aGraphCrop.Top != 0 || aGraphCrop.Bottom != 0;
                // Positive GraphicCrop values means "crop" here.
                bool bNeedCrop = aGraphCrop.Left >= 0 && aGraphCrop.Right >= 0 && aGraphCrop.Top >= 0 && aGraphCrop.Bottom >= 0;

                if(mbIsCustomShape && bHasCropValues && bNeedCrop)
                {
                    xGraphic = lclCropGraphic(xGraphic, CropQuotientsFromSrcRect(oClipRect));
                }
            }
        }

        if(mbIsCustomShape)
        {
            // it is a cropped graphic.
            rPropMap.setProperty(PROP_FillStyle, FillStyle_BITMAP);
            rPropMap.setProperty(PROP_FillBitmapMode, BitmapMode_STRETCH);

            // It is a bitmap filled and rotated graphic.
            // When custom shape is rotated, bitmap have to be rotated too.
            if(rPropMap.hasProperty(PROP_RotateAngle))
            {
                tools::Long nAngle = rPropMap.getProperty(PROP_RotateAngle).get<tools::Long>();
                xGraphic = lclRotateGraphic(xGraphic, Degree10(nAngle/10) );
            }

            // We have not core feature that flips graphic in the shape.
            // Here we are applying flip property to bitmap directly.
            if(bFlipH || bFlipV)
                xGraphic = lclMirrorGraphic(xGraphic, bFlipH, bFlipV );

            if(eColorMode == ColorMode_GREYS)
                xGraphic = lclGreysScaleGraphic( xGraphic );

            rPropMap.setProperty(PROP_FillBitmap, xGraphic);
        }
        else
            rPropMap.setProperty(PROP_Graphic, xGraphic);


        if ( maBlipProps.moAlphaModFix.has_value() )
        {
            rPropMap.setProperty(PROP_Transparency, static_cast<sal_Int16>(100 - (maBlipProps.moAlphaModFix.value() / PER_PERCENT)));
        }
    }
    rPropMap.setProperty(PROP_GraphicColorMode, eColorMode);

    // brightness and contrast
    if( nBrightness != 0 )
        rPropMap.setProperty(PROP_AdjustLuminance, nBrightness);
    if( nContrast != 0 )
        rPropMap.setProperty(PROP_AdjustContrast, nContrast);

    // Media content
    if (!m_sMediaPackageURL.isEmpty())
    {
        rPropMap.setProperty(PROP_MediaURL, m_sMediaPackageURL);
        if (m_xMediaStream.is())
            rPropMap.setProperty(PROP_PrivateStream, m_xMediaStream);
    }
}

bool ArtisticEffectProperties::isEmpty() const
{
    return msName.isEmpty();
}

css::beans::PropertyValue ArtisticEffectProperties::getEffect()
{
    css::beans::PropertyValue aRet;
    if( msName.isEmpty() )
        return aRet;

    css::uno::Sequence< css::beans::PropertyValue > aSeq( maAttribs.size() + 1 );
    auto pSeq = aSeq.getArray();
    sal_uInt32 i = 0;
    for (auto const& attrib : maAttribs)
    {
        pSeq[i].Name = attrib.first;
        pSeq[i].Value = attrib.second;
        i++;
    }

    if( mrOleObjectInfo.maEmbeddedData.hasElements() )
    {
        css::uno::Sequence< css::beans::PropertyValue > aGraphicSeq{
            comphelper::makePropertyValue("Id", mrOleObjectInfo.maProgId),
            comphelper::makePropertyValue("Data", mrOleObjectInfo.maEmbeddedData)
        };

        pSeq[i].Name = "OriginalGraphic";
        pSeq[i].Value <<= aGraphicSeq;
    }

    aRet.Name = msName;
    aRet.Value <<= aSeq;

    return aRet;
}

void ArtisticEffectProperties::assignUsed( const ArtisticEffectProperties& rSourceProps )
{
    if( !rSourceProps.isEmpty() )
    {
        msName = rSourceProps.msName;
        maAttribs = rSourceProps.maAttribs;
    }
}

OUString ArtisticEffectProperties::getEffectString( sal_Int32 nToken )
{
    switch( nToken )
    {
        // effects
        case OOX_TOKEN( a14, artisticBlur ):                return "artisticBlur";
        case OOX_TOKEN( a14, artisticCement ):              return "artisticCement";
        case OOX_TOKEN( a14, artisticChalkSketch ):         return "artisticChalkSketch";
        case OOX_TOKEN( a14, artisticCrisscrossEtching ):   return "artisticCrisscrossEtching";
        case OOX_TOKEN( a14, artisticCutout ):              return "artisticCutout";
        case OOX_TOKEN( a14, artisticFilmGrain ):           return "artisticFilmGrain";
        case OOX_TOKEN( a14, artisticGlass ):               return "artisticGlass";
        case OOX_TOKEN( a14, artisticGlowDiffused ):        return "artisticGlowDiffused";
        case OOX_TOKEN( a14, artisticGlowEdges ):           return "artisticGlowEdges";
        case OOX_TOKEN( a14, artisticLightScreen ):         return "artisticLightScreen";
        case OOX_TOKEN( a14, artisticLineDrawing ):         return "artisticLineDrawing";
        case OOX_TOKEN( a14, artisticMarker ):              return "artisticMarker";
        case OOX_TOKEN( a14, artisticMosiaicBubbles ):      return "artisticMosiaicBubbles";
        case OOX_TOKEN( a14, artisticPaintStrokes ):        return "artisticPaintStrokes";
        case OOX_TOKEN( a14, artisticPaintBrush ):          return "artisticPaintBrush";
        case OOX_TOKEN( a14, artisticPastelsSmooth ):       return "artisticPastelsSmooth";
        case OOX_TOKEN( a14, artisticPencilGrayscale ):     return "artisticPencilGrayscale";
        case OOX_TOKEN( a14, artisticPencilSketch ):        return "artisticPencilSketch";
        case OOX_TOKEN( a14, artisticPhotocopy ):           return "artisticPhotocopy";
        case OOX_TOKEN( a14, artisticPlasticWrap ):         return "artisticPlasticWrap";
        case OOX_TOKEN( a14, artisticTexturizer ):          return "artisticTexturizer";
        case OOX_TOKEN( a14, artisticWatercolorSponge ):    return "artisticWatercolorSponge";
        case OOX_TOKEN( a14, brightnessContrast ):          return "brightnessContrast";
        case OOX_TOKEN( a14, colorTemperature ):            return "colorTemperature";
        case OOX_TOKEN( a14, saturation ):                  return "saturation";
        case OOX_TOKEN( a14, sharpenSoften ):               return "sharpenSoften";

        // attributes
        case XML_visible:           return "visible";
        case XML_trans:             return "trans";
        case XML_crackSpacing:      return "crackSpacing";
        case XML_pressure:          return "pressure";
        case XML_numberOfShades:    return "numberOfShades";
        case XML_grainSize:         return "grainSize";
        case XML_intensity:         return "intensity";
        case XML_smoothness:        return "smoothness";
        case XML_gridSize:          return "gridSize";
        case XML_pencilSize:        return "pencilSize";
        case XML_size:              return "size";
        case XML_brushSize:         return "brushSize";
        case XML_scaling:           return "scaling";
        case XML_detail:            return "detail";
        case XML_bright:            return "bright";
        case XML_contrast:          return "contrast";
        case XML_colorTemp:         return "colorTemp";
        case XML_sat:               return "sat";
        case XML_amount:            return "amount";
    }
    SAL_WARN( "oox.drawingml", "ArtisticEffectProperties::getEffectString: unexpected token " << nToken );
    return OUString();
}

sal_Int32 ArtisticEffectProperties::getEffectToken( const OUString& sName )
{
    // effects
    if( sName == "artisticBlur" )
        return XML_artisticBlur;
    else if( sName == "artisticCement" )
        return XML_artisticCement;
    else if( sName == "artisticChalkSketch" )
        return XML_artisticChalkSketch;
    else if( sName == "artisticCrisscrossEtching" )
        return XML_artisticCrisscrossEtching;
    else if( sName == "artisticCutout" )
        return XML_artisticCutout;
    else if( sName == "artisticFilmGrain" )
        return XML_artisticFilmGrain;
    else if( sName == "artisticGlass" )
        return XML_artisticGlass;
    else if( sName == "artisticGlowDiffused" )
        return XML_artisticGlowDiffused;
    else if( sName == "artisticGlowEdges" )
        return XML_artisticGlowEdges;
    else if( sName == "artisticLightScreen" )
        return XML_artisticLightScreen;
    else if( sName == "artisticLineDrawing" )
        return XML_artisticLineDrawing;
    else if( sName == "artisticMarker" )
        return XML_artisticMarker;
    else if( sName == "artisticMosiaicBubbles" )
        return XML_artisticMosiaicBubbles;
    else if( sName == "artisticPaintStrokes" )
        return XML_artisticPaintStrokes;
    else if( sName == "artisticPaintBrush" )
        return XML_artisticPaintBrush;
    else if( sName == "artisticPastelsSmooth" )
        return XML_artisticPastelsSmooth;
    else if( sName == "artisticPencilGrayscale" )
        return XML_artisticPencilGrayscale;
    else if( sName == "artisticPencilSketch" )
        return XML_artisticPencilSketch;
    else if( sName == "artisticPhotocopy" )
        return XML_artisticPhotocopy;
    else if( sName == "artisticPlasticWrap" )
        return XML_artisticPlasticWrap;
    else if( sName == "artisticTexturizer" )
        return XML_artisticTexturizer;
    else if( sName == "artisticWatercolorSponge" )
        return XML_artisticWatercolorSponge;
    else if( sName == "brightnessContrast" )
        return XML_brightnessContrast;
    else if( sName == "colorTemperature" )
        return XML_colorTemperature;
    else if( sName == "saturation" )
        return XML_saturation;
    else if( sName == "sharpenSoften" )
        return XML_sharpenSoften;

    // attributes
    else if( sName == "visible" )
        return XML_visible;
    else if( sName == "trans" )
        return XML_trans;
    else if( sName == "crackSpacing" )
        return XML_crackSpacing;
    else if( sName == "pressure" )
        return XML_pressure;
    else if( sName == "numberOfShades" )
        return XML_numberOfShades;
    else if( sName == "grainSize" )
        return XML_grainSize;
    else if( sName == "intensity" )
        return XML_intensity;
    else if( sName == "smoothness" )
        return XML_smoothness;
    else if( sName == "gridSize" )
        return XML_gridSize;
    else if( sName == "pencilSize" )
        return XML_pencilSize;
    else if( sName == "size" )
        return XML_size;
    else if( sName == "brushSize" )
        return XML_brushSize;
    else if( sName == "scaling" )
        return XML_scaling;
    else if( sName == "detail" )
        return XML_detail;
    else if( sName == "bright" )
        return XML_bright;
    else if( sName == "contrast" )
        return XML_contrast;
    else if( sName == "colorTemp" )
        return XML_colorTemp;
    else if( sName == "sat" )
        return XML_sat;
    else if( sName == "amount" )
        return XML_amount;

    SAL_WARN( "oox.drawingml", "ArtisticEffectProperties::getEffectToken - unexpected token name: " << sName );
    return XML_none;
}

} // namespace oox

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
