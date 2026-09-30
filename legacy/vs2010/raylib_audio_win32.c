#define WIN32_LEAN_AND_MEAN
/* Include Win32 under temporary names for identifiers also used by raylib. */
#define Rectangle BBWin32Rectangle
#define CloseWindow BBWin32CloseWindow
#include <windows.h>
#include <mmsystem.h>
#undef CloseWindow
#undef Rectangle

#ifdef PlaySound
#undef PlaySound
#endif
#ifdef LoadImage
#undef LoadImage
#endif
#ifdef DrawText
#undef DrawText
#endif
#ifdef DrawTextEx
#undef DrawTextEx
#endif

#include "raylib.h"
#include "bb_audio_decode.h"
#include "bb_platform.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every resource is converted before reaching the mixer, so waveOut always
 * receives the same signed 16-bit, stereo, 44.1 kHz buffer format. */
#define BB_AUDIO_SAMPLE_RATE 44100U
#define BB_AUDIO_CHANNELS 2U
#define BB_AUDIO_BITS 16U
#define BB_AUDIO_BUFFER_COUNT 4
#define BB_AUDIO_BUFFER_FRAMES 512
#define BB_AUDIO_MAX_SOUNDS 64
#define BB_AUDIO_MAX_MUSIC 4
#define BB_AUDIO_MAX_VOICES 32
#define BB_AUDIO_FILE_LIMIT (128L * 1024L * 1024L)

typedef struct BBSoundResource {
    short *samples;
    unsigned int frame_count;
    unsigned int generation;
    bool used;
} BBSoundResource;

typedef struct BBMusicResource {
    short *samples;
    unsigned int frame_count;
    unsigned int cursor;
    unsigned int generation;
    float volume;
    bool used;
    bool playing;
    bool looping;
} BBMusicResource;

typedef struct BBVoice {
    unsigned int sound_index;
    unsigned int generation;
    unsigned int cursor;
    unsigned int serial;
    bool active;
} BBVoice;

/* The critical section protects resources, cursors, voices and volumes from
 * the worker. Fault fields use interlocked operations across both threads. */
typedef struct BBAudioEngine {
    CRITICAL_SECTION lock;
    bool lock_initialized;
    bool ready;
    volatile LONG faulted;
    volatile LONG worker_error;
    HWAVEOUT output;
    HANDLE completion_event;
    HANDLE shutdown_event;
    HANDLE worker;
    WAVEHDR headers[BB_AUDIO_BUFFER_COUNT];
    short buffers[BB_AUDIO_BUFFER_COUNT][BB_AUDIO_BUFFER_FRAMES * BB_AUDIO_CHANNELS];
    bool header_prepared[BB_AUDIO_BUFFER_COUNT];
    float master_volume;
    unsigned int next_voice;
    unsigned int play_serial;
    DWORD last_recovery_tick;
    BBSoundResource sounds[BB_AUDIO_MAX_SOUNDS];
    BBMusicResource music[BB_AUDIO_MAX_MUSIC];
    BBVoice voices[BB_AUDIO_MAX_VOICES];
} BBAudioEngine;

static BBAudioEngine bb_audio;

static void bb_audio_report_error(const char *operation, MMRESULT result);

static unsigned int bb_audio_next_generation(unsigned int value)
{
    ++value;
    if (value == 0) value = 1;
    return value;
}

static float bb_audio_clamp_volume(float volume)
{
    if (!(volume >= 0.0f)) return 0.0f;
    if (volume > 1.0f) return 1.0f;
    return volume;
}

static short bb_audio_clamp_sample(int value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (short)value;
}

/* Handles use 1-based slot IDs. A generation change invalidates copies of a
 * handle after its slot is unloaded and reused. Call with the lock held. */
static bool bb_audio_sound_valid_locked(Sound sound, unsigned int *index)
{
    unsigned int value;
    if (sound.id == 0 || sound.id > BB_AUDIO_MAX_SOUNDS || sound.generation == 0) return false;
    value = sound.id - 1;
    if (!bb_audio.sounds[value].used ||
        bb_audio.sounds[value].generation != sound.generation ||
        bb_audio.sounds[value].samples == NULL ||
        bb_audio.sounds[value].frame_count == 0) return false;
    if (index != NULL) *index = value;
    return true;
}

