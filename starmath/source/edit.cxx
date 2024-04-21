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
#include <typeindex>

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
#include <dialog.hxx>
#include <logging.hxx>

#include "imath/option.hxx"
#include "imath/printing.hxx"
#include <imath/iFormulaLine.hxx>

#include <com/sun/star/container/XChild.hpp>

using namespace com::sun::star::accessibility;
using namespace com::sun::star;
using fparts = std::initializer_list<OUString>; // Just a convenient shortcut

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
    // mathmlimport.cxx creates a document and cals SetImText(). But later another SmDocShell() is constructed (copied?) and SetImText() is never called on it, so compilation is not triggered
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

#define IMGUIWINDOW_COL_INSERT_BEFORE 0
#define IMGUIWINDOW_COL_DELETE 1
#define IMGUIWINDOW_COL_HIDE 2
#define IMGUIWINDOW_COL_OPTIONS 3
#define IMGUIWINDOW_COL_LABEL 4
#define IMGUIWINDOW_COL_LABEL_HIDE 5
#define IMGUIWINDOW_COL_TYPE 6
#define IMGUIWINDOW_COL_FORMULA 7
#define IMGUIWINDOW_COL_ERRMSG 8
#define IMGUIWINDOW_COL_LAST 9

ImGuiWindow::ImGuiWindow(SmCmdBoxWindow& rMyCmdBoxWin, weld::Builder& rBuilder)
    : rCmdBox(rMyCmdBoxWin)
    , mxNotebook(rBuilder.weld_notebook("notebook"))
    , mxFormulaList(rBuilder.weld_tree_view("iformulalist"))
    , mpOptionsDialog(nullptr)
    , lastOptionsPage(0)
    , mNumClicks(0)
    , mClickedColumn(-1)
    , mEditedColumn(-1)
{
    if (!mxFormulaList)
        return;

    mxFormulaList->set_size_request(mxFormulaList->get_approximate_digit_width() * 60, mxFormulaList->get_height_rows(5));
    mxFormulaList->enable_toggle_buttons(weld::ColumnToggleType::Check);

    mxFormulaList->connect_key_release(LINK(this, ImGuiWindow, KeyReleaseHdl));
    mxFormulaList->connect_mouse_press(LINK(this, ImGuiWindow, MousePressHdl));
    mxFormulaList->connect_editing(LINK(this, ImGuiWindow, EditingEntryHdl), LINK(this, ImGuiWindow, EditedEntryHdl));
    mxFormulaList->set_selection_mode(SelectionMode::Single);

    ResetModel();

    mxFormulaList->columns_autosize();

ImGuiWindow::~ImGuiWindow() COVERITY_NOEXCEPT_FALSE
{
}

weld::Window* ImGuiWindow::GetFrameWeld() const
{
    return rCmdBox.GetFrameWeld();
}

namespace
{
    static const std::vector<std::type_index> nodesWithHide = {
        typeid(iFormulaNodeEq), typeid(iFormulaNodeEx), typeid(iFormulaNodeConst), typeid(iFormulaNodeFuncdef),
        typeid(iFormulaNodeVectordef), typeid(iFormulaNodeMatrixdef), typeid(iFormulaNodeExplainval), typeid(iFormulaNodePrintval)
    };
    static const std::vector<std::type_index> nodesWithoutFormula = {
        typeid(iFormulaNodeStmClearall), typeid(iFormulaNodeStmReadfile), typeid(iFormulaNodeStmOptions), typeid(iFormulaNodeStmDelete)
    };
}

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
        typeid(iFormulaNodeEq), typeid(iFormulaNodeEx), typeid(iFormulaNodeConst), typeid(iFormulaNodeFuncdef),
        typeid(iFormulaNodeVectordef), typeid(iFormulaNodeMatrixdef), typeid(iFormulaNodeExplainval), typeid(iFormulaNodePrintval)
    };

    for (const auto& fLine : pDoc->GetFormulaLines())
    {
        if (typeid(*fLine) == typeid(iFormulaNodeResult))
            continue;

        mxFormulaList->append(xIter.get());
        mxFormulaList->set_id(*xIter, weld::toId(&fLine));

        // Note: On column layout, see gtkinst.cxx GtkInstanceTreeView::GtkInstanceTreeView()
        // [data columns] id_column [text weight columns] [text sensitive columns]
        // All liststore columns must have treeview columns, otherwise the count goes wrong
        mxFormulaList->set_image(*xIter, BMP_IMGUI_INSERT_BEFORE, IMGUIWINDOW_COL_INSERT_BEFORE);
        mxFormulaList->set_image(*xIter, BMP_IMGUI_DELETE, IMGUIWINDOW_COL_DELETE);
        mxFormulaList->set_image(*xIter, "", IMGUIWINDOW_COL_HIDE); // Note: Toggle remains invisible until we set a value
        mxFormulaList->set_image(*xIter, "", IMGUIWINDOW_COL_OPTIONS); // Note: image columns do not implement set_sensitive()
        mxFormulaList->set_sensitive(*xIter, false, IMGUIWINDOW_COL_LABEL);
        mxFormulaList->set_image(*xIter, "", IMGUIWINDOW_COL_LABEL_HIDE); // Note: Toggle remains invisible until we set a value
        mxFormulaList->set_text(*xIter, fLine->getCommand(), IMGUIWINDOW_COL_TYPE);
        mxFormulaList->set_sensitive(*xIter, true, IMGUIWINDOW_COL_TYPE);
        mxFormulaList->set_sensitive(*xIter, false, IMGUIWINDOW_COL_FORMULA);
        mxFormulaList->set_text(*xIter, fLine->getErrorMessage(), IMGUIWINDOW_COL_ERRMSG); // Tooltip for table row. Note: Column number is hard-coded in .ui file

        if (std::find(nodesWithoutFormula.begin(), nodesWithoutFormula.end(), typeid(*fLine)) == nodesWithoutFormula.end())
        {
            mxFormulaList->set_sensitive(*xIter, true, IMGUIWINDOW_COL_FORMULA);
            mxFormulaList->set_text(*xIter, fLine->printFormula(), IMGUIWINDOW_COL_FORMULA);
        }
        if (std::find(nodesWithHide.begin(), nodesWithHide.end(), typeid(*fLine)) != nodesWithHide.end())
        {
            auto expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(fLine);
            mxFormulaList->set_image(*xIter, expr->getHide() ? OUString(BMP_IMGUI_HIDE) : OUString(BMP_IMGUI_SHOW), IMGUIWINDOW_COL_HIDE);
        }
        if (fLine->isExpression())
        {
            auto expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(fLine);
            mxFormulaList->set_text(*xIter, expr->getLabel(), IMGUIWINDOW_COL_LABEL);
            mxFormulaList->set_sensitive(*xIter, true, IMGUIWINDOW_COL_LABEL);
            option o = fLine->getOption(o_showlabels);
            mxFormulaList->set_image(*xIter, o.value.boolean ? OUString(BMP_IMGUI_SHOWLABEL) : OUString(BMP_IMGUI_HIDELABEL), IMGUIWINDOW_COL_LABEL_HIDE);
        }
        if (fLine->canHaveOptions())
        {
            mxFormulaList->set_image(*xIter, OUString(BMP_IMGUI_OPTIONS), IMGUIWINDOW_COL_OPTIONS);
        }
        {

        }
        {
        else if (typeid(*fLine) == typeid(iFormulaNodeError))
        {
            auto error = std::dynamic_pointer_cast<iFormulaNodeError>(fLine);
            mxFormulaList->set_text(*xIter, fLine->printFormula(), IMGUIWINDOW_COL_FORMULA);
        }

        if (lineCount == currentSelection)
            mxFormulaList->select(*xIter);
        ++lineCount;
    }

    mxFormulaList->columns_autosize();

    if (mpOptionsDialog != nullptr)
        mpOptionsDialog->setFormulaLinePointer(GetSelectedLine());
}

