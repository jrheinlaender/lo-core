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

#include <starmath.hrc>
#include <helpids.h>

#include <vcl/commandevent.hxx>
#include <vcl/event.hxx>
#include <vcl/ptrstyle.hxx>
#include <vcl/settings.hxx>

#include <editeng/editview.hxx>
#include <editeng/editeng.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/sfxsids.hrc>
#include <svl/stritem.hxx>
#include <svl/itemset.hxx>
#include <sfx2/viewfrm.hxx>
#include <osl/diagnose.h>
#include <o3tl/string_view.hxx>

#include <edit.hxx>
#include <smmod.hxx>
#include <view.hxx>
#include <document.hxx>
#include <cfgitem.hxx>
#include <smediteng.hxx>
#include <bitmaps.hlst>
#include <logging.hxx>

#include <com/sun/star/container/XChild.hpp>

using namespace com::sun::star::accessibility;
using namespace com::sun::star;


void SmGetLeftSelectionPart(const ESelection &rSel,
                            sal_Int32 &nPara, sal_uInt16 &nPos)
    // returns paragraph number and position of the selections left part
{
    // compare start and end of selection and use the one that comes first
    if (    rSel.nStartPara <  rSel.nEndPara
        ||  (rSel.nStartPara == rSel.nEndPara  &&  rSel.nStartPos < rSel.nEndPos) )
    {   nPara = rSel.nStartPara;
        nPos  = rSel.nStartPos;
    }
    else
    {   nPara = rSel.nEndPara;
        nPos  = rSel.nEndPos;
    }
}

AbstractEditTextWindow::AbstractEditTextWindow(AbstractEditWindow& rEditWindow)
    : mrEditWindow(rEditWindow)
    , aModifyIdle("SmEditWindow ModifyIdle")
    , aCursorMoveIdle("SmEditWindow CursorMoveIdle")
{
    SetAcceptsTab(true);

    aModifyIdle.SetInvokeHandler(LINK(this, AbstractEditTextWindow, ModifyTimerHdl));
    aModifyIdle.SetPriority(TaskPriority::LOWEST);

    if (!SmViewShell::IsInlineEditEnabled())
    {
        aCursorMoveIdle.SetInvokeHandler(LINK(this, AbstractEditTextWindow, CursorMoveTimerHdl));
        aCursorMoveIdle.SetPriority(TaskPriority::LOWEST);
    }
}

AbstractEditTextWindow::~AbstractEditTextWindow()
{
    aModifyIdle.Stop();
    StartCursorMove();
}

SmEditTextWindow::SmEditTextWindow(AbstractEditWindow& rEditWindow)
    : AbstractEditTextWindow(rEditWindow)
{

}

ImEditTextWindow::ImEditTextWindow(AbstractEditWindow& rEditWindow)
    : AbstractEditTextWindow(rEditWindow)
{

}

SmEditTextWindow::~SmEditTextWindow()
{
}

ImEditTextWindow::~ImEditTextWindow()
{
}

EditEngine* SmEditTextWindow::GetEditEngine() const
{
    SmDocShell *pDoc = mrEditWindow.GetDoc();
    assert(pDoc);
    return &pDoc->GetEditEngine();
}

EditEngine* ImEditTextWindow::GetEditEngine() const
{
    SmDocShell *pDoc = mrEditWindow.GetDoc();
    assert(pDoc);
    return &pDoc->GetImEditEngine();
}

void AbstractEditTextWindow::EditViewScrollStateChange()
{
    mrEditWindow.SetScrollBarRanges();
}

void AbstractEditTextWindow::SetDrawingArea(weld::DrawingArea* pDrawingArea)
{
    weld::CustomWidgetController::SetDrawingArea(pDrawingArea);

    const StyleSettings& rStyleSettings = Application::GetSettings().GetStyleSettings();
    Color aBgColor = rStyleSettings.GetWindowColor();

    OutputDevice& rDevice = pDrawingArea->get_ref_device();
    rDevice.SetBackground(aBgColor);

    SetHelpId(HID_SMA_COMMAND_WIN_EDIT);

    EnableRTL(false);

    EditEngine* pEditEngine = GetEditEngine();

    m_xEditView.reset(new EditView(pEditEngine, nullptr));
    m_xEditView->setEditViewCallbacks(this);

    pEditEngine->InsertView(m_xEditView.get());

    m_xEditView->SetOutputArea(mrEditWindow.AdjustScrollBars());

    m_xEditView->SetBackgroundColor(aBgColor);

    pDrawingArea->set_cursor(PointerStyle::Text);

    pEditEngine->SetStatusEventHdl(LINK(this, AbstractEditTextWindow, EditStatusHdl));

    InitAccessible();

    //Apply zoom to smeditwindow text
    if(GetEditView())
        static_cast<SmEditEngine*>(GetEditEngine())->executeZoom(GetEditView());
}

AbstractEditWindow::AbstractEditWindow(SmCmdBoxWindow& rMyCmdBoxWin, weld::Builder& rBuilder, const OString& id)
    : rCmdBox(rMyCmdBoxWin)
    , mxNotebook(rBuilder.weld_notebook("notebook"))
    , mxScrolledWindow(rBuilder.weld_scrolled_window(id, true))
{
    mxScrolledWindow->connect_vadjustment_changed(LINK(this, AbstractEditWindow, ScrollHdl));

    // Note: CreateEditView must be called from specialized constructors because it is pure virtual
}

