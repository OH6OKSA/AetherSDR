#include "ScreenAwake.h"

#import <UIKit/UIKit.h>

namespace AetherSDR::mobile {

void setKeepScreenAwake(bool awake)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIApplication sharedApplication].idleTimerDisabled = awake ? YES : NO;
    });
}

} // namespace AetherSDR::mobile
