// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: the Cocoa view factory that kAudioUnitProperty_CocoaUI names. Hosts
// load it from the component bundle and ask it for Convolver's window, which
// uses the unit only through the Audio Unit API (properties by value, as
// Apple asks, and the Gain, Mix and Latency parameters), so it also works when the
// host runs the unit in another process (GarageBand does).
#include "convolver_identities.hpp"
#import "diagnostic_view.h"
#include "view_properties.hpp"
#import <AudioToolbox/AudioUnitUtilities.h>
#include <cmath>
#if __has_include(<AudioToolbox/AUCocoaUIView.h>)
#import <AudioToolbox/AUCocoaUIView.h>
#else
#import <AudioUnit/AUCocoaUIView.h>
#endif

#ifndef CONVOLVER_AU_VIEW_FACTORY
#error "CONVOLVER_AU_VIEW_FACTORY names the factory class; plugins/au/CMakeLists.txt defines it."
#endif

namespace {
// A Core Foundation value from one of the unit's custom properties, which
// the caller owns; nullptr if the unit cannot give it.
CFTypeRef property_value(AudioUnit unit, AudioUnitPropertyID id) {
    CFTypeRef value = nullptr;
    UInt32 size = sizeof value;
    if (AudioUnitGetProperty(unit, id, kAudioUnitScope_Global, 0, &value, &size) != noErr)
        return nullptr;
    return value;
}

// One of the unit's settings as the window's slider: its range from the
// unit, with its default (Option-click), its value read and changed through
// the Audio Unit API. AUParameterSet
// tells the host and its other views of each change. PLUG-52: begin and end
// send the parameter's change gestures (AUEventListenerNotify), as Apple asks
// of a view, so a host records a drag as one automation move and Logic's
// Touch and Latch modes know when the user lets go.
NSDictionary<NSString*, id>* setting(AudioUnit unit, AudioUnitParameterID parameter_id,
                                     NSString* name, NSString* unit_text) {
    AudioUnitParameterInfo info{};
    UInt32 size = sizeof info;
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global,
                             parameter_id, &info, &size) != noErr)
        return nil;
    if (info.flags & kAudioUnitParameterFlag_CFNameRelease && info.cfNameString)
        CFRelease(info.cfNameString);
    ConvolverSettingGetter get = ^double(void) {
      AudioUnitParameterValue value = 0;
      return AudioUnitGetParameter(unit, parameter_id, kAudioUnitScope_Global, 0, &value) == noErr
                 ? value
                 : NAN;
    };
    ConvolverSettingSetter set = ^(double value) {
      const AudioUnitParameter parameter{unit, parameter_id, kAudioUnitScope_Global, 0};
      AUParameterSet(nullptr, nullptr, &parameter, static_cast<AudioUnitParameterValue>(value), 0);
    };
    const auto gesture = [unit, parameter_id](AudioUnitEventType type) -> ConvolverSettingGesture {
        return ^(void) {
          AudioUnitEvent event{};
          event.mEventType = type;
          event.mArgument.mParameter = {unit, parameter_id, kAudioUnitScope_Global, 0};
          AUEventListenerNotify(nullptr, nullptr, &event);
        };
    };
    ConvolverSettingGesture begin = gesture(kAudioUnitEvent_BeginParameterChangeGesture);
    ConvolverSettingGesture end = gesture(kAudioUnitEvent_EndParameterChangeGesture);
    return @{
        @"name" : name,
        @"minimum" : @(info.minValue),
        @"maximum" : @(info.maxValue),
        @"default" : @(info.defaultValue),
        @"unit" : unit_text,
        @"get" : [get copy],
        @"set" : [set copy],
        @"begin" : begin,
        @"end" : end
    };
}