AbstractEditWindow::~AbstractEditWindow() COVERITY_NOEXCEPT_FALSE
{
    DeleteEditView();
    mxScrolledWindow.reset();
}

SmEditWindow::SmEditWindow(SmCmdBoxWindow &rMyCmdBoxWin, weld::Builder& rBuilder)
    : AbstractEditWindow(rMyCmdBoxWin, rBuilder, "scrolledwindow")
{
    CreateEditView(rBuilder);
}

SmEditWindow::~SmEditWindow() COVERITY_NOEXCEPT_FALSE
{
}

ImEditWindow::ImEditWindow(SmCmdBoxWindow &rMyCmdBoxWin, weld::Builder& rBuilder)
    : AbstractEditWindow(rMyCmdBoxWin, rBuilder, "iscrolledwindow")
{
    CreateEditView(rBuilder);
    EditEngine *pEditEngine = const_cast< ImEditWindow* >(this)->GetEditEngine();
    OSL_ENSURE( pEditEngine, "EditEngine missing" );

    // TODO The wish is to compile the Math formula after it has been opened (in stand-alone Math) Is there a better place/way?
    if (SmDocShell *pDoc = GetDoc()) {
        // Check for stand-alone formula
        Reference<com::sun::star::container::XChild> xChild(pDoc->GetModel(), UNO_QUERY);
        Reference<XModel> xParent;
        if (xChild.is())
            xParent = Reference<XModel>(xChild->getParent(), UNO_QUERY);
        Reference<XModel> xModel;

        if (!xParent.is())
            pDoc->Compile();
    }
}

ImEditWindow::~ImEditWindow() COVERITY_NOEXCEPT_FALSE
{
}

#define IMGUIWINDOW_COL_HIDDEN 0
#define IMGUIWINDOW_COL_LABEL 1
#define IMGUIWINDOW_COL_TYPE 2
#define IMGUIWINDOW_COL_FORMULA 3

