// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-11: Convolver's window in VST3 hosts on macOS. The view is the Audio
// Unit's (plugins/editor/mac/), so both formats show the same window. It is
// reached the way the VST3 SDK documents for an editor without VSTGUI: the
// controller's createView(ViewType::kEditor) returns an IPlugView, here the
// SDK's EditorView (CPluginView) for kPlatformTypeNSView. The host asks its
// size (getSize), then attaches it to an NSView of its own (attached); the
// view asks the host to resize the window when its content changes height
// (IPlugFrame::resizeView, after which the host calls onSize), and is not
// resizable by the user (canResize is false). The view talks only to the
// edit controller, on the host's main thread: settings as parameter edits
// (beginEdit, performEdit, endEdit, with setParamNormalized for the
// controller's own copy), the report the processor sent, and a chosen file
// as a message the processor handles (Controller::load_file).
#include "editor.hpp"
#import "diagnostic_view.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "processor.hpp"
#include "public.sdk/source/vst/vsteditcontroller.h"
#import <AppKit/AppKit.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

namespace convolver::vst3 {
namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;

// What the window's blocks reach the controller through. The editor clears
// it when it goes, so nothing the view still does then (a timer that has
// not stopped yet, a file dialog closing) reaches a controller the host may
// have released.
struct Link {
    Controller* controller = nullptr;
};
using SharedLink = std::shared_ptr<Link>;

NSString* ns(const std::string& text) {
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size()
                                  encoding:NSUTF8StringEncoding];
}

// One of the settings as the window's slider (setting_rows.h): its range and
// default from the controller's parameter, the value read from the
// controller and changed as the SDK asks of an editor, with a drag as one
// edit (begin and end).
NSDictionary<NSString*, id>* setting(const SharedLink& link, ParamID tag, NSString* name,
                                     NSString* unit) {
    Controller* controller = link->controller;
    Parameter* parameter = controller ? controller->getParameterObject(tag) : nullptr;
    if (!parameter)
        return nil;
    SharedLink held = link;
    ConvolverSettingGetter get = ^double(void) {
      Controller* c = held->controller;
      Parameter* p = c ? c->getParameterObject(tag) : nullptr;
      return p ? p->toPlain(c->getParamNormalized(tag)) : NAN;
    };
    ConvolverSettingSetter set = ^(double plain) {
      Controller* c = held->controller;
      Parameter* p = c ? c->getParameterObject(tag) : nullptr;
      if (!p)
          return;
      const ParamValue normalized = p->toNormalized(plain);
      c->setParamNormalized(tag, normalized);
      c->performEdit(tag, normalized);
    };
    ConvolverSettingGesture begin = ^(void) {
      if (Controller* c = held->controller)
          c->beginEdit(tag);
    };
    ConvolverSettingGesture end = ^(void) {
      if (Controller* c = held->controller)
          c->endEdit(tag);
    };
    return @{
        @"name" : name,
        @"minimum" : @(parameter->toPlain(0)),
        @"maximum" : @(parameter->toPlain(1)),
        @"default" : @(parameter->toPlain(parameter->getInfo().defaultNormalizedValue)),
        @"unit" : unit,
        @"get" : [get copy],
        @"set" : [set copy],
        @"begin" : [begin copy],
        @"end" : [end copy]
    };
}

// A setting chosen from a list (Latency) as the window's menu: the item
// names the controller gives, the value an item's index.
NSDictionary<NSString*, id>* choice(const SharedLink& link, ParamID tag, NSString* name,
                                    NSString* tip) {
    NSMutableDictionary<NSString*, id>* entry = [setting(link, tag, name, @"") mutableCopy];
    Parameter* parameter = link->controller ? link->controller->getParameterObject(tag) : nullptr;
    if (!entry || !parameter)
        return nil;
    NSMutableArray<NSString*>* titles = [NSMutableArray array];
    const int32 count = parameter->getInfo().stepCount + 1;
    for (int32 i = 0; i < count; ++i) {
        String128 text{};
        parameter->toString(parameter->toNormalized(i), text);
        NSUInteger length = 0;
        while (length < 127 && text[length])
            ++length;
        [titles addObject:[NSString stringWithCharacters:reinterpret_cast<const unichar*>(text)
                                                  length:length]];
    }
    entry[@"choices"] = titles;
    entry[@"tip"] = tip;
    return entry;
}

class Editor final : public EditorView {
  public:
    explicit Editor(Controller& controller);
    ~Editor() override;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override {
        return type && std::strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void* parent, FIDString type) override;
    tresult PLUGIN_API removed() override;
    tresult PLUGIN_API onSize(ViewRect* size) override;

  private:
    // Asks the host for the view's own size, when it differs from the last
    // size the host gave (rect).
    void follow_size();
    void stop_following();
    SharedLink link_;
    CONVOLVER_DIAGNOSTIC_VIEW* view_ = nil;
    id observer_ = nil; // The view's frame-change observer while attached.
};

