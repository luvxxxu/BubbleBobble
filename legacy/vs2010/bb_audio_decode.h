#ifndef BB_AUDIO_DECODE_H
#define BB_AUDIO_DECODE_H

#include <stddef.h>

#include "bb_compat.h"

/* Interleaved signed 16-bit PCM. The caller owns samples returned by a
 * successful decoder and releases them with bb_audio_pcm_free(). */
typedef struct BBAudioPcm {
    short *samples;
    unsigned int frame_count;
    unsigned int sample_rate;
    unsigned int channels;
} BBAudioPcm;

/* Decoders clear out on failure and accept only the formats the WinMM mixer
 * can normalize. Input bytes remain owned by the caller. */
bool bb_audio_decode_wav(const unsigned char *data, size_t size, BBAudioPcm *out);
bool bb_audio_decode_ogg(const unsigned char *data, size_t size, BBAudioPcm *out);
/* Converts in place to the requested stereo rate; failure leaves pcm owned
 * by the caller in its original form. */
bool bb_audio_pcm_normalize(BBAudioPcm *pcm, unsigned int sample_rate, unsigned int channels);
void bb_audio_pcm_free(BBAudioPcm *pcm);

#endif
