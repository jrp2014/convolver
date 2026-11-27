// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-12: Convolver's window in VST3 on Windows, as a host opens it: the
// controller's createView("editor") gives an IPlugView for HWND, which is
// sized (getSize, also at a host's content scale), attached to a host
// window on the desktop and removed again. Checks, through the window's
// controls as a user reaches them, that it shows the processor's report,
// that its settings edit the parameters as the SDK asks (beginEdit,
// performEdit, endEdit) when typed in, moved with a key or chosen from the
// list, Bypass too; that a dropped file loads, marks the project changed and
// is reloaded by hand and when it changes; that Copy Diagnostics fills the
// clipboard; that UI Automation (what Narrator and NVDA read) names the
// controls; that Tab moves between them; and that a taller window asks the
// host to resize. With a path argument it keeps a picture of the window
// there (a .bmp), for CI to keep with the run. The test plays the host
// with the same handler and frame as editor_tests.mm (view_host.hpp).
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "editor_win.hpp"
#include "host_log.hpp"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "recent_files.hpp"
#include "view_host.hpp"
#include "window_model.hpp"
#include <windows.h>

#include <commctrl.h>
#include <shlobj.h>
#include <uiautomation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace convolver::vst3;
using namespace convolver::vst3::test;

namespace {
namespace fs = std::filesystem;

std::string utf8(const std::wstring& text) {
    if (text.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n,
                        nullptr, nullptr);
    return out;
}
std::wstring text(HWND control) {
    const int n = GetWindowTextLengthW(control);
    std::wstring out(static_cast<std::size_t>(n) + 1, L'\0');
    out.resize(static_cast<std::size_t>(GetWindowTextW(control, out.data(), n + 1)));
    return out;
}
void pump() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
HWND item(HWND view, int id) {
    HWND control = GetDlgItem(view, id);
    require(control != nullptr, "no control " + std::to_string(id));
    return control;
}
HWND setting(HWND view, int row, int part) {
    return item(view, win::settings + win::setting_stride * row + part);
}
void click(HWND button) {
    SendMessageW(button, BM_CLICK, 0, 0);
    pump();
}
// What the window does on its timers, without waiting for them.
void tick(HWND view, unsigned timer) {
    SendMessageW(view, WM_TIMER, timer, 0);
    pump();
}
// The tooltip windows on this thread. Each belongs to the host's top-level
// window, not the view: a child window cannot own one (PLUG-67).
std::vector<HWND> tip_windows() {
    std::vector<HWND> found;
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND window, LPARAM list) -> BOOL {
            wchar_t kind[64] = {};
            GetClassNameW(window, kind, 64);
            if (std::wstring(kind) == TOOLTIPS_CLASSW)
                reinterpret_cast<std::vector<HWND>*>(list)->push_back(window);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}
// The text of the tooltip that `view`'s window gives `control`, from the
// tooltip window that knows that tool (PLUG-67); empty if none does.
std::wstring tip_text(HWND view, HWND control) {
    for (HWND tips : tip_windows()) {
        std::wstring buffer(1024, L'\0'); // TTM_GETTOOLINFOW may copy the text.
        TOOLINFOW info{};
        info.cbSize = sizeof info;
        info.uFlags = TTF_IDISHWND;
        info.hwnd = view;
        info.uId = reinterpret_cast<UINT_PTR>(control);
        info.lpszText = buffer.data();
        if (!SendMessageW(tips, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&info)))
            continue;
        info.lpszText = buffer.data();
        SendMessageW(tips, TTM_GETTEXTW, buffer.size(), reinterpret_cast<LPARAM>(&info));
        return buffer.c_str();
    }
    return {};
}
// Types `typed` in a setting's box and presses Return.
void type(HWND box, const std::wstring& typed) {
    SetFocus(box);
    SetWindowTextW(box, typed.c_str());
    SendMessageW(box, WM_KEYDOWN, VK_RETURN, 0);
    pump();
}
bool checked(HWND box) {
    return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
HFONT font_of(HWND control) {
    return reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
}
// Whether a static control's text, wrapped at its width in its own font,
// fits its height, so no line is cut off.
bool fits(HWND control) {
    RECT client{};
    GetClientRect(control, &client);
    const auto words = text(control);
    HDC dc = GetDC(control);
    const auto old = SelectObject(dc, font_of(control));
    RECT need{0, 0, std::max<LONG>(client.right, 1), 0};
    DrawTextW(dc, words.c_str(), static_cast<int>(words.size()), &need,
              DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(control, dc);
    return need.bottom - need.top <= client.bottom - client.top;
}

// A file dropped on the window from File Explorer.
void drop(HWND view, const fs::path& file) {
    const std::wstring path = file.wstring();
    const std::size_t bytes = sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    require(memory != nullptr, "drop memory");
    auto* files = static_cast<DROPFILES*>(GlobalLock(memory));
    files->pFiles = sizeof(DROPFILES);
    files->fWide = TRUE;
    std::memcpy(reinterpret_cast<char*>(files) + sizeof(DROPFILES), path.c_str(),
                path.size() * sizeof(wchar_t));
    GlobalUnlock(memory);
    SendMessageW(view, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0); // The window frees it.
    pump();
}

std::wstring clipboard() {
    std::wstring out;
    if (!OpenClipboard(nullptr))
        return out;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* chars = static_cast<const wchar_t*>(GlobalLock(data))) {
            out = chars;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return out;
}

// The name UI Automation gives a control: what a screen reader reads.
std::wstring spoken_name(HWND control) {
    IUIAutomation* automation = nullptr;
    std::wstring name;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IUIAutomation), reinterpret_cast<void**>(&automation))))
        return L"(no UI Automation)";
    IUIAutomationElement* element = nullptr;
    if (SUCCEEDED(automation->ElementFromHandle(control, &element)) && element) {
        BSTR value = nullptr;
        if (SUCCEEDED(element->get_CurrentName(&value)) && value) {
            name = value;
            SysFreeString(value);
        }
        element->Release();
    }
    automation->Release();
    return name;
}