static bool bb_audio_music_valid_locked(Music music, unsigned int *index)
{
    unsigned int value;
    if (music.id == 0 || music.id > BB_AUDIO_MAX_MUSIC || music.generation == 0) return false;
    value = music.id - 1;
    if (!bb_audio.music[value].used ||
        bb_audio.music[value].generation != music.generation ||
        bb_audio.music[value].samples == NULL ||
        bb_audio.music[value].frame_count == 0) return false;
    if (index != NULL) *index = value;
    return true;
}

/* Called under the engine lock. Accumulate all active voices before clamping
 * each output sample, so one loud source does not clip the others early. */
static void bb_audio_mix(short *output, unsigned int frame_count)
{
    unsigned int frame;
    unsigned int voice_index;
    unsigned int music_index;
    BBVoice *voice;
    BBSoundResource *sound;
    BBMusicResource *music;
    int left;
    int right;

    for (frame = 0; frame < frame_count; ++frame) {
        left = 0;
        right = 0;
        for (voice_index = 0; voice_index < BB_AUDIO_MAX_VOICES; ++voice_index) {
            voice = &bb_audio.voices[voice_index];
            if (!voice->active) continue;
            if (voice->sound_index >= BB_AUDIO_MAX_SOUNDS) {
                voice->active = false;
                continue;
            }
            sound = &bb_audio.sounds[voice->sound_index];
            if (!sound->used || sound->generation != voice->generation ||
                sound->samples == NULL || voice->cursor >= sound->frame_count) {
                voice->active = false;
                continue;
            }
            left += sound->samples[(size_t)voice->cursor * 2];
            right += sound->samples[(size_t)voice->cursor * 2 + 1];
            ++voice->cursor;
            if (voice->cursor >= sound->frame_count) voice->active = false;
        }
        for (music_index = 0; music_index < BB_AUDIO_MAX_MUSIC; ++music_index) {
            music = &bb_audio.music[music_index];
            if (!music->used || !music->playing || music->samples == NULL) continue;
            if (music->cursor >= music->frame_count) {
                if (music->looping) music->cursor = 0;
                else {
                    music->playing = false;
                    continue;
                }
            }
            left += (int)((float)music->samples[(size_t)music->cursor * 2] * music->volume);
            right += (int)((float)music->samples[(size_t)music->cursor * 2 + 1] * music->volume);
            ++music->cursor;
        }
        left = (int)((float)left * bb_audio.master_volume);
        right = (int)((float)right * bb_audio.master_volume);
        output[(size_t)frame * 2] = bb_audio_clamp_sample(left);
        output[(size_t)frame * 2 + 1] = bb_audio_clamp_sample(right);
    }
}

/* CALLBACK_EVENT only signals completion. Refill every WHDR_DONE header on
 * this worker thread, rather than decoding or mixing inside a driver callback. */
static DWORD WINAPI bb_audio_worker(void *parameter)
{
    HANDLE events[2];
    DWORD wait_result;
    int i;
    MMRESULT result;
    (void)parameter;
    events[0] = bb_audio.shutdown_event;
    events[1] = bb_audio.completion_event;

    for (;;) {
        wait_result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (wait_result == WAIT_OBJECT_0) break;
        if (wait_result != WAIT_OBJECT_0 + 1) {
            InterlockedExchange(&bb_audio.faulted, 1);
            break;
        }
        ResetEvent(bb_audio.completion_event);
        for (i = 0; i < BB_AUDIO_BUFFER_COUNT; ++i) {
            if (!bb_audio.header_prepared[i] ||
                (bb_audio.headers[i].dwFlags & WHDR_DONE) == 0) continue;
            EnterCriticalSection(&bb_audio.lock);
            bb_audio_mix(bb_audio.buffers[i], BB_AUDIO_BUFFER_FRAMES);
            LeaveCriticalSection(&bb_audio.lock);
            result = waveOutWrite(
                bb_audio.output, &bb_audio.headers[i], (UINT)sizeof bb_audio.headers[i]);
            if (result != MMSYSERR_NOERROR) {
                InterlockedExchange(&bb_audio.worker_error, (LONG)result);
                InterlockedExchange(&bb_audio.faulted, 1);
                return 0;
            }
        }
    }
    return 0;
}

