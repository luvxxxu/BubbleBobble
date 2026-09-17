#include "bb_audio_decode.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BB_TEST_ASSET_DIR
#define BB_TEST_ASSET_DIR "Assets"
#endif

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *file;
    long length;
    unsigned char *data;
    size_t received;
    *size = 0;
    file = fopen(path, "rb");
    if (file == NULL) return NULL;
    assert(fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length > 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    data = (unsigned char *)malloc((size_t)length);
    assert(data != NULL);
    received = fread(data, 1, (size_t)length, file);
    assert(fclose(file) == 0);
    assert(received == (size_t)length);
    *size = (size_t)length;
    return data;
}

static void write_u16le(unsigned char *out, unsigned int value)
{
    out[0] = (unsigned char)(value & 0xffU);
    out[1] = (unsigned char)((value >> 8) & 0xffU);
}

static void write_u32le(unsigned char *out, uint32_t value)
{
    out[0] = (unsigned char)(value & 0xffU);
    out[1] = (unsigned char)((value >> 8) & 0xffU);
    out[2] = (unsigned char)((value >> 16) & 0xffU);
    out[3] = (unsigned char)((value >> 24) & 0xffU);
}

static void test_wav_chunks(void)
{
    unsigned char wav[62];
    BBAudioPcm pcm;
    size_t length;
    memset(wav, 0, sizeof wav);
    memcpy(wav, "RIFF", 4);
    write_u32le(wav + 4, 52);
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "JUNK", 4);
    write_u32le(wav + 16, 3);
    wav[20] = 1;
    wav[21] = 2;
    wav[22] = 3;
    wav[23] = 0;
    memcpy(wav + 24, "fmt ", 4);
    write_u32le(wav + 28, 16);
    write_u16le(wav + 32, 1);
    write_u16le(wav + 34, 2);
    write_u32le(wav + 36, 44100);
    write_u32le(wav + 40, 176400);
    write_u16le(wav + 44, 4);
    write_u16le(wav + 46, 16);
    memcpy(wav + 48, "data", 4);
    write_u32le(wav + 52, 4);
    write_u16le(wav + 56, 0x8000U);
    write_u16le(wav + 58, 0x7fffU);
    wav[60] = 0xaa;
    wav[61] = 0xbb;

    assert(bb_audio_decode_wav(wav, sizeof wav, &pcm));
    assert(pcm.frame_count == 1);
    assert(pcm.sample_rate == 44100);
    assert(pcm.channels == 2);
    assert(pcm.samples[0] == -32768);
    assert(pcm.samples[1] == 32767);
    bb_audio_pcm_free(&pcm);
    for (length = 0; length < 60; ++length)
        assert(!bb_audio_decode_wav(wav, length, &pcm));
}

static void test_asset(const char *relative, unsigned int frames, bool ogg)
{
    char path[1024];
    unsigned char *data;
    size_t size;
    BBAudioPcm pcm;
    int result;
    result = snprintf(path, sizeof path, "%s/%s", BB_TEST_ASSET_DIR, relative);
    assert(result > 0 && (size_t)result < sizeof path);
    data = read_file(path, &size);
    assert(data != NULL);
    if (ogg) assert(bb_audio_decode_ogg(data, size, &pcm));
    else assert(bb_audio_decode_wav(data, size, &pcm));
    assert(pcm.frame_count == frames);
    assert(pcm.sample_rate == 44100);
    assert(pcm.channels == 2);
    assert(bb_audio_pcm_normalize(&pcm, 44100, 2));
    assert(pcm.frame_count == frames);
    bb_audio_pcm_free(&pcm);
    if (ogg) assert(!bb_audio_decode_ogg(data, size / 2, &pcm));
    free(data);
}

int main(void)
{
    test_wav_chunks();
    test_asset("SFX/Bubble Bobble SFX (2).wav", 54784, false);
    test_asset("SFX/Bubble Bobble SFX (3).wav", 54784, false);
    test_asset("SFX/Jump.wav", 67584, false);
    test_asset("SFX/The Quest Begins.ogg", 7938432, true);
    puts("legacy audio decoder tests passed");
    return 0;
}
