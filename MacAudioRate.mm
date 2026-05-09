// MacAudioRate.mm — set a device's nominal CoreAudio rate to a
// 12-kHz multiple so QtSM bypasses the HAL's built-in SRC.
//
// The HAL resampler on 44.1 kHz hardware introduces clock jitter
// that breaks AFSK bit-recovery. Switching the device's nominal
// rate to 48 kHz (or 96/24/12) before opening the QAudioSource
// avoids the SRC entirely. Mirrors the manual "open Audio MIDI
// Setup and pick 48 kHz" workaround.

#import <CoreAudio/CoreAudio.h>
#import <CoreFoundation/CoreFoundation.h>
#import <dispatch/dispatch.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    QSM_RETUNE_OK_NO_CHANGE         =  0,
    QSM_RETUNE_OK_CHANGED           =  1,
    QSM_RETUNE_ERR_NOT_MACOS        = -1,
    QSM_RETUNE_ERR_DEVICE_NOT_FOUND = -2,
    QSM_RETUNE_ERR_NO_MATCHING_RATE = -3,
    QSM_RETUNE_ERR_SET_FAILED       = -4,
    QSM_RETUNE_ERR_TIMEOUT          = -5,
    QSM_RETUNE_ERR_QUERY_FAILED     = -6,
    QSM_RETUNE_ERR_NOT_SETTABLE     = -7,
};

static AudioDeviceID lookupDeviceByUID(const char *uidUtf8) {
    if (!uidUtf8 || !*uidUtf8) return kAudioObjectUnknown;
    CFStringRef uid = CFStringCreateWithCString(
        kCFAllocatorDefault, uidUtf8, kCFStringEncodingUTF8);
    if (!uid) return kAudioObjectUnknown;

    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyTranslateUIDToDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioDeviceID dev = kAudioObjectUnknown;
    UInt32 sz = sizeof(dev);
    OSStatus st = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &addr,
        sizeof(uid), &uid,
        &sz, &dev);
    CFRelease(uid);
    if (st != noErr) return kAudioObjectUnknown;
    return dev;
}

static OSStatus getCurrentRate(AudioDeviceID dev, Float64 *out) {
    AudioObjectPropertyAddress a = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 sz = sizeof(*out);
    return AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, out);
}

static Boolean rateIsSettable(AudioDeviceID dev) {
    AudioObjectPropertyAddress a = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    Boolean settable = false;
    OSStatus st = AudioObjectIsPropertySettable(dev, &a, &settable);
    return (st == noErr) && settable;
}

static Boolean deviceSupportsRate(AudioDeviceID dev, Float64 rate) {
    AudioObjectPropertyAddress a = {
        kAudioDevicePropertyAvailableNominalSampleRates,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(dev, &a, 0, NULL, &sz) != noErr || sz == 0)
        return false;
    UInt32 nRanges = sz / sizeof(AudioValueRange);
    AudioValueRange *ranges = (AudioValueRange *)malloc(sz);
    if (!ranges) return false;
    Boolean found = false;
    if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, ranges) == noErr) {
        for (UInt32 i = 0; i < nRanges; i++) {
            if (rate + 0.5 >= ranges[i].mMinimum &&
                rate - 0.5 <= ranges[i].mMaximum) { found = true; break; }
        }
    }
    free(ranges);
    return found;
}

static Boolean isAggregateDevice(AudioDeviceID dev) {
    AudioObjectPropertyAddress a = {
        kAudioObjectPropertyClass,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 cls = 0;
    UInt32 sz = sizeof(cls);
    if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &cls) != noErr)
        return false;
    return cls == kAudioAggregateDeviceClassID;
}