static void bb_audio_release_resources_locked(void)
{
    int i;
    for (i = 0; i < BB_AUDIO_MAX_VOICES; ++i) bb_audio.voices[i].active = false;
    for (i = 0; i < BB_AUDIO_MAX_SOUNDS; ++i) {
        free(bb_audio.sounds[i].samples);
        bb_audio.sounds[i].samples = NULL;
        bb_audio.sounds[i].frame_count = 0;
        bb_audio.sounds[i].used = false;
        bb_audio.sounds[i].generation =
            bb_audio_next_generation(bb_audio.sounds[i].generation);
    }
    for (i = 0; i < BB_AUDIO_MAX_MUSIC; ++i) {
        free(bb_audio.music[i].samples);
        bb_audio.music[i].samples = NULL;
        bb_audio.music[i].frame_count = 0;
        bb_audio.music[i].cursor = 0;
        bb_audio.music[i].used = false;
        bb_audio.music[i].playing = false;
        bb_audio.music[i].looping = false;
        bb_audio.music[i].generation =
            bb_audio_next_generation(bb_audio.music[i].generation);
    }
}

static bool bb_audio_device_is_gone(MMRESULT result)
{
    return result == MMSYSERR_INVALHANDLE || result == MMSYSERR_NODRIVER;
}

/* An invalid handle or removed driver cannot be unprepared through waveOut.
 * Forget only that unusable device state; its buffers remain static storage. */
static void bb_audio_abandon_device(void)
{
    bb_audio.output = NULL;
    memset(bb_audio.header_prepared, 0, sizeof bb_audio.header_prepared);
}

/* Reset returns queued buffers before unpreparing them. If a bounded wait
 * cannot finish, retain the remaining device state for another cleanup try. */
static bool bb_audio_release_device(DWORD timeout_ms)
{
    int i;
    MMRESULT result;
    DWORD wait_result;
    DWORD started;
    DWORD elapsed;
    DWORD wait_ms;
    if (bb_audio.output == NULL) return true;
    started = GetTickCount();
    result = waveOutReset(bb_audio.output);
    if (result != MMSYSERR_NOERROR) {
        bb_audio_report_error("device reset", result);
        if (bb_audio_device_is_gone(result)) {
            bb_audio_abandon_device();
            return true;
        }
        return false;
    }
    for (i = 0; i < BB_AUDIO_BUFFER_COUNT; ++i) {
        if (!bb_audio.header_prepared[i]) continue;
        result = WAVERR_STILLPLAYING;
        while (result == WAVERR_STILLPLAYING) {
            result = waveOutUnprepareHeader(
                bb_audio.output, &bb_audio.headers[i], (UINT)sizeof bb_audio.headers[i]);
            if (result == WAVERR_STILLPLAYING) {
                elapsed = (DWORD)(GetTickCount() - started);
                if (elapsed >= timeout_ms) break;
                wait_ms = timeout_ms - elapsed;
                if (wait_ms > 10U) wait_ms = 10U;
                wait_result = bb_audio.completion_event != NULL ?
                    WaitForSingleObject(bb_audio.completion_event, wait_ms) : WAIT_TIMEOUT;
                if (wait_result == WAIT_OBJECT_0) ResetEvent(bb_audio.completion_event);
                else if (wait_result == WAIT_TIMEOUT && bb_audio.completion_event == NULL) Sleep(wait_ms);
                else if (wait_result != WAIT_TIMEOUT) break;
            }
        }
        if (result != MMSYSERR_NOERROR) {
            bb_audio_report_error("buffer release", result);
            if (bb_audio_device_is_gone(result)) {
                bb_audio_abandon_device();
                return true;
            }
            return false;
        }
        bb_audio.header_prepared[i] = false;
    }
    result = waveOutClose(bb_audio.output);
    if (result != MMSYSERR_NOERROR) {
        bb_audio_report_error("device close", result);
        if (bb_audio_device_is_gone(result)) {
            bb_audio_abandon_device();
            return true;
        }
        return false;
    }
    bb_audio.output = NULL;
    return true;
}

/* Join the refill worker before closing waveOut or its events: the worker may
 * still be reading both the headers and the completion event. */
