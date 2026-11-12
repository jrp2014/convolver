// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-12: Convolver's window in VST3 hosts on Windows, the same window as on
// macOS (docs/use/plugins.md#convolvers-window) built from Win32's standard
// controls, as the windows plan chose
// (docs/project/plans/2026-11-10-windows-linux-plugin-windows.md#the-win32-window).
// What it shows and what a user's action means come from the shared window
// model (portable/window_model.hpp), so both windows behave the same; this
// file keeps the controls and their layout.
//
// The view is the SDK's EditorView for kPlatformTypeHWND: the host asks its
// size (getSize), then attaches it to an HWND of its own (attached), where
// the view adds a child window holding the controls. The view asks the host
// to resize when its content changes height (IPlugFrame::resizeView) and is
// not resizable by the user. It talks only to the edit controller, on the
// host's UI thread, as editor.mm does on macOS: settings as parameter edits
// (beginEdit, performEdit, endEdit), the report the processor sent, and a
// chosen file as a message the processor handles (Controller::load_file).
//
// The rest, as the plan describes it:
// - Common Controls 6: a DLL gets the current control styles only through an
//   activation context of its own, from the manifest built into it
//   (editor_win.manifest); the window creates its controls inside it.
// - Screen scaling: everything is laid out in units of 1/96 inch and scaled
//   to the window's DPI (GetDpiForWindow), with the system's message font at
//   that DPI; WM_DPICHANGED_AFTERPARENT, or the host's
//   IPlugViewContentScaleSupport, lays it out again.
// - Keyboard: the window is a control parent (WS_EX_CONTROLPARENT) and every
//   control moves the focus on Tab itself, since a plug-in does not run the
//   message loop that would call IsDialogMessage.
// - Light only: Win32's standard controls have no supported dark theme. In
//   high-contrast mode the window uses the system's colours only.
// - Accessibility: each trackbar, box and list is named by its setting
//   (IAccPropServices), so Narrator and NVDA read "Gain" through UI
//   Automation; the status is a word, never only a colour.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10: GetDpiForWindow and the rest.
#endif
#ifndef WINVER
#define WINVER _WIN32_WINNT
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "editor_win.hpp"
#include "editor.hpp"
#include "host_log.hpp"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "processor.hpp"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "recent_files.hpp"
#include "utf8.hpp"
#include "window_model.hpp"
#include <windows.h>

#include <commctrl.h>
#include <oleacc.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace convolver::vst3 {
namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace convolver::view;

// oleacc.h declares these two but the Windows SDK's import libraries do not
// define them (MinGW's do), so the window carries its own copies.
constexpr CLSID access_services = {
    0xb5f8350b, 0x0548, 0x48b1, {0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7}};
constexpr MSAAPROPID accessible_name = {
    0x608d3df8, 0x8128, 0x4aa7, {0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91}};

// The layout, in units of 1/96 inch, as the macOS window's points
// (the AppKit window VST3 and the Audio Unit share): a fixed width and a height that
// fits the content, one column top to bottom in reading and Tab order, the
// form's labels right-aligned with a colon.
constexpr double width = 560;
constexpr double margin = 20;
constexpr double inner = width - 2 * margin;
constexpr double form_label = 64;  // "Latency:" and the other form labels.
constexpr double form_gap = 10;    // Between a form label and its row.
constexpr double fact_label = 120; // "Host sample rate" and the other Audio labels.
constexpr double padding = 10;     // Inside the box round a problem.
constexpr double most_text = 200;  // A longer message scrolls.
constexpr double value_box = 56;   // A setting's typed value.
constexpr double unit_width = 40;
constexpr double button_height = 23;
constexpr double box_height = 21; // A typed value's box.
constexpr double check_height = 17;
constexpr double trackbar_height = 26;
constexpr double dot = 10; // The status's dot.
constexpr UINT refresh_ms = 500, files_ms = 1000, copied_ms = 2000;
constexpr int recent_first = 1000; // The Recent menu's commands.
constexpr int recent_clear = 999;

// Text between the shared model's UTF-8 and Win32's UTF-16; line ends as an
// edit box wants them.
std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    const int n =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(n, 0)), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}
std::string utf8(const std::wstring& text) {
    if (text.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(n, 0)), '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n,
                            nullptr, nullptr);
    return out;
}
std::wstring crlf(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text) {
        if (c == L'\n')
            out += L'\r';
        out += c;
    }
    return out;
}
std::wstring window_text(HWND control) {
    const int n = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(std::max(n, 0)) + 1, L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(control, text.data(), n + 1)));
    return text;
}
void set_text(HWND control, const std::wstring& text) {
    if (control && window_text(control) != text)
        SetWindowTextW(control, text.c_str());
}

// This DLL, for its window class, its manifest and its activation context.
HMODULE own_module() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&own_module), &module);
    return module;
}

// What every open window shares: the window class and the activation
// context for Common Controls 6, made by the first window and released by
// the last, since a DLL's classes outlive it otherwise.
struct Shared {
    int windows = 0;
    HANDLE context = INVALID_HANDLE_VALUE;
    ATOM window_class = 0;
};
Shared& shared() {
    static Shared s;
    return s;
}

// Controls created while this is alive take Common Controls 6's styles.
class Styles {
  public:
    Styles() {
        if (shared().context != INVALID_HANDLE_VALUE)
            active_ = ActivateActCtx(shared().context, &cookie_) != FALSE;
    }
    ~Styles() {
        if (active_)
            DeactivateActCtx(0, cookie_);
    }
    Styles(const Styles&) = delete;
    Styles& operator=(const Styles&) = delete;

  private:
    ULONG_PTR cookie_ = 0;
    bool active_ = false;
};

// COM on this thread for the file dialog, Explorer and the accessible names;
// hosts have usually started it already.
class Com {
  public:
    Com() : started_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ~Com() {
        if (started_)
            CoUninitialize();
    }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;

  private:
    bool started_;
};

bool high_contrast() {
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof contrast;
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof contrast, &contrast, 0) &&
           (contrast.dwFlags & HCF_HIGHCONTRASTON);
}

COLORREF blend(COLORREF over, COLORREF under, double alpha) {
    const auto mix = [&](int a, int b) {
        return static_cast<BYTE>(std::lround(a * alpha + b * (1 - alpha)));
    };
    return RGB(mix(GetRValue(over), GetRValue(under)), mix(GetGValue(over), GetGValue(under)),
               mix(GetBValue(over), GetBValue(under)));
}

// DIAG-2's message as the window's text: the problem and what plays, then
// the cause and the fix on lines of their own.
std::wstring message_text(const WindowContent& c) {
    std::wstring text = wide(c.problem);
    if (!c.plays.empty())
        text += (text.empty() ? L"" : L" ") + wide(c.plays);
    if (!c.cause.empty())
        text += (text.empty() ? L"" : L"\r\n") + std::wstring(L"Cause: ") + wide(c.cause);
    if (!c.fix.empty())
        text += (text.empty() ? L"" : L"\r\n") + std::wstring(L"Fix: ") + wide(c.fix);
    return text;
}
// The file, software and log lines, "Label: value" each.
std::wstring reference_text(const std::vector<ReferenceLine>& lines) {
    std::wstring text;
    for (const auto& line : lines)
        text += wide(line.label.empty() ? line.value : line.label + ": " + line.value) + L"\r\n";
    return text;
}
constexpr std::array<const wchar_t*, 4> fact_names{L"Sample rate", L"Channels", L"Length",
                                                   L"Speakers"};

// One of the settings (Gain, Gain mode, Mix, Latency) as a row of the form.
struct Setting {
    ParamID tag = 0;
    std::wstring name, unit, low, high, tip;
    bool list = false; // Gain mode, Latency: a list whose value is the item's index.
    double minimum = 0, maximum = 1, initial = 0;
    std::vector<std::wstring> choices;
    HWND label = nullptr, control = nullptr, value = nullptr, unit_label = nullptr;
    HWND low_label = nullptr, high_label = nullptr;
    bool typed = false; // The user has changed the text in its box.
};

// One Audio fact's row.
struct AudioRow {
    HWND label = nullptr, value = nullptr, note = nullptr;
};

class Editor final : public EditorView, public IPlugViewContentScaleSupport {
  public:
    explicit Editor(Controller& controller);
    ~Editor() override;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override {
        return type && std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void* parent, FIDString type) override;
    tresult PLUGIN_API removed() override;
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override;

