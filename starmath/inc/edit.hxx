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

#include <svx/weldeditview.hxx>
#include <vcl/idle.hxx>
#include "vcl/weld.hxx"

class SmDocShell;
class SmViewShell;
class EditView;
class EditEngine;
class EditStatus;
class DataChangedEvent;
class SmCmdBoxWindow;
class CommandEvent;
class Timer;

void SmGetLeftSelectionPart(const ESelection& rSelection, sal_Int32& nPara, sal_uInt16& nPos);

class AbstractEditWindow;

class AbstractEditTextWindow : public WeldEditView
{
protected:
    AbstractEditWindow& mrEditWindow;

    Idle aModifyIdle;
    Idle aCursorMoveIdle;

    ESelection aOldSelection;

    DECL_LINK(ModifyTimerHdl, Timer*, void);
    DECL_LINK(CursorMoveTimerHdl, Timer*, void);
    DECL_LINK(EditStatusHdl, EditStatus&, void);

public:
    AbstractEditTextWindow(AbstractEditWindow& rEditWindow);
    virtual ~AbstractEditTextWindow() override;

    virtual EditEngine* GetEditEngine() const override = 0;

    virtual void EditViewScrollStateChange() override;

    virtual void SetDrawingArea(weld::DrawingArea* pDrawingArea) override;

    virtual bool KeyInput(const KeyEvent& rKeyEvt) override;
    virtual bool MouseButtonUp(const MouseEvent& rEvt) override;
    virtual bool Command(const CommandEvent& rCEvt) override;
    virtual void GetFocus() override;
    virtual void LoseFocus() override;
    virtual void StyleUpdated() override;

    void SetText(const OUString& rText);
    void InsertText(const OUString& rText);
    void SelNextMark();
    ESelection GetSelection() const;
    void UserPossiblyChangedText();
    void Flush(const sal_uInt16 sid);
    virtual void Flush() = 0;
    void UpdateStatus(bool bSetDocModified);
    void StartCursorMove();
};

class SmEditTextWindow : public AbstractEditTextWindow
{
public:
    SmEditTextWindow(AbstractEditWindow& rEditWindow);
    virtual ~SmEditTextWindow() override;

    EditEngine* GetEditEngine() const override;

    void Flush() override;
};

class ImEditTextWindow : public AbstractEditTextWindow
{
public:
    ImEditTextWindow(AbstractEditWindow& rEditWindow);
    virtual ~ImEditTextWindow() override;

    EditEngine* GetEditEngine() const override;

    void Flush() override;
};

#define SM_EDITWINDOW_TAB_SM 0
#define SM_EDITWINDOW_TAB_IMGUI 1
#define SM_EDITWINDOW_TAB_IMTXT 2

class AbstractEditWindow
{
protected:
    SmCmdBoxWindow& rCmdBox;
    std::unique_ptr<weld::Notebook> mxNotebook;
    std::unique_ptr<weld::ScrolledWindow> mxScrolledWindow;
    std::unique_ptr<AbstractEditTextWindow> mxTextControl;
    std::unique_ptr<weld::CustomWeld> mxTextControlWin;

    DECL_LINK(ScrollHdl, weld::ScrolledWindow&, void);
    virtual void CreateEditView(weld::Builder& rBuilder) = 0;

public:
    AbstractEditWindow(SmCmdBoxWindow& rMyCmdBoxWin, weld::Builder& rBuilder, const OString& id);
    virtual ~AbstractEditWindow() COVERITY_NOEXCEPT_FALSE;

    weld::Window* GetFrameWeld() const;

    SmDocShell* GetDoc();
    SmViewShell* GetView();
    EditView* GetEditView() const;
    virtual EditEngine* GetEditEngine() = 0;
    SmCmdBoxWindow& GetCmdBox() const { return rCmdBox; }

    void SetText(const OUString& rText);
    OUString GetText() const;
    void Flush();
    virtual void GrabFocus();
    bool HasFocus() const;
    // Is the page of this view current in the notebook?
    virtual bool IsCurrent() const = 0;

    css::uno::Reference<css::datatransfer::clipboard::XClipboard> GetClipboard() const
    {
        return mxTextControl->GetClipboard();
    }

    ESelection GetSelection() const;
    void SetSelection(const ESelection& rSel);
    void UpdateStatus();

    bool IsEmpty() const;
    bool IsSelected() const;
    bool IsAllSelected() const;
    void SetScrollBarRanges();
    tools::Rectangle AdjustScrollBars();
    void InvalidateSlots();
    void Cut();
    void Copy();
    void Paste();
    void Delete();
    void SelectAll();
    void InsertText(const OUString& rText);
    void MarkError(const Point& rPos);
    void SelNextMark();
    void SelPrevMark();

    void DeleteEditView();

    // Allow distinguishing window types. Not good style, but required because we have only one document to handle Starmath and iMath texts
    virtual bool IsImWindow() const = 0;
};

class SmEditWindow : public AbstractEditWindow
{
private:
    void CreateEditView(weld::Builder& rBuilder) override;

public:
    SmEditWindow(SmCmdBoxWindow& rMyCmdBoxWin, weld::Builder& rBuilder);
    virtual ~SmEditWindow() COVERITY_NOEXCEPT_FALSE override;