static bool bb_audio_stop_output(DWORD timeout_ms)
{
    bool device_released;
    LONG worker_error;
    DWORD wait_result;
    bb_audio.ready = false;
    if (bb_audio.shutdown_event != NULL && !SetEvent(bb_audio.shutdown_event)) {
        fprintf(stderr, "Audio worker signal failed (%lu).\n", (unsigned long)GetLastError());
        InterlockedExchange(&bb_audio.faulted, 1);
        return false;
    }
    if (bb_audio.worker != NULL) {
        wait_result = WaitForSingleObject(bb_audio.worker, timeout_ms);
        if (wait_result != WAIT_OBJECT_0) {
            if (wait_result == WAIT_FAILED)
                fprintf(stderr, "Audio worker wait failed (%lu).\n", (unsigned long)GetLastError());
            InterlockedExchange(&bb_audio.faulted, 1);
            return false;
        }
        if (!CloseHandle(bb_audio.worker)) {
            fprintf(stderr, "Audio worker handle release failed (%lu).\n", (unsigned long)GetLastError());
            InterlockedExchange(&bb_audio.faulted, 1);
            return false;
        }
        bb_audio.worker = NULL;
    }
    worker_error = InterlockedExchange(&bb_audio.worker_error, 0);
    if (worker_error != 0)
        bb_audio_report_error("buffer refill", (MMRESULT)worker_error);
    device_released = bb_audio_release_device(timeout_ms);
    if (device_released && bb_audio.completion_event != NULL) {
        CloseHandle(bb_audio.completion_event);
        bb_audio.completion_event = NULL;
    }
    if (bb_audio.shutdown_event != NULL) {
        CloseHandle(bb_audio.shutdown_event);
        bb_audio.shutdown_event = NULL;
    }
    if (device_released) {
        memset(bb_audio.headers, 0, sizeof bb_audio.headers);
        memset(bb_audio.header_prepared, 0, sizeof bb_audio.header_prepared);
    }
    InterlockedExchange(&bb_audio.faulted, device_released ? 0 : 1);
    return device_released;
}

static bool bb_audio_stop_backend(void)
{
    bool device_released;
    device_released = bb_audio_stop_output(5000U);
    if (bb_audio.worker != NULL) return false;
    if (bb_audio.lock_initialized) {
        EnterCriticalSection(&bb_audio.lock);
        bb_audio_release_resources_locked();
        LeaveCriticalSection(&bb_audio.lock);
        DeleteCriticalSection(&bb_audio.lock);
        bb_audio.lock_initialized = false;
    }
    bb_audio.next_voice = 0;
    bb_audio.play_serial = 0;
    bb_audio.last_recovery_tick = 0;
    bb_audio.master_volume = 1.0f;
    InterlockedExchange(&bb_audio.faulted, device_released ? 0 : 1);
    return device_released;
}

static void bb_audio_report_error(const char *operation, MMRESULT result)
{
    char message[MAXERRORLENGTH];
    if (waveOutGetErrorTextA(result, message, (UINT)sizeof message) == MMSYSERR_NOERROR)
        fprintf(stderr, "Audio %s failed: %s (%u).\n", operation, message, (unsigned int)result);
    else fprintf(stderr, "Audio %s failed with error %u.\n", operation, (unsigned int)result);
}