    OBJ_METHODS(Editor, EditorView)
    DEFINE_INTERFACES
    DEF_INTERFACE(IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(EditorView)
    REFCOUNT_METHODS(EditorView)

  private:
    static LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK control_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT handle(UINT, WPARAM, LPARAM);
    bool control_key(HWND control, UINT message, WPARAM key);

    Controller& owner() {
        return *static_cast<Controller*>(controller.get());
    }
    Parameter* parameter(ParamID tag) {
        return controller ? controller->getParameterObject(tag) : nullptr;
    }
    void add_setting(ParamID tag, const wchar_t* name, const wchar_t* unit);

    // Building, measuring and placing the controls.
    void create_controls();
    HWND make(const wchar_t* type, const std::wstring& text, DWORD style, int id,
              DWORD extended = 0);
    void name_for_screen_readers(HWND control, const std::wstring& name);
    void scale_to(double scale);
    void make_fonts();
    void delete_fonts();
    void make_tint();
    void close_window();
    void forget_controls();
    int px(double units) const {
        return static_cast<int>(std::lround(units * scale_));
    }
    struct Extent {
        int cx = 0, cy = 0;
    };
    Extent extent(HFONT font, const std::wstring& text) const;
    int wrapped_height(HFONT font, const std::wstring& text, int wide) const;
    int arrange(bool place);
    void place(HWND control, int x, int y, int w, int h, bool shown = true);
    void follow_size();

    // Reading the report and the settings.
    void refresh();
    WindowContent read_content();
    void show(const WindowContent& c);
    void rebuild_audio(const std::vector<AudioFact>& facts);
    void read_settings();
    void read_bypass();
    double plain(const Setting& s);
    int position(const Setting& s, double value) const;

    // What the user does.
    void command(int id, int code, HWND control);
    void scrolled(HWND control, int code);
    void set_value(std::size_t i, double value);
    void begin_gesture(std::size_t i);
    void end_gesture();
    void apply_typed(std::size_t i);
    bool load(const std::string& path);
    void choose_filter();
    void stop_choosing();
    void recent_menu();
    void reload();
    void check_files();
    void copy_diagnostics();
    void show_log();
    void dropped(HDROP drop);

    HWND parent_ = nullptr, hwnd_ = nullptr;
    double scale_ = 1;
    std::optional<double> host_scale_; // From setContentScaleFactor, until attached.
    HFONT body_ = nullptr, small_ = nullptr, heading_ = nullptr, strong_ = nullptr;
    HBRUSH background_ = nullptr, tint_ = nullptr;
    COLORREF tint_colour_ = 0;
    COLORREF grey_ =
        0; // Secondary text: the system's grey text colour when the controls were made.
    HDC measure_ = nullptr;

    HWND title_ = nullptr, bypass_ = nullptr, name_ = nullptr, status_ = nullptr;
    std::array<HWND, 4> fact_headings_{}, fact_values_{};
    HWND filter_note_ = nullptr, message_ = nullptr, note_label_ = nullptr, note_ = nullptr;
    HWND choose_ = nullptr, recent_ = nullptr, reload_ = nullptr, watch_box_ = nullptr;
    HWND mix_note_ = nullptr, audio_heading_ = nullptr;
    HWND reference_toggle_ = nullptr, diagnostics_ = nullptr, log_ = nullptr, reference_ = nullptr;
    std::vector<Setting> settings_;
    std::size_t mix_ = SIZE_MAX;
    std::vector<AudioRow> audio_;
    std::vector<HWND> named_; // Controls with a name for screen readers, cleared on close.

    WindowContent content_; // What the window shows.
    std::optional<WindowContent>
        shown_; // What the controls show, so equal content keeps a selection.
    std::map<HWND, COLORREF> colours_; // Text colours other than the window's text colour.
    bool contrast_ = false;
    bool reference_shown_ = false;
    bool setting_text_ = false; // The window is writing a box's text, not the user.
    // Where the window paints: the status's dot, the message's box and the
    // separators.
    RECT dot_{}, box_{};
    std::vector<int> separators_;

    int gesture_ = -1; // The setting whose edit is open (beginEdit called), or -1.
    ChooseRequests choose_requests_;
    IFileDialog* dialog_ = nullptr; // Choose…'s dialog while it is open.
    FileWatch watch_;
};

Editor::Editor(Controller& controller) : EditorView(&controller) {
    auto& s = shared();
    if (s.windows++ == 0) {
        const HMODULE module = own_module();
        ACTCTXW context{};
        context.cbSize = sizeof context;
        context.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;
        context.hModule = module;
        context.lpResourceName = MAKEINTRESOURCEW(2); // ISOLATIONAWARE_MANIFEST_RESOURCE_ID
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(module, path, MAX_PATH);
        context.lpSource = path;
        s.context = CreateActCtxW(&context);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof window_class;
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = module;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = win::window_class;
        s.window_class = RegisterClassExW(&window_class);
    }
    {
        Styles styles;
        INITCOMMONCONTROLSEX classes{};
        classes.dwSize = sizeof classes;
        classes.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
        InitCommonControlsEx(&classes);
    }
    add_setting(gain_id, L"Gain", L"dB");
    add_setting(gain_mode_id, L"Gain mode", L"");
    add_setting(mix_id, L"Mix", L"% wet");
    add_setting(latency_id, L"Latency", L"");
    for (std::size_t i = 0; i < settings_.size(); ++i) {
        if (settings_[i].tag == mix_id) {
            mix_ = i;
            settings_[i].low = L"Dry"; // As reverbs label a wet/dry slider.
            settings_[i].high = L"Wet";
        }
        if (settings_[i].tag == gain_mode_id)
            settings_[i].tip =
                L"Which gain plays. Set: the Gain setting. Estimate: the filter's own estimate, "
                L"from noise played through it, as convolver --gain-db auto finds. No clipping: "
                L"turned down so that no input within full scale can clip, as convolver "
                L"--gain-db no-clip; it is not a limiter, so input above full scale still can. "
                L"Each filter plays at its own figure; the Gain setting is kept for Set.";
        if (settings_[i].tag == latency_id)
            settings_[i].tip =
                L"The delay Convolver adds, in frames. Smaller is less delay and more processor "
                L"load. A new choice plays at once, after a gap in the sound; if it cannot be "
                L"prepared, the previous one keeps playing and this row says the choice is not "
                L"in use yet. The host is told the delay, so it can delay other tracks to keep "
                L"them in time.";
    }
    measure_ = CreateCompatibleDC(nullptr);
    // The size the host asks for before attaching: at the host's scale if it
    // gave one, else the system's.
    content_ = read_content();
    scale_to(GetDpiForSystem() / 96.0);
}

Editor::~Editor() {
    stop_choosing();
    end_gesture();
    close_window();
    delete_fonts();
    if (tint_)
        DeleteObject(tint_);
    if (measure_)
        DeleteDC(measure_);
    auto& s = shared();
    if (--s.windows == 0) {
        if (s.window_class)
            UnregisterClassW(win::window_class, own_module());
        s.window_class = 0;
        if (s.context != INVALID_HANDLE_VALUE)
            ReleaseActCtx(s.context);
        s.context = INVALID_HANDLE_VALUE;
    }
}

void Editor::add_setting(ParamID tag, const wchar_t* name, const wchar_t* unit) {
    Parameter* p = parameter(tag);
    if (!p)
        return;
    Setting s;
    s.tag = tag;
    s.name = name;
    s.unit = unit;
    s.minimum = p->toPlain(0);
    s.maximum = p->toPlain(1);
    s.initial = p->toPlain(p->getInfo().defaultNormalizedValue);
    if (p->getInfo().flags & ParameterInfo::kIsList) {
        s.list = true;
        for (int32 i = 0; i <= p->getInfo().stepCount; ++i) {
            String128 text{};
            p->toString(p->toNormalized(i), text);
            std::wstring item;
            for (std::size_t c = 0; c < 128 && text[c]; ++c)
                item += static_cast<wchar_t>(text[c]);
            s.choices.push_back(item);
        }
    }
    settings_.push_back(std::move(s));
}

// The window is made when the host attaches the view, and goes when it
// removes it.
tresult PLUGIN_API Editor::attached(void* parent, FIDString type) {
    if (!parent || isPlatformTypeSupported(type) != kResultTrue || !shared().window_class)
        return kResultFalse;
    if (hwnd_)
        removed();     // A host that attaches again without removing first.
    forget_controls(); // What a window the host closed first left behind.
    parent_ = static_cast<HWND>(parent);
    hwnd_ =
        CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_ACCEPTFILES, win::window_class, L"Convolver",
                        WS_CHILD | WS_CLIPCHILDREN | WS_TABSTOP, 0, 0, rect.getWidth(),
                        rect.getHeight(), parent_, nullptr, own_module(), this);
    if (!hwnd_)
        return kResultFalse;
    const tresult result = EditorView::attached(parent, type);
    contrast_ = high_contrast();
    create_controls();
    scale_to(GetDpiForWindow(hwnd_) / 96.0);
    shown_.reset();
    refresh();
    ShowWindow(hwnd_, SW_SHOWNA);
    SetTimer(hwnd_, win::refresh_timer, refresh_ms, nullptr);
    SetTimer(hwnd_, win::files_timer, files_ms, nullptr);
    return result;
}

