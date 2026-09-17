#include "bb_audio_decode.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcomment"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#endif
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define BB_AUDIO_PCM_LIMIT ((size_t)256U * 1024U * 1024U)

static unsigned int bb_audio_u16le(const unsigned char *data)
{
    return (unsigned int)data[0] | ((unsigned int)data[1] << 8);
}

static uint32_t bb_audio_u32le(const unsigned char *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static short bb_audio_s16le(const unsigned char *data)
{
    unsigned int value;
    value = bb_audio_u16le(data);
    if (value >= 0x8000U) return (short)((int)value - 0x10000L);
    return (short)value;
}

static bool bb_audio_bytes_equal(const unsigned char *data, const char expected[4])
{
    return data[0] == (unsigned char)expected[0] &&
           data[1] == (unsigned char)expected[1] &&
           data[2] == (unsigned char)expected[2] &&
           data[3] == (unsigned char)expected[3];
}

static bool bb_audio_sample_capacity(unsigned int frames, unsigned int channels, size_t *bytes)
{
    size_t samples;
    if (frames == 0 || channels == 0) return false;
    if ((size_t)frames > ((size_t)-1) / (size_t)channels) return false;
    samples = (size_t)frames * (size_t)channels;
    if (samples > ((size_t)-1) / sizeof(short)) return false;
    *bytes = samples * sizeof(short);
    if (*bytes > BB_AUDIO_PCM_LIMIT) return false;
    return true;
}

void bb_audio_pcm_free(BBAudioPcm *pcm)
{
    if (pcm == NULL) return;
    free(pcm->samples);
    memset(pcm, 0, sizeof *pcm);
}

bool bb_audio_decode_wav(const unsigned char *data, size_t size, BBAudioPcm *out)
{
    size_t scan_end;
    size_t offset;
    size_t payload;
    size_t chunk_size;
    size_t padded_size;
    const unsigned char *sample_data;
    size_t sample_data_size;
    unsigned int format_tag;
    unsigned int channels;
    unsigned int sample_rate;
    unsigned int byte_rate;
    unsigned int block_align;
    unsigned int bits_per_sample;
    size_t frame_count;
    size_t sample_count;
    size_t allocation_size;
    size_t i;
    short *samples;
    bool have_format;

    if (out == NULL) return false;
    memset(out, 0, sizeof *out);
    if (data == NULL || size < 12 ||
        !bb_audio_bytes_equal(data, "RIFF") ||
        !bb_audio_bytes_equal(data + 8, "WAVE")) return false;

    chunk_size = (size_t)bb_audio_u32le(data + 4);
    if (chunk_size < 4 || chunk_size > size - 8) return false;
    scan_end = chunk_size + 8;
    offset = 12;
    sample_data = NULL;
    sample_data_size = 0;
    format_tag = 0;
    channels = 0;
    sample_rate = 0;
    byte_rate = 0;
    block_align = 0;
    bits_per_sample = 0;
    have_format = false;

    while (offset <= scan_end && scan_end - offset >= 8) {
        payload = offset + 8;
        chunk_size = (size_t)bb_audio_u32le(data + offset + 4);
        if (chunk_size > scan_end - payload) return false;
        if (bb_audio_bytes_equal(data + offset, "fmt ")) {
            if (chunk_size < 16) return false;
            format_tag = bb_audio_u16le(data + payload);
            channels = bb_audio_u16le(data + payload + 2);
            sample_rate = (unsigned int)bb_audio_u32le(data + payload + 4);
            byte_rate = (unsigned int)bb_audio_u32le(data + payload + 8);
            block_align = bb_audio_u16le(data + payload + 12);
            bits_per_sample = bb_audio_u16le(data + payload + 14);
            have_format = true;
        } else if (bb_audio_bytes_equal(data + offset, "data") && sample_data == NULL) {
            sample_data = data + payload;
            sample_data_size = chunk_size;
        }
        padded_size = chunk_size;
        if ((padded_size & 1U) != 0) {
            if (padded_size == (size_t)-1) return false;
            ++padded_size;
        }
        if (padded_size > scan_end - payload) return false;
        offset = payload + padded_size;
    }

    if (offset != scan_end) return false;
    if (!have_format || sample_data == NULL || sample_data_size == 0) return false;
    if (format_tag != 1 || (channels != 1 && channels != 2) ||
        sample_rate == 0 || sample_rate > 192000U || bits_per_sample != 16) return false;
    if (block_align != channels * 2U ||
        byte_rate != sample_rate * block_align ||
        sample_data_size % block_align != 0) return false;

    frame_count = sample_data_size / block_align;
    if (frame_count == 0 || frame_count > UINT_MAX) return false;
    if (!bb_audio_sample_capacity((unsigned int)frame_count, channels, &allocation_size)) return false;
    samples = (short *)malloc(allocation_size);
    if (samples == NULL) return false;
    sample_count = frame_count * channels;
    for (i = 0; i < sample_count; ++i) samples[i] = bb_audio_s16le(sample_data + i * 2);

    out->samples = samples;
    out->frame_count = (unsigned int)frame_count;
    out->sample_rate = sample_rate;
    out->channels = channels;
    return true;
}

static bool bb_audio_validate_ogg_container(const unsigned char *data, size_t size)
{
    size_t offset;
    size_t segment_table_end;
    size_t body_size;
    size_t page_end;
    unsigned int segment_count;
    unsigned int i;
    unsigned int header_type;
    uint32_t serial;
    uint32_t sequence;
    uint32_t expected_sequence;
    bool first_page;
    bool end_of_stream;

    if (data == NULL || size < 27) return false;
    offset = 0;
    serial = 0;
    expected_sequence = 0;
    first_page = true;
    end_of_stream = false;
    while (offset < size) {
        if (size - offset < 27 || !bb_audio_bytes_equal(data + offset, "OggS") ||
            data[offset + 4] != 0) return false;
        header_type = data[offset + 5];
        if ((header_type & ~0x07U) != 0 || end_of_stream) return false;
        segment_count = data[offset + 26];
        segment_table_end = offset + 27 + segment_count;
        if (segment_table_end < offset || segment_table_end > size) return false;
        body_size = 0;
        for (i = 0; i < segment_count; ++i) {
            if (body_size > size - (size_t)data[offset + 27 + i]) return false;
            body_size += data[offset + 27 + i];
        }
        if (body_size > size - segment_table_end) return false;
        page_end = segment_table_end + body_size;
        sequence = bb_audio_u32le(data + offset + 18);
        if (first_page) {
            if ((header_type & 0x02U) == 0 || sequence != 0) return false;
            serial = bb_audio_u32le(data + offset + 14);
            first_page = false;
        } else if ((header_type & 0x02U) != 0 ||
                   bb_audio_u32le(data + offset + 14) != serial ||
                   sequence != expected_sequence) return false;
        expected_sequence = sequence + 1;
        end_of_stream = (header_type & 0x04U) != 0;
        offset = page_end;
    }
    return !first_page && end_of_stream && offset == size;
}

bool bb_audio_decode_ogg(const unsigned char *data, size_t size, BBAudioPcm *out)
{
    stb_vorbis *decoder;
    stb_vorbis_info info;
    unsigned int frame_count;
    unsigned int decoded_frames;
    int received;
    int remaining_samples;
    size_t allocation_size;
    short *samples;

    if (out == NULL) return false;
    memset(out, 0, sizeof *out);
    if (data == NULL || size == 0 || size > INT_MAX ||
        !bb_audio_validate_ogg_container(data, size)) return false;
    decoder = stb_vorbis_open_memory(data, (int)size, NULL, NULL);
    if (decoder == NULL) return false;
    info = stb_vorbis_get_info(decoder);
    frame_count = stb_vorbis_stream_length_in_samples(decoder);
    if ((info.channels != 1 && info.channels != 2) ||
        info.sample_rate == 0 || info.sample_rate > 192000U || frame_count == 0 ||
        !bb_audio_sample_capacity(frame_count, (unsigned int)info.channels, &allocation_size)) {
        stb_vorbis_close(decoder);
        return false;
    }
    samples = (short *)malloc(allocation_size);
    if (samples == NULL) {
        stb_vorbis_close(decoder);
        return false;
    }

    decoded_frames = 0;
    while (decoded_frames < frame_count) {
        if (frame_count - decoded_frames > (unsigned int)(INT_MAX / info.channels)) {
            free(samples);
            stb_vorbis_close(decoder);
            return false;
        }
        remaining_samples = (int)((frame_count - decoded_frames) * (unsigned int)info.channels);
        received = stb_vorbis_get_samples_short_interleaved(
            decoder, info.channels,
            samples + (size_t)decoded_frames * (unsigned int)info.channels,
            remaining_samples);
        if (received <= 0) break;
        decoded_frames += (unsigned int)received;
    }
    stb_vorbis_close(decoder);
    if (decoded_frames != frame_count) {
        free(samples);
        return false;
    }

    out->samples = samples;
    out->frame_count = frame_count;
    out->sample_rate = info.sample_rate;
    out->channels = (unsigned int)info.channels;
    return true;
}

static short bb_audio_interpolate(short first, short second, double fraction)
{
    double value;
    value = (double)first + ((double)second - (double)first) * fraction;
    if (value > 32767.0) value = 32767.0;
    if (value < -32768.0) value = -32768.0;
    return (short)value;
}

bool bb_audio_pcm_normalize(BBAudioPcm *pcm, unsigned int sample_rate, unsigned int channels)
{
    uint64_t output_count_64;
    unsigned int output_count;
    size_t allocation_size;
    short *output;
    unsigned int frame;
    unsigned int channel;
    double source_position;
    unsigned int source_frame;
    unsigned int next_frame;
    double fraction;
    unsigned int source_channel;
    short first;
    short second;

    if (pcm == NULL || pcm->samples == NULL || pcm->frame_count == 0 ||
        pcm->sample_rate == 0 || (pcm->channels != 1 && pcm->channels != 2) ||
        sample_rate == 0 || channels != 2) return false;
    if (pcm->sample_rate == sample_rate && pcm->channels == channels) return true;

    output_count_64 = ((uint64_t)pcm->frame_count * (uint64_t)sample_rate +
                       (uint64_t)pcm->sample_rate - 1U) / (uint64_t)pcm->sample_rate;
    if (output_count_64 == 0 || output_count_64 > UINT_MAX) return false;
    output_count = (unsigned int)output_count_64;
    if (!bb_audio_sample_capacity(output_count, channels, &allocation_size)) return false;
    output = (short *)malloc(allocation_size);
    if (output == NULL) return false;

    for (frame = 0; frame < output_count; ++frame) {
        source_position = (double)frame * (double)pcm->sample_rate / (double)sample_rate;
        source_frame = (unsigned int)source_position;
        if (source_frame >= pcm->frame_count) source_frame = pcm->frame_count - 1;
        next_frame = source_frame + 1 < pcm->frame_count ? source_frame + 1 : source_frame;
        fraction = source_position - (double)source_frame;
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
        for (channel = 0; channel < channels; ++channel) {
            source_channel = pcm->channels == 1 ? 0 : channel;
            first = pcm->samples[(size_t)source_frame * pcm->channels + source_channel];
            second = pcm->samples[(size_t)next_frame * pcm->channels + source_channel];
            output[(size_t)frame * channels + channel] =
                bb_audio_interpolate(first, second, fraction);
        }
    }

    free(pcm->samples);
    pcm->samples = output;
    pcm->frame_count = output_count;
    pcm->sample_rate = sample_rate;
    pcm->channels = channels;
    return true;
}