static int setRateAndWait(AudioDeviceID dev, Float64 desired,
                          char *errBuf, int errBufLen) {
    AudioObjectPropertyAddress a = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    if (!rateIsSettable(dev)) {
        if (errBuf) snprintf(errBuf, errBufLen,
            "device does not allow programmatic rate changes "
            "(kAudioDevicePropertyNominalSampleRate is read-only)");
        return QSM_RETUNE_ERR_NOT_SETTABLE;
    }

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    AudioObjectPropertyListenerBlock listener =
        ^(UInt32 inNumberAddresses, const AudioObjectPropertyAddress *inAddresses) {
            (void)inNumberAddresses; (void)inAddresses;
            dispatch_semaphore_signal(sem);
        };
    dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
    OSStatus addSt = AudioObjectAddPropertyListenerBlock(dev, &a, q, listener);

    OSStatus setSt = AudioObjectSetPropertyData(
        dev, &a, 0, NULL, sizeof(desired), &desired);

    int rc;
    if (setSt != noErr) {
        if (errBuf) snprintf(errBuf, errBufLen,
            "AudioObjectSetPropertyData failed (OSStatus %d). "
            "Another application may be holding this device.",
            (int)setSt);
        rc = QSM_RETUNE_ERR_SET_FAILED;
    } else {
        if (addSt == noErr) {
            dispatch_time_t deadline =
                dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC);
            dispatch_semaphore_wait(sem, deadline);
        } else {
            usleep(500 * 1000);
        }
        Float64 cur = 0.0;
        if (getCurrentRate(dev, &cur) == noErr && fabs(cur - desired) < 0.5) {
            rc = QSM_RETUNE_OK_CHANGED;
        } else {
            if (errBuf) snprintf(errBuf, errBufLen,
                "device acknowledged rate change request but did "
                "not commit %.0f Hz within 2 s (current %.0f Hz)",
                desired, cur);
            rc = QSM_RETUNE_ERR_TIMEOUT;
        }
    }

    if (addSt == noErr)
        AudioObjectRemovePropertyListenerBlock(dev, &a, q, listener);

    // This .mm builds without ARC (matching MacPermissions.mm); the
    // dispatch_semaphore_create reference must be released here or
    // every retune attempt leaks one semaphore. Under modern macOS
    // OS_OBJECT_USE_OBJC_RETAIN_RELEASE may stub dispatch_release
    // to a no-op, in which case this is harmless.
    dispatch_release(sem);
    return rc;
}

extern "C" int macSetDeviceNominalSampleRate(
    const char *uidUtf8, double *outChosenRate,
    char *errBuf, int errBufLen)
{
    if (errBuf && errBufLen) errBuf[0] = '\0';
    if (!uidUtf8 || !*uidUtf8) {
        if (errBuf) snprintf(errBuf, errBufLen, "empty device UID");
        return QSM_RETUNE_ERR_DEVICE_NOT_FOUND;
    }

    AudioDeviceID dev = lookupDeviceByUID(uidUtf8);
    if (dev == kAudioObjectUnknown) {
        if (errBuf) snprintf(errBuf, errBufLen,
            "device with UID '%s' not found in CoreAudio", uidUtf8);
        return QSM_RETUNE_ERR_DEVICE_NOT_FOUND;
    }

    if (isAggregateDevice(dev)) {
        fprintf(stderr,
            "[QtSM] note: device UID '%s' is an aggregate; "
            "rate change applies to all member devices.\n", uidUtf8);
    }

    Float64 cur = 0.0;
    if (getCurrentRate(dev, &cur) != noErr) {
        if (errBuf) snprintf(errBuf, errBufLen,
            "could not read current nominal rate");
        return QSM_RETUNE_ERR_QUERY_FAILED;
    }

    static const double kSafe[] = { 12000.0, 24000.0, 48000.0, 96000.0 };
    for (size_t i = 0; i < sizeof(kSafe)/sizeof(kSafe[0]); i++) {
        if (fabs(cur - kSafe[i]) < 0.5) {
            if (outChosenRate) *outChosenRate = cur;
            return QSM_RETUNE_OK_NO_CHANGE;
        }
    }

    static const double kLadder[] = { 48000.0, 96000.0, 24000.0, 12000.0 };
    for (size_t i = 0; i < sizeof(kLadder)/sizeof(kLadder[0]); i++) {
        if (deviceSupportsRate(dev, kLadder[i])) {
            int rc = setRateAndWait(dev, kLadder[i], errBuf, errBufLen);
            if (rc == QSM_RETUNE_OK_CHANGED && outChosenRate)
                *outChosenRate = kLadder[i];
            return rc;
        }
    }

    if (errBuf) snprintf(errBuf, errBufLen,
        "device does not support any of 48/96/24/12 kHz nominally "
        "(current rate %.0f Hz)", cur);
    return QSM_RETUNE_ERR_NO_MATCHING_RATE;
}