bool under_wine() {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

// The host's window as it is on screen, as a 32-bit .bmp.
void keep_picture(HWND host, const fs::path& file) {
    RECT r;
    GetClientRect(host, &r);
    const int w = r.right, h = r.bottom;
    const HDC screen = GetDC(nullptr);
    const HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h; // Top down.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    const HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    const auto old = SelectObject(dc, bitmap);
    // Windows draws the window and its controls even when it is covered;
    // Wine's PrintWindow draws no child windows, so there it is copied from
    // the screen.
    if (under_wine()) {
        const HDC window_dc = GetDC(host);
        BitBlt(dc, 0, 0, w, h, window_dc, 0, 0, SRCCOPY);
        ReleaseDC(host, window_dc);
    } else {
        PrintWindow(host, dc, PW_CLIENTONLY | 2 /* PW_RENDERFULLCONTENT */);
    }
    GdiFlush();
    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42; // "BM"
    header.bfOffBits = sizeof header + sizeof info.bmiHeader;
    const auto size = static_cast<DWORD>(w) * static_cast<DWORD>(h) * 4;
    header.bfSize = header.bfOffBits + size;
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof header);
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof info.bmiHeader);
    out.write(static_cast<const char*>(pixels), size);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    require(static_cast<bool>(out), "could not write the picture " + file.string());
    std::cout << "the window's picture: " << file.string() << '\n';
}

