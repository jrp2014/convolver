// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: the Cocoa view factory that kAudioUnitProperty_CocoaUI names. Hosts
// load it from the component bundle and ask it for Convolver's window, which
// uses the unit only through the Audio Unit API (properties by value, as
// Apple asks, and the Gain and Mix parameters), so it also works when the
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
// unit, its value read and changed through the Audio Unit API. AUParameterSet
// tells the host and its other views of each change; the slider sends no
// begin and end of a gesture, so a host records automation from it as
// separate changes.
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
    return @{
        @"name" : name,
        @"minimum" : @(info.minValue),
        @"maximum" : @(info.maxValue),
        @"unit" : unit_text,
        @"get" : [get copy],
        @"set" : [set copy]
    };
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
    ConvolverReportSource report = ^NSDictionary<NSString*, NSString*>*(void) {
        id value = CFBridgingRelease(property_value(unit, convolver::au::report_property));
        return [value isKindOfClass:[NSDictionary class]] ? value : nil;
    };
    ConvolverDiagnosticsSource diagnostics = ^NSString*(void) {
      id value = CFBridgingRelease(property_value(unit, convolver::au::diagnostics_property));
      return [value isKindOfClass:[NSString class]] ? value : nil;
    };
    NSMutableArray<NSDictionary<NSString*, id>*>* settings = [NSMutableArray array];
    if (NSDictionary<NSString*, id>* gain =
            setting(unit, convolver::identity::au_gain_parameter_id, @"Gain", @" dB"))
        [settings addObject:gain];
    if (NSDictionary<NSString*, id>* mix =
            setting(unit, convolver::identity::au_mix_parameter_id, @"Mix", @"% wet"))
        [settings addObject:mix];
    return [[CONVOLVER_DIAGNOSTIC_VIEW alloc] initWithFrame:frame
                                                     report:report
                                                diagnostics:diagnostics
                                                   settings:settings];
}
@end