// Note: This will detect the column where the mouse was pressed
// Many UI functions will detect the column where the mouse was released instead
// But that does not work for CellRendererCombo because the mouse release never makes it to our handler
IMPL_LINK(ImGuiWindow, MousePressHdl, const MouseEvent&, rMEvt, bool)
{
    SAL_INFO_LEVEL(1, "starmath.imath", "Mouse press handler with edited column=" << mEditedColumn);

    if (mEditedColumn > 0)
        return false; // Ignore mouse clicks when a cell is being edited

    SmDocShell* pDoc = GetDoc();
        if (!pDoc)
            return false;

    // Detect clicked row and column
    // The alternative is to pass the click on to the next handler if mxFormulaList->get_selected() returns a nullptr
    // In that case the user must first select a line and then click again to take action in some column
    int row = 0;
    if (!getClickedCell(mxFormulaList, rMEvt, row, mClickedColumn, IMGUIWINDOW_COL_LAST))
        return false; // User clicked somewhere else

    if (rMEvt.GetClicks() > 1)
        return false; // We only handle single clicks here

    // Ensure that the clicked line is selected
    mxFormulaList->select(row);
    auto pLine = GetSelectedLine();
    if (pLine == nullptr)
        return false; // line number not found

    switch (mClickedColumn)
    {
        case IMGUIWINDOW_COL_INSERT_BEFORE:
        {
            pDoc->insertFormulaLineBefore(pLine, std::make_shared<iFormulaNodeEq>(GiNaC::unitvec(), pLine->getGlobalOptions(), GiNaC::optionmap(), fparts({"E=m c^2"}), pDoc->GetTempFormulaLabel(), GiNaC::equation(), false));

            break;
        }
        case IMGUIWINDOW_COL_DELETE:
        {
            pDoc->eraseFormulaLine(pLine); // This calls UpdateGuiText()
            break;
        }
        case IMGUIWINDOW_COL_HIDE:
        {
            if (std::find(nodesWithHide.begin(), nodesWithHide.end(), typeid(*pLine)) == nodesWithHide.end())
                return false; // Line cannot be hidden

            iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(pLine);
            if (expr != nullptr)
            {
                expr->setHide(!expr->getHide());
                mxFormulaList->set_image(row, expr->getHide() ? OUString(BMP_IMGUI_HIDE) : OUString(BMP_IMGUI_SHOW), IMGUIWINDOW_COL_HIDE);

                pDoc->UpdateGuiText();
            }
            break;
        }
        case IMGUIWINDOW_COL_OPTIONS:
        {
            if (!pLine->canHaveOptions())
                return false;

            mpOptionsDialog = std::make_unique<ImGuiOptionsDialog>(GetFrameWeld(), this, pLine, lastOptionsPage);
            // Position dialog at bottom center so that it does not hide the formula display
            auto dialogSize = mpOptionsDialog->getDialog()->get_size();
            auto parentSize = GetFrameWeld()->get_size();
            auto parentPos = GetFrameWeld()->get_position();
            mpOptionsDialog->getDialog()->window_move(parentPos.X() + parentSize.Width()/2 - dialogSize.Width()/2, parentPos.Y() + parentSize.Height() - dialogSize.Height());
            mpOptionsDialog->run();
            lastOptionsPage = mpOptionsDialog->getCurrentPage();
            mpOptionsDialog = nullptr;
            break;
        }
        case IMGUIWINDOW_COL_LABEL_HIDE:
        {
            if (!pLine->isExpression())
                return false; // Line type cannot have labels

            option o = pLine->getOption(o_showlabels);
            mxFormulaList->set_image(row, o.value.boolean ? OUString(BMP_IMGUI_SHOW) : OUString(BMP_IMGUI_HIDE), IMGUIWINDOW_COL_LABEL_HIDE);
            pLine->setOption(o_showlabels, !o.value.boolean);

            pDoc->UpdateGuiText();

            break;
        }
        // Note: Text columns are handled in EditedEntryHdl
        case IMGUIWINDOW_COL_LABEL:
        case IMGUIWINDOW_COL_FORMULA:
        {
            if (mxFormulaList->get_sensitive(row, mClickedColumn))
            {
                mEditedColumn = mClickedColumn;
                SAL_INFO_LEVEL(1, "starmath.imath", "Editing detected in column " << mEditedColumn);
            }
            else
                SAL_INFO_LEVEL(1, "starmath.imath", "... but column " << mClickedColumn << " is not sensitive");
            break;
        }
        case IMGUIWINDOW_COL_TYPE:
        {
            // Note: The get_sensitive() fails for CellRendererCombo
            mEditedColumn = mClickedColumn;
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

OUString getLhs(const OUString& equation)
{
    auto idx = equation.indexOf("="); // Note: This does not handle inequalities
    if (idx >= 0)
        return equation.copy(0, idx);
    return "x";
}

OUString getRhs(const OUString& equation)
{
    auto idx = equation.indexOf("="); // Note: This does not handle inequalities
    if (idx >= 0)
        return equation.copy(idx + 1);
    return "x";
}

IMPL_LINK(ImGuiWindow, EditedEntryHdl, const IterString&, rIterString, bool)
{
    SAL_INFO_LEVEL(1, "starmath.imath", "EditedEntryHdl, old string=" + mxFormulaList->get_text(rIterString.first, mEditedColumn));
    SmDocShell* pDoc = GetDoc();
    if (!pDoc)
        goto finished;

    if (mEditedColumn >= 0 && mxFormulaList->get_text(rIterString.first, mEditedColumn) == rIterString.second)
        goto finished; // Nothing changed

    {
        auto pLine = GetSelectedLine();
        if (pLine == nullptr)
            goto finished; // line number not found

        SAL_INFO_LEVEL(1, "starmath.imath", "Edited string=" + rIterString.second + " in column " << mEditedColumn);
        if (mEditedColumn < 0)
            goto finished;

        switch (mEditedColumn)
        {
            case IMGUIWINDOW_COL_LABEL:
            {
                iExpression_ptr expr = std::dynamic_pointer_cast<iFormulaNodeExpression>(pLine);
                if (expr != nullptr)
                {
                    expr->setLabel(rIterString.second);
                    pDoc->UpdateGuiText();
                }
                break;
            }
            case IMGUIWINDOW_COL_TYPE:
            {
                OUString previousType = mxFormulaList->get_text(rIterString.first, IMGUIWINDOW_COL_TYPE);
                OUString newType = rIterString.second;
                if (previousType.equals(newType))
                    goto finished;
                if (newType.equals("ERROR"))
                    goto finished; // Changing the formula type to an error makes no sense
                // VECTORDEF and MATRIXDEF have two different nodes
                if (previousType == "VECTORDEF")
                {
                    auto pTest = std::dynamic_pointer_cast<iFormulaNodeStmVectordef>(pLine);
                    if (pTest)
                        previousType = "STMVECTOR";
                }
                else if (previousType == "MATRIXDEF")
                {
                    auto pTest = std::dynamic_pointer_cast<iFormulaNodeStmMatrixdef>(pLine);
                    if (pTest)
                        previousType = "STMMATRIX";
                }
                SAL_INFO_LEVEL(1, "starmath.imath", "Changing line type from " << previousType << " to " << newType);

                std::shared_ptr<iFormulaLine> pNew = nullptr;
                OUString useEx("x"); // Use parts of the old type for the new type even if they are not 100% compatible
                OUString useEq("E = m c^2");

                if (previousType == "EQDEF")
                {
                    if (newType == "CONSTDEF")
                        pNew = iFormulaLine::move<iFormulaNodeEq, iFormulaNodeConst>(pLine);
                    else if (newType == "FUNCDEF")
                    {
                       iExpression_ptr pExpr = std::dynamic_pointer_cast<iFormulaNodeExpression>(pLine);
                        pNew = std::make_shared<iFormulaNodeFuncdef>(GiNaC::unitvec(), pLine->getGlobalOptions(), GiNaC::optionmap(),
                                                                     fparts({"f", "(", "x", ")", "=", getRhs(pLine->getFormula())}),
                                                                     pExpr->getLabel(), GiNaC::equation(), pExpr->getHide());
                    }
                    else if (newType == "VECTORDEF")
                        pNew = iFormulaLine::move<iFormulaNodeEq, iFormulaNodeVectordef>(pLine);
                    else if (newType == "MATRIXDEF")
                        pNew = iFormulaLine::move<iFormulaNodeEq, iFormulaNodeMatrixdef>(pLine);
                    else
                    {
                        useEq = pLine->getFormula();
                        useEx = getLhs(useEq);
                    }

                }
                else if (previousType == "CONSTDEF")
                {
                    if (newType == "EQDEF")
                        pNew = iFormulaLine::move<iFormulaNodeConst, iFormulaNodeEq>(pLine);
                    else if (newType == "FUNCDEF")
                    {
                       iExpression_ptr pExpr = std::dynamic_pointer_cast<iFormulaNodeExpression>(pLine);
                        pNew = std::make_shared<iFormulaNodeFuncdef>(GiNaC::unitvec(), pLine->getGlobalOptions(), GiNaC::optionmap(),
                                                                     fparts({"f", "(", "x", ")", "=", getRhs(pLine->getFormula())}),
                                                                     pExpr->getLabel(), GiNaC::equation(), pExpr->getHide());
                    }
                    else if (newType == "VECTORDEF")
                        pNew = iFormulaLine::move<iFormulaNodeConst, iFormulaNodeVectordef>(pLine);
                    else if (newType == "MATRIXDEF")
                        pNew = iFormulaLine::move<iFormulaNodeConst, iFormulaNodeMatrixdef>(pLine);
                    else
                    {
                        useEq = pLine->getFormula();
                        useEx = getLhs(useEq);
                    }
                }
                else if (previousType == "FUNCDEF")
                {
                    if (newType == "EQDEF")
                        pNew = iFormulaLine::move<iFormulaNodeFuncdef, iFormulaNodeEq>(pLine);
                    else if (newType == "CONSTDEF")
                        pNew = iFormulaLine::move<iFormulaNodeFuncdef, iFormulaNodeConst>(pLine);
                    else if (newType == "VECTORDEF")
                        pNew = iFormulaLine::move<iFormulaNodeFuncdef, iFormulaNodeVectordef>(pLine);
                    else if (newType == "MATRIXDEF")
                        pNew = iFormulaLine::move<iFormulaNodeFuncdef, iFormulaNodeMatrixdef>(pLine);
                    else
                    {
                        useEq = pLine->getFormula();
                        useEx = getRhs(useEq);
                    }
                }
                else if (previousType == "VECTORDEF")
                {
                    if (newType == "EQDEF")
                        pNew = iFormulaLine::move<iFormulaNodeVectordef, iFormulaNodeEq>(pLine);
                    else if (newType == "CONSTDEF")
                        pNew = iFormulaLine::move<iFormulaNodeVectordef, iFormulaNodeConst>(pLine);
                    else if (newType == "FUNCDEF")
                    {
                       iExpression_ptr pExpr = std::dynamic_pointer_cast<iFormulaNodeExpression>(pLine);
                        pNew = std::make_shared<iFormulaNodeFuncdef>(GiNaC::unitvec(), pLine->getGlobalOptions(), GiNaC::optionmap(),
                                                                     fparts({"f", "(", "x", ")", "=", getRhs(pLine->getFormula())}),
                                                                     pExpr->getLabel(), GiNaC::equation(), pExpr->getHide());
                    }
                    else if (newType == "MATRIXDEF")
                        pNew = iFormulaLine::move<iFormulaNodeVectordef, iFormulaNodeMatrixdef>(pLine);
                    else
                    {
                        useEq = pLine->getFormula();
                        useEx = getRhs(useEq);
                    }
                }
                else if (previousType == "MATRIXDEF")
                {
                    if (newType == "EQDEF")
                        pNew = iFormulaLine::move<iFormulaNodeFuncdef, iFormulaNodeEq>(pLine);
                    else if (newType == "CONSTDEF")
                        pNew = iFormulaLine::move<iFormulaNodeFuncdef, iFormulaNodeConst>(pLine);
                    else if (newType == "FUNCDEF")
                    {
                       iExpression_ptr pExpr = std::dynamic_pointer_cast<iFormulaNodeExpression>(pLine);
                        pNew = std::make_shared<iFormulaNodeFuncdef>(GiNaC::unitvec(), pLine->getGlobalOptions(), GiNaC::optionmap(),
                                                                     fparts({"f", "(", "x", ")", "=", getRhs(pLine->getFormula())}),
                                                                     pExpr->getLabel(), GiNaC::equation(), pExpr->getHide());
                    }
                    else if (newType == "VECTORDEF")
                        pNew = iFormulaLine::move<iFormulaNodeFuncdef, iFormulaNodeVectordef>(pLine);
                    else
                    {
                        useEq = pLine->getFormula();
                        useEx = getRhs(useEq);
                    }
                }
                else if (previousType == "EXDEF")
                {
                    if (newType == "PRINTVAL")
                        pNew = iFormulaLine::move<iFormulaNodeEx, iFormulaNodePrintval>(pLine);
                    else
                    {
                        useEx = pLine->getFormula();
                        useEq = useEx + " = 0";
                    }
                }
                else if (previousType == "PRINTVAL")
                {
                    if (newType == "EXDEF")
                        pNew = iFormulaLine::move<iFormulaNodePrintval, iFormulaNodeEx>(pLine);
                    else
                    {
                        useEx = pLine->getFormula();
                        useEq = useEx + " = 0";
                    }
                }
                else if (previousType == "REALVARDEF")
                {
                    if (newType == "POSVARDEF")
                        pNew = iFormulaLine::move<iFormulaNodeStmRealvardef, iFormulaNodeStmPosvardef>(pLine);
                    else
                    {
                        useEx = pLine->getFormula();
                        useEq = useEx + " = 0";
                    }
                }
                else if (previousType == "POSVARDEF")
                {
                    if (newType == "REALVARDEF")
                        pNew = iFormulaLine::move<iFormulaNodeStmPosvardef, iFormulaNodeStmRealvardef>(pLine);
                    else
                    {
                        useEx = pLine->getFormula();
                        useEq = useEx + " = 0";
                    }
                }
                else if (previousType == "STMVECTOR")
                {
                    if (newType == "MATRIXDEF")
                        pNew = iFormulaLine::move<iFormulaNodeStmVectordef, iFormulaNodeStmMatrixdef>(pLine);
                    else
                    {
                        useEx = pLine->getFormula();
                        useEq = useEx + " = 0";
                    }
                }
                else if (previousType == "STMMATRIX")
                {
                    if (newType == "VECTORDEF")
                        pNew = iFormulaLine::move<iFormulaNodeStmMatrixdef, iFormulaNodeStmVectordef>(pLine);
                    else
                    {
                        useEx = pLine->getFormula();
                        useEq = useEx + " = 0";
                    }
                }

                if (pNew == nullptr)
                {
                    SAL_INFO_LEVEL(1, "starmath.imath", "Conversion not possible, replacing line with a default");
                    auto uvec = GiNaC::unitvec();
                    auto gopt = pLine->getGlobalOptions();
                    if (newType == "EQDEF")
                        pNew = std::make_shared<iFormulaNodeEq>(uvec, gopt, GiNaC::optionmap(), fparts({useEq}), pDoc->GetTempFormulaLabel(), GiNaC::equation(), false);
                    else if (newType == "EXDEF")
                        pNew = std::make_shared<iFormulaNodeEx>(uvec, gopt, GiNaC::optionmap(), fparts({useEx}), "", GiNaC::expression(), false);
                    else if (newType == "CONSTDEF")
                        pNew = std::make_shared<iFormulaNodeConst>(uvec, gopt, GiNaC::optionmap(), fparts({OUString("C=") + useEx}), pDoc->GetTempFormulaLabel(), GiNaC::equation(), false);
                    else if (newType == "FUNCTION")
                        pNew = std::make_shared<iFormulaNodeStmFunction>(gopt, fparts({"{", "{none}", ",", "f", ",", useEx, "}"}), GiNaC::expression());
                    else if (newType == "FUNCDEF")
                        pNew = std::make_shared<iFormulaNodeFuncdef>(uvec, gopt, GiNaC::optionmap(), fparts({"f", "(", "x", ")", "=", useEx}), pDoc->GetTempFormulaLabel(), GiNaC::equation(), false);
                    else if (newType == "VECTORDEF")
                        pNew = std::make_shared<iFormulaNodeVectordef>(uvec, gopt, GiNaC::optionmap(), fparts({"vec v", "=", useEx}), pDoc->GetTempFormulaLabel(), GiNaC::equation(), false);
                    else if (newType == "MATRIXDEF")
                        pNew = std::make_shared<iFormulaNodeMatrixdef>(uvec, gopt, GiNaC::optionmap(), fparts({"bar M", "=", useEx}), pDoc->GetTempFormulaLabel(), GiNaC::equation(), false);
                    else if (newType == "UNITDEF")
                        pNew = std::make_shared<iFormulaNodeStmUnitdef>(gopt, fparts({"{", "\"\"", ",", "\%unit", "=", useEx, "}"}));
                    else if (newType == "OPTIONS")
                        pNew = std::make_shared<iFormulaNodeStmOptions>(gopt,fparts({""}));
                    else if (newType == "CHART")
                        pNew = std::make_shared<iFormulaNodeStmChart>(gopt, fparts({"{", "\"chartname\"", ",", useEx, ",", "0:10", ",", "y", ",", useEx, ",", "1", ",", "\"series\"", "}"}));
                    else if (newType == "TEXT")
                        pNew = std::make_shared<iFormulaNodeText>(uvec, gopt, GiNaC::optionmap(), fparts({"="}), std::vector<std::shared_ptr<textItem>>());
                    else if (newType == "CLEAREQUATIONS")
                        pNew = std::make_shared<iFormulaNodeStmClearall>(gopt);
                    else if (newType == "DELETE")
                        pNew = std::make_shared<iFormulaNodeStmDelete>(gopt, fparts({"{", "@__label__@", "}"}));
                    else if (newType == "EXPLAINVAL")
                        pNew = std::make_shared<iFormulaNodeExplainval>(uvec, gopt, GiNaC::optionmap(), fparts({useEx}), "", GiNaC::expression(), false, GiNaC::expression(), GiNaC::expression(), GiNaC::exhashmap<GiNaC::ex>());
                    else if (newType == "PRINTVAL")
                        pNew = std::make_shared<iFormulaNodePrintval>(uvec, gopt, GiNaC::optionmap(), fparts({useEx}), "", GiNaC::expression(), false, GiNaC::expression(), false, false);
                    else if (newType == "READFILE")
                        pNew = std::make_shared<iFormulaNodeStmReadfile>(gopt, fparts({"{", "\"filename\"", "}"}));
                    else if (newType == "BEGIN_NS")
                        pNew = std::make_shared<iFormulaNodeStmNamespace>(gopt, "BEGIN", fparts({"namespace"}));
                    else if (newType == "END_NS")
                        pNew = std::make_shared<iFormulaNodeStmNamespace>(gopt, "END", fparts({"namespace"}));
                    else if (newType == "PREFIXDEF")
                        pNew = std::make_shared<iFormulaNodeStmPrefixdef>(gopt, fparts({"{", "\%prefix", "=", useEx, "}"}));
                    else if (newType == "REALVARDEF")
                        pNew = std::make_shared<iFormulaNodeStmRealvardef>(gopt, fparts{"r_var"});
                    else if (newType == "POSVARDEF")
                        pNew = std::make_shared<iFormulaNodeStmPosvardef>(gopt, fparts{"p_var"});
                    else if (newType == "UPDATE")
                        pNew = std::make_shared<iFormulaNodeStmUpdate>(gopt, fparts{"\"Object1\""});
                    else if (newType == "SETTABLECELL")
                        pNew = std::make_shared<iFormulaNodeStmTablecell>(gopt, fparts({"{", "\"tablename\"", ",", "\"A1\"", ",", "1", "}"}));
                    else if (newType == "SETCALCCELLS")
                        pNew = std::make_shared<iFormulaNodeStmCalccell>(gopt, fparts({"{", "\"filename\"", ",", "\"tablename\"", ",", "\"A1\"", ",", "1", "}"}));
                    else
                    {
                        pNew = std::make_shared<iFormulaNodeError>(gopt, pLine->print());
                        pNew->markError(pLine->print() + "\n", 5, 5, pLine->print().getLength() - 4, "Invalid formula type");
                    }
                }

                pDoc->insertFormulaLineBefore(pLine, pNew);
                pDoc->eraseFormulaLine(pLine); // This calls UpdateGuiText()
                break;
            }
            case IMGUIWINDOW_COL_FORMULA:
            {
                pLine->setFormula(rIterString.second);
                pDoc->UpdateGuiText();
                break;
            }
        }
    }

finished:
    mEditedColumn = -1;
    return true;
}

IMPL_LINK(ImGuiWindow, KeyReleaseHdl, const ::KeyEvent&, rKEvt, bool)
{
    SmDocShell* pDoc = GetDoc();
    if (!pDoc)
        return false;

    bool handled = false;
    const vcl::KeyCode& rKeyCode = rKEvt.GetKeyCode();

    switch (rKeyCode.GetModifier() | rKeyCode.GetCode())
    {
        case KEY_TAB:
        {
            auto pLine = GetSelectedLine();
            const auto& lines = pDoc->GetFormulaLines();
            auto it = std::find(lines.begin(), lines.end(), pLine);
            if (it == lines.end())
                break;

            while (++it != lines.end() && typeid(**it) == typeid(iFormulaNodeResult));
            if (it == lines.end())
            {
                pDoc->insertFormulaLineBefore(nullptr, std::make_shared<iFormulaNodeEq>(GiNaC::unitvec(), pLine->getGlobalOptions(), GiNaC::optionmap(), fparts({"E=m c^2"}), pDoc->GetTempFormulaLabel(), GiNaC::equation(), false));
                pDoc->UpdateGuiText();
                handled = true;
            }
            break;
        }
    }

    return handled;
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

iFormulaLine_ptr ImGuiWindow::GetSelectedLine()
{
    auto ppLine = weld::fromId<std::shared_ptr<iFormulaLine>*>(mxFormulaList->get_selected_id());
    if (ppLine == nullptr)
        return nullptr; // line not found
    return *ppLine;
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
