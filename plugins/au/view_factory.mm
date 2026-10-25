// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: the Cocoa view factory that kAudioUnitProperty_CocoaUI names. Hosts
// load it from the component bundle and ask it for Convolver's window, which
// reads the unit only through AudioUnitGetProperty, by value, as Apple asks,
// so it also works when the host runs the unit in another process
// (GarageBand does).
#import "diagnostic_view.h"
#include "view_properties.hpp"
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
    return [[CONVOLVER_DIAGNOSTIC_VIEW alloc] initWithFrame:frame
                                                     report:report
                                                diagnostics:diagnostics];
}
@end
