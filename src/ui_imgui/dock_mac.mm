// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#import <Cocoa/Cocoa.h>
#include "ui_imgui/dock.h"

void setDockProgress(float fraction) {
    @autoreleasepool {
        NSDockTile* tile = [NSApp dockTile];
        if (!tile) return;
        static NSView* view = nil;
        static NSProgressIndicator* bar = nil;
        if (fraction < 0.0f) {  // rien a afficher : l'icone normale revient
            if (view) {
                [tile setContentView:nil];
                [tile display];
            }
            return;
        }
        if (!view) {
            view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 128, 128)];
            NSImageView* icon = [[NSImageView alloc] initWithFrame:[view bounds]];
            [icon setImage:[NSApp applicationIconImage]];
            [view addSubview:icon];
            bar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(8, 6, 112, 14)];
            [bar setStyle:NSProgressIndicatorStyleBar];
            [bar setIndeterminate:NO];
            [bar setMinValue:0.0];
            [bar setMaxValue:1.0];
            [view addSubview:bar];
        }
        [bar setDoubleValue:fraction];
        [tile setContentView:view];
        [tile display];
    }
}
