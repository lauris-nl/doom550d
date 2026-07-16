#include <audio.h>
#include <dryos.h>
#include <mem.h>
#include <module.h>

#include "deh_str.h"
#include "doom_audio_ml.h"
#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

#define AUDIO_RATE 48000
#define AUDIO_VOLUME 5
#define BUFFER_SAMPLES 4096
#define SFX_CHANNELS 16
#define MUS_CHANNELS 16
#define MUS_VOICES 32
#define MUS_TICK_RATE 140
#define MUSIC_PEAK 20000
#define OUTPUT_PEAK 30000

extern void StartASIFDMADAC(void *, int, void *, int, void (*)(void *), void *);
extern void SetNextASIFDACBuffer(void *, int);
extern void StopASIFDMADAC(void (*)(void *), int);
extern void SetSamplingRate(int, int);
extern void PowerAudioOutput(void);
extern void SetAudioVolumeOut(int);
extern int beep_playing;

typedef struct sfx_cache sfx_cache_t;
struct sfx_cache
{
    sfxinfo_t *owner;
    uint8_t *samples;
    unsigned int length;
    unsigned int rate;
    sfx_cache_t *next;
};

typedef struct
{
    volatile int active;
    sfx_cache_t *sound;
    unsigned int position;
    unsigned int step;
    int volume;
} sfx_channel_t;

typedef struct
{
    int active;
    int channel;
    int note;
    int amplitude;
    unsigned int phase;
    unsigned int period;
} mus_voice_t;

typedef struct
{
    const uint8_t *data;
    int cursor;
    int end;
    int samples_to_event;
    int tick_remainder;
    uint8_t velocity[MUS_CHANNELS];
    mus_voice_t voices[MUS_VOICES];
} mus_state_t;

typedef struct
{
    const uint8_t *data;
    int length;
} song_t;

static int16_t buffers[2][BUFFER_SAMPLES];
static sfx_channel_t sfx[SFX_CHANNELS];
static sfx_cache_t *cache_list;
static mus_state_t mus;
static song_t *song;
static volatile int audio_running;
static volatile int music_running;
static volatile int music_paused;
static int music_looping;
static int music_volume = 96;
static int next_buffer;
static int sfx_prefix;

/* Chocolate Doom binds these legacy SDL settings when sound is enabled. */
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int clamp(int value, int peak)
{
    if (value > peak) return peak;
    if (value < -peak) return -peak;
    return value;
}

static int mus_header(const uint8_t *data, int size, int *start, int *end)
{
    int score_length;
    int instruments;

    if (!data || size < 16 || data[0] != 'M' || data[1] != 'U'
        || data[2] != 'S' || data[3] != 0x1a)
        return 0;
    score_length = le16(data + 4);
    *start = le16(data + 6);
    instruments = le16(data + 12);
    if (*start < 16 + instruments * 2 || *start > size
        || score_length > size - *start)
        return 0;
    *end = *start + score_length;
    return 1;
}

static unsigned int note_period(int note)
{
    static const unsigned int base[12] =
    {
        8176, 8662, 9177, 9723, 10300, 10913,
        11562, 12250, 12978, 13750, 14568, 15434
    };
    unsigned int frequency = base[note % 12] << (note / 12);
    unsigned int period = 48000000U / frequency;
    return period < 2 ? 2 : period;
}

static void voices_stop(void)
{
    int i;
    for (i = 0; i < MUS_VOICES; i++) mus.voices[i].active = 0;
}

static void note_stop(int channel, int note)
{
    int i;
    for (i = 0; i < MUS_VOICES; i++)
        if (mus.voices[i].active && mus.voices[i].channel == channel
            && mus.voices[i].note == note)
            mus.voices[i].active = 0;
}

static void note_start(int channel, int note, int velocity)
{
    mus_voice_t *voice = NULL;
    int i;

    note_stop(channel, note);
    if (!velocity) return;
    for (i = 0; i < MUS_VOICES; i++)
        if (!mus.voices[i].active) { voice = &mus.voices[i]; break; }
    if (!voice) voice = &mus.voices[0];
    voice->active = 1;
    voice->channel = channel;
    voice->note = note;
    voice->amplitude = velocity * 32;
    voice->phase = 0;
    voice->period = note_period(note);
}

static int mus_reset(const uint8_t *data, int size)
{
    int start;
    int end;
    int i;

    if (!mus_header(data, size, &start, &end)) return 0;
    memset(&mus, 0, sizeof(mus));
    mus.data = data;
    mus.cursor = start;
    mus.end = end;
    for (i = 0; i < MUS_CHANNELS; i++) mus.velocity[i] = 127;
    return 1;
}

static int mus_byte(uint8_t *value)
{
    if (mus.cursor >= mus.end) return 0;
    *value = mus.data[mus.cursor++];
    return 1;
}

