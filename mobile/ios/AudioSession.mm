#include "AudioSession.h"

#import <AVFAudio/AVFAudio.h>
#import <UIKit/UIKit.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcAudioSession, "aether.mobile.audiosession")

namespace AetherSDR::mobile {

namespace {

// Whether the last activation took. A launch iOS makes in the background
// (TestFlight update, prewarm) cannot activate a session; it is retried
// when the app comes to the front.
bool g_active = false;

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
    // Ask for 48 kHz, the rate the TX voice chain works at: in play-and-record
    // with the built-in mic some iPhones otherwise run the input at a lower
    // rate than the format the app is handed (a tester's iPhone 17 sent 71 %
    // of real time, so speech came out broken).
    error = nil;
    [session setPreferredSampleRate:48000.0 error:&error];
    error = nil;
    if (![session setActive:YES error:&error]) {
        g_active = false;
        const bool background = UIApplication.sharedApplication.applicationState != UIApplicationStateActive;
        if (background) {
            qCInfo(lcAudioSession) << "setActive deferred (app not in front yet):"
                                   << QString::fromNSString(error.localizedDescription);
        } else {
            qCWarning(lcAudioSession) << "setActive failed:"
                                      << QString::fromNSString(error.localizedDescription);
        }
    } else {
        g_active = true;
        qCInfo(lcAudioSession) << "play-and-record session active, route"
                               << QString::fromNSString(session.currentRoute.outputs.firstObject.portType)
                               << "rate" << session.sampleRate << "Hz, input channels"
                               << static_cast<int>(session.inputNumberOfChannels) << ", IO buffer"
                               << qRound(session.IOBufferDuration * 1000.0) << "ms";
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
    // A session that could not start at launch: take it once the app is
    // in front.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationDidBecomeActiveNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                    if (!g_active) {
                        qCInfo(lcAudioSession) << "app in front: activating the session";
                        activate();
                    }
                }];
}

} // namespace AetherSDR::mobile
