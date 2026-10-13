#include "AudioSession.h"

#import <AVFAudio/AVFAudio.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcAudioSession, "aether.mobile.audiosession")

namespace AetherSDR::mobile {

namespace {

void activate()
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* error = nil;
    const AVAudioSessionCategoryOptions options = AVAudioSessionCategoryOptionDefaultToSpeaker
        | AVAudioSessionCategoryOptionAllowBluetooth
        | AVAudioSessionCategoryOptionAllowBluetoothA2DP;
    if (![session setCategory:AVAudioSessionCategoryPlayAndRecord
                         mode:AVAudioSessionModeDefault
                      options:options
                        error:&error]) {
        qCWarning(lcAudioSession) << "setCategory failed:"
                                  << QString::fromNSString(error.localizedDescription);
    }
    error = nil;
    if (![session setActive:YES error:&error]) {
        qCWarning(lcAudioSession) << "setActive failed:"
                                  << QString::fromNSString(error.localizedDescription);
    } else {
        qCInfo(lcAudioSession) << "play-and-record session active, route"
                               << QString::fromNSString(session.currentRoute.outputs.firstObject.portType);
    }
}

} // namespace

void configureAudioSession()
{
    activate();
    static bool observing = false;
    if (observing) {
        return;
    }
    observing = true;
    // After a call, alarm or Siri the system ends the interruption; take the
    // session back so receive audio resumes.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:AVAudioSessionInterruptionNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
                    const auto type = static_cast<AVAudioSessionInterruptionType>(
                        [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue]);
                    qCInfo(lcAudioSession) << "interruption"
                                           << (type == AVAudioSessionInterruptionTypeBegan ? "began" : "ended");
                    if (type == AVAudioSessionInterruptionTypeEnded) {
                        activate();
                    }
                }];
}

} // namespace AetherSDR::mobile