// A setting chosen from a list, as the window's menu: the item names from
// the unit (kAudioUnitProperty_ParameterValueStrings), the value an item's
// index.
NSDictionary<NSString*, id>* choice(AudioUnit unit, AudioUnitParameterID parameter_id,
                                    NSString* name) {
    NSMutableDictionary<NSString*, id>* entry =
        [setting(unit, parameter_id, name, @"") mutableCopy];
    CFArrayRef names = nullptr;
    UInt32 size = sizeof names;
    if (!entry ||
        AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterValueStrings, kAudioUnitScope_Global,
                             parameter_id, &names, &size) != noErr)
        return nil;
    NSArray* titles = CFBridgingRelease(names);
    if (![titles isKindOfClass:[NSArray class]] || titles.count == 0)
        return nil;
    entry[@"choices"] = titles;
    return entry;
}
} // namespace

@interface CONVOLVER_AU_VIEW_FACTORY : NSObject <AUCocoaUIBase>
@end

@implementation CONVOLVER_AU_VIEW_FACTORY
- (unsigned)interfaceVersion {
    return 0;
}

// The view sizes itself to its text; the host's preferred size is not used
// (GarageBand offers a large one, which left most of the window blank).
- (NSView*)uiViewForAudioUnit:(AudioUnit)unit withSize:(NSSize)size {
    static_cast<void>(size);
    if (!unit)
        return nil;
    const NSRect frame = NSZeroRect;
    ConvolverReportSource report = ^std::optional<convolver::HostReport>(void) {
        id value = CFBridgingRelease(property_value(unit, convolver::au::report_property));
        if (![value isKindOfClass:[NSData class]])
            return std::nullopt;
        NSData* data = value;
        try {
            return convolver::decode_report(
                std::string_view(static_cast<const char*>(data.bytes), data.length));
        } catch (...) { // Out of memory: no report this time.
            return std::nullopt;
        }
    };
    ConvolverDiagnosticsSource diagnostics = ^NSString*(void) {
      id value = CFBridgingRelease(property_value(unit, convolver::au::diagnostics_property));
      return [value isKindOfClass:[NSString class]] ? value : nil;
    };
    NSMutableArray<NSDictionary<NSString*, id>*>* settings = [NSMutableArray array];
    if (NSDictionary<NSString*, id>* gain =
            setting(unit, convolver::identity::au_gain_parameter_id, @"Gain", @" dB")) {
        // PLUG-52: the slider's taper (window_model.hpp, gain_position()).
        NSMutableDictionary<NSString*, id>* tapered = [gain mutableCopy];
        tapered[@"taper"] = @"gain";
        [settings addObject:tapered];
    }
    // PLUG-50: the menu leads the Gain row, whose box shows the gain the mode plays.
    if (NSDictionary<NSString*, id>* mode =
            choice(unit, convolver::identity::au_gain_mode_parameter_id, @"Gain mode")) {
        NSMutableDictionary<NSString*, id>* noted = [mode mutableCopy];
        noted[@"note"] = @"gain";
        [settings addObject:noted];
    }
    if (NSDictionary<NSString*, id>* mix =
            setting(unit, convolver::identity::au_mix_parameter_id, @"Mix", @"% wet")) {
        NSMutableDictionary<NSString*, id>* ends = [mix mutableCopy];
        ends[@"low"] = @"Dry"; // As reverbs label a wet/dry slider.
        ends[@"high"] = @"Wet";
        [settings addObject:ends];
    }
    if (NSDictionary<NSString*, id>* latency =
            choice(unit, convolver::identity::au_latency_parameter_id, @"Latency"))
        [settings addObject:latency];
    // A chosen or dropped file goes to the unit by path, as a property: the
    // unit reads it, so this also works when the host runs it in another process.
    ConvolverFileLoader loader = ^BOOL(NSString* path) {
      CFStringRef value = (__bridge CFStringRef)path;
      return AudioUnitSetProperty(unit, convolver::au::load_property, kAudioUnitScope_Global, 0,
                                  &value, sizeof value) == noErr;
    };
    // No Bypass checkbox: Audio Unit hosts show their own bypass button
    // (kAudioUnitProperty_BypassEffect).
    return [[CONVOLVER_DIAGNOSTIC_VIEW alloc] initWithFrame:frame
                                                     report:report
                                                diagnostics:diagnostics
                                                   settings:settings
                                                     bypass:nil
                                                     loader:loader];
}
@end
