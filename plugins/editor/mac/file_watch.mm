// SPDX-License-Identifier: GPL-2.0-or-later
#import "file_watch.h"

namespace convolver::view {

void FileWatch::follow(NSString* path, NSString* files) {
    path_ = path;
    if ([files isEqualToString:text_])
        return;
    text_ = files;
    NSMutableArray<NSString*>* list = [NSMutableArray array];
    for (NSString* line in [files componentsSeparatedByString:@"\n"])
        if (line.length)
            [list addObject:line];
    watched_ = list;
    settle();
}

void FileWatch::settle() {
    stamps_ = stamps();
    changed_ = nil;
}

bool FileWatch::due() {
    if (path_.length == 0 || watched_.count == 0)
        return false;
    NSDictionary<NSString*, id>* now = stamps();
    if ([now isEqualToDictionary:stamps_]) {
        changed_ = nil;
        return false;
    }
    if (![now isEqualToDictionary:changed_]) {
        changed_ = now; // Changed: due once it has stayed so for one check.
        return false;
    }
    stamps_ = now;
    changed_ = nil;
    return ![now.allValues containsObject:[NSNull null]];
}

NSDictionary<NSString*, id>* FileWatch::stamps() const {
    NSMutableDictionary<NSString*, id>* now = [NSMutableDictionary dictionary];
    NSFileManager* manager = [NSFileManager defaultManager];
    for (NSString* path in watched_) {
        NSDictionary<NSFileAttributeKey, id>* attributes = [manager attributesOfItemAtPath:path
                                                                                     error:nil];
        NSDate* modified = attributes.fileModificationDate;
        now[path] = modified ? @[ modified, @(attributes.fileSize) ] : [NSNull null];
    }
    return now;
}

} // namespace convolver::view
