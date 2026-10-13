#include "ShareText.h"

#import <UIKit/UIKit.h>

namespace AetherSDR::mobile {

namespace {

UIViewController* topViewController()
{
    UIWindow* window = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) {
            continue;
        }
        for (UIWindow* w in static_cast<UIWindowScene*>(scene).windows) {
            if (w.isKeyWindow) {
                window = w;
                break;
            }
        }
        if (window) {
            break;
        }
    }
    UIViewController* top = window.rootViewController;
    while (top.presentedViewController) {
        top = top.presentedViewController;
    }
    return top;
}

} // namespace

void copyText(const QString& text)
{
    UIPasteboard.generalPasteboard.string = text.toNSString();
}

bool shareText(const QString& subject, const QString& text)
{
    UIViewController* top = topViewController();
    if (!top) {
        copyText(text);
        return false;
    }
    UIActivityViewController* sheet =
        [[UIActivityViewController alloc] initWithActivityItems:@[text.toNSString()]
                                          applicationActivities:nil];
    [sheet setValue:subject.toNSString() forKey:@"subject"];
    // iPad shows the sheet as a popover, which needs an anchor.
    if (sheet.popoverPresentationController) {
        sheet.popoverPresentationController.sourceView = top.view;
        sheet.popoverPresentationController.sourceRect =
            CGRectMake(CGRectGetMidX(top.view.bounds), CGRectGetMidY(top.view.bounds), 1, 1);
        sheet.popoverPresentationController.permittedArrowDirections = 0;
    }
    [top presentViewController:sheet animated:YES completion:nil];
    return true;
}

} // namespace AetherSDR::mobile
