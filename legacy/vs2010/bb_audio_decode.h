#ifndef BB_AUDIO_DECODE_H
#define BB_AUDIO_DECODE_H

#include <stddef.h>

#include "bb_compat.h"

typedef struct BBAudioPcm {
    short *samples;
    unsigned int frame_count;
    unsigned int sample_rate;
    unsigned int channels;
} BBAudioPcm;

bool bb_audio_decode_wav(const unsigned char *data, size_t size, BBAudioPcm *out);
bool bb_audio_decode_ogg(const unsigned char *data, size_t size, BBAudioPcm *out);
bool bb_audio_pcm_normalize(BBAudioPcm *pcm, unsigned int sample_rate, unsigned int channels);
void bb_audio_pcm_free(BBAudioPcm *pcm);

#endif