tresult PLUGIN_API Editor::removed() {
    // A file chosen for this window, or a drag it no longer sees, ends here.
    stop_choosing();
    end_gesture();
    close_window();
    parent_ = nullptr;
    return EditorView::removed();
}

// The window and its controls go; the view stays, ready to be attached
// again with everything made afresh.
void Editor::close_window() {
    if (!hwnd_)
        return;
    forget_controls();
    HWND window = hwnd_;
    hwnd_ = nullptr; // What the closing controls send no longer reaches the view.
    DestroyWindow(window);
}

// Forgets what belonged to the window's controls: its timers, the names
// given to screen readers, the Audio rows, the colours, what the controls
// show and where the window paints.
void Editor::forget_controls() {
    if (hwnd_) {
        KillTimer(hwnd_, win::refresh_timer);
        KillTimer(hwnd_, win::files_timer);
        KillTimer(hwnd_, win::copied_timer);
    }
    if (!named_.empty()) {
        Com com;
        IAccPropServices* services = nullptr;
        if (SUCCEEDED(CoCreateInstance(access_services, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_IAccPropServices,
                                       reinterpret_cast<void**>(&services)))) {
            MSAAPROPID name = accessible_name;
            for (HWND control : named_)
                services->ClearHwndProps(control, OBJID_CLIENT, CHILDID_SELF, &name, 1);
            services->Release();
        }
        named_.clear();
    }
    audio_.clear();
    colours_.clear();
    separators_.clear();
    dot_ = {};
    box_ = {};
    reference_shown_ = false;
    shown_.reset();
}

tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor factor) {
    if (!(factor > 0))
        return kInvalidArgument;
    host_scale_ = factor;
    // A window that knows its DPI follows the DPI; the host's factor sizes
    // the view before it is attached.
    scale_to(hwnd_ ? GetDpiForWindow(hwnd_) / 96.0 : factor);
    return kResultTrue;
}

LRESULT CALLBACK Editor::window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(l);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        // A host that destroyed its own window before removing the view:
        // the view no longer has a window.
        if (editor && editor->hwnd_ == window) {
            editor->hwnd_ = nullptr;
            editor->parent_ = nullptr;
        }
        return DefWindowProcW(window, message, w, l);
    }
    if (!editor || editor->hwnd_ != window)
        return DefWindowProcW(window, message, w, l);
    return editor->handle(message, w, l);
}