// A plain top-level window, as a host's plug-in window.
HWND host_window(int w, int h) {
    static const ATOM host_class = [] {
        WNDCLASSEXW c{};
        c.cbSize = sizeof c;
        c.lpfnWndProc = DefWindowProcW;
        c.hInstance = GetModuleHandleW(nullptr);
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        c.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        c.lpszClassName = L"ConvolverTestHost";
        return RegisterClassExW(&c);
    }();
    require(host_class != 0, "host window class");
    RECT r{0, 0, w, h};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    HWND host =
        CreateWindowExW(0, L"ConvolverTestHost", L"Convolver test host",
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 40, 40, r.right - r.left,
                        r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(host != nullptr, "host window");
    ShowWindow(host, SW_SHOWNOACTIVATE);
    pump();
    return host;
}

// Gives the host's window the size the view asks for, as hosts do.
class Resizing final : public IPlugFrame {
  public:
    explicit Resizing(Frame& recorded) : recorded_(recorded) {}
    HWND host = nullptr;
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* size) override {
        if (host && size) {
            RECT r{0, 0, size->getWidth(), size->getHeight()};
            AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
            SetWindowPos(host, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return recorded_.resizeView(view, size);
    }
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        return recorded_.queryInterface(_iid, obj);
    }
    uint32 PLUGIN_API addRef() override {
        return 1;
    }
    uint32 PLUGIN_API release() override {
        return 1;
    }

  private:
    Frame& recorded_;
};

void window(const fs::path& picture) {
    HostApplication context;
    Handler handler; // Outlive the controller and the view, which hold them.
    Frame frame;
    Resizing resizing(frame);
    Processor p;
    Controller c;
    require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk &&
                c.setComponentHandler(&handler) == kResultOk,
            "initialize");
    require(p.connect(&c) == kResultOk && c.connect(&p) == kResultOk, "connect");
    SpeakerArrangement mono = SpeakerArr::kMono;
    require(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk, "buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kRealtime, kSample32, 512, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk, "activate");

    // The recent files beside this test's log (CMake sets CONVOLVER_LOG).
    convolver::forget_recent_files();
    require(c.createView("other") == nullptr, "a view for another name");
    IPlugView* view = c.createView(ViewType::kEditor);
    require(view != nullptr, "no editor view");
    require(view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue &&
                view->isPlatformTypeSupported(kPlatformTypeNSView) != kResultTrue,
            "platform types");
    require(view->canResize() != kResultTrue, "the view says it is resizable");

    // A host's content scale sizes the view before it is attached.
    IPlugViewContentScaleSupport* scaling = nullptr;
    require(view->queryInterface(IPlugViewContentScaleSupport::iid,
                                 reinterpret_cast<void**>(&scaling)) == kResultOk &&
                scaling,
            "no IPlugViewContentScaleSupport");
    ViewRect size;
    require(scaling->setContentScaleFactor(2) == kResultTrue &&
                view->getSize(&size) == kResultTrue && size.getWidth() == 1120,
            "at twice the size: " + std::to_string(size.getWidth()));
    require(
        scaling->setContentScaleFactor(1) == kResultTrue && view->getSize(&size) == kResultTrue &&
            size.getWidth() == 560 && size.getHeight() > 100 && size.getHeight() < 1000,
        "view size " + std::to_string(size.getWidth()) + " x " + std::to_string(size.getHeight()));
    scaling->release();
    require(view->setFrame(&resizing) == kResultTrue, "set frame");

    HWND host = host_window(size.getWidth(), size.getHeight());
    resizing.host = host;
    require(view->attached(host, kPlatformTypeHWND) == kResultOk, "attach");
    pump();
    HWND shown = FindWindowExW(host, nullptr, win::window_class, nullptr);
    require(shown != nullptr && IsWindowVisible(shown),
            "the Convolver window is not in the host's");
    // The test makes each file check itself (tick): the window's own
    // once-a-second check, dispatched by any later pump(), would be a second
    // check, so on a slow runner a changed file reloaded after one tick
    // (CI on f5fa715).
    require(KillTimer(shown, win::files_timer) != FALSE, "the file check timer");
    require(text(item(shown, win::status)) == L"No filter",
            "status " + utf8(text(item(shown, win::status))));
    // PLUG-67 (jrp's first-pass review of 0.18.27 to 0.18.34): the status
    // word's tooltip says what the status means, and follows it.
    const auto no_filter_tip = tip_text(shown, item(shown, win::status));
    require(!no_filter_tip.empty(), "the status word has no tooltip");
    require(text(item(shown, win::title)).find(L"VST3 #") != std::wstring::npos,
            "title " + utf8(text(item(shown, win::title))));
    HWND gain = setting(shown, 0, win::control);
    HWND gain_box = setting(shown, 0, win::value);
    HWND latency = setting(shown, 3, win::control);
    wchar_t kind[64] = {};
    GetClassNameW(gain, kind, 64);
    require(std::wstring(kind) == TRACKBAR_CLASSW && SendMessageW(latency, CB_GETCOUNT, 0, 0) == 8,
            "settings rows");
    require(text(setting(shown, 2, win::low)) == L"Dry" &&
                text(setting(shown, 2, win::high)) == L"Wet",
            "Mix's ends");
    // PLUG-50: the Gain mode list leads the Gain row (its own label is not
    // shown), with a note under the row only when the Gain box cannot say
    // what plays.
    HWND mode = setting(shown, 1, win::control);
    RECT mode_rect{}, gain_rect{};
    GetWindowRect(mode, &mode_rect);
    GetWindowRect(gain, &gain_rect);
    require(SendMessageW(mode, CB_GETCOUNT, 0, 0) == 3 &&
                SendMessageW(mode, CB_GETCURSEL, 0, 0) == 0 &&
                text(setting(shown, 1, win::value)).empty() &&
                !IsWindowVisible(setting(shown, 1, win::label)) &&
                mode_rect.right <= gain_rect.left && mode_rect.top < gain_rect.bottom &&
                gain_rect.top < mode_rect.bottom && IsWindowEnabled(gain),
            "Gain mode in the Gain row");
    handler.edits.clear();
    SendMessageW(mode, CB_SETCURSEL, 2, 0);
    SendMessageW(shown, WM_COMMAND,
                 MAKEWPARAM(win::settings + win::setting_stride + win::control, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(mode));
    pump();
    tick(shown, win::refresh_timer);
    require(handler.kinds(gain_mode_id) == "bpe" &&
                p.settings().gain_mode == convolver::GainMode::no_clipping &&
                text(setting(shown, 1, win::value)) == L"Each filter plays at its own figure." &&
                IsWindowVisible(setting(shown, 1, win::value)) && !IsWindowEnabled(gain) &&
                !IsWindowEnabled(gain_box) && text(gain_box).empty(),
            "Gain mode choice: " + handler.kinds(gain_mode_id) + ", " +
                utf8(text(setting(shown, 1, win::value))));
    // VST3-21 (jrp's review of 0.18.14): the note is in the small type of
    // Mix's note, which its height was measured in; in the larger type it
    // was cut off.
    require(font_of(setting(shown, 1, win::value)) == font_of(item(shown, win::mix_note)) &&
                fits(setting(shown, 1, win::value)),
            "the note under the Gain row is not in the notes' small type, or is cut off");
    SendMessageW(mode, CB_SETCURSEL, 0, 0);
    SendMessageW(shown, WM_COMMAND,
                 MAKEWPARAM(win::settings + win::setting_stride + win::control, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(mode));
    pump();
    require(p.settings().gain_mode == convolver::GainMode::set && IsWindowEnabled(gain) &&
                IsWindowEnabled(gain_box) && text(gain_box) == L"0",
            "Gain mode back to Manual: " + utf8(text(gain_box)));

    // A Gain the host set shows in the window, the trackbar on Gain's taper
    // (PLUG-52: thousandths of its travel, along gain_position()).
    const auto track = [](double db) {
        return static_cast<LRESULT>(std::lround(convolver::view::gain_position(db) * 1000));
    };
    c.setParamNormalized(gain_id, normalized_gain(-6));
    tick(shown, win::refresh_timer);
    require(text(gain_box) == L"-6" && SendMessageW(gain, TBM_GETPOS, 0, 0) == track(-6),
            "Gain from the host: " + utf8(text(gain_box)));
    // A typed value, with a decimal comma: one edit.
    handler.edits.clear();
    type(gain_box, L"-3,5");
    require(handler.kinds(gain_id) == "bpe" &&
                std::abs(c.getParamNormalized(gain_id) - normalized_gain(-3.5)) < 1e-12 &&
                text(gain_box) == L"-3.5",
            "typed Gain: " + handler.kinds(gain_id) + ", " + utf8(text(gain_box)));
    // A key on the trackbar: one edit, 1% of its travel.
    handler.edits.clear();
    SetFocus(gain);
    SendMessageW(gain, WM_KEYDOWN, VK_RIGHT, 0);
    SendMessageW(gain, WM_KEYUP, VK_RIGHT, 0);
    pump();
    const double stepped =
        convolver::view::gain_at_position(static_cast<double>(track(-3.5) + 10) / 1000);
    require(handler.kinds(gain_id) == "bpe" &&
                std::abs(c.getParamNormalized(gain_id) - normalized_gain(stepped)) < 1e-9 &&
                utf8(text(gain_box)) == convolver::setting_text(stepped, -100, 24),
            "Gain from a key: " + handler.kinds(gain_id) + ", " + utf8(text(gain_box)));
    // PLUG-52: Ctrl-click puts the default back, as Option-click does on
    // the Mac: one edit, and the trackbar does not take the click. PLUG-67:
    // also while a value typed in the box was not set with Return: the
    // typing is dropped, neither set first nor refused with a beep.
    for (const wchar_t* typed : {L"", L"-6", L"abc"}) {
        c.setParamNormalized(gain_id, normalized_gain(-3));
        tick(shown, win::refresh_timer);
        if (*typed) {
            SetFocus(gain_box);
            SetWindowTextW(gain_box, typed); // Not set: no Return.
            pump();
        }
        handler.edits.clear();
        SendMessageW(gain, WM_LBUTTONDOWN, MK_CONTROL, MAKELPARAM(2, 2));
        SendMessageW(gain, WM_LBUTTONUP, 0, MAKELPARAM(2, 2));
        pump();
        require(handler.kinds(gain_id) == "bpe" &&
                    std::abs(c.getParamNormalized(gain_id) - normalized_gain(0)) < 1e-12 &&
                    text(gain_box) == L"0" && SendMessageW(gain, TBM_GETPOS, 0, 0) == track(0),
                "Ctrl-click on Gain after typing \"" + utf8(typed) +
                    "\": " + handler.kinds(gain_id) + ", " + utf8(text(gain_box)));
    }
    // PLUG-68 (pre-1.0 review of 0.19.21): a plain click on the track
    // after typing in the box without Return drops the typing too: the
    // click's own change is the one edit, with no edit or beep first for
    // the typed text when the focus leaves the box.
    RECT channel{};
    SendMessageW(gain, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
    const LPARAM on_track = MAKELPARAM(channel.left + 2, (channel.top + channel.bottom) / 2);
    for (const wchar_t* typed : {L"-6", L"abc"}) {
        c.setParamNormalized(gain_id, normalized_gain(-3));
        tick(shown, win::refresh_timer);
        SetFocus(gain_box);
        SetWindowTextW(gain_box, typed); // Not set: no Return.
        pump();
        handler.edits.clear();
        SendMessageW(gain, WM_LBUTTONDOWN, 0, on_track);
        SendMessageW(gain, WM_LBUTTONUP, 0, on_track);
        pump();
        const auto at = SendMessageW(gain, TBM_GETPOS, 0, 0);
        const double now = convolver::view::gain_at_position(static_cast<double>(at) / 1000);
        require(handler.kinds(gain_id) == "bpe" && at < track(-3) &&
                    std::abs(c.getParamNormalized(gain_id) - normalized_gain(now)) < 1e-9 &&
                    utf8(text(gain_box)) == convolver::setting_text(now, -100, 24),
                "a click on the Gain track after typing \"" + utf8(typed) +
                    "\": " + handler.kinds(gain_id) + ", " + utf8(text(gain_box)));
    }

    // A Latency choice: one edit, the processor plays it at once (VST3-16),
    // the host is asked to restart, and the row shows the time it adds.
    handler.edits.clear();
    SendMessageW(latency, CB_SETCURSEL, 5, 0);
    SendMessageW(shown, WM_COMMAND,
                 MAKEWPARAM(win::settings + 3 * win::setting_stride + win::control, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(latency));
    pump();
    require(handler.kinds(latency_id) == "bpe" && p.settings().latency == 1024 &&
                p.getLatencySamples() == 1024 && handler.latency_restarts == 1,
            "Latency choice: " + handler.kinds(latency_id));
    require(text(setting(shown, 3, win::value)) == L"adds 21.3 ms",
            "Latency note " + utf8(text(setting(shown, 3, win::value))));
    // In the open list, the items the arrow keys pass are not choices (each
    // would rebuild the engine), and Escape puts the list back.
    handler.edits.clear();
    SendMessageW(latency, CB_SHOWDROPDOWN, TRUE, 0);
    require(SendMessageW(latency, CB_GETDROPPEDSTATE, 0, 0) != FALSE, "the Latency list opens");
    SendMessageW(latency, CB_SETCURSEL, 4, 0);
    const WPARAM latency_id_code =
        MAKEWPARAM(win::settings + 3 * win::setting_stride + win::control, 0);
    SendMessageW(shown, WM_COMMAND, latency_id_code | (CBN_SELCHANGE << 16),
                 reinterpret_cast<LPARAM>(latency));
    require(handler.edits.empty() && p.settings().latency == 1024,
            "an item passed in the open Latency list was chosen: " + handler.kinds(latency_id));
    SendMessageW(shown, WM_COMMAND, latency_id_code | (CBN_SELENDCANCEL << 16),
                 reinterpret_cast<LPARAM>(latency));
    require(SendMessageW(latency, CB_GETCURSEL, 0, 0) == 5,
            "Escape did not put the Latency list back");
    SendMessageW(latency, CB_SHOWDROPDOWN, FALSE, 0);
    pump();
    require(handler.edits.empty() && p.settings().latency == 1024,
            "closing the Latency list changed it: " + handler.kinds(latency_id));

    // Bypass: a click is one edit, and the host's own bypass shows.
    HWND bypass = item(shown, win::bypass);
    require(IsWindowVisible(bypass) && !checked(bypass) && text(bypass) == L"Bypass",
            "Bypass checkbox");
    handler.edits.clear();
    click(bypass);
    require(handler.kinds(bypass_id) == "bpe" && c.getParamNormalized(bypass_id) == 1.0 &&
                checked(bypass),
            "Bypass click edits: " + handler.kinds(bypass_id));
    c.setParamNormalized(bypass_id, 0.0);
    tick(shown, win::refresh_timer);
    require(!checked(bypass), "Bypass from the host");

    // A file dropped on the window loads and marks the project changed.
    const auto folder = fs::temp_directory_path() /
                        ("convolver-vst3-editor-" + std::to_string(std::random_device{}()));
    fs::create_directories(folder);
    const auto file = folder / "room.wav";
    write_wav(file);
    drop(shown, file);
    require(handler.dirty == 1, "dropped file not loaded");
    require(text(item(shown, win::status)) == L"Active" &&
                text(item(shown, win::filter_name)) == L"room.wav" &&
                IsWindowVisible(item(shown, win::fact_values)),
            "after loading: " + utf8(text(item(shown, win::status))) + ", " +
                utf8(text(item(shown, win::filter_name))));
    require(convolver::recent_files().size() == 1, "not in the recent files");
    const auto active_tip = tip_text(shown, item(shown, win::status));
    require(!active_tip.empty() && active_tip != no_filter_tip,
            "the status word's tooltip did not follow the status: " + utf8(active_tip));

    // PLUG-50: the Choose Filter menu's Reload reads it again; a change to
    // the file reloads it once the change has settled for one check; with
    // Reload When the File Changes unticked it does not. The window takes
    // the menu's commands as WM_COMMAND, so the menu need not open.
    const auto menu = [&](int command) {
        SendMessageW(shown, WM_COMMAND, MAKEWPARAM(command, 0), 0);
        pump();
    };
    menu(win::menu_reload);
    require(handler.dirty == 2, "Reload did not load the file again");
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(file, now + std::chrono::minutes(1));
    tick(shown, win::files_timer);
    require(handler.dirty == 2, "reloaded before the change settled");
    tick(shown, win::files_timer);
    require(handler.dirty == 3, "a changed file was not reloaded");
    tick(shown, win::files_timer);
    require(handler.dirty == 3, "reloaded again without a change");
    menu(win::menu_watch); // Off.
    fs::last_write_time(file, now + std::chrono::minutes(2));
    tick(shown, win::files_timer);
    tick(shown, win::files_timer);
    require(handler.dirty == 3, "reloaded with the automatic reload unticked");
    menu(win::menu_watch); // On again: the change made while off is not reloaded.
    tick(shown, win::files_timer);
    tick(shown, win::files_timer);
    require(handler.dirty == 3, "reloaded a change made while the automatic reload was unticked");
    // A recent file from the menu loads it.
    menu(win::menu_recent_first);
    require(handler.dirty == 4, "the recent file did not load");

    // Copy Diagnostics puts the report and the log lines on the clipboard.
    click(item(shown, win::diagnostics));
    require(clipboard().find(L"Recent Convolver log lines for VST3 #") != std::wstring::npos,
            "diagnostics: " + utf8(clipboard().substr(0, 200)));
    require(text(item(shown, win::diagnostics)) == L"Copied", "the button does not say Copied");
    tick(shown, win::copied_timer);
    require(text(item(shown, win::diagnostics)) == L"Copy Diagnostics", "Copied stays");

    // What a screen reader reads, through UI Automation. Wine (where a
    // Linux session can run this test) does not give the names a window
    // sets through IAccPropServices, so it skips this part there.
    for (const auto& [control, name] :
         {std::pair{gain, L"Gain"}, std::pair{gain_box, L"Gain"}, std::pair{latency, L"Latency"},
          std::pair{item(shown, win::choose), L"Choose Filter"},
          std::pair{item(shown, win::bypass), L"Bypass"}}) {
        if (under_wine())
            break;
        const auto spoken = spoken_name(control);
        require(spoken == name, "UI Automation name " + utf8(spoken) + ", not " + utf8(name));
    }

    // Tab moves from control to control, in reading order: Choose Filter,
    // then the Gain row's Gain mode list, its trackbar and its box.
    SetFocus(item(shown, win::choose));
    SendMessageW(item(shown, win::choose), WM_KEYDOWN, VK_TAB, 0);
    require(GetFocus() == mode, "Tab from Choose Filter");
    SendMessageW(mode, WM_KEYDOWN, VK_TAB, 0);
    require(GetFocus() == gain, "Tab from Gain mode");
    SendMessageW(gain, WM_KEYDOWN, VK_TAB, 0);
    require(GetFocus() == gain_box, "Tab from the Gain trackbar");

    // Showing the file, software and log lines makes the window taller: it
    // asks the host for the new size.
    frame.asked.clear();
    click(item(shown, win::reference_toggle));
    RECT own;
    GetWindowRect(shown, &own);
    require(IsWindowVisible(item(shown, win::reference)) && !frame.asked.empty() &&
                frame.asked.back().getHeight() == own.bottom - own.top &&
                frame.asked.back().getWidth() == 560,
            "no resize request for a taller window");
    ViewRect now_size;
    require(view->getSize(&now_size) == kResultTrue &&
                now_size.getHeight() == frame.asked.back().getHeight(),
            "the host's size was not kept");
    RedrawWindow(host, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    pump();
    if (!picture.empty())
        keep_picture(host, picture);

    // A refused file: the earlier filter still plays, the message says why
    // in its box, and Choose Filter is drawn as the button that fixes it.
    const auto broken = folder / "broken.wav";
    std::ofstream(broken) << "not a sound file";
    drop(shown, broken);
    HWND message = item(shown, win::message);
    require(text(item(shown, win::status)) == L"Load failed" && IsWindowVisible(message) &&
                text(message).find(L"broken.wav") != std::wstring::npos &&
                (GetWindowLongPtrW(item(shown, win::choose), GWL_STYLE) & 0xF) == BS_DEFSPLITBUTTON,
            "a refused file: " + utf8(text(item(shown, win::status))) + ", " + utf8(text(message)));
    if (!picture.empty()) {
        RedrawWindow(host, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        pump();
        auto refused = picture;
        refused.replace_filename(picture.stem().string() + "-refused" +
                                 picture.extension().string());
        keep_picture(host, refused);
    }
    // The box keeps its tint when the system's settings change (a font,
    // the colours, a DPI change make the fonts again).
    const auto tinted = [&] {
        HDC dc = GetDC(message);
        SendMessageW(shown, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc),
                     reinterpret_cast<LPARAM>(message));
        const bool differs = GetBkColor(dc) != GetSysColor(COLOR_WINDOW);
        ReleaseDC(message, dc);
        return differs;
    };
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof contrast;
    const bool high = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof contrast, &contrast, 0) &&
                      (contrast.dwFlags & HCF_HIGHCONTRASTON);
    if (!high) {
        require(tinted(), "the problem's box is not tinted");
        SendMessageW(shown, WM_SETTINGCHANGE, SPI_SETNONCLIENTMETRICS, 0);
        SendMessageW(shown, WM_SYSCOLORCHANGE, 0, 0);
        require(tinted(), "the problem's box lost its tint after a settings change");
    }

    const auto tips_open = tip_windows().size();
    require(view->removed() == kResultOk &&
                FindWindowExW(host, nullptr, win::window_class, nullptr) == nullptr,
            "remove");
    // PLUG-67: closing the view takes its tooltips with it; they belong to
    // the host's window, which stays.
    require(tip_windows().size() + 1 == tips_open,
            "closing the window left its tooltips: " + std::to_string(tip_windows().size()) +
                " tooltip windows, " + std::to_string(tips_open) + " while open");
    // Attached again, the same view makes everything afresh: the Audio rows,
    // the folded file lines, the problem in its box.
    require(view->attached(host, kPlatformTypeHWND) == kResultOk, "attach again");
    pump();
    shown = FindWindowExW(host, nullptr, win::window_class, nullptr);
    require(shown != nullptr && IsWindowVisible(shown), "no window when attached again");
    HWND first_fact = item(shown, win::audio_rows + 1);
    require(first_fact != nullptr && IsWindowVisible(first_fact) && !text(first_fact).empty(),
            "no Audio rows when attached again");
    require(!checked(item(shown, win::reference_toggle)) &&
                !IsWindowVisible(item(shown, win::reference)),
            "the file lines show while their box is unticked");
    require(text(item(shown, win::status)) == L"Load failed" &&
                IsWindowVisible(item(shown, win::message)),
            "the problem is not shown when attached again");
    require(text(item(shown, win::filter_note)).rfind(L"broken.wav was refused.", 0) == 0,
            "the refused file's note: " + utf8(text(item(shown, win::filter_note))));
    // PLUG-52: a shorter message after a longer one (Active after the
    // refusal) keeps the window's height, and the controls stay where they
    // were; the line under the name says which file is watched for changes.
    const auto own_rect = [](HWND control) {
        RECT r{};
        GetWindowRect(control, &r);
        return r;
    };
    const RECT window_then = own_rect(shown), choose_then = own_rect(item(shown, win::choose));
    menu(win::menu_reload);
    tick(shown, win::refresh_timer);
    const RECT window_now = own_rect(shown), choose_now = own_rect(item(shown, win::choose));
    require(text(item(shown, win::status)) == L"Active" &&
                window_now.bottom - window_now.top == window_then.bottom - window_then.top &&
                choose_now.top - window_now.top == choose_then.top - window_then.top,
            "a shorter message moved the controls: " + utf8(text(item(shown, win::status))) +
                ", height " + std::to_string(window_now.bottom - window_now.top) + ", was " +
                std::to_string(window_then.bottom - window_then.top));
    require(text(item(shown, win::filter_note)) == L"Reloads when room.wav changes.",
            "watch note: " + utf8(text(item(shown, win::filter_note))));
    menu(win::menu_watch); // Off: the line stays, empty.
    require(text(item(shown, win::filter_note)).empty() &&
                own_rect(shown).bottom - own_rect(shown).top == window_now.bottom - window_now.top,
            "watch note while off: " + utf8(text(item(shown, win::filter_note))));
    menu(win::menu_watch);
    // PLUG-67: a window closed while it showed a refusal, whose filter was
    // fixed while it was closed, opens as tall as a new window showing what
    // plays now, without the refusal's height as a blank gap.
    drop(shown, broken);
    require(text(item(shown, win::status)) == L"Load failed", "the refusal again");
    const RECT refused_window = own_rect(shown);
    require(view->removed() == kResultOk, "remove while refused");
    require(c.load_file(file.string()), "the filter fixed while the window was closed");
    require(view->attached(host, kPlatformTypeHWND) == kResultOk, "attach after the fix");
    pump();
    shown = FindWindowExW(host, nullptr, win::window_class, nullptr);
    require(shown != nullptr && text(item(shown, win::status)) == L"Active",
            "not Active when attached after the fix");
    const RECT fixed_window = own_rect(shown);
    require(fixed_window.bottom - fixed_window.top < refused_window.bottom - refused_window.top,
            "the window opened with the closed refusal's height: " +
                std::to_string(fixed_window.bottom - fixed_window.top) + ", refused " +
                std::to_string(refused_window.bottom - refused_window.top));
    // A host that destroys its window before removing the view.
    DestroyWindow(host);
    pump();
    require(view->removed() == kResultOk, "remove after the host's window went");
    view->setFrame(nullptr);
    view->release();
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    convolver::forget_recent_files();
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    c.terminate();
    p.terminate();
}

// PLUG-22 with PLUG-23: a 2->6 crossover on a six-channel track has no dry
// signal, so Mix is dimmed with the report's note under it, across
// refreshes, and the Audio section names the ignored inputs.
void mix_note() {
    HostApplication context;
    Handler handler;
    Frame frame;
    Processor p;
    Controller c;
    require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk &&
                c.setComponentHandler(&handler) == kResultOk && p.connect(&c) == kResultOk &&
                c.connect(&p) == kResultOk,
            "mix note initialize");
    SpeakerArrangement six = SpeakerArr::k51;
    require(p.setBusArrangements(&six, 1, &six, 1) == kResultOk, "mix note buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    convolver::HostState saved;
    convolver::Configuration x{48000, 2, 6, 0, {}, {}, {}};
    for (std::size_t o = 0; o < 6; ++o)
        x.paths.push_back({{1.f}, {{o % 2, 1}}, {{o, 1}}});
    saved.configuration = x;
    MemoryStream state;
    require(write_state(&state, saved), "mix note state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    ProcessSetup setup{kRealtime, kSample32, 512, 48000};
    require(p.setState(&state) == kResultOk && p.setupProcessing(setup) == kResultOk &&
                p.setActive(true) == kResultOk,
            "mix note activate");
    IPlugView* view = c.createView(ViewType::kEditor);
    ViewRect size;
    require(view && view->getSize(&size) == kResultTrue && view->setFrame(&frame) == kResultTrue,
            "mix note view");
    HWND host = host_window(size.getWidth(), size.getHeight());
    require(view->attached(host, kPlatformTypeHWND) == kResultOk, "mix note attach");
    HWND shown = FindWindowExW(host, nullptr, win::window_class, nullptr);
    require(shown != nullptr, "mix note: no window");
    for (int i = 0; i < 3; ++i) {
        tick(shown, win::refresh_timer);
        HWND note = item(shown, win::mix_note);
        require(text(item(shown, win::status)) == L"Active" && IsWindowVisible(note) &&
                    text(note).find(L"Mix has no effect") != std::wstring::npos &&
                    !IsWindowEnabled(setting(shown, 2, win::control)) &&
                    !IsWindowEnabled(setting(shown, 2, win::value)) &&
                    IsWindowEnabled(setting(shown, 0, win::control)),
                "Mix not dimmed with a note, refresh " + std::to_string(i) + ": " +
                    utf8(text(note)));
    }
    bool ignored = false;
    for (int id = win::audio_rows; id < win::audio_rows + 60; ++id)
        if (HWND fact = GetDlgItem(shown, id))
            ignored = ignored || text(fact).find(L"inputs 3\u20136 ignored") != std::wstring::npos;
    require(ignored, "Host channels does not name the ignored inputs");
    require(view->removed() == kResultOk, "mix note remove");
    view->release();
    DestroyWindow(host);
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    c.terminate();
    p.terminate();
}
} // namespace

int main(int argc, char** argv) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int status = 0;
    try {
        window(argc > 1 ? fs::path(argv[1]) : fs::path());
        mix_note();
        std::cout << "VST3 window checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        status = 1;
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return status;
}
