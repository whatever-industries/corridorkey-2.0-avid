#include "PreviewProbe_mac.h"

#import <AppKit/AppKit.h>
#include <cstring>

namespace {

NSView* InputColumn(NSString* name, const PreviewProbeInput& input)
{
    NSStackView* column = [NSStackView new];
    column.orientation = NSUserInterfaceLayoutOrientationVertical;
    column.spacing = 8;
    [column addArrangedSubview:[NSTextField labelWithString:name]];

    NSImageView* view = [NSImageView new];
    [view.widthAnchor constraintEqualToConstant:288].active = YES;
    [view.heightAnchor constraintEqualToConstant:168].active = YES;
    view.imageScaling = NSImageScaleProportionallyUpOrDown;
    if (!input.rgba.empty()) {
        NSBitmapImageRep* pixels = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nullptr pixelsWide:input.width pixelsHigh:input.height
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
            bytesPerRow:input.width * 4 bitsPerPixel:32];
        if (pixels.bitmapData) {
            std::memcpy(pixels.bitmapData, input.rgba.data(), input.rgba.size());
            NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(input.width, input.height)];
            [image addRepresentation:pixels];
            view.image = image;
        }
    }
    [column addArrangedSubview:view];

    NSString* description = [NSString stringWithUTF8String:input.description.c_str()];
    [column addArrangedSubview:[NSTextField labelWithString:description ?: @""]];
    return column;
}

}

void ShowPreviewProbe(acfUInt32 frame, const std::array<PreviewProbeInput, 2>& inputs, bool distinct)
{
    // AVX contexts belong to the callback. The thumbnails already own their pixels.
    if (![NSThread isMainThread])
        return;

    @autoreleasepool {
        NSStackView* row = [NSStackView stackViewWithViews:@[
            InputColumn(@"V1 · Background", inputs[0]), InputColumn(@"V2 · Foreground", inputs[1])
        ]];
        row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        row.spacing = 16;
        row.frame = NSMakeRect(0, 0, 592, 230);

        NSAlert* alert = [NSAlert new];
        alert.messageText = @"CorridorKey Timeline Input Test";
        alert.informativeText = [NSString stringWithFormat:@"Frame %u. %@", static_cast<unsigned>(frame),
            distinct ? @"AVX2 returned two distinct, non-black frames."
                     : @"Use a frame with distinct, visible footage on V1 and V2."];
        alert.accessoryView = row;
        [alert addButtonWithTitle:@"Close"];
        [alert runModal];
    }
}