LRESULT Editor::handle(UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_TIMER:
        if (w == win::refresh_timer) {
            // The report only while the window is on screen: a host that
            // keeps a hidden window does not have it polled.
            if (IsWindowVisible(hwnd_) && !IsIconic(GetAncestor(hwnd_, GA_ROOT)))
                refresh();
        } else if (w == win::files_timer) {
            check_files();
        } else if (w == win::copied_timer) {
            KillTimer(hwnd_, win::copied_timer);
            set_text(diagnostics_, L"Copy Diagnostics");
        }
        return 0;
    case WM_COMMAND:
        command(LOWORD(w), HIWORD(w), reinterpret_cast<HWND>(l));
        return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<const NMHDR*>(l)->code == BCN_DROPDOWN &&
            reinterpret_cast<const NMHDR*>(l)->hwndFrom == recent_)
            recent_menu(); // May release this view: nothing after it.
        return 0;
    case WM_HSCROLL:
        scrolled(reinterpret_cast<HWND>(l), LOWORD(w));
        return 0;
    case WM_DROPFILES:
        dropped(reinterpret_cast<HDROP>(w));
        return 0;
    case WM_DPICHANGED_AFTERPARENT:
        scale_to(GetDpiForWindow(hwnd_) / 96.0);
        return 0;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE: {
        contrast_ = high_contrast();
        const COLORREF grey = GetSysColor(COLOR_GRAYTEXT);
        for (auto& entry : colours_)
            if (entry.second == grey_)
                entry.second = grey;
        grey_ = grey;
        make_tint();
        scale_to(GetDpiForWindow(hwnd_) / 96.0); // The message font may have changed too.
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        // Text colours other than the window's (secondary text, the status,
        // a problem), unless high contrast is on; the box's tint behind the
        // message.
        const auto dc = reinterpret_cast<HDC>(w);
        const auto control = reinterpret_cast<HWND>(l);
        const auto found = colours_.find(control);
        SetTextColor(dc, found != colours_.end() && !contrast_ ? found->second
                                                               : GetSysColor(COLOR_WINDOWTEXT));
        const bool boxed = !contrast_ && content_.boxed() && tint_ &&
                           (control == message_ || control == note_label_ || control == note_);
        SetBkColor(dc, boxed ? tint_colour_ : GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(boxed ? tint_ : background_);
    }
    case WM_ERASEBKGND: {
        RECT all;
        GetClientRect(hwnd_, &all);
        FillRect(reinterpret_cast<HDC>(w), &all, GetSysColorBrush(COLOR_WINDOW));
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        const HDC dc = BeginPaint(hwnd_, &paint);
        // The separators.
        const HPEN line = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
        const auto old_pen = SelectObject(dc, line);
        for (const int y : separators_) {
            MoveToEx(dc, px(margin), y, nullptr);
            LineTo(dc, px(margin + inner), y);
        }
        // The message's box, tinted with the status colour, round a problem.
        if (content_.boxed() && tint_ && !IsRectEmpty(&box_)) {
            const auto colours = colours_.find(status_);
            const COLORREF edge =
                contrast_ ? GetSysColor(COLOR_WINDOWTEXT)
                          : blend(colours != colours_.end() ? colours->second
                                                            : GetSysColor(COLOR_GRAYTEXT),
                                  GetSysColor(COLOR_WINDOW), 0.4);
            const HPEN border = CreatePen(PS_SOLID, 1, edge);
            SelectObject(dc, border);
            const auto old_brush = SelectObject(dc, tint_);
            RoundRect(dc, box_.left, box_.top, box_.right, box_.bottom, px(8), px(8));
            SelectObject(dc, old_brush);
            SelectObject(dc, line);
            DeleteObject(border);
        }
        // The status's dot, filled in its colour (a ring while nothing
        // plays): the status word beside it says the same.
        if (!IsRectEmpty(&dot_)) {
            const auto colours = colours_.find(status_);
            const COLORREF colour = contrast_ || colours == colours_.end()
                                        ? GetSysColor(COLOR_WINDOWTEXT)
                                        : colours->second;
            const HPEN ring = CreatePen(PS_SOLID, std::max(1, px(1.5)), colour);
            const HBRUSH fill = CreateSolidBrush(colour);
            SelectObject(dc, ring);
            const bool filled = content_.symbol != StatusSymbol::empty &&
                                content_.symbol != StatusSymbol::none &&
                                content_.symbol != StatusSymbol::bypassed;
            const auto old_brush = SelectObject(dc, filled ? fill : GetStockObject(HOLLOW_BRUSH));
            Ellipse(dc, dot_.left, dot_.top, dot_.right, dot_.bottom);
            SelectObject(dc, old_brush);
            SelectObject(dc, line);
            DeleteObject(fill);
            DeleteObject(ring);
        }
        SelectObject(dc, old_pen);
        DeleteObject(line);
        EndPaint(hwnd_, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetFocus(hwnd_); // A click beside the controls ends typing in a box.
        return 0;
    case WM_DESTROY: // Only when the host destroys its window first (removed() forgets first).
        stop_choosing();
        end_gesture();
        forget_controls();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, w, l);
}

// Every control moves the focus on Tab, and a setting's box takes Return
// (set the value) and Escape (show the setting again).
LRESULT CALLBACK Editor::control_proc(HWND control, UINT message, WPARAM w, LPARAM l, UINT_PTR,
                                      DWORD_PTR data) {
    auto* editor = reinterpret_cast<Editor*>(data);
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(control, control_proc, 1);
    else if (editor && editor->hwnd_ && editor->control_key(control, message, w))
        return 0;
    return DefSubclassProc(control, message, w, l);
}

bool Editor::control_key(HWND control, UINT message, WPARAM key) {
    if (message == WM_KEYDOWN && key == VK_TAB) {
        const bool back = GetKeyState(VK_SHIFT) < 0;
        if (HWND next = GetNextDlgTabItem(hwnd_, control, back ? TRUE : FALSE))
            SetFocus(next);
        return true;
    }
    for (std::size_t i = 0; i < settings_.size(); ++i) {
        Setting& s = settings_[i];
        if (s.list || control != s.value)
            continue;
        if (message == WM_KEYDOWN && key == VK_RETURN) {
            apply_typed(i);
            SendMessageW(control, EM_SETSEL, 0, -1);
            return true;
        }
        if (message == WM_KEYDOWN && key == VK_ESCAPE) {
            s.typed = false;
            read_settings();
            SendMessageW(control, EM_SETSEL, 0, -1);
            return true;
        }
    }
    // A single-line box beeps at these characters; the keys above did it.
    return message == WM_CHAR && (key == L'\t' || key == L'\r' || key == 0x1b);
}

HWND Editor::make(const wchar_t* type, const std::wstring& text, DWORD style, int id,
                  DWORD extended) {
    HWND control =
        CreateWindowExW(extended, type, text.c_str(), WS_CHILD | style, 0, 0, 0, 0, hwnd_,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), own_module(), nullptr);
    if (control)
        SetWindowSubclass(control, control_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    return control;
}

// The name Narrator and NVDA read for a control whose own text is not its
// name (a trackbar, a box, a list), through UI Automation's MSAA bridge.
void Editor::name_for_screen_readers(HWND control, const std::wstring& name) {
    if (!control)
        return;
    Com com;
    IAccPropServices* services = nullptr;
    if (FAILED(CoCreateInstance(access_services, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IAccPropServices, reinterpret_cast<void**>(&services))))
        return;
    if (SUCCEEDED(services->SetHwndPropStr(control, OBJID_CLIENT, CHILDID_SELF, accessible_name,
                                           name.c_str())))
        named_.push_back(control);
    services->Release();
}

void Editor::create_controls() {
    Styles styles;
    grey_ = GetSysColor(COLOR_GRAYTEXT);
    const DWORD label = SS_LEFT | SS_NOPREFIX;
    const DWORD shown = WS_VISIBLE;
    // Which instance this is, with Bypass at the end of the same line.
    title_ = make(L"STATIC", L"", shown | label | SS_ENDELLIPSIS, win::title);
    bypass_ = make(L"BUTTON", L"Bypass", shown | WS_TABSTOP | BS_AUTOCHECKBOX, win::bypass);
    // The heading: the filter's file name (a long one loses its middle, so
    // the extension stays), the status beside it as a dot and a word.
    name_ = make(L"STATIC", L"", shown | label | SS_PATHELLIPSIS, win::filter_name);
    status_ = make(L"STATIC", L"", shown | SS_RIGHT | SS_NOPREFIX, win::status);
    for (std::size_t i = 0; i < fact_names.size(); ++i) {
        fact_headings_[i] =
            make(L"STATIC", fact_names[i], label, win::fact_headings + static_cast<int>(i));
        fact_values_[i] =
            make(L"STATIC", L"", label | SS_ENDELLIPSIS, win::fact_values + static_cast<int>(i));
        colours_[fact_headings_[i]] = grey_;
        name_for_screen_readers(fact_values_[i], fact_names[i]);
    }
    filter_note_ = make(L"STATIC", L"", shown | label, win::filter_note);
    colours_[filter_note_] = grey_;
    // DIAG-2's message: read-only text the user can select and copy, in a
    // box tinted with the status colour when it states a problem.
    message_ =
        make(L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, win::message);
    name_for_screen_readers(message_, L"What Convolver plays, why, and what to do");
    note_label_ = make(L"STATIC", L"Note:", SS_RIGHT | SS_NOPREFIX, win::note_label);
    note_ = make(L"STATIC", L"", label, win::note);
    colours_[note_label_] = colours_[note_] = grey_;
    // Choose…, the recent files, Reload, and reloading when the file changes.
    choose_ = make(L"BUTTON", L"Choose\u2026", shown | WS_TABSTOP | BS_PUSHBUTTON, win::choose);
    name_for_screen_readers(choose_, L"Choose Filter");
    // A split button draws its own arrow; either part opens the menu.
    recent_ = make(L"BUTTON", L"Recent", shown | WS_TABSTOP | BS_SPLITBUTTON, win::recent);
    name_for_screen_readers(recent_, L"Recent Filters");
    reload_ = make(L"BUTTON", L"Reload", shown | WS_TABSTOP | BS_PUSHBUTTON, win::reload);
    name_for_screen_readers(reload_, L"Reload Filter");
    watch_box_ = make(L"BUTTON", L"Reload when the file changes",
                      shown | WS_TABSTOP | BS_AUTOCHECKBOX, win::watch);
    SendMessageW(watch_box_, BM_SETCHECK, BST_CHECKED, 0);
    // The settings, each a row of the form.
    for (std::size_t i = 0; i < settings_.size(); ++i) {
        Setting& s = settings_[i];
        const int id = win::settings + win::setting_stride * static_cast<int>(i);
        s.label = make(L"STATIC", s.name + L":", shown | SS_RIGHT | SS_NOPREFIX, id + win::label);
        if (s.list) {
            s.control = make(WC_COMBOBOXW, L"", shown | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                             id + win::control);
            for (const auto& item : s.choices)
                SendMessageW(s.control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
            s.value = make(L"STATIC", L"", shown | label, id + win::value);
            colours_[s.value] = grey_;
        } else {
            s.control = make(TRACKBAR_CLASSW, L"", shown | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                             id + win::control);
            const int steps = position(s, s.maximum);
            SendMessageW(s.control, TBM_SETRANGEMIN, FALSE, 0);
            SendMessageW(s.control, TBM_SETRANGEMAX, FALSE, steps);
            SendMessageW(s.control, TBM_SETLINESIZE, 0, 10); // 1 dB, 1%.
            SendMessageW(s.control, TBM_SETPAGESIZE, 0, 100);
            s.value = make(L"EDIT", L"", shown | WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL,
                           id + win::value);
            name_for_screen_readers(s.value, s.name);
            s.unit_label = make(L"STATIC", s.unit, shown | label, id + win::unit);
            colours_[s.unit_label] = grey_;
            if (!s.low.empty()) {
                s.low_label = make(L"STATIC", s.low, shown | label, id + win::low);
                s.high_label =
                    make(L"STATIC", s.high, shown | SS_RIGHT | SS_NOPREFIX, id + win::high);
                colours_[s.low_label] = colours_[s.high_label] = grey_;
            }
        }
        name_for_screen_readers(s.control, s.name);
    }
    if (mix_ != SIZE_MAX) {
        mix_note_ = make(L"STATIC", L"", label, win::mix_note);
        colours_[mix_note_] = grey_;
    }
    audio_heading_ = make(L"STATIC", L"Audio", shown | label, win::audio_heading);
    // The file, software and log lines, folded away, with Copy Diagnostics
    // and Show Log on their row.
    reference_toggle_ = make(L"BUTTON", L"File, software and log",
                             shown | WS_TABSTOP | BS_AUTOCHECKBOX, win::reference_toggle);
    diagnostics_ =
        make(L"BUTTON", L"Copy Diagnostics", shown | WS_TABSTOP | BS_PUSHBUTTON, win::diagnostics);
    log_ = make(L"BUTTON", L"Show Log", shown | WS_TABSTOP | BS_PUSHBUTTON, win::log);
    reference_ = make(L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_READONLY, win::reference);
    name_for_screen_readers(reference_, L"File, software and log");

    // Tooltips, as the macOS window has them.
    HWND tips =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr, own_module(), nullptr);
    if (tips) {
        SendMessageW(tips, TTM_SETMAXTIPWIDTH, 0, px(360));
        const auto tip = [&](HWND control, const wchar_t* text) {
            if (!control)
                return;
            TOOLINFOW info{};
            info.cbSize = sizeof info;
            info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            info.hwnd = hwnd_;
            info.uId = reinterpret_cast<UINT_PTR>(control);
            info.lpszText = const_cast<wchar_t*>(text);
            SendMessageW(tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        };
        tip(bypass_, L"Lets the audio through without the filter, as the host's own bypass "
                     L"does; the host records the change like any other.");
        tip(choose_, L"Loads an IR file, .cfg, configuration list or Convolver .vstpreset as the "
                     L"filter (a preset brings its Gain, Mix and Latency too); you can also drop "
                     L"the file on this window. The project keeps the filter itself and its "
                     L"file's name, as a preset does, so moving the file later does not change "
                     L"it.");
        tip(recent_, L"Filter files loaded recently in a Convolver window, in any host.");
        tip(reload_, L"Reads the filter file again, for example after changing it in another "
                     L"program. Available for a file loaded in this window, not for a filter "
                     L"from the saved project or a preset.");
        tip(watch_box_, L"While this window is open, reads the filter again when its file, or a "
                        L"file a .cfg names, changes.");
        tip(diagnostics_, L"Copies this report and Convolver's recent log lines, ready to paste "
                          L"into a problem report.");
        tip(log_, L"Shows the Convolver log file in File Explorer.");
        for (const auto& s : settings_) // Gain mode's and Latency's.
            if (s.list && !s.tip.empty())
                tip(s.control, s.tip.c_str());
    }
}

void Editor::delete_fonts() {
    for (HFONT* font : {&body_, &small_, &heading_, &strong_}) {
        if (*font)
            DeleteObject(*font);
        *font = nullptr;
    }
    if (background_)
        DeleteObject(background_);
    background_ = nullptr;
}

// The system's message font at the window's DPI: the body text, a smaller
// size for secondary text, a bold heading and a semibold status word.
void Editor::make_fonts() {
    const UINT dpi = static_cast<UINT>(std::lround(96 * scale_));
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof metrics;
    LOGFONTW base{};
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0, dpi))
        base = metrics.lfMessageFont;
    else {
        base.lfHeight = -px(12);
        std::wcscpy(base.lfFaceName, L"Segoe UI");
    }
    body_ = CreateFontIndirectW(&base);
    LOGFONTW font = base;
    font.lfHeight = static_cast<LONG>(std::lround(base.lfHeight * 11.0 / 12));
    small_ = CreateFontIndirectW(&font);
    font = base;
    font.lfHeight = static_cast<LONG>(std::lround(base.lfHeight * 17.0 / 12));
    font.lfWeight = FW_BOLD;
    heading_ = CreateFontIndirectW(&font);
    font = base;
    font.lfWeight = FW_SEMIBOLD;
    strong_ = CreateFontIndirectW(&font);
    background_ = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
}

