// MacAudioRate.h — shared declaration for MacAudioRate.mm.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum {
    QSM_RETUNE_OK_NO_CHANGE         =  0,
    QSM_RETUNE_OK_CHANGED           =  1,
    QSM_RETUNE_ERR_DEVICE_NOT_FOUND = -2,
    QSM_RETUNE_ERR_NO_MATCHING_RATE = -3,
    QSM_RETUNE_ERR_SET_FAILED       = -4,
    QSM_RETUNE_ERR_TIMEOUT          = -5,
    QSM_RETUNE_ERR_QUERY_FAILED     = -6,
    QSM_RETUNE_ERR_NOT_SETTABLE     = -7,
};

// Fills errBuf with a reason on every non-OK return.
int macSetDeviceNominalSampleRate(const char *uidUtf8,
    double *outChosenRate, char *errBuf, int errBufLen);

#ifdef __cplusplus
}
#endif