static int mus_delay(void)
{
    unsigned int ticks = 0;
    uint8_t value;
    do
    {
        if (!mus_byte(&value) || ticks > 0x00ffffffU) return 0;
        ticks = ticks * 128 + (value & 0x7f);
    }
    while (value & 0x80);
    if (ticks > 40000U) return 0;
    mus.tick_remainder += ticks * AUDIO_RATE;
    mus.samples_to_event = mus.tick_remainder / MUS_TICK_RATE;
    mus.tick_remainder %= MUS_TICK_RATE;
    return 1;
}

static int mus_group(void)
{
    uint8_t descriptor;
    do
    {
        uint8_t value;
        int channel;
        int event;
        if (!mus_byte(&descriptor)) return 0;
        channel = descriptor & 0x0f;
        event = descriptor & 0x70;
        switch (event)
        {
            case 0x00:
                if (!mus_byte(&value)) return 0;
                note_stop(channel, value & 0x7f);
                break;
            case 0x10:
            {
                int note;
                if (!mus_byte(&value)) return 0;
                note = value & 0x7f;
                if (value & 0x80)
                {
                    if (!mus_byte(&value)) return 0;
                    mus.velocity[channel] = value & 0x7f;
                }
                note_start(channel, note, mus.velocity[channel]);
                break;
            }
            case 0x20:
            case 0x30:
                if (!mus_byte(&value)) return 0;
                break;
            case 0x40:
                if (!mus_byte(&value) || !mus_byte(&value)) return 0;
                break;
            case 0x60:
                voices_stop();
                if (music_looping && song) return mus_reset(song->data, song->length);
                music_running = 0;
                return 1;
            default:
                return 0;
        }
    }
    while (!(descriptor & 0x80));
    return mus_delay();
}

static int music_sample(void)
{
    int mixed = 0;
    int i;

    if (!music_running || music_paused) return 0;
    while (music_running && mus.samples_to_event == 0)
        if (!mus_group()) { music_running = 0; voices_stop(); return 0; }
    for (i = 0; i < MUS_VOICES; i++)
    {
        mus_voice_t *voice = &mus.voices[i];
        if (!voice->active) continue;
        mixed += voice->phase < voice->period / 2
            ? voice->amplitude : -voice->amplitude;
        if (++voice->phase >= voice->period) voice->phase = 0;
    }
    if (mus.samples_to_event > 0) mus.samples_to_event--;
    return clamp(mixed, MUSIC_PEAK) * music_volume / 127;
}

static int sfx_sample(void)
{
    int mixed = 0;
    int i;
    for (i = 0; i < SFX_CHANNELS; i++)
    {
        sfx_channel_t *channel = &sfx[i];
        unsigned int index;
        int sample;
        if (!channel->active || !channel->sound) continue;
        index = channel->position >> 16;
        if (index >= channel->sound->length) { channel->active = 0; continue; }
        sample = ((int)channel->sound->samples[index] - 128) << 8;
        mixed += sample * channel->volume / 127;
        channel->position += channel->step;
        if ((channel->position >> 16) >= channel->sound->length)
            channel->active = 0;
    }
    return mixed;
}

static void render(int16_t *buffer)
{
    int i;
    for (i = 0; i < BUFFER_SAMPLES; i++)
        buffer[i] = clamp(music_sample() + sfx_sample(), OUTPUT_PEAK);
}

static void stopped(void *ctx)
{
    (void)ctx;
    audio_running = 0;
    beep_playing = 0;
    audio_configure(1);
}

static void next(void *ctx)
{
    (void)ctx;
    if (!audio_running) return;
    render(buffers[next_buffer]);
    SetNextASIFDACBuffer(buffers[next_buffer], sizeof(buffers[next_buffer]));
    next_buffer = !next_buffer;
}

static int audio_start(void)
{
    if (audio_running) return 1;
    if (beep_playing)
    {
        printf("doom550d: Canon audio output is already in use\n");
        return 0;
    }
    render(buffers[0]);
    render(buffers[1]);
    next_buffer = 0;
    audio_running = 1;
    beep_playing = 1;
    SetSamplingRate(AUDIO_RATE, 1);
    MEM(0xC0920210) = 4;
    PowerAudioOutput();
    audio_configure(1);
    SetAudioVolumeOut(AUDIO_VOLUME);
    StartASIFDMADAC(buffers[0], sizeof(buffers[0]), buffers[1],
                    sizeof(buffers[1]), next, NULL);
    return 1;
}

void doom_audio_ml_force_shutdown(void)
{
    int i;
    music_running = 0;
    voices_stop();
    for (i = 0; i < SFX_CHANNELS; i++) sfx[i].active = 0;
    if (audio_running)
    {
        StopASIFDMADAC(stopped, 0);
        while (audio_running) msleep(20);
    }
}

static void lump_name(sfxinfo_t *info, char *name)
{
    sfxinfo_t *source = info->link ? info->link : info;
    const char *base = DEH_String(source->name);
    int offset = 0;
    int i;
    if (sfx_prefix) { name[offset++] = 'd'; name[offset++] = 's'; }
    for (i = 0; i < 8 - offset && base[i]; i++) name[offset + i] = base[i];
    name[offset + i] = '\0';
}