void Editor::scale_to(double scale) {
    scale_ = scale > 0 ? scale : 1;
    if (!hwnd_ && host_scale_)
        scale_ = *host_scale_;
    // The old fonts stay until no control uses them.
    const std::array<HGDIOBJ, 5> old{body_, small_, heading_, strong_, background_};
    make_fonts();
    if (hwnd_) {
        const auto font = [](HWND control, HFONT f) {
            if (control)
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(f), FALSE);
        };
        font(title_, small_);
        font(bypass_, body_);
        font(name_, heading_);
        font(status_, strong_);
        for (std::size_t i = 0; i < fact_values_.size(); ++i) {
            font(fact_headings_[i], small_);
            font(fact_values_[i], small_);
        }
        for (HWND control : {filter_note_, note_label_, note_, mix_note_, reference_})
            font(control, small_);
        for (HWND control : {message_, choose_, recent_, reload_, watch_box_, reference_toggle_,
                             diagnostics_, log_})
            font(control, body_);
        font(audio_heading_, strong_);
        for (auto& s : settings_) {
            for (HWND control : {s.label, s.control, s.value, s.unit_label})
                font(control, body_);
            font(s.low_label, small_);
            font(s.high_label, small_);
            if (s.list && s.control)
                SendMessageW(s.control, CB_SETMINVISIBLE, s.choices.size(), 0);
        }
        for (const auto& row : audio_) {
            font(row.label, body_);
            font(row.value, body_);
            font(row.note, body_);
        }
    }
    for (HGDIOBJ object : old)
        if (object)
            DeleteObject(object);
    const int height = arrange(hwnd_ != nullptr);
    if (!hwnd_) {
        rect = ViewRect(0, 0, px(width), height);
        return;
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
    follow_size();
}

Editor::Extent Editor::extent(HFONT font, const std::wstring& text) const {
    SIZE size{0, 0};
    const auto old = SelectObject(measure_, font);
    const std::wstring measured = text.empty() ? L"X" : text;
    GetTextExtentPoint32W(measure_, measured.c_str(), static_cast<int>(measured.size()), &size);
    SelectObject(measure_, old);
    return {text.empty() ? 0 : static_cast<int>(size.cx), static_cast<int>(size.cy)};
}