static bool bb_audio_open_output(void)
{
    WAVEFORMATEX format;
    MMRESULT result;
    int i;
    bool failed;

    if (!bb_audio.lock_initialized || bb_audio.output != NULL || bb_audio.worker != NULL) return false;
    InterlockedExchange(&bb_audio.faulted, 0);
    InterlockedExchange(&bb_audio.worker_error, 0);
    bb_audio.completion_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    bb_audio.shutdown_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (bb_audio.completion_event == NULL || bb_audio.shutdown_event == NULL) {
        fprintf(stderr, "Audio event initialization failed (%lu).\n", (unsigned long)GetLastError());
        (void)bb_audio_stop_output(500U);
        InterlockedExchange(&bb_audio.faulted, 1);
        return false;
    }

    memset(&format, 0, sizeof format);
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = (WORD)BB_AUDIO_CHANNELS;
    format.nSamplesPerSec = BB_AUDIO_SAMPLE_RATE;
    format.wBitsPerSample = (WORD)BB_AUDIO_BITS;
    format.nBlockAlign = (WORD)(BB_AUDIO_CHANNELS * (BB_AUDIO_BITS / 8U));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    result = waveOutOpen(&bb_audio.output, WAVE_MAPPER, &format,
                         (DWORD_PTR)bb_audio.completion_event, 0, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
        bb_audio_report_error("device open", result);
        bb_audio.output = NULL;
        (void)bb_audio_stop_output(500U);
        InterlockedExchange(&bb_audio.faulted, 1);
        return false;
    }

    failed = false;
    for (i = 0; i < BB_AUDIO_BUFFER_COUNT; ++i) {
        memset(&bb_audio.headers[i], 0, sizeof bb_audio.headers[i]);
        memset(bb_audio.buffers[i], 0, sizeof bb_audio.buffers[i]);
        bb_audio.headers[i].lpData = (LPSTR)bb_audio.buffers[i];
        bb_audio.headers[i].dwBufferLength = (DWORD)sizeof bb_audio.buffers[i];
        result = waveOutPrepareHeader(
            bb_audio.output, &bb_audio.headers[i], (UINT)sizeof bb_audio.headers[i]);
        if (result != MMSYSERR_NOERROR) {
            bb_audio_report_error("buffer preparation", result);
            failed = true;
            break;
        }
        bb_audio.header_prepared[i] = true;
    }
    if (failed) {
        (void)bb_audio_stop_output(500U);
        InterlockedExchange(&bb_audio.faulted, 1);
        return false;
    }

    bb_audio.worker = CreateThread(NULL, 0, bb_audio_worker, NULL, 0, NULL);
    if (bb_audio.worker == NULL) {
        fprintf(stderr, "Audio worker initialization failed (%lu).\n", (unsigned long)GetLastError());
        (void)bb_audio_stop_output(500U);
        InterlockedExchange(&bb_audio.faulted, 1);
        return false;
    }
    (void)SetThreadPriority(bb_audio.worker, THREAD_PRIORITY_ABOVE_NORMAL);
    /* Prime the four prepared buffers with silence; completions then drive
     * refills without requiring a foreground update every frame. */
    ResetEvent(bb_audio.completion_event);
    for (i = 0; i < BB_AUDIO_BUFFER_COUNT; ++i) {
        result = waveOutWrite(
            bb_audio.output, &bb_audio.headers[i], (UINT)sizeof bb_audio.headers[i]);
        if (result != MMSYSERR_NOERROR) {
            bb_audio_report_error("buffer submission", result);
            failed = true;
            break;
        }
    }
    if (failed) {
        (void)bb_audio_stop_output(500U);
        InterlockedExchange(&bb_audio.faulted, 1);
        return false;
    }
    if (InterlockedCompareExchange(&bb_audio.faulted, 0, 0) != 0) {
        (void)bb_audio_stop_output(500U);
        InterlockedExchange(&bb_audio.faulted, 1);
        return false;
    }
    bb_audio.ready = true;
    return true;
}

/* Recovery runs on foreground API calls after a worker fault. Limit retries
 * to once per second so a disconnected device cannot stall every frame. */
static void bb_audio_recover_if_needed(void)
{
    DWORD now;
    if (!bb_audio.lock_initialized ||
        InterlockedCompareExchange(&bb_audio.faulted, 0, 0) == 0) return;
    now = GetTickCount();
    if (bb_audio.last_recovery_tick != 0 &&
        (DWORD)(now - bb_audio.last_recovery_tick) < 1000U) return;
    bb_audio.last_recovery_tick = now;
    if (!bb_audio_stop_output(250U)) return;
    if (bb_audio_open_output()) fprintf(stderr, "Audio output device recovered.\n");
}

void InitAudioDevice(void)
{
    if (bb_audio.ready && InterlockedCompareExchange(&bb_audio.faulted, 0, 0) == 0) return;
    if (bb_audio.lock_initialized || bb_audio.output != NULL || bb_audio.worker != NULL)
        if (!bb_audio_stop_backend()) return;
    bb_audio.master_volume = 1.0f;
    InitializeCriticalSection(&bb_audio.lock);
    bb_audio.lock_initialized = true;
    if (!bb_audio_open_output()) (void)bb_audio_stop_backend();
}

void CloseAudioDevice(void)
{
    if (!bb_audio.lock_initialized && bb_audio.output == NULL && bb_audio.worker == NULL) return;
    bb_audio_stop_backend();
}