static int get_lump(sfxinfo_t *info)
{
    char name[9];
    lump_name(info, name);
    return W_GetNumForName(name);
}

static sfx_cache_t *cache_sound(sfxinfo_t *requested)
{
    sfxinfo_t *owner = requested->link ? requested->link : requested;
    sfx_cache_t *cached = owner->driver_data;
    const uint8_t *lump;
    unsigned int lump_size;
    unsigned int length;
    unsigned int rate;
    int lumpnum;

    if (cached) return cached;
    lumpnum = requested->lumpnum >= 0 ? requested->lumpnum : get_lump(requested);
    lump = W_CacheLumpNum(lumpnum, PU_STATIC);
    lump_size = W_LumpLength(lumpnum);
    if (!lump || lump_size < 8 || lump[0] != 3 || lump[1] != 0) goto done;
    rate = le16(lump + 2);
    length = le32(lump + 4);
    if (!rate || length > lump_size - 8 || length <= 48) goto done;
    cached = malloc(sizeof(*cached));
    if (!cached) goto done;
    memset(cached, 0, sizeof(*cached));
    length -= 32;
    cached->samples = malloc(length);
    if (!cached->samples) { free(cached); cached = NULL; goto done; }
    memcpy(cached->samples, lump + 24, length);
    cached->owner = owner;
    cached->length = length;
    cached->rate = rate;
    cached->next = cache_list;
    cache_list = cached;
    owner->driver_data = cached;
done:
    W_ReleaseLumpNum(lumpnum);
    return cached;
}

static boolean sound_init(boolean prefix)
{
    sfx_prefix = prefix;
    memset(sfx, 0, sizeof(sfx));
    return audio_start() ? true : false;
}

static void sound_shutdown(void)
{
    sfx_cache_t *cached;
    doom_audio_ml_force_shutdown();
    cached = cache_list;
    while (cached)
    {
        sfx_cache_t *next_cache = cached->next;
        cached->owner->driver_data = NULL;
        free(cached->samples);
        free(cached);
        cached = next_cache;
    }
    cache_list = NULL;
}

static void sound_update(void) {}
static void sound_params(int channel, int volume, int separation)
{
    (void)separation;
    if (channel >= 0 && channel < SFX_CHANNELS) sfx[channel].volume = volume;
}

static int sound_start(sfxinfo_t *info, int channel, int volume, int separation)
{
    sfx_cache_t *cached;
    (void)separation;
    if (channel < 0 || channel >= SFX_CHANNELS) return -1;
    cached = cache_sound(info);
    if (!cached) return -1;
    sfx[channel].active = 0;
    sfx[channel].sound = cached;
    sfx[channel].position = 0;
    sfx[channel].step = (cached->rate << 16) / AUDIO_RATE;
    if (!sfx[channel].step) sfx[channel].step = 1;
    sfx[channel].volume = volume;
    sfx[channel].active = 1;
    return channel;
}

static void sound_stop(int channel)
{
    if (channel >= 0 && channel < SFX_CHANNELS) sfx[channel].active = 0;
}

static boolean sound_playing(int channel)
{
    return channel >= 0 && channel < SFX_CHANNELS && sfx[channel].active;
}

static void sound_precache(sfxinfo_t *sounds, int count)
{
    (void)sounds;
    (void)count;
}

static boolean music_init(void) { return audio_start() ? true : false; }
static void music_shutdown(void) { doom_audio_ml_force_shutdown(); }
static void music_set_volume(int volume) { music_volume = volume; }
static void music_pause(void) { music_paused = 1; }
static void music_resume(void) { music_paused = 0; }

static void *music_register(void *data, int length)
{
    song_t *registered;
    int start;
    int end;
    if (!mus_header(data, length, &start, &end)) return NULL;
    registered = malloc(sizeof(*registered));
    if (!registered) return NULL;
    registered->data = data;
    registered->length = length;
    return registered;
}

static void music_unregister(void *handle)
{
    song_t *registered = handle;
    if (!registered) return;
    if (song == registered) { music_running = 0; song = NULL; voices_stop(); }
    free(registered);
}

static void music_play(void *handle, boolean looping)
{
    song_t *registered = handle;
    music_running = 0;
    voices_stop();
    song = registered;
    music_looping = looping;
    music_paused = 0;
    if (registered && mus_reset(registered->data, registered->length))
        music_running = 1;
}

static void music_stop(void) { music_running = 0; song = NULL; voices_stop(); }
static boolean music_playing(void) { return music_running ? true : false; }
static void music_poll(void) {}

static snddevice_t devices[] = { SNDDEVICE_SB, SNDDEVICE_GENMIDI };

sound_module_t DG_sound_module =
{
    devices, arrlen(devices), sound_init, sound_shutdown, get_lump,
    sound_update, sound_params, sound_start, sound_stop, sound_playing,
    sound_precache,
};

music_module_t DG_music_module =
{
    devices, arrlen(devices), music_init, music_shutdown, music_set_volume,
    music_pause, music_resume, music_register, music_unregister, music_play,
    music_stop, music_playing, music_poll,
};
