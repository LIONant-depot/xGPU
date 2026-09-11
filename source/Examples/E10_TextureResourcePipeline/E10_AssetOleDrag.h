#ifndef E10_ASSET_OLE_DRAG_H
#define E10_ASSET_OLE_DRAG_H
#pragma once

#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <atomic>
#include <vector>
#include <string>

//-----------------------------------------------------------------------------------
//
// Real Win32 OLE drag-and-drop OUT of the Asset Tree, to a real Windows Explorer window (or any other
// shell drop target) - zero precedent anywhere in this codebase before this file (confirmed by an
// exhaustive search for IDropTarget/IDropSource/IDataObject/DoDragDrop/CF_HDROP: none existed). Direct
// user request, explicitly framed as a separate Win32 OLE project distinct from this app's existing,
// entirely in-process ImGui drag-drop (E10_asset_browser_files_tab.h's own "E10_ASSET_FILE_DRAG"
// payload, which only ever works between rows/folders inside this one window).
//
// Scope, deliberately: this file only supports dragging FILES OUT (our rows -> a real shell window).
// Dragging IN (a real Explorer selection -> our window) would require registering this app's own HWND
// as an IDropTarget - a much bigger undertaking given multi-viewport (every undocked panel is its own
// native child HWND, see xgpu_imgui_breach.cpp's CreateChildWindow) and genuinely separate from this
// pass. Not attempted here.
//
// DoDragDrop is inherently modal to the calling thread - it pumps its OWN internal message loop and
// does not return until the drag ends (dropped or cancelled), so this app's own frame loop is paused
// for the duration of a real OS drag gesture (the window will look frozen, exactly like any other
// single-threaded Win32 app mid-drag). Acceptable for a short user gesture; flagged here rather than
// silently glossed over.
//
namespace e10::ole_drag
{
    // Minimal IDropSource: continues the drag while the left button is down, cancels on Escape, and
    // reports "drop now" the instant the button is released - default cursors throughout (no custom
    // drag imagery), matching how Explorer's own drag-out already looks with no help from us.
    struct drop_source : IDropSource
    {
        std::atomic<ULONG> m_Ref{ 1 };

        HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) noexcept override
        {
            if (riid == IID_IUnknown || riid == IID_IDropSource) { *ppv = this; AddRef(); return S_OK; }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG __stdcall AddRef(void) noexcept override { return ++m_Ref; }
        ULONG __stdcall Release(void) noexcept override
        {
            const ULONG N = --m_Ref;
            if (N == 0) delete this;
            return N;
        }

        HRESULT __stdcall QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) noexcept override
        {
            if (fEscapePressed)                 return DRAGDROP_S_CANCEL;
            if (!(grfKeyState & MK_LBUTTON))     return DRAGDROP_S_DROP;
            return S_OK;
        }
        HRESULT __stdcall GiveFeedback(DWORD) noexcept override { return DRAGDROP_S_USEDEFAULTCURSORS; }
    };

    // Minimal IDataObject exposing exactly one format - CF_HDROP - enough for Explorer (and any other
    // standard shell drop target) to accept a real-file drag. No other clipboard format is offered;
    // this app has never needed to produce one before and nothing here requires it.
    struct hdrop_data_object : IDataObject
    {
        std::atomic<ULONG>        m_Ref{ 1 };
        std::vector<std::wstring> m_AbsolutePaths;

        explicit hdrop_data_object(std::vector<std::wstring> Paths) noexcept : m_AbsolutePaths(std::move(Paths)) {}

        static bool IsHDrop(const FORMATETC* pFE) noexcept
        {
            return pFE && pFE->cfFormat == CF_HDROP && (pFE->tymed & TYMED_HGLOBAL) && pFE->dwAspect == DVASPECT_CONTENT;
        }

        HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) noexcept override
        {
            if (riid == IID_IUnknown || riid == IID_IDataObject) { *ppv = this; AddRef(); return S_OK; }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG __stdcall AddRef(void) noexcept override { return ++m_Ref; }
        ULONG __stdcall Release(void) noexcept override
        {
            const ULONG N = --m_Ref;
            if (N == 0) delete this;
            return N;
        }

        HRESULT __stdcall GetData(FORMATETC* pFE, STGMEDIUM* pMedium) noexcept override
        {
            if (!IsHDrop(pFE)) return DV_E_FORMATETC;

            std::size_t Bytes = sizeof(DROPFILES) + sizeof(wchar_t); // trailing double-NUL terminator
            for (auto& P : m_AbsolutePaths) Bytes += (P.size() + 1) * sizeof(wchar_t);

            HGLOBAL hG = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, Bytes);
            if (!hG) return E_OUTOFMEMORY;

            auto* pDF = static_cast<DROPFILES*>(::GlobalLock(hG));
            if (!pDF) { ::GlobalFree(hG); return E_OUTOFMEMORY; }
            pDF->pFiles = sizeof(DROPFILES);
            pDF->fWide  = TRUE;
            auto* pDst  = reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(pDF) + sizeof(DROPFILES));
            for (auto& P : m_AbsolutePaths)
            {
                std::memcpy(pDst, P.c_str(), (P.size() + 1) * sizeof(wchar_t));
                pDst += P.size() + 1;
            }
            *pDst = L'\0';
            ::GlobalUnlock(hG);

            pMedium->tymed          = TYMED_HGLOBAL;
            pMedium->hGlobal        = hG;
            pMedium->pUnkForRelease = nullptr;
            return S_OK;
        }

        HRESULT __stdcall GetDataHere(FORMATETC*, STGMEDIUM*) noexcept override { return E_NOTIMPL; }
        HRESULT __stdcall QueryGetData(FORMATETC* pFE) noexcept override { return IsHDrop(pFE) ? S_OK : DV_E_FORMATETC; }
        HRESULT __stdcall GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pOut) noexcept override { pOut->ptd = nullptr; return E_NOTIMPL; }
        HRESULT __stdcall SetData(FORMATETC*, STGMEDIUM*, BOOL) noexcept override { return E_NOTIMPL; }
        HRESULT __stdcall EnumFormatEtc(DWORD Dir, IEnumFORMATETC** ppEnum) noexcept override
        {
            if (Dir != DATADIR_GET) { *ppEnum = nullptr; return E_NOTIMPL; }
            FORMATETC FE{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            return ::SHCreateStdEnumFmtEtc(1, &FE, ppEnum);
        }
        HRESULT __stdcall DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) noexcept override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT __stdcall DUnadvise(DWORD) noexcept override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT __stdcall EnumDAdvise(IEnumSTATDATA**) noexcept override { return OLE_E_ADVISENOTSUPPORTED; }
    };

    inline void EnsureOleInitialized(void) noexcept
    {
        static bool s_bDone = false;
        if (!s_bDone) { ::OleInitialize(nullptr); s_bDone = true; }
    }

    // Blocks the calling thread until the real OS drag completes (dropped or cancelled). Only call this
    // once the gesture has already left our own window (see files_tab.h's IsCursorOutsideMainWindow) -
    // ReleaseCapture() is required first so DoDragDrop's own internal loop actually receives mouse
    // input instead of it staying captured by our HWND.
    inline void RunFileDragOut(HWND OwnerHwnd, std::vector<std::wstring> AbsolutePaths) noexcept
    {
        if (AbsolutePaths.empty()) return;
        EnsureOleInitialized();

        ::ReleaseCapture();

        auto* pData   = new hdrop_data_object(std::move(AbsolutePaths));
        auto* pSource = new drop_source();

        DWORD Effect = 0;
        ::DoDragDrop(pData, pSource, DROPEFFECT_COPY | DROPEFFECT_MOVE, &Effect);

        pData->Release();
        pSource->Release();

        // The real mouse-up that ended the drag was consumed entirely by DoDragDrop's own modal loop -
        // it never reached OwnerHwnd's WindowProc (mouse capture was released above, and standard hit-
        // testing during the OS drag routes messages to whatever's under the cursor, e.g. Explorer, not
        // us). Without this, this app's own mouse-button bookkeeping (xgpu_windows_window.cpp's
        // WM_LBUTTONDOWN/UP handling, which ImGui's own io.MouseDown is bridged from every frame) would
        // stay stuck believing the left button is still held down. Post a synthetic WM_LBUTTONUP at the
        // CURRENT cursor position so the exact same existing handler resets it correctly - no new
        // engine-internal coupling needed.
        if (OwnerHwnd)
        {
            POINT Pt{};
            ::GetCursorPos(&Pt);
            ::ScreenToClient(OwnerHwnd, &Pt);
            ::PostMessageW(OwnerHwnd, WM_LBUTTONUP, 0, MAKELPARAM(static_cast<short>(Pt.x), static_cast<short>(Pt.y)));
        }
    }
}

#endif
