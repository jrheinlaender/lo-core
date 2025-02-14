/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#pragma once

#include <docmodel/dllapi.h>
#include <tools/color.hxx>
#include <docmodel/theme/ThemeColor.hxx>
#include <com/sun/star/graphic/XGraphic.hpp>

namespace model
{
enum class ColorType
{
    Unused,
    RGB,
    CRGB,
    HSL,
    Scheme,
    Palette,
    System,
    Placeholder
};

enum class SystemColorType
{
    Unused,
    DarkShadow3D,
    Light3D,
    ActiveBorder,
    ActiveCaption,
    AppWorkspace,
    Background,
    ButtonFace,
    ButtonHighlight,
    ButtonShadow,
    ButtonText,
    CaptionText,
    GradientActiveCaption,
    GradientInactiveCaption,
    GrayText,
    Highlight,
    HighlightText,
    HotLight,
    InactiveBorder,
    InactiveCaption,
    InactiveCaptionText,
    InfoBack,
    InfoText,
    Menu,
    MenuBar,
    MenuHighlight,
    MenuText,
    ScrollBar,
    Window,
    WindowFrame,
    WindowText
};

/** Definition of a color with multiple representations
 *
 * A color that can be expresses as a RGB, CRGB or HSL representation or
 * a more abstract representation as for example system color, palette,
 * scheme (theme) color or a placeholder. In these representations the
 * color needs to be additionally
 *
 * The color can also have transformations defined, which in addition
 * manipulates the resulting color (i.e. tints, shades, alpha,...).
 */
class DOCMODEL_DLLPUBLIC ComplexColor
{
public:
    ColorType meType = ColorType::Unused;

    sal_Int32 mnComponent1 = 0; // Red, Hue
    sal_Int32 mnComponent2 = 0; // Green, Saturation
    sal_Int32 mnComponent3 = 0; // Blue, Luminance

    SystemColorType meSystemColorType = SystemColorType::Unused;
    ::Color maLastColor;

    ThemeColorType meSchemeType = ThemeColorType::Unknown;
    std::vector<Transformation> maTransformations;

public:
    ColorType getType() const { return meType; }

    ThemeColorType getSchemeType() const { return meSchemeType; }

    Color getRGBColor() const { return Color(mnComponent1, mnComponent2, mnComponent3); }

    std::vector<Transformation> const& getTransformations() const { return maTransformations; }

    void setTransformations(std::vector<Transformation> const& rTransformations)
    {
        maTransformations = rTransformations;
    }

    void addTransformation(Transformation const& rTransform)
    {
        maTransformations.push_back(rTransform);
    }

    void removeTransformations(TransformationType eType)
    {
        maTransformations.erase(std::remove_if(maTransformations.begin(), maTransformations.end(),
                                               [eType](Transformation const& rTransform) {
                                                   return rTransform.meType == eType;
                                               }),
                                maTransformations.end());
    }

    void clearTransformations() { maTransformations.clear(); }

    void setCRGB(sal_Int32 nR, sal_Int32 nG, sal_Int32 nB)
    {
        mnComponent1 = nR;
        mnComponent2 = nG;
        mnComponent3 = nB;
        meType = ColorType::CRGB;
    }

    void setRGB(sal_Int32 nRGB)
    {
        ::Color aColor(ColorTransparency, nRGB);
        mnComponent1 = aColor.GetRed();
        mnComponent2 = aColor.GetGreen();
        mnComponent3 = aColor.GetBlue();
        meType = ColorType::RGB;
    }

    void setHSL(sal_Int32 nH, sal_Int32 nS, sal_Int32 nL)
    {
        mnComponent1 = nH;
        mnComponent2 = nS;
        mnComponent3 = nL;
        meType = ColorType::HSL;
    }

    void setSystemColor(SystemColorType eSystemColorType, sal_Int32 nRGB)
    {
        maLastColor = ::Color(ColorTransparency, nRGB);
        meSystemColorType = eSystemColorType;
        meType = ColorType::System;
    }

    void setSchemePlaceholder() { meType = ColorType::Placeholder; }

    void setSchemeColor(ThemeColorType eType)
    {
        meSchemeType = eType;
        meType = ColorType::Scheme;
    }

    model::ThemeColor createThemeColor() const
    {
        model::ThemeColor aThemeColor;
        if (meType == ColorType::Scheme)
        {
            aThemeColor.setType(meSchemeType);
            aThemeColor.setTransformations(maTransformations);
        }
        return aThemeColor;
    }

    bool operator==(const ComplexColor& rComplexColor) const
    {
        return meType == rComplexColor.meType && mnComponent1 == rComplexColor.mnComponent1
               && mnComponent2 == rComplexColor.mnComponent2
               && mnComponent3 == rComplexColor.mnComponent3
               && meSystemColorType == rComplexColor.meSystemColorType
               && maLastColor == rComplexColor.maLastColor
               && meSchemeType == rComplexColor.meSchemeType
               && maTransformations.size() == rComplexColor.maTransformations.size()
               && std::equal(maTransformations.begin(), maTransformations.end(),
                             rComplexColor.maTransformations.begin());
    }

    /** Applies the defined transformations to the input color */
    Color applyTransformations(Color const& rColor) const
    {
        Color aColor(rColor);

        for (auto const& rTransform : maTransformations)
        {
            switch (rTransform.meType)
            {
                case TransformationType::Tint:
                    aColor.ApplyTintOrShade(rTransform.mnValue);
                    break;
                case TransformationType::Shade:
                    aColor.ApplyTintOrShade(-rTransform.mnValue);
                    break;
                case TransformationType::LumMod:
                    aColor.ApplyLumModOff(rTransform.mnValue, 0);
                    break;
                case TransformationType::LumOff:
                    aColor.ApplyLumModOff(10000, rTransform.mnValue);
                    break;
                default:
                    break;
            }
        }
        return aColor;
    }
};

} // end of namespace svx

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