bool IsAudioDeviceReady(void)
{
    bb_audio_recover_if_needed();
    return bb_audio.ready &&
           InterlockedCompareExchange(&bb_audio.faulted, 0, 0) == 0;
}

void SetMasterVolume(float volume)
{
    if (!bb_audio.lock_initialized) return;
    EnterCriticalSection(&bb_audio.lock);
    bb_audio.master_volume = bb_audio_clamp_volume(volume);
    LeaveCriticalSection(&bb_audio.lock);
}

/* Read WAV or Ogg input into one bounded allocation. Decoding has its own
 * PCM limit because a small Ogg file can expand substantially. */
static unsigned char *bb_audio_read_file(const char *path, size_t *size)
{
    FILE *file;
    long length;
    unsigned char *data;
    size_t received;
    int closed;
    *size = 0;
    if (path == NULL) return NULL;
    file = bb_platform_fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length <= 0 || length > BB_AUDIO_FILE_LIMIT || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (unsigned char *)malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    received = fread(data, 1, (size_t)length, file);
    closed = fclose(file);
    if (received != (size_t)length || closed != 0) {
        free(data);
        return NULL;
    }
    *size = (size_t)length;
    return data;
}

static char bb_audio_ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z') return (char)(value - 'A' + 'a');
    return value;
}

static bool bb_audio_has_extension(const char *path, const char *extension)
{
    size_t path_length;
    size_t extension_length;
    size_t i;
    if (path == NULL || extension == NULL) return false;
    path_length = strlen(path);
    extension_length = strlen(extension);
    if (extension_length > path_length) return false;
    path += path_length - extension_length;
    for (i = 0; i < extension_length; ++i)
        if (bb_audio_ascii_lower(path[i]) != bb_audio_ascii_lower(extension[i])) return false;
    return true;
}

Wave LoadWave(const char *fileName)
{
    Wave wave;
    BBAudioPcm pcm;
    unsigned char *data;
    size_t size;
    bool decoded;
    memset(&wave, 0, sizeof wave);
    data = bb_audio_read_file(fileName, &size);
    if (data == NULL) return wave;
    decoded = false;
    if (bb_audio_has_extension(fileName, ".wav")) decoded = bb_audio_decode_wav(data, size, &pcm);
    else if (bb_audio_has_extension(fileName, ".ogg")) decoded = bb_audio_decode_ogg(data, size, &pcm);
    free(data);
    if (!decoded) return wave;
    /* Transfer the decoder allocation to Wave; UnloadWave owns the free. */
    wave.data = pcm.samples;
    wave.frameCount = pcm.frame_count;
    wave.sampleRate = pcm.sample_rate;
    wave.sampleSize = 16;
    wave.channels = pcm.channels;
    return wave;
}

bool IsWaveValid(Wave wave)
{
    return wave.data != NULL && wave.frameCount > 0 && wave.sampleRate > 0 &&
           wave.sampleSize == 16 && (wave.channels == 1 || wave.channels == 2);
}

void UnloadWave(Wave wave)
{
    free(wave.data);
}

Sound LoadSound(const char *fileName)
{
    Sound sound;
    Wave wave;
    BBAudioPcm pcm;
    unsigned int i;
    memset(&sound, 0, sizeof sound);
    if (!IsAudioDeviceReady()) return sound;
    wave = LoadWave(fileName);
    if (!IsWaveValid(wave)) return sound;
    pcm.samples = (short *)wave.data;
    pcm.frame_count = wave.frameCount;
    pcm.sample_rate = wave.sampleRate;
    pcm.channels = wave.channels;
    if (!bb_audio_pcm_normalize(&pcm, BB_AUDIO_SAMPLE_RATE, BB_AUDIO_CHANNELS)) {
        bb_audio_pcm_free(&pcm);
        return sound;
    }

    /* The slot takes the normalized PCM allocation on success. On a full
     * resource table, bb_audio_pcm_free releases it after unlocking. */
    EnterCriticalSection(&bb_audio.lock);
    for (i = 0; i < BB_AUDIO_MAX_SOUNDS; ++i) if (!bb_audio.sounds[i].used) break;
    if (i < BB_AUDIO_MAX_SOUNDS) {
        bb_audio.sounds[i].generation =
            bb_audio_next_generation(bb_audio.sounds[i].generation);
        bb_audio.sounds[i].samples = pcm.samples;
        bb_audio.sounds[i].frame_count = pcm.frame_count;
        bb_audio.sounds[i].used = true;
        sound.id = i + 1;
        sound.generation = bb_audio.sounds[i].generation;
        sound.frameCount = pcm.frame_count;
        pcm.samples = NULL;
    }
    LeaveCriticalSection(&bb_audio.lock);
    bb_audio_pcm_free(&pcm);
    return sound;
}