Editor::Editor(Controller& controller) : EditorView(&controller), link_(std::make_shared<Link>()) {
    link_->controller = &controller;
    SharedLink held = link_;
    ConvolverReportSource report = ^NSDictionary<NSString*, NSString*>*(void) {
        Controller* c = held->controller;
        if (!c)
            return nil;
        const auto fields = c->report();
        if (fields.empty())
            return nil;
        NSMutableDictionary<NSString*, NSString*>* dictionary = [NSMutableDictionary dictionary];
        for (const auto& field : fields) {
            NSString* key = ns(field.first);
            NSString* value = ns(field.second);
            if (key && value)
                dictionary[key] = value;
        }
        return dictionary;
    };
    ConvolverDiagnosticsSource diagnostics = ^NSString*(void) {
      Controller* c = held->controller;
      const std::string text = c ? c->diagnostics() : std::string();
      return text.empty() ? nil : ns(text);
    };
    NSMutableArray<NSDictionary<NSString*, id>*>* settings = [NSMutableArray array];
    if (NSDictionary<NSString*, id>* gain = setting(link_, gain_id, @"Gain", @" dB"))
        [settings addObject:gain];
    if (NSDictionary<NSString*, id>* mix = setting(link_, mix_id, @"Mix", @"% wet")) {
        NSMutableDictionary<NSString*, id>* ends = [mix mutableCopy];
        ends[@"low"] = @"Dry"; // As reverbs label a wet/dry slider.
        ends[@"high"] = @"Wet";
        [settings addObject:ends];
    }
    if (NSDictionary<NSString*, id>* latency =
            choice(link_, latency_id, @"Latency",
                   @"The delay Convolver adds, in frames. Smaller is less delay and more processor "
                   @"load. A new choice plays once the host restarts the plug-in, which it is "
                   @"asked to do at once, after a gap in the sound."))
        [settings addObject:latency];
    // A chosen or dropped file goes to the processor by path, in a message:
    // the processor reads it, as for a state the host restores.
    ConvolverFileLoader loader = ^BOOL(NSString* path) {
      Controller* c = held->controller;
      const char* utf8 = path.UTF8String;
      return c && utf8 && c->load_file(utf8) ? YES : NO;
    };
    view_ = [[CONVOLVER_DIAGNOSTIC_VIEW alloc] initWithFrame:NSZeroRect
                                                      report:report
                                                 diagnostics:diagnostics
                                                    settings:settings
                                                      loader:loader];
    // The view has fitted itself to its content: the size the host asks for
    // before attaching it.
    const NSSize size = view_.frame.size;
    rect = ViewRect(0, 0, static_cast<int32>(std::lround(size.width)),
                    static_cast<int32>(std::lround(size.height)));
}

Editor::~Editor() {
    stop_following();
    [view_ removeFromSuperview];
    view_ = nil;
    link_->controller = nullptr;
}

tresult PLUGIN_API Editor::attached(void* parent, FIDString type) {
    if (!parent || !view_ || isPlatformTypeSupported(type) != kResultTrue)
        return kResultFalse;
    const tresult result = EditorView::attached(parent, type);
    NSView* host = (__bridge NSView*)parent;
    [view_ setFrameOrigin:NSZeroPoint];
    [host addSubview:view_];
    view_.postsFrameChangedNotifications = YES;
    Editor* editor = this;
    observer_ =
        [[NSNotificationCenter defaultCenter] addObserverForName:NSViewFrameDidChangeNotification
                                                          object:view_
                                                           queue:nil
                                                      usingBlock:^(NSNotification* note) {
                                                        static_cast<void>(note);
                                                        editor->follow_size();
                                                      }];
    follow_size(); // The view rereads the report as it joins the window.
    return result;
}

tresult PLUGIN_API Editor::removed() {
    stop_following();
    [view_ removeFromSuperview];
    return EditorView::removed();
}

tresult PLUGIN_API Editor::onSize(ViewRect* size) {
    const tresult result = EditorView::onSize(size);
    if (view_.superview)
        [view_ setFrameOrigin:NSZeroPoint]; // The host's view now has the view's size.
    return result;
}

void Editor::follow_size() {
    if (!isAttached() || !plugFrame || !view_)
        return;
    const NSSize size = view_.frame.size;
    ViewRect wanted(0, 0, static_cast<int32>(std::lround(size.width)),
                    static_cast<int32>(std::lround(size.height)));
    if (wanted.getWidth() == rect.getWidth() && wanted.getHeight() == rect.getHeight())
        return;
    plugFrame->resizeView(this, &wanted);
}

void Editor::stop_following() {
    if (observer_)
        [[NSNotificationCenter defaultCenter] removeObserver:observer_];
    observer_ = nil;
}
} // namespace

IPlugView* create_editor(Controller& controller) {
    @autoreleasepool {
        return new Editor(controller);
    }
}
} // namespace convolver::vst3
