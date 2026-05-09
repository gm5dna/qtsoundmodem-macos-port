/*
Copyright (C) 2024 gm5dna QtSoundModem macOS port contributors

This file is part of QtSoundModem.

QtSoundModem is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// Microphone authorisation probe.
//
// Without NSMicrophoneUsageDescription in Info.plist, or with
// permission denied, QAudioSource on macOS returns zero samples
// indefinitely with no error. This shim returns the current
// AVCaptureDevice authorisation status so QtSoundInit() can show a
// dialog directing the user to System Settings instead of suffering
// a silent zero-samples failure.
//
// Status codes are kept ABI-equivalent to AVAuthorizationStatus
// (NotDetermined=0, Restricted=1, Denied=2, Authorized=3) so the
// caller does not need to import AVFoundation.

#import <AVFoundation/AVFoundation.h>

extern "C" int macAudioAuthorisationStatus(void)
{
    if (@available(macOS 10.14, *))
    {
        AVAuthorizationStatus status =
            [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        return (int)status;
    }
    // Pre-10.14: no permission system, treat as authorised.
    return 3; // AVAuthorizationStatusAuthorized
}

extern "C" void macRequestAudioAuthorisation(void)
{
    if (@available(macOS 10.14, *))
    {
        // Fire-and-forget request. The user sees the native TCC prompt
        // asynchronously, with body text from NSMicrophoneUsageDescription
        // in Info.plist; no Qt dialog is layered on top. On subsequent
        // launches QtSoundInit() consults macAudioAuthorisationStatus() so
        // it can warn the user if access has since been Denied or
        // Restricted (states macOS will not re-prompt for).
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL granted) {
            (void)granted;
        }];
    }
}