bool IsSoundValid(Sound sound)
{
    bool valid;
    if (!bb_audio.lock_initialized) return false;
    EnterCriticalSection(&bb_audio.lock);
    valid = bb_audio_sound_valid_locked(sound, NULL);
    LeaveCriticalSection(&bb_audio.lock);
    return valid;
}

void UnloadSound(Sound sound)
{
    unsigned int sound_index;
    unsigned int i;
    short *samples;
    if (!bb_audio.lock_initialized) return;
    samples = NULL;
    EnterCriticalSection(&bb_audio.lock);
    if (bb_audio_sound_valid_locked(sound, &sound_index)) {
        for (i = 0; i < BB_AUDIO_MAX_VOICES; ++i)
            if (bb_audio.voices[i].active &&
                bb_audio.voices[i].sound_index == sound_index &&
                bb_audio.voices[i].generation == sound.generation)
                bb_audio.voices[i].active = false;
        samples = bb_audio.sounds[sound_index].samples;
        bb_audio.sounds[sound_index].samples = NULL;
        bb_audio.sounds[sound_index].frame_count = 0;
        bb_audio.sounds[sound_index].used = false;
        bb_audio.sounds[sound_index].generation =
            bb_audio_next_generation(bb_audio.sounds[sound_index].generation);
    }
    LeaveCriticalSection(&bb_audio.lock);
    free(samples);
}

void PlaySound(Sound sound)
{
    unsigned int sound_index;
    unsigned int i;
    unsigned int selected;
    unsigned int oldest_serial;
    if (!IsAudioDeviceReady()) return;
    EnterCriticalSection(&bb_audio.lock);
    if (!bb_audio_sound_valid_locked(sound, &sound_index)) {
        LeaveCriticalSection(&bb_audio.lock);
        return;
    }
    /* Replaying an already active sound restarts that voice. Otherwise prefer
     * a free voice and replace the oldest when the pool is full. */
    for (i = 0; i < BB_AUDIO_MAX_VOICES; ++i) {
        if (bb_audio.voices[i].active &&
            bb_audio.voices[i].sound_index == sound_index &&
            bb_audio.voices[i].generation == sound.generation) {
            bb_audio.voices[i].cursor = 0;
            bb_audio.voices[i].serial = ++bb_audio.play_serial;
            LeaveCriticalSection(&bb_audio.lock);
            return;
        }
    }
    selected = BB_AUDIO_MAX_VOICES;
    for (i = 0; i < BB_AUDIO_MAX_VOICES; ++i) {
        sound_index = (bb_audio.next_voice + i) % BB_AUDIO_MAX_VOICES;
        if (!bb_audio.voices[sound_index].active) {
            selected = sound_index;
            break;
        }
    }
    if (selected == BB_AUDIO_MAX_VOICES) {
        selected = 0;
        oldest_serial = bb_audio.voices[0].serial;
        for (i = 1; i < BB_AUDIO_MAX_VOICES; ++i) {
            if (bb_audio.voices[i].serial < oldest_serial) {
                oldest_serial = bb_audio.voices[i].serial;
                selected = i;
            }
        }
    }
    bb_audio.voices[selected].sound_index = sound.id - 1;
    bb_audio.voices[selected].generation = sound.generation;
    bb_audio.voices[selected].cursor = 0;
    bb_audio.voices[selected].serial = ++bb_audio.play_serial;
    bb_audio.voices[selected].active = true;
    bb_audio.next_voice = (selected + 1) % BB_AUDIO_MAX_VOICES;
    LeaveCriticalSection(&bb_audio.lock);
}