int Editor::wrapped_height(HFONT font, const std::wstring& text, int wide) const {
    if (text.empty())
        return 0;
    RECT r{0, 0, std::max(wide, 1), 0};
    const auto old = SelectObject(measure_, font);
    DrawTextW(measure_, text.c_str(), static_cast<int>(text.size()), &r,
              DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
    SelectObject(measure_, old);
    return r.bottom - r.top;
}

void Editor::place(HWND control, int x, int y, int w, int h, bool shown) {
    if (!control)
        return;
    SetWindowPos(control, nullptr, x, y, std::max(w, 0), std::max(h, 0),
                 SWP_NOZORDER | SWP_NOACTIVATE | (shown ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

// Lays out the controls for content_ from the top, as the macOS window's
// stack does, and returns the height the content takes. Without `place`
// it only measures (for getSize before the host attaches the view).
int Editor::arrange(bool place_controls) {
    const auto put = [&](HWND control, int x, int y, int w, int h, bool shown = true) {
        if (place_controls)
            place(control, x, y, w, h, shown);
    };
    const WindowContent& c = content_;
    const int left = px(margin), full = px(inner);
    const int line = extent(body_, L"Xg").cy;
    const int small_line = extent(small_, L"Xg").cy;
    const int heading_line = extent(heading_, L"Xg").cy;
    const int button = std::max(px(button_height), line + px(8));
    const int form = px(form_label), gap = px(form_gap);
    const int content_left = left + form + gap, content_wide = full - form - gap;
    const auto button_wide = [&](const std::wstring& text) {
        return std::max(extent(body_, text).cx + px(24), px(75));
    };
    const auto check_wide = [&](const std::wstring& text) {
        return extent(body_, text).cx + px(24);
    };
    separators_.clear();
    int y = px(14);

    // The title line, Bypass at its end.
    const int bypass_wide = check_wide(L"Bypass");
    const int title_row = std::max(px(check_height), small_line);
    put(title_, left, y + (title_row - small_line) / 2, full - bypass_wide - px(8), small_line);
    put(bypass_, left + full - bypass_wide, y, bypass_wide, title_row);
    y += title_row + px(6);

    // The filter's name and the status's dot and word.
    const std::wstring status = wide(c.status_name);
    const int status_wide = extent(strong_, status).cx + px(2);
    const int dot_size = px(dot);
    const int status_left = left + full - status_wide;
    put(name_, left, y, full - status_wide - dot_size - px(16), heading_line);
    put(status_, status_left, y + (heading_line - line) / 2, status_wide, line);
    const int dot_top = y + (heading_line - dot_size) / 2;
    dot_ = c.symbol == StatusSymbol::none ? RECT{}
                                          : RECT{status_left - dot_size - px(5), dot_top,
                                                 status_left - px(5), dot_top + dot_size};
    y += heading_line + px(4);

    // The filter's own facts, as labelled columns.
    if (c.facts_shown) {
        const std::array<std::string, 4> values{c.filter_rate, c.filter_channels, c.filter_length,
                                                c.filter_speakers};
        int x = left;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const bool shown = i < 3 || !values[i].empty();
            int column =
                std::max(extent(small_, fact_names[i]).cx, extent(small_, wide(values[i])).cx);
            if (i == 3)
                column = std::max(left + full - x, 0); // The speakers take the rest.
            put(fact_headings_[i], x, y, column, small_line, shown);
            put(fact_values_[i], x, y + small_line + px(1), column, small_line, shown);
            x += column + px(22);
        }
        y += 2 * small_line + px(1) + px(6);
    } else {
        for (std::size_t i = 0; i < fact_values_.size(); ++i) {
            put(fact_headings_[i], 0, 0, 0, 0, false);
            put(fact_values_[i], 0, 0, 0, 0, false);
        }
    }
    // A refused file and the drop hint.
    const int note_height = wrapped_height(small_, wide(c.filter_note), full);
    put(filter_note_, left, y, full, note_height, note_height > 0);
    y += note_height + px(10);

    // The message, in a box round a problem.
    const std::wstring message = message_text(c);
    const bool text = !message.empty();
    const bool note = !c.note.empty();
    box_ = RECT{};
    if (text || note) {
        const int inset = c.boxed() ? px(padding) : 0;
        const int top = y;
        y += inset;
        const int text_wide = full - 2 * inset;
        if (text) {
            const int needed = wrapped_height(body_, message, text_wide - px(4)) + px(4);
            const int shown = std::min(needed, px(most_text));
            if (place_controls && message_) {
                // A scroll bar only when the message is longer than it shows.
                const LONG_PTR style = GetWindowLongPtrW(message_, GWL_STYLE);
                const LONG_PTR wanted =
                    needed > shown ? (style | WS_VSCROLL) : (style & ~LONG_PTR{WS_VSCROLL});
                if (wanted != style) {
                    SetWindowLongPtrW(message_, GWL_STYLE, wanted);
                    SetWindowPos(message_, nullptr, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                }
            }
            put(message_, left + inset, y, text_wide, shown);
            y += shown + (note ? px(8) : 0);
        } else {
            put(message_, 0, 0, 0, 0, false);
        }
        if (note) {
            const int note_wide = text_wide - form - gap;
            const int h = wrapped_height(small_, wide(c.note), note_wide);
            put(note_label_, left + inset, y, form, small_line);
            put(note_, left + inset + form + gap, y, note_wide, h);
            y += h;
        } else {
            put(note_label_, 0, 0, 0, 0, false);
            put(note_, 0, 0, 0, 0, false);
        }
        y += inset;
        if (c.boxed())
            box_ = RECT{left, top, left + full, y};
        y += px(10);
    } else {
        put(message_, 0, 0, 0, 0, false);
        put(note_label_, 0, 0, 0, 0, false);
        put(note_, 0, 0, 0, 0, false);
    }

    // Choose…, Recent, Reload and the watch box, in the form's content column.
    {
        int x = content_left;
        for (HWND b : {choose_, recent_, reload_}) {
            const std::wstring label = b == choose_   ? L"Choose\u2026"
                                       : b == recent_ ? L"Recent"
                                                      : L"Reload";
            const int w = button_wide(label) + (b == recent_ ? px(16) : 0); // The arrow.
            put(b, x, y, w, button);
            x += w + px(8);
        }
        const std::wstring watch = L"Reload when the file changes";
        put(watch_box_, x, y + (button - px(check_height)) / 2,
            std::min(check_wide(watch), left + full - x), px(check_height));
        y += button + px(12);
    }
    separators_.push_back(y);
    y += px(12);

    // The settings.
    for (std::size_t i = 0; i < settings_.size(); ++i) {
        const Setting& s = settings_[i];
        if (s.list) {
            int menu = px(110);
            for (const auto& item : s.choices)
                menu = std::max(menu, extent(body_, item).cx + px(40));
            put(s.label, left, y + (button - line) / 2, form, line);
            // A drop-down list's height includes its open list.
            put(s.control, content_left, y, menu,
                button + line * static_cast<int>(s.choices.size() + 1));
            put(s.value, content_left + menu + px(12), y + (button - line) / 2,
                content_wide - menu - px(12), line);
            y += button + px(12);
            continue;
        }
        const int row = std::max(px(trackbar_height), px(box_height));
        const int track = content_wide - px(value_box) - px(unit_width) - px(16);
        put(s.label, left, y + (row - line) / 2, form, line);
        put(s.control, content_left, y, track, px(trackbar_height));
        put(s.value, content_left + track + px(8), y + (row - px(box_height)) / 2, px(value_box),
            px(box_height));
        put(s.unit_label, content_left + track + px(8) + px(value_box) + px(8),
            y + (row - line) / 2, px(unit_width), line);
        y += row;
        if (s.low_label) {
            const int end = extent(small_, s.high).cx + px(2);
            put(s.low_label, content_left + px(4), y, track / 2, small_line);
            put(s.high_label, content_left + track - end - px(4), y, end, small_line);
            y += small_line;
        }
        if (i == mix_) {
            const int h = wrapped_height(small_, wide(c.mix_note), content_wide);
            put(mix_note_, content_left, y + px(4), content_wide, h, h > 0);
            y += h > 0 ? h + px(4) : 0;
        }
        y += px(12);
    }
    y += px(2);
    separators_.push_back(y);
    y += px(12);

    // The Audio facts.
    put(audio_heading_, left, y, full, line);
    y += line + px(4);
    const int facts = px(fact_label);
    for (std::size_t i = 0; i < audio_.size() && i < c.audio.size(); ++i) {
        const auto& fact = c.audio[i];
        const auto& row = audio_[i];
        const int value_left = left + facts + gap, value_wide = full - facts - gap;
        const int value = std::min(extent(body_, wide(fact.value)).cx + px(2), value_wide);
        const std::wstring note_text = wide(fact.note);
        const int note_left = value_left + value + px(6);
        const int note_wide = std::max(left + full - note_left, 0);
        const int note_h = wrapped_height(body_, note_text, note_wide);
        const int h = std::max(line, note_h);
        put(row.label, left, y, facts, line);
        put(row.value, value_left, y, value, line);
        put(row.note, note_left, y, note_wide, note_h, note_h > 0);
        y += h + px(3);
    }
    y += px(12);

    // The file, software and log lines' row, and the lines when shown.
    {
        const int log_wide = button_wide(L"Show Log");
        const int copy_wide = button_wide(L"Copy Diagnostics");
        put(log_, left + full - log_wide, y, log_wide, button);
        put(diagnostics_, left + full - log_wide - px(12) - copy_wide, y, copy_wide, button);
        const std::wstring toggle = L"File, software and log";
        put(reference_toggle_, left, y + (button - px(check_height)) / 2, check_wide(toggle),
            px(check_height));
        y += button + px(8);
        const int h =
            reference_shown_
                ? wrapped_height(small_, reference_text(c.reference), full - px(4)) + px(4)
                : 0;
        put(reference_, left, y, full, h, reference_shown_);
        y += h;
    }
    y += px(margin);
    if (place_controls && hwnd_)
        SetWindowPos(hwnd_, nullptr, 0, 0, px(width), y,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    return y;
}

// Asks the host for the window's own size when it differs from the last
// size the host gave.
void Editor::follow_size() {
    if (!hwnd_)
        return;
    RECT own;
    GetWindowRect(hwnd_, &own);
    ViewRect wanted(0, 0, own.right - own.left, own.bottom - own.top);
    if (wanted.getWidth() == rect.getWidth() && wanted.getHeight() == rect.getHeight())
        return;
    if (plugFrame)
        plugFrame->resizeView(this, &wanted);
    else
        rect = wanted;
}

WindowContent Editor::read_content() {
    const auto fields = controller ? owner().report() : decltype(owner().report()){};
    const auto report = report_map(fields);
    return window_content(fields.empty() ? nullptr : &report, true);
}

void Editor::refresh() {
    if (!hwnd_)
        return;
    // What to show comes from the shared model, so every system's window
    // shows the same for the same report.
    const WindowContent c = read_content();
    read_settings();
    read_bypass();
    watch_.follow(c.filter_path, c.filter_files);
    EnableWindow(reload_, !watch_.path().empty());
    EnableWindow(watch_box_, !watch_.path().empty());
    EnableWindow(log_, !c.log_path.empty());
    if (shown_ && *shown_ == c)
        return;
    show(c);
}

void Editor::show(const WindowContent& c) {
    content_ = c;
    shown_ = c;
    set_text(title_, wide(c.title));
    set_text(status_, wide(c.status_name));
    switch (c.tone) {
    case StatusTone::positive:
        colours_[status_] = RGB(16, 124, 16);
        break;
    case StatusTone::failure:
        colours_[status_] = RGB(196, 43, 28);
        break;
    case StatusTone::warning:
        colours_[status_] = RGB(157, 93, 0);
        break;
    case StatusTone::plain:
        colours_.erase(status_);
        break;
    }
    make_tint();
    set_text(name_, wide(c.filter_name));
    set_text(filter_note_, wide(c.filter_note));
    const std::array<std::string, 4> facts{c.filter_rate, c.filter_channels, c.filter_length,
                                           c.filter_speakers};
    for (std::size_t i = 0; i < facts.size(); ++i)
        set_text(fact_values_[i], wide(facts[i]));
    set_text(message_, message_text(c));
    set_text(note_, wide(c.note));
    // The button that carries out the fix is drawn as the default button,
    // without being one: Return still goes to the host.
    SendMessageW(choose_, BM_SETSTYLE,
                 c.action == WindowAction::choose_filter ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);
    SendMessageW(diagnostics_, BM_SETSTYLE,
                 c.action == WindowAction::copy_diagnostics ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON,
                 TRUE);
    // PLUG-23: Mix has no effect without a dry signal: its controls dim and
    // the note under it says why.
    if (mix_ != SIZE_MAX) {
        Setting& mix = settings_[mix_];
        const bool applies = c.mix_note.empty();
        set_text(mix_note_, wide(c.mix_note));
        for (HWND control : {mix.control, mix.value})
            EnableWindow(control, applies);
        if (applies)
            colours_.erase(mix.label);
        else
            colours_[mix.label] = grey_;
    }
    set_text(reference_, reference_text(c.reference));
    rebuild_audio(c.audio);
    read_settings(); // The Latency row's note uses the rate.
    arrange(true);
    InvalidateRect(hwnd_, nullptr, TRUE);
    follow_size();
}

// The box's tint: the status colour over the window's, faintly.
void Editor::make_tint() {
    if (tint_)
        DeleteObject(tint_);
    const auto found = colours_.find(status_);
    tint_colour_ =
        blend(found != colours_.end() ? found->second : grey_, GetSysColor(COLOR_WINDOW), 0.08);
    tint_ = CreateSolidBrush(tint_colour_);
}

// The Audio rows, made again when the facts change.
void Editor::rebuild_audio(const std::vector<AudioFact>& facts) {
    while (audio_.size() > facts.size()) {
        for (HWND control : {audio_.back().label, audio_.back().value, audio_.back().note}) {
            colours_.erase(control);
            DestroyWindow(control);
        }
        audio_.pop_back();
    }
    Styles styles;
    while (audio_.size() < facts.size()) {
        const int id = win::audio_rows + 3 * static_cast<int>(audio_.size());
        AudioRow row;
        const DWORD style = WS_VISIBLE | SS_LEFT | SS_NOPREFIX;
        row.label = make(L"STATIC", L"", style | SS_ENDELLIPSIS, id);
        row.value = make(L"STATIC", L"", style, id + 1);
        row.note = make(L"STATIC", L"", style, id + 2);
        for (HWND control : {row.label, row.value, row.note})
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(body_), FALSE);
        colours_[row.label] = grey_;
        audio_.push_back(row);
    }
    for (std::size_t i = 0; i < facts.size(); ++i) {
        const auto& fact = facts[i];
        set_text(audio_[i].label, wide(fact.label));
        set_text(audio_[i].value, wide(fact.value));
        // A problem's note in red after a warning sign, so the words say it
        // too; another note in the secondary colour.
        set_text(audio_[i].note,
                 fact.note.empty() ? L"" : (fact.problem ? L"\u26A0 " : L"") + wide(fact.note));
        colours_[audio_[i].note] = fact.problem ? RGB(196, 43, 28) : grey_;
    }
}

double Editor::plain(const Setting& s) {
    Parameter* p = parameter(s.tag);
    return p ? p->toPlain(controller->getParamNormalized(s.tag)) : NAN;
}

int Editor::position(const Setting& s, double value) const {
    // Tenths of a dB or a percent: arrows move one, Page Up and Down ten.
    return static_cast<int>(
        std::lround((std::clamp(value, s.minimum, s.maximum) - s.minimum) * 10));
}

// Shows the settings as the host has them now (automation, its own
// controls), except the one the user is dragging or typing.
void Editor::read_settings() {
    for (std::size_t i = 0; i < settings_.size(); ++i) {
        Setting& s = settings_[i];
        const double value = plain(s);
        if (std::isnan(value) || !s.control)
            continue;
        if (s.list) {
            const auto index = static_cast<LRESULT>(std::lround(value));
            if (SendMessageW(s.control, CB_GETCURSEL, 0, 0) != index)
                SendMessageW(s.control, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
            if (s.tag == gain_mode_id) { // PLUG-10: the gain the mode plays.
                set_text(s.value, wide(content_.gain_note));
                continue;
            }
            // The time the choice adds at the host's rate; the items begin
            // with the frames ("256 frames").
            const double frames =
                index >= 0 && static_cast<std::size_t>(index) < s.choices.size()
                    ? std::wcstod(s.choices[static_cast<std::size_t>(index)].c_str(), nullptr)
                    : 0;
            set_text(s.value,
                     wide(latency_note(frames, content_.sample_rate, content_.latency_waits)));
            continue;
        }
        if (static_cast<int>(i) != gesture_)
            SendMessageW(s.control, TBM_SETPOS, TRUE, position(s, value));
        if (!(s.typed && GetFocus() == s.value)) {
            s.typed = false;
            setting_text_ = true;
            set_text(s.value, wide(setting_text(value, s.minimum, s.maximum)));
            setting_text_ = false;
        }
    }
}

void Editor::read_bypass() {
    Parameter* p = parameter(bypass_id);
    if (!p || !bypass_) {
        if (bypass_)
            ShowWindow(bypass_, SW_HIDE);
        return;
    }
    const bool on = controller->getParamNormalized(bypass_id) >= 0.5;
    SendMessageW(bypass_, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

void Editor::begin_gesture(std::size_t i) {
    if (gesture_ == static_cast<int>(i))
        return;
    end_gesture();
    gesture_ = static_cast<int>(i);
    if (controller)
        controller->beginEdit(settings_[i].tag);
}

void Editor::end_gesture() {
    if (gesture_ < 0)
        return;
    const auto i = static_cast<std::size_t>(gesture_);
    gesture_ = -1;
    if (controller && i < settings_.size())
        controller->endEdit(settings_[i].tag);
}

// Passes `value` to the host as the SDK asks of an editor: inside the open
// edit of a drag, or as an edit of its own.
void Editor::set_value(std::size_t i, double value) {
    Parameter* p = parameter(settings_[i].tag);
    if (!p)
        return;
    const bool own = gesture_ != static_cast<int>(i);
    if (own)
        begin_gesture(i);
    const ParamValue normalized = p->toNormalized(value);
    owner().setParamNormalized(settings_[i].tag, normalized);
    controller->performEdit(settings_[i].tag, normalized);
    if (own)
        end_gesture();
}

void Editor::apply_typed(std::size_t i) {
    Setting& s = settings_[i];
    if (!s.typed)
        return;
    s.typed = false;
    const auto value = typed_value(utf8(window_text(s.value)), s.minimum, s.maximum);
    if (value)
        set_value(i, *value);
    else
        MessageBeep(MB_OK);
    read_settings();
}

// A trackbar moved: a drag is one edit, from the thumb's first move to its
// release; a key or a click on the track is an edit of its own.
void Editor::scrolled(HWND control, int code) {
    for (std::size_t i = 0; i < settings_.size(); ++i) {
        Setting& s = settings_[i];
        if (s.list || control != s.control)
            continue;
        if (code == TB_ENDTRACK) {
            end_gesture();
            read_settings();
            return;
        }
        if (code == TB_THUMBTRACK)
            begin_gesture(i);
        const auto pos = SendMessageW(control, TBM_GETPOS, 0, 0);
        const double value = s.minimum + static_cast<double>(pos) / 10;
        s.typed = false;
        set_value(i, std::min(value, s.maximum));
        setting_text_ = true;
        set_text(s.value, wide(setting_text(plain(s), s.minimum, s.maximum)));
        setting_text_ = false;
        return;
    }
}

void Editor::command(int id, int code, HWND control) {
    // A setting's box or list.
    for (std::size_t i = 0; i < settings_.size(); ++i) {
        Setting& s = settings_[i];
        // Latency changes when a choice is made: a click or Return in the
        // open list, or an arrow key or the wheel on the closed one, not
        // each item the arrow keys pass in the open list (each change
        // rebuilds the engine); Escape in the open list puts it back.
        if (s.list && control == s.control && code == CBN_SELENDCANCEL) {
            read_settings();
            return;
        }
        if (s.list && control == s.control &&
            (code == CBN_SELENDOK ||
             (code == CBN_SELCHANGE && !SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0)))) {
            const auto index = SendMessageW(control, CB_GETCURSEL, 0, 0);
            if (index >= 0 && plain(s) == static_cast<double>(index))
                return; // The choice it already had.
            if (index >= 0)
                set_value(i, static_cast<double>(index));
            refresh(); // The report says when a new Latency takes effect.
            read_settings();
            return;
        }
        if (!s.list && control == s.value) {
            if (code == EN_CHANGE && !setting_text_)
                s.typed = true;
            else if (code == EN_KILLFOCUS)
                apply_typed(i);
            return;
        }
    }
    if (code != BN_CLICKED)
        return;
    switch (id) {
    case win::bypass:
        if (parameter(bypass_id)) {
            // One click is one edit, so the host records it as one change.
            const bool on = SendMessageW(bypass_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            controller->beginEdit(bypass_id);
            owner().setParamNormalized(bypass_id, on ? 1.0 : 0.0);
            controller->performEdit(bypass_id, on ? 1.0 : 0.0);
            controller->endEdit(bypass_id);
        }
        refresh();
        break;
    case win::choose:
        choose_filter(); // May release this view: nothing after it.
        break;
    case win::recent:
        recent_menu(); // As choose_filter.
        break;
    case win::reload:
        reload();
        break;
    case win::watch:
        watch_.settle(); // Changes made while it was off are not reloaded.
        break;
    case win::reference_toggle:
        reference_shown_ = SendMessageW(reference_toggle_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        arrange(true);
        InvalidateRect(hwnd_, nullptr, TRUE);
        follow_size();
        break;
    case win::diagnostics:
        copy_diagnostics();
        break;
    case win::log:
        show_log();
        break;
    default:
        break;
    }
}

bool Editor::load(const std::string& path) {
    const bool accepted = controller && !path.empty() && owner().load_file(path);
    if (accepted)
        remember_recent_file(path);
    shown_.reset();
    refresh(); // The report says what plays now, and why a refused file was refused.
    if (accepted)
        watch_.settle(); // What was read is what plays: nothing to reload.
    return accepted;
}

// The system's file dialog, owned by the host's window. Show runs a modal
// loop in which the host may remove and release this view: the view holds a
// reference meanwhile, removing it closes the dialog, and a file chosen for
// a window that has gone is not loaded.
void Editor::choose_filter() {
    if (!hwnd_)
        return;
    stop_choosing();
    const auto request = choose_requests_.start();
    Com com;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog)))) {
        MessageBeep(MB_OK);
        return;
    }
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    dialog->SetTitle(
        L"Choose an IR file, .cfg, configuration list or Convolver .vstpreset to use as "
        L"the filter");
    dialog->SetOkButtonLabel(L"Load");
    dialog_ = dialog;
    addRef();
    std::wstring path;
    if (SUCCEEDED(dialog->Show(GetAncestor(hwnd_, GA_ROOT)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR chosen = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &chosen)) && chosen) {
                path = chosen;
                CoTaskMemFree(chosen);
            }
            item->Release();
        }
    }
    if (dialog_ == dialog)
        dialog_ = nullptr;
    dialog->Release();
    // Forgotten: the window closed, or a newer request replaced it.
    if (choose_requests_.finish(request) && hwnd_ && !path.empty() && !load(utf8(path)))
        MessageBeep(MB_OK);
    release();
}

void Editor::stop_choosing() {
    choose_requests_.stop();
    if (IFileDialog* dialog = dialog_) {
        dialog_ = nullptr;
        dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    }
}

// The recent files, "room.cfg — Filters", under the Recent button.
void Editor::recent_menu() {
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    const auto entries = recent_entries(recent_files());
    for (std::size_t i = 0; i < entries.size(); ++i)
        AppendMenuW(menu, MF_STRING, recent_first + i, wide(entries[i].title).c_str());
    if (entries.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No Recent Filters");
    } else {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, recent_clear, L"Clear Menu");
    }
    RECT button;
    GetWindowRect(recent_, &button);
    addRef(); // The menu runs a modal loop, as the file dialog does.
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                                      button.left, button.bottom, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (hwnd_) {
        if (chosen == recent_clear)
            forget_recent_files();
        else if (chosen >= recent_first &&
                 static_cast<std::size_t>(chosen - recent_first) < entries.size() &&
                 !load(entries[static_cast<std::size_t>(chosen - recent_first)].path))
            MessageBeep(MB_OK);
    }
    release();
}

void Editor::reload() {
    const std::string path = watch_.path();
    if (path.empty() || !load(path))
        MessageBeep(MB_OK);
}

// Once a second while the window is open, even behind another window (the
// program that changes the files is then in front), and Reload When the
// File Changes is ticked.
void Editor::check_files() {
    if (!hwnd_ || !IsWindowVisible(hwnd_) ||
        SendMessageW(watch_box_, BM_GETCHECK, 0, 0) != BST_CHECKED || !watch_.due())
        return;
    // The host may have replaced the filter (a preset, an undo) while the
    // window was not reading the report: reload only the file that still
    // plays.
    const std::string path = watch_.path();
    const std::string files = watch_.files();
    refresh();
    if (watch_.path() != path || watch_.files() != files)
        return;
    load(path);
}

void Editor::copy_diagnostics() {
    const std::string text = controller ? owner().diagnostics() : std::string();
    const std::wstring copied = crlf(wide(text));
    if (copied.empty() || !OpenClipboard(hwnd_)) {
        MessageBeep(MB_OK);
        return;
    }
    EmptyClipboard();
    const std::size_t bytes = (copied.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* to = GlobalLock(memory)) {
            std::memcpy(to, copied.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    // The button says so for two seconds.
    set_text(diagnostics_, L"Copied");
    SetTimer(hwnd_, win::copied_timer, copied_ms, nullptr);
}

// Selects the log in File Explorer, or opens its folder when the file is not
// there yet.
void Editor::show_log() {
    const std::string log = content_.log_path;
    if (log.empty()) {
        MessageBeep(MB_OK);
        return;
    }
    const auto file = detail::utf8_path(log);
    std::error_code error;
    Com com;
    if (std::filesystem::exists(file, error)) {
        if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(file.c_str())) {
            const HRESULT shown = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
            ILFree(item);
            if (SUCCEEDED(shown))
                return;
        }
    }
    const auto folder = file.parent_path();
    if (reinterpret_cast<INT_PTR>(
            ShellExecuteW(hwnd_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        MessageBeep(MB_OK);
}

// The first file dropped on the window, as if chosen.
void Editor::dropped(HDROP drop) {
    std::wstring path;
    if (DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) > 0) {
        const UINT n = DragQueryFileW(drop, 0, nullptr, 0);
        path.resize(n + 1);
        path.resize(DragQueryFileW(drop, 0, path.data(), n + 1));
    }
    DragFinish(drop);
    if (!path.empty() && !load(utf8(path)))
        MessageBeep(MB_OK); // Taken, even if refused: the window says why.
}
} // namespace

IPlugView* create_editor(Controller& controller) {
    return new Editor(controller);
}
bool has_editor() noexcept {
    return true;
}
} // namespace convolver::vst3
