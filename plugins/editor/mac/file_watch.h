// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-11: Reload When the File Changes in Convolver's window, apart from the
// window (AU-11, jrp's review of 1 November 2026). This object follows the
// filter that plays, as the report names it: its file ("filter path") and
// the files it was read from ("filter files"), with their modification times
// and sizes when it was loaded. Once a second the window asks it whether the
// filter is due to be reloaded: the files changed and the change has stayed
// the same for one check, so a file still being written is not read half
// way. The window keeps the timer, the checkbox and the loading.
//
// A C++ class, not an Objective-C one, so it needs no versioned name: its
// symbols stay inside the bundle it is compiled into, as the portable code's
// do. Main thread only, like the window.
#pragma once
#import <Foundation/Foundation.h>

namespace convolver::view {

class FileWatch {
  public:
    // The playing filter as the report gives it now (empty strings when
    // there is none). A new list of files (one per line) starts watching
    // them, with their times and sizes as they are now; the same list keeps
    // what was recorded.
    void follow(NSString* path, NSString* files);
    // The filter's file, and its files as the report lists them.
    NSString* path() const {
        return path_;
    }
    NSString* files() const {
        return text_;
    }
    // Takes the times and sizes as they are now as what plays: after a load
    // (what was read is what plays) or when watching is turned on (changes
    // made while it was off are not reloaded).
    void settle();
    // One check: whether the filter's files changed since it was loaded and
    // the change has stayed the same since the last check, with every file
    // still there. A moved or deleted file is not reloaded (the filter that
    // plays stays), and its change is then taken as seen.
    bool due();

  private:
    // Each watched file's modification time and size, or NSNull when it is
    // not there.
    NSDictionary<NSString*, id>* stamps() const;
    NSString* path_ = nil;
    NSString* text_ = nil; // The report's list, as given.
    NSArray<NSString*>* watched_ = nil;
    NSDictionary<NSString*, id>* stamps_ = nil;  // When it was loaded.
    NSDictionary<NSString*, id>* changed_ = nil; // A change waiting to settle.
};

} // namespace convolver::view