Music LoadMusicStreamFromMemory(const char *fileType, const unsigned char *data, int dataSize)
{
    Music music;
    BBAudioPcm pcm;
    unsigned int i;
    memset(&music, 0, sizeof music);
    /* Despite the raylib name, this backend decodes the whole Ogg in memory;
     * the mixer advances its cursor on the worker, bounded by the PCM limit. */
    if (!IsAudioDeviceReady() || fileType == NULL || data == NULL || dataSize <= 0 ||
        !bb_audio_has_extension(fileType, ".ogg") ||
        !bb_audio_decode_ogg(data, (size_t)dataSize, &pcm)) return music;
    if (!bb_audio_pcm_normalize(&pcm, BB_AUDIO_SAMPLE_RATE, BB_AUDIO_CHANNELS)) {
        bb_audio_pcm_free(&pcm);
        return music;
    }

    EnterCriticalSection(&bb_audio.lock);
    for (i = 0; i < BB_AUDIO_MAX_MUSIC; ++i) if (!bb_audio.music[i].used) break;
    if (i < BB_AUDIO_MAX_MUSIC) {
        bb_audio.music[i].generation =
            bb_audio_next_generation(bb_audio.music[i].generation);
        bb_audio.music[i].samples = pcm.samples;
        bb_audio.music[i].frame_count = pcm.frame_count;
        bb_audio.music[i].cursor = 0;
        bb_audio.music[i].volume = 1.0f;
        bb_audio.music[i].used = true;
        bb_audio.music[i].playing = false;
        bb_audio.music[i].looping = false;
        music.id = i + 1;
        music.generation = bb_audio.music[i].generation;
        music.looping = false;
        pcm.samples = NULL;
    }
    LeaveCriticalSection(&bb_audio.lock);
    bb_audio_pcm_free(&pcm);
    return music;
}

bool IsMusicValid(Music music)
{
    bool valid;
    if (!bb_audio.lock_initialized) return false;
    EnterCriticalSection(&bb_audio.lock);
    valid = bb_audio_music_valid_locked(music, NULL);
    LeaveCriticalSection(&bb_audio.lock);
    return valid;
}

void UnloadMusicStream(Music music)
{
    unsigned int music_index;
    short *samples;
    if (!bb_audio.lock_initialized) return;
    samples = NULL;
    EnterCriticalSection(&bb_audio.lock);
    if (bb_audio_music_valid_locked(music, &music_index)) {
        samples = bb_audio.music[music_index].samples;
        bb_audio.music[music_index].samples = NULL;
        bb_audio.music[music_index].frame_count = 0;
        bb_audio.music[music_index].cursor = 0;
        bb_audio.music[music_index].used = false;
        bb_audio.music[music_index].playing = false;
        bb_audio.music[music_index].looping = false;
        bb_audio.music[music_index].generation =
            bb_audio_next_generation(bb_audio.music[music_index].generation);
    }
    LeaveCriticalSection(&bb_audio.lock);
    free(samples);
}

void PlayMusicStream(Music music)
{
    unsigned int music_index;
    if (!IsAudioDeviceReady()) return;
    EnterCriticalSection(&bb_audio.lock);
    if (bb_audio_music_valid_locked(music, &music_index)) {
        bb_audio.music[music_index].cursor = 0;
        bb_audio.music[music_index].looping = music.looping;
        bb_audio.music[music_index].playing = true;
    }
    LeaveCriticalSection(&bb_audio.lock);
}

void UpdateMusicStream(Music music)
{
    unsigned int music_index;
    /* Mixing is independent of frame updates; this synchronizes the caller's
     * looping flag and gives a faulted output device a recovery opportunity. */
    bb_audio_recover_if_needed();
    if (!bb_audio.lock_initialized) return;
    EnterCriticalSection(&bb_audio.lock);
    if (bb_audio_music_valid_locked(music, &music_index))
        bb_audio.music[music_index].looping = music.looping;
    LeaveCriticalSection(&bb_audio.lock);
}

void SetMusicVolume(Music music, float volume)
{
    unsigned int music_index;
    if (!bb_audio.lock_initialized) return;
    EnterCriticalSection(&bb_audio.lock);
    if (bb_audio_music_valid_locked(music, &music_index))
        bb_audio.music[music_index].volume = bb_audio_clamp_volume(volume);
    LeaveCriticalSection(&bb_audio.lock);
}