ImGuiWindow::ImGuiWindow(SmCmdBoxWindow& rMyCmdBoxWin, weld::Builder& rBuilder)
    : rCmdBox(rMyCmdBoxWin)
    , mxNotebook(rBuilder.weld_notebook("notebook"))
    , mxScrolledWindow(rBuilder.weld_scrolled_window("iguiscrolledwindow", true))
    , mxFormulaList(rBuilder.weld_tree_view("iformulalist"))
    , mSelected(false)
    , mNumClicks(0)
    , mClickedColumn(-1)
    , mEditedColumn(-1)
{
    if (!mxScrolledWindow || !mxFormulaList)
        return;

    mxFormulaList->set_size_request(mxFormulaList->get_approximate_digit_width() * 60, mxFormulaList->get_height_rows(5));
    mxFormulaList->enable_toggle_buttons(weld::ColumnToggleType::Check);

    mxFormulaList->connect_key_release(LINK(this, ImGuiWindow, KeyReleaseHdl));
    mxFormulaList->connect_changed(LINK(this, ImGuiWindow, SelectHdl));
    mxFormulaList->connect_mouse_release(LINK(this, ImGuiWindow, MouseReleaseHdl));
    mxFormulaList->connect_editing(LINK(this, ImGuiWindow, EditingEntryHdl), LINK(this, ImGuiWindow, EditedEntryHdl));
    mxFormulaList->set_selection_mode(SelectionMode::Single);

    ResetModel();

    mxFormulaList->columns_autosize();

ImGuiWindow::~ImGuiWindow() COVERITY_NOEXCEPT_FALSE
{
}

// TODO Should we / Must we listen to the Broadcast emitted in SmDocShell::SetModified() ?
void ImGuiWindow::ResetModel()
{
    // Remember the current selection
    int currentSelection = 0;
    auto  xIter = mxFormulaList->make_iterator();
    if (mxFormulaList->get_iter_first(*xIter.get()))
        do
        {
            if (mxFormulaList->is_selected(*xIter.get()))
                break;
            ++currentSelection;
        } while (mxFormulaList->iter_next(*xIter.get()));

    mxFormulaList->clear();

    SmDocShell* pDoc = GetDoc();
    if (!pDoc) return;

    int lineCount = 0;

    for (const auto& fLine : pDoc->GetFormulaLines())
    {
        if (fLine->getSelectionType() == formulaTypeResult) continue;

        mxFormulaList->append(xIter.get());

        mxFormulaList->set_id(*xIter, weld::toId(&fLine));

        iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(fLine);
        if (expr != nullptr && !std::dynamic_pointer_cast<iFormulaNodeText>(fLine))
        {
            mxFormulaList->set_toggle(*xIter, expr->getHide() ? TRISTATE_TRUE : TRISTATE_FALSE, IMGUIWINDOW_COL_HIDDEN);
            mxFormulaList->set_image(*xIter, expr->getHide() ? OUString(BMP_IMGUI_HIDE) : OUString(BMP_IMGUI_SHOW), IMGUIWINDOW_COL_HIDDEN);
            mxFormulaList->set_sensitive(*xIter, true, IMGUIWINDOW_COL_LABEL);
            option o = fLine->getOption(o_showlabels);
            mxFormulaList->set_image(*xIter, o.value.boolean ? OUString(BMP_IMGUI_SHOWLABEL) : OUString(BMP_IMGUI_HIDELABEL), IMGUIWINDOW_COL_LABEL_HIDE);
            mxFormulaList->set_text(*xIter, expr->getLabel(), IMGUIWINDOW_COL_LABEL);
        }
        else
        {
            // Note toggle remains invisible since we do not set a value
            mxFormulaList->set_sensitive(*xIter, false, IMGUIWINDOW_COL_LABEL); // Make Label read-only
            mxFormulaList->set_image(*xIter, "");
        }

        mxFormulaList->set_text(*xIter, fLine->getCommand(), IMGUIWINDOW_COL_TYPE);
        mxFormulaList->set_text(*xIter, fLine->printFormula(), IMGUIWINDOW_COL_FORMULA);

        if (lineCount == currentSelection)
            mxFormulaList->select(*xIter);
        ++lineCount;
    }

    mxFormulaList->columns_autosize();

    mSelected = false; // A row has already been selected
}

IMPL_LINK_NOARG(ImGuiWindow, SelectHdl, weld::TreeView&, void)
{
    mSelected = true;
}

namespace {
    OUString makeNewFormula(const std::list<std::shared_ptr<iFormulaLine>>& fLines)
    {
        OUString newFormula;

        for (const auto& line : fLines)
        {
            if (line->getSelectionType() == formulaTypeResult) continue;
            newFormula += line->print().copy(5) + "\n";
        }

        return newFormula;
    }
}

IMPL_LINK(ImGuiWindow, MouseReleaseHdl, const MouseEvent&, rMEvt, bool)
{
    if (mSelected) {
        mSelected = false;
        mNumClicks = 0;
        return false;  // This  click selected a new entry
    }

    if (mEditedColumn > 0)
        return false; // Ignore mouse clicks when a cell is being edited

    mNumClicks = rMEvt.GetClicks();

    mClickedColumn = -1;
    auto xIter(mxFormulaList->make_iterator());
    if (!mxFormulaList->get_selected(xIter.get()))
        return false; // No entry is selected

    Point mousePos = rMEvt.GetPosPixel();

    for (int col = 0; col <= IMGUIWINDOW_COL_LAST; ++col)
    {
        tools::Rectangle cellArea = mxFormulaList->get_cell_area(*xIter, col);
        if (cellArea.Contains(mousePos))
        {
            mClickedColumn = col;
            break;
        }
    }

    if (mNumClicks > 1)
        return false; // We only handle single clicks here
        SmDocShell* pDoc = GetDoc();
        if (!pDoc) return false;

    switch (mClickedColumn)
    {

            auto fLines = pDoc->GetFormulaLines();
            auto itLine = weld::fromId<std::shared_ptr<iFormulaLine>*>(mxFormulaList->get_selected_id());
        if (std::dynamic_pointer_cast<iFormulaNodeText>(*itLine)) return false; // Text lines cannot be hidden

            if (itLine == nullptr)
                return false; // line number not found
        iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(*itLine);
        if (expr != nullptr)
        {
            expr->setHide(!expr->getHide());
            mxFormulaList->set_image(*xIter, expr->getHide() ? OUString(BMP_IMGUI_HIDE) : OUString(BMP_IMGUI_SHOW), IMGUIWINDOW_COL_HIDDEN);
            auto fLines = pDoc->GetFormulaLines();
            auto itLine = weld::fromId<std::shared_ptr<iFormulaLine>*>(mxFormulaList->get_selected_id());

            if (itLine == nullptr)
                return false; // line number not found
            pDoc->SetImText(makeNewFormula(fLines));
            ResetModel();
            return true;
        // Note: Text columns are handled in EditedEntryHdl
        case IMGUIWINDOW_COL_LABEL:
        case IMGUIWINDOW_COL_TYPE:
        case IMGUIWINDOW_COL_FORMULA:
        {
            if (mxFormulaList->get_sensitive(*xIter, IMGUIWINDOW_COL_LABEL))
            {
                mEditedColumn = mClickedColumn;
                SAL_INFO_LEVEL(3, "starmath.imath", "Editing detected in column " << mEditedColumn);
            }
            break;
        }
        default:
            return false;
    }

    // Click was handled
    mNumClicks = 0;
    mClickedColumn = -1;

    return false;
}

IMPL_LINK(ImGuiWindow, EditingEntryHdl, const weld::TreeIter&, rIter, bool)
{
    (void)rIter;
    return true; // Allow editing (called for text and combo cell renderers)
}

IMPL_LINK(ImGuiWindow, EditedEntryHdl, const IterString&, rIterString, bool)
{
    if (mxFormulaList->get_text(rIterString.first) == rIterString.second)
        return true; // Nothing changed

    SmDocShell* pDoc = GetDoc();
    if (!pDoc) return true; // Returning false would pass the call on to the next handler


    {
        auto fLines = pDoc->GetFormulaLines();
        auto itLine = weld::fromId<std::shared_ptr<iFormulaLine>*>(mxFormulaList->get_id(rIterString.first));
        if (itLine == nullptr)
            goto finished; // line number not found

        switch (mEditedColumn)
        {
            case IMGUIWINDOW_COL_LABEL:
            {
                iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(*itLine);
                if (expr != nullptr)
                    expr->setLabel(rIterString.second);
                break;
            }
            case IMGUIWINDOW_COL_FORMULA:
                (*itLine)->setFormula(rIterString.second);
                break;
        }
        case IMGUIWINDOW_COL_TYPE:
            //(*itLine)->setLabel(mxFormulaList->get_text(rIterString.first, IMGUIWINDOW_COL_TYPE));
            break;

        pDoc->SetImText(makeNewFormula(fLines));
        ResetModel();
    }

finished:
    mEditedColumn = -1;
    return true;
}

IMPL_LINK(ImGuiWindow, KeyReleaseHdl, const ::KeyEvent&, rKEvt, bool)
{
    // Pass-through any key presses but use the trigger to update the iFormula
    sal_Unicode cCharCode = rKEvt.GetCharCode();
    (void)cCharCode;

    SmDocShell* pDoc = GetDoc();
    if (!pDoc) return false;

    // TODO How do we get the changed text out of the GtkCellRendererText? The TreeView returns the old text
    std::cout << "Text=" << mxFormulaList->get_text(*xIter, col) << std::endl;
    /*
    auto fLines = pDoc->GetFormulaLines();
    auto itLine = getLineIterator(fLines, mxFormulaList->get_id(*xIter).toUInt64());
    if (itLine == fLines.end()) return false; // line number not found

    if (col == IMGUIWINDOW_COL_LABEL)
    {
        iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(*itLine);
        if (expr != nullptr)
            expr->setLabel(mxFormulaList->get_text(*xIter, IMGUIWINDOW_COL_LABEL));
    } else if (col == IMGUIWINDOW_COL_FORMULA)
        (*itLine)->setFormula(mxFormulaList->get_text(*xIter, IMGUIWINDOW_COL_FORMULA));

    pDoc->SetImText(makeNewFormula(fLines));
    ResetModel();
    */

    return false; // Let text editor handle the key
}

IMPL_LINK(ImGuiWindow, ToggleHdl, const weld::TreeView::iter_col&, rRowCol, void)
{
    SmDocShell* pDoc = GetDoc();
    if (!pDoc) return;

    auto fLines = pDoc->GetFormulaLines();
    auto itLine = weld::fromId<std::shared_ptr<iFormulaLine>*>(mxFormulaList->get_id(rRowCol.first));

    if (itLine == nullptr) return; // line number not found
    if (std::dynamic_pointer_cast<iFormulaNodeText>(*itLine)) return; // Text lines cannot be hidden

    iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(*itLine);
    if (expr != nullptr)
    {
        expr->setHide(mxFormulaList->get_toggle(rRowCol.first, rRowCol.second) == TRISTATE_TRUE);
        pDoc->SetImText(makeNewFormula(fLines));
        ResetModel();
    }
}

weld::Window* AbstractEditWindow::GetFrameWeld() const
{
    return rCmdBox.GetFrameWeld();
}

void AbstractEditTextWindow::StartCursorMove()
{
    if (!SmViewShell::IsInlineEditEnabled())
        aCursorMoveIdle.Stop();
}

void AbstractEditWindow::InvalidateSlots()
{
    GetView()->InvalidateSlots();
}

SmViewShell * AbstractEditWindow::GetView()
{
    return rCmdBox.GetView();
}

SmDocShell * AbstractEditWindow::GetDoc()
{
    SmViewShell *pView = rCmdBox.GetView();
    return pView ? pView->GetDoc() : nullptr;
}

SmDocShell * ImGuiWindow::GetDoc()
{
    SmViewShell *pView = rCmdBox.GetView();
    return pView ? pView->GetDoc() : nullptr;
}

EditView * AbstractEditWindow::GetEditView() const
{
    return mxTextControl ? mxTextControl->GetEditView() : nullptr;
}

EditEngine * SmEditWindow::GetEditEngine()
{
    if (SmDocShell *pDoc = GetDoc())
        return &pDoc->GetEditEngine();
    return nullptr;
}

EditEngine * ImEditWindow::GetEditEngine()
{
    if (SmDocShell *pDoc = GetDoc())
        return &pDoc->GetImEditEngine();
    return nullptr;
}

void AbstractEditTextWindow::StyleUpdated()
{
    WeldEditView::StyleUpdated();
    EditEngine *pEditEngine = GetEditEngine();
    SmDocShell *pDoc = mrEditWindow.GetDoc();

    if (pEditEngine && pDoc)
    {
        //!
        //! see also SmDocShell::GetEditEngine() !
        //!
        const StyleSettings& rStyleSettings = Application::GetSettings().GetStyleSettings();

        pDoc->UpdateEditEngineDefaultFonts();
        pEditEngine->SetBackgroundColor(rStyleSettings.GetFieldColor());
        pEditEngine->SetDefTab(sal_uInt16(GetTextWidth("XXXX")));

        // forces new settings to be used
        // unfortunately this resets the whole edit engine
        // thus we need to save at least the text
        OUString aTxt( pEditEngine->GetText() );
        pEditEngine->Clear();   //incorrect font size
        pEditEngine->SetText( aTxt );

        Resize();
    }

    // Apply zoom to smeditwindow text
    static_cast<SmEditEngine*>(GetEditEngine())->executeZoom(GetEditView());
}

IMPL_LINK_NOARG(AbstractEditTextWindow, ModifyTimerHdl, Timer *, void)
{
    UpdateStatus(false);
    aModifyIdle.Stop();
}

IMPL_LINK_NOARG(AbstractEditTextWindow, CursorMoveTimerHdl, Timer *, void)
    // every once in a while check cursor position (selection) of edit
    // window and if it has changed (try to) set the formula-cursor
    // according to that.
{
    if (SmViewShell::IsInlineEditEnabled())
        return;

    ESelection aNewSelection(GetSelection());

    if (aNewSelection != aOldSelection)
    {
        if (SmViewShell *pViewSh = mrEditWindow.GetView())
        {
            // get row and column to look for
            sal_Int32  nRow;
            sal_uInt16 nCol;
            SmGetLeftSelectionPart(aNewSelection, nRow, nCol);
            pViewSh->GetGraphicWidget().SetCursorPos(static_cast<sal_uInt16>(nRow), nCol);
            aOldSelection = aNewSelection;
        }
    }
    aCursorMoveIdle.Stop();
}

bool AbstractEditTextWindow::MouseButtonUp(const MouseEvent &rEvt)
{
    bool bRet = WeldEditView::MouseButtonUp(rEvt);
    if (!SmViewShell::IsInlineEditEnabled())
        CursorMoveTimerHdl(&aCursorMoveIdle);
    mrEditWindow.InvalidateSlots();
    return bRet;
}

bool AbstractEditTextWindow::Command(const CommandEvent& rCEvt)
{
    // no zooming in Command window
    const CommandWheelData* pWData = rCEvt.GetWheelData();
    if (pWData && CommandWheelMode::ZOOM == pWData->GetMode())
        return true;

    //pass alt press/release to parent impl
    if (rCEvt.GetCommand() == CommandEventId::ModKeyChange)
        return false;

    if (rCEvt.GetCommand() == CommandEventId::ContextMenu)
    {
        ReleaseMouse();
        SmCmdBoxWindow& rCmdBox = mrEditWindow.GetCmdBox();
        rCmdBox.ShowContextMenu(rCmdBox.WidgetToWindowPos(*GetDrawingArea(), rCEvt.GetMousePosPixel()));
        GrabFocus();
        return true;
    }

    bool bConsumed = WeldEditView::Command(rCEvt);
    if (bConsumed)
        UserPossiblyChangedText();
    return bConsumed;
}

bool AbstractEditTextWindow::KeyInput(const KeyEvent& rKEvt)
{
    if (rKEvt.GetKeyCode().GetCode() == KEY_F1)
    {
        mrEditWindow.GetView()->StartMainHelp();
        return true;
    }

    if (rKEvt.GetKeyCode().GetCode() == KEY_ESCAPE)
    {
        bool bCallBase = true;
        SfxViewShell* pViewShell = mrEditWindow.GetView();
        if ( dynamic_cast<const SmViewShell *>(pViewShell) )
        {
            // Terminate possible InPlace mode
            bCallBase = !pViewShell->Escape();
        }
        return !bCallBase;
    }

    StartCursorMove();

    bool autoClose = false;
    EditView* pEditView = GetEditView();
    ESelection aSelection = pEditView->GetSelection();
    // as we don't support RTL in Math, we need to swap values from selection when they were done
    // in RTL form
    aSelection.Adjust();
    OUString selected = pEditView->GetEditEngine()->GetText(aSelection);

    // Check is auto close brackets/braces is disabled
    SmModule *pMod = SM_MOD();
    if (pMod && !pMod->GetConfig()->IsAutoCloseBrackets())
        autoClose = false;
    else if (o3tl::trim(selected) == u"<?>")
        autoClose = true;
    else if (selected.isEmpty() && !aSelection.HasRange())
    {
        selected = pEditView->GetEditEngine()->GetText(aSelection.nEndPara);
        if (!selected.isEmpty())
        {
            sal_Int32 index = selected.indexOf("\n", aSelection.nEndPos);
            if (index != -1)
            {
                selected = selected.copy(index, sal_Int32(aSelection.nEndPos-index));
                if (o3tl::trim(selected).empty())
                    autoClose = true;
            }
            else
            {
                sal_Int32 length = selected.getLength();
                if (aSelection.nEndPos == length)
                    autoClose = true;
                else
                {
                    selected = selected.copy(aSelection.nEndPos);
                    if (o3tl::trim(selected).empty())
                        autoClose = true;
                }
            }
        }
        else
            autoClose = true;
    }

    bool bConsumed = WeldEditView::KeyInput(rKEvt);
    if (!bConsumed)
    {
        SmViewShell *pView = mrEditWindow.GetView();
        if (pView)
            bConsumed = pView->KeyInput(rKEvt);
        if (pView && !bConsumed)
        {
            // F1 (help) leads to the destruction of this
            Flush();
            if ( aModifyIdle.IsActive() )
                aModifyIdle.Stop();
        }
        else
        {
            // SFX has maybe called a slot of the view and thus (because of a hack in SFX)
            // set the focus to the view
            SmViewShell* pVShell = mrEditWindow.GetView();
            if ( pVShell && pVShell->GetGraphicWidget().HasFocus() )
            {
                GrabFocus();
            }
        }
    }
    else
    {
        UserPossiblyChangedText();
    }

    // get the current char of the key event
    sal_Unicode cCharCode = rKEvt.GetCharCode();
    OUString sClose;

    if (cCharCode == '{')
        sClose = "  }";
    else if (cCharCode == '[')
        sClose = "  ]";
    else if (cCharCode == '(')
        sClose = "  )";

    // auto close the current character only when needed
    if (!sClose.isEmpty() && autoClose)
    {
        pEditView->InsertText(sClose);
        // position it at center of brackets
        aSelection.nStartPos += 2;
        aSelection.nEndPos = aSelection.nStartPos;
        pEditView->SetSelection(aSelection);
    }

    mrEditWindow.InvalidateSlots();
    return bConsumed;
}

void AbstractEditTextWindow::UserPossiblyChangedText()
{
    // have doc-shell modified only for formula input/change and not
    // cursor travelling and such things...
    SmDocShell *pDocShell = mrEditWindow.GetDoc();
    EditEngine *pEditEngine = GetEditEngine();
    if (pDocShell && pEditEngine && pEditEngine->IsModified())
        pDocShell->SetModified(true);
    aModifyIdle.Start();
}

// Note: this must be specialized because of the new ...EditTextWindow() call
void SmEditWindow::CreateEditView(weld::Builder& rBuilder)
{
    assert(!mxTextControl);

    EditEngine *pEditEngine = GetEditEngine();
    //! pEditEngine may be 0.
    //! For example when the program is used by the document-converter
    if (!pEditEngine)
        return;

    mxTextControl.reset(new SmEditTextWindow(*this));
    mxTextControlWin.reset(new weld::CustomWeld(rBuilder, "editview", *mxTextControl));

    SetScrollBarRanges();
}

void ImEditWindow::CreateEditView(weld::Builder& rBuilder)
{
    assert(!mxTextControl);

    EditEngine *pEditEngine = GetEditEngine();
    //! pEditEngine may be 0.
    //! For example when the program is used by the document-converter
    if (!pEditEngine)
        return;

    mxTextControl.reset(new ImEditTextWindow(*this));
    mxTextControlWin.reset(new weld::CustomWeld(rBuilder, "ieditview", *mxTextControl));

    SetScrollBarRanges();
}

IMPL_LINK_NOARG(AbstractEditTextWindow, EditStatusHdl, EditStatus&, void)
{
    Resize();
}

IMPL_LINK(AbstractEditWindow, ScrollHdl, weld::ScrolledWindow&, rScrolledWindow, void)
{
    if (EditView* pEditView = GetEditView())
    {
        pEditView->SetVisArea(tools::Rectangle(
                    Point(0,
                          rScrolledWindow.vadjustment_get_value()),
                    pEditView->GetVisArea().GetSize()));
        pEditView->Invalidate();
    }
}

tools::Rectangle AbstractEditWindow::AdjustScrollBars()
{
    tools::Rectangle aRect(Point(), rCmdBox.GetOutputSizePixel());

    if (mxScrolledWindow)
    {
        const auto nScrollSize = mxScrolledWindow->get_scroll_thickness();
        const auto nMargin = nScrollSize + 2;
        aRect.AdjustRight(-nMargin);
        aRect.AdjustBottom(-nMargin);
    }

    return aRect;
}

void AbstractEditWindow::SetScrollBarRanges()
{
    EditEngine *pEditEngine = GetEditEngine();
    if (!pEditEngine)
        return;
    if (!mxScrolledWindow)
        return;
    EditView* pEditView = GetEditView();
    if (!pEditView)
        return;

    int nVUpper = pEditEngine->GetTextHeight();
    int nVCurrentDocPos = pEditView->GetVisArea().Top();
    const Size aOut(pEditView->GetOutputArea().GetSize());
    int nVStepIncrement = aOut.Height() * 2 / 10;
    int nVPageIncrement = aOut.Height() * 8 / 10;
    int nVPageSize = aOut.Height();

    /* limit the page size to below nUpper because gtk's gtk_scrolled_window_start_deceleration has
       effectively...

       lower = gtk_adjustment_get_lower
       upper = gtk_adjustment_get_upper - gtk_adjustment_get_page_size

       and requires that upper > lower or the deceleration animation never ends
    */
    nVPageSize = std::min(nVPageSize, nVUpper);

    mxScrolledWindow->vadjustment_configure(nVCurrentDocPos, 0, nVUpper,
                                            nVStepIncrement, nVPageIncrement, nVPageSize);
}

OUString AbstractEditWindow::GetText() const
{
    OUString aText;
    EditEngine *pEditEngine = const_cast< AbstractEditWindow* >(this)->GetEditEngine();
    OSL_ENSURE( pEditEngine, "EditEngine missing" );
    if (pEditEngine)
        aText = pEditEngine->GetText();
    return aText;
}

void AbstractEditWindow::SetText(const OUString& rText)
{
    if (!mxTextControl)
        return;
    mxTextControl->SetText(rText);
}

void AbstractEditWindow::Flush()
{
    if (!mxTextControl)
        return;
    mxTextControl->Flush();
}

void AbstractEditWindow::GrabFocus()
{
    if (!mxTextControl)
        return;
    mxTextControl->GrabFocus();
}

void SmEditWindow::GrabFocus()
{
    mxNotebook->set_current_page(SM_EDITWINDOW_TAB_SM);
    AbstractEditWindow::GrabFocus();
}

void ImEditWindow::GrabFocus()
{
    mxNotebook->set_current_page(SM_EDITWINDOW_TAB_IMTXT);
    AbstractEditWindow::GrabFocus();
}

void ImGuiWindow::GrabFocus()
{
    mxNotebook->set_current_page(SM_EDITWINDOW_TAB_IMGUI);
    mxFormulaList->grab_focus();
}

bool AbstractEditWindow::HasFocus() const
{
    if (!mxTextControl)
        return false;

    return mxTextControl->HasFocus();
}

bool ImGuiWindow::HasFocus() const
{
    if (!mxFormulaList)
        return false;

    return mxFormulaList->has_focus();
}

void AbstractEditTextWindow::SetText(const OUString& rText)
{
    EditEngine *pEditEngine = GetEditEngine();
    OSL_ENSURE( pEditEngine, "EditEngine missing" );
    if (!pEditEngine || pEditEngine->IsModified())
        return;

    EditView* pEditView = GetEditView();
    ESelection eSelection = pEditView->GetSelection();

    pEditEngine->SetText(rText);
    pEditEngine->ClearModifyFlag();

    // Restarting the timer here, prevents calling the handlers for other (currently inactive)
    // math tasks
    aModifyIdle.Start();

    // Apply zoom to smeditwindow text
    static_cast<SmEditEngine*>(pEditView->GetEditEngine())->executeZoom(pEditView);
    pEditView->SetSelection(eSelection);
}

void AbstractEditTextWindow::GetFocus()
{
    WeldEditView::GetFocus();

    EditEngine *pEditEngine = GetEditEngine();
    if (pEditEngine)
        pEditEngine->SetStatusEventHdl(LINK(this, AbstractEditTextWindow, EditStatusHdl));

    //Let SmViewShell know we got focus
    if (mrEditWindow.GetView() && SmViewShell::IsInlineEditEnabled())
        mrEditWindow.GetView()->SetInsertIntoEditWindow(true);
}

void AbstractEditTextWindow::LoseFocus()
{
    EditEngine *pEditEngine = GetEditEngine();
    if (pEditEngine)
        pEditEngine->SetStatusEventHdl( Link<EditStatus&,void>() );

    WeldEditView::LoseFocus();
}

bool AbstractEditWindow::IsAllSelected() const
{
    EditEngine *pEditEngine = const_cast<AbstractEditWindow *>(this)->GetEditEngine();
    if (!pEditEngine)
        return false;
    EditView* pEditView = GetEditView();
    if (!pEditView)
        return false;
    bool bRes = false;
    ESelection eSelection( pEditView->GetSelection() );
    sal_Int32 nParaCnt = pEditEngine->GetParagraphCount();
    if (!(nParaCnt - 1))
    {
        sal_Int32 nTextLen = pEditEngine->GetText().getLength();
        bRes = !eSelection.nStartPos && (eSelection.nEndPos == nTextLen - 1);
    }
    else
    {
        bRes = !eSelection.nStartPara && (eSelection.nEndPara == nParaCnt - 1);
    }
    return bRes;
}

void AbstractEditWindow::SelectAll()
{
    if (EditView* pEditView = GetEditView())
    {
        // ALL as last two parameters refers to the end of the text
        pEditView->SetSelection( ESelection( 0, 0, EE_PARA_ALL, EE_TEXTPOS_ALL ) );
    }
}

void AbstractEditWindow::MarkError(const Point &rPos)
{
    if (EditView* pEditView = GetEditView())
    {
        const sal_uInt16        nCol = sal::static_int_cast< sal_uInt16 >(rPos.X());
        const sal_uInt16        nRow = sal::static_int_cast< sal_uInt16 >(rPos.Y() - 1);

        pEditView->SetSelection(ESelection(nRow, nCol - 1, nRow, nCol));
        GrabFocus();
    }
}

void AbstractEditWindow::SelNextMark()
{
    if (!mxTextControl)
        return;
    mxTextControl->SelNextMark();
}

// Makes selection to next <?> symbol
void AbstractEditTextWindow::SelNextMark()
{
    EditEngine *pEditEngine = GetEditEngine();
    if (!pEditEngine)
        return;
    EditView* pEditView = GetEditView();
    if (!pEditView)
        return;

    ESelection eSelection = pEditView->GetSelection();
    sal_Int32 nPos = eSelection.nEndPos;
    sal_Int32 nCounts = pEditEngine->GetParagraphCount();

    while (eSelection.nEndPara < nCounts)
    {
        OUString aText = pEditEngine->GetText(eSelection.nEndPara);
        nPos = aText.indexOf("<?>", nPos);
        if (nPos != -1)
        {
            pEditView->SetSelection(ESelection(
                eSelection.nEndPara, nPos, eSelection.nEndPara, nPos + 3));
            break;
        }

        nPos = 0;
        eSelection.nEndPara++;
    }
}

void AbstractEditWindow::SelPrevMark()
{
    EditEngine *pEditEngine = GetEditEngine();
    if (!pEditEngine)
        return;
    EditView* pEditView = GetEditView();
    if (!pEditView)
        return;

    ESelection eSelection = pEditView->GetSelection();
    sal_Int32 nPara = eSelection.nStartPara;
    sal_Int32 nMax = eSelection.nStartPos;
    OUString aText(pEditEngine->GetText(nPara));
    static constexpr OUStringLiteral aMark(u"<?>");
    sal_Int32 nPos;

    while ( (nPos = aText.lastIndexOf(aMark, nMax)) < 0 )
    {
        if (--nPara < 0)
            return;
        aText = pEditEngine->GetText(nPara);
        nMax = aText.getLength();
    }
    pEditView->SetSelection(ESelection(nPara, nPos, nPara, nPos + 3));
}

// returns true iff 'rText' contains a mark
static bool HasMark(std::u16string_view rText)
{
    return rText.find(u"<?>") != std::u16string_view::npos;
}

ESelection AbstractEditWindow::GetSelection() const
{
    if (mxTextControl)
        return mxTextControl->GetSelection();
    return ESelection();
}

ESelection AbstractEditTextWindow::GetSelection() const
{
    // pointer may be 0 when reloading a document and the old view
    // was already destroyed
    if (EditView* pEditView = GetEditView())
        return pEditView->GetSelection();
    return ESelection();
}

void AbstractEditWindow::SetSelection(const ESelection &rSel)
{
    if (EditView* pEditView = GetEditView())
        pEditView->SetSelection(rSel);
    InvalidateSlots();
}

bool AbstractEditWindow::IsEmpty() const
{
    EditEngine *pEditEngine = const_cast<AbstractEditWindow *>(this)->GetEditEngine();
    bool bEmpty = ( pEditEngine && pEditEngine->GetTextLen() == 0 );
    return bEmpty;
}

bool AbstractEditWindow::IsSelected() const
{
    EditView* pEditView = GetEditView();
    return pEditView && pEditView->HasSelection();
}

void AbstractEditTextWindow::UpdateStatus(bool bSetDocModified)
{
    SmModule *pMod = SM_MOD();
    if (pMod && pMod->GetConfig()->IsAutoRedraw())
        Flush();

    if (SmDocShell *pModifyDoc = bSetDocModified ? mrEditWindow.GetDoc() : nullptr)
        pModifyDoc->SetModified();

    static_cast<SmEditEngine*>(GetEditEngine())->executeZoom(GetEditView());
}

void AbstractEditWindow::UpdateStatus()
{
    mxTextControl->UpdateStatus(/*bSetDocModified*/false);
}

void AbstractEditWindow::Cut()
{
    if (mxTextControl)
    {
        mxTextControl->Cut();
        mxTextControl->UpdateStatus(true);
    }
}

void AbstractEditWindow::Copy()
{
    if (mxTextControl)
        mxTextControl->Copy();
}

void AbstractEditWindow::Paste()
{
    if (mxTextControl)
    {
        mxTextControl->Paste();
        mxTextControl->UpdateStatus(true);
    }
}

void AbstractEditWindow::Delete()
{
    if (mxTextControl)
    {
        mxTextControl->Delete();
        mxTextControl->UpdateStatus(true);
    }
}

void AbstractEditWindow::InsertText(const OUString& rText)
{
    if (!mxTextControl)
        return;
    mxTextControl->InsertText(rText);
}

void AbstractEditTextWindow::InsertText(const OUString& rText)
{
    EditView* pEditView = GetEditView();
    if (!pEditView)
        return;

    // Note: Insertion of a space in front of commands is done here and
    // in SmEditWindow::InsertCommand.
    ESelection aSelection = pEditView->GetSelection();
    OUString aCurrentFormula = pEditView->GetEditEngine()->GetText();
    sal_Int32 nStartIndex = 0;

    // get the start position (when we get a multi line formula)
    for (sal_Int32 nParaPos = 0; nParaPos < aSelection.nStartPara; nParaPos++)
         nStartIndex = aCurrentFormula.indexOf("\n", nStartIndex) + 1;

    nStartIndex += aSelection.nStartPos;

    // TODO: unify this function with the InsertCommand: The do the same thing for different
    // callers
    OUString string(rText);

    OUString selected(pEditView->GetSelected());
    // if we have text selected, use it in the first placeholder
    if (!selected.isEmpty())
        string = string.replaceFirst("<?>", selected);

    // put a space before a new command if not in the beginning of a line
    if (aSelection.nStartPos > 0 && aCurrentFormula[nStartIndex - 1] != ' ')
        string = " " + string;

    pEditView->InsertText(string);

    // Remember start of the selection and move the cursor there afterwards.
    aSelection.nEndPara = aSelection.nStartPara;
    if (HasMark(string))
    {
        aSelection.nEndPos = aSelection.nStartPos;
        pEditView->SetSelection(aSelection);
        SelNextMark();
    }
    else
    {   // set selection after inserted text
        aSelection.nEndPos = aSelection.nStartPos + string.getLength();
        aSelection.nStartPos = aSelection.nEndPos;
        pEditView->SetSelection(aSelection);
    }

    aModifyIdle.Start();
    StartCursorMove();

    GrabFocus();
}

void AbstractEditTextWindow::Flush(const sal_uInt16 sid)
{
    EditEngine *pEditEngine = GetEditEngine();
    if (pEditEngine  &&  pEditEngine->IsModified())
    {
        pEditEngine->ClearModifyFlag();
        if (SmViewShell *pViewSh = mrEditWindow.GetView())
        {
            SfxStringItem aTextToFlush(sid, GetText());
            pViewSh->GetViewFrame().GetDispatcher()->ExecuteList(
                    sid, SfxCallMode::RECORD,
                    { &aTextToFlush });
        }
    }
    if (aCursorMoveIdle.IsActive())
    {
        aCursorMoveIdle.Stop();
        CursorMoveTimerHdl(&aCursorMoveIdle);
    }
}

void SmEditTextWindow::Flush()
{
    AbstractEditTextWindow::Flush(SID_TEXT);
}

void ImEditTextWindow::Flush()
{
    AbstractEditTextWindow::Flush(SID_ITEXT);
}

void AbstractEditWindow::DeleteEditView()
{
    if (EditView* pEditView = GetEditView())
    {
        if (EditEngine* pEditEngine = pEditView->GetEditEngine())
        {
            pEditEngine->SetStatusEventHdl( Link<EditStatus&,void>() );
            pEditEngine->RemoveView(pEditView);
        }
        mxTextControlWin.reset();
        mxTextControl.reset();
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