    EditEngine* GetEditEngine() override;

    void GrabFocus() override;

    bool IsImWindow() const override { return false; }
    bool IsCurrent() const override { return mxNotebook ? (mxNotebook->get_current_page() == SM_EDITWINDOW_TAB_SM) : true; }
};

class ImEditWindow : public AbstractEditWindow
{
private:
    void CreateEditView(weld::Builder& rBuilder) override;

public:
    ImEditWindow(SmCmdBoxWindow& rMyCmdBoxWin, weld::Builder& rBuilder);
    virtual ~ImEditWindow() COVERITY_NOEXCEPT_FALSE override;

    EditEngine* GetEditEngine() override;

    void GrabFocus() override;

    bool IsImWindow() const override { return true; }
    bool IsCurrent() const override { return mxNotebook ? (mxNotebook->get_current_page() == SM_EDITWINDOW_TAB_IMTXT) : false; }
};

class iFormulaLine;
class ImGuiOptionsDialog;

class ImGuiWindow
{
private:
    SmCmdBoxWindow& rCmdBox;
    std::unique_ptr<weld::Builder> mxBuilder;
    std::unique_ptr<weld::Notebook> mxNotebook;
    std::unique_ptr<weld::TreeView> mxFormulaList;
    std::unique_ptr<ImGuiOptionsDialog> mpOptionsDialog;

    DECL_LINK(MousePressHdl, const MouseEvent&, bool);
    int mNumClicks;
    int mClickedColumn;
    DECL_LINK(KeyReleaseHdl, const ::KeyEvent&, bool);

    DECL_LINK(EditingEntryHdl, const weld::TreeIter&, bool);
    typedef std::pair<const weld::TreeIter&, OUString> IterString;
    DECL_LINK(EditedEntryHdl, const IterString&, bool);
    int mEditedColumn;

public:
    ImGuiWindow(SmCmdBoxWindow& rMyCmdBoxWin, weld::Builder& rBuilder);
    virtual ~ImGuiWindow() COVERITY_NOEXCEPT_FALSE;

    weld::Window* GetFrameWeld() const;
    SmDocShell* GetDoc();
    std::shared_ptr<iFormulaLine> GetSelectedLine();

    // Rebuild the TreeView model from the document
    void ResetModel();

    virtual void GrabFocus();
    bool HasFocus() const;
    // Is the page of this view current in the notebook?
    bool IsCurrent() const { return mxNotebook ? (mxNotebook->get_current_page() == SM_EDITWINDOW_TAB_IMGUI) : false; }
};

class ImGuiOptionsDialog final : public weld::GenericDialogController
{
    std::unique_ptr<weld::CheckButton> mxAutoformat;
    std::unique_ptr<weld::CheckButton> mxAutoalign;
    std::unique_ptr<weld::CheckButton> mxAutochain;
    std::unique_ptr<weld::CheckButton> mxAutofraction;
    std::unique_ptr<weld::MetricSpinButton> mxMintextsize;
    std::unique_ptr<weld::CheckButton> mxAutotextmode;

    std::unique_ptr<weld::ComboBox> mxAllunits;
    std::unique_ptr<weld::TreeView> mxActiveunits;
    std::unique_ptr<weld::CheckButton> mxSuppressunits;

    std::unique_ptr<weld::SpinButton> mxPrecision;
    std::unique_ptr<weld::CheckButton> mxFixed;
    std::unique_ptr<weld::SpinButton> mxFixedexponent;
    std::unique_ptr<weld::SpinButton> mxMinposexp;
    std::unique_ptr<weld::SpinButton> mxMaxnegexp;

    std::unique_ptr<weld::CheckButton> mxInhibitunderflow;
    std::unique_ptr<weld::CheckButton> mxAllowimplicit;
    std::unique_ptr<weld::CheckButton> mxEvalrealroots;

    std::unique_ptr<weld::RadioButton> mxDiffline;
    std::unique_ptr<weld::RadioButton> mxDiffdot;
    std::unique_ptr<weld::RadioButton> mxDiffdfdt;

    std::unique_ptr<weld::CheckButton> mxEchoformula;

    DECL_LINK(CheckBoxClickHdl, weld::Toggleable&, void);
    DECL_LINK(SpinButtonModifyHdl, weld::SpinButton&, void);
    DECL_LINK(MetricSpinButtonModifyHdl, weld::MetricSpinButton&, void);
    DECL_LINK(RadioButtonModifyHdl, weld::Toggleable&, void);
    DECL_LINK(ComboBoxHdl, weld::ComboBox&, void);
    DECL_LINK(DoubleClickHdl, weld::TreeView&, bool);
    DECL_LINK(MousePressHdl, const MouseEvent&, bool);

public:
    ImGuiOptionsDialog(weld::Window *pParent, ImGuiWindow* pGuiWindow, std::shared_ptr<iFormulaLine> pLine);
    virtual ~ImGuiOptionsDialog() override;

    // The model was reset, update the line pointer
    void setFormulaLinePointer(std::shared_ptr<iFormulaLine> pLine) { mpLine = pLine; }

private:
    // The formula line for this options dialog
    std::shared_ptr<iFormulaLine> mpLine;
    // The parent edit window
    ImGuiWindow* mpGuiWindow;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
