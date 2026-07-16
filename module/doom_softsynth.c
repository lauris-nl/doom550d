/* Low-cost, fuller MUS synthesizer for the Canon 550D. */

#include <stdio.h>
#include <string.h>

#include "doom_softsynth.h"

#define SYNTH_RATE 24000
#define SYNTH_VOICES 24
#define SYNTH_CHANNELS 16
#define ENV_MAX 32767

enum { WAVE_SQUARE, WAVE_PULSE, WAVE_TRIANGLE, WAVE_SAW, WAVE_NOISE };
enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct
{
    uint8_t waveform;
    uint8_t attack_ms;
    uint16_t decay_ms;
    uint8_t sustain;
    uint16_t release_ms;
    uint8_t gain;
}
synth_preset_t;

typedef struct
{
    int program;
    int volume;
    int bend;
}
synth_channel_t;

typedef struct
{
    int active;
    int releasing;
    int channel;
    int key;
    int note;
    int synth_note;
    int velocity;
    int waveform;
    int env_state;
    int env;
    int sustain;
    int attack_step;
    int decay_step;
    int release_ms;
    int level;
    int gain;
    uint32_t phase;
    uint32_t step;
    uint32_t noise;
    unsigned int age;
}
synth_voice_t;

/* One cheap character per General MIDI family (program / 8). */
static const synth_preset_t presets[16] =
{
    { WAVE_TRIANGLE,  3, 260, 176, 180, 236 }, /* piano */
    { WAVE_SQUARE,    2, 180, 152, 130, 210 }, /* chromatic percussion */
    { WAVE_PULSE,     5, 220, 180, 180, 210 }, /* organ */
    { WAVE_TRIANGLE,  8, 300, 188, 260, 230 }, /* guitar */
    { WAVE_SQUARE,    3, 180, 192, 160, 224 }, /* bass */
    { WAVE_SAW,      28, 360, 208, 420, 170 }, /* strings */
    { WAVE_SAW,      16, 280, 192, 300, 172 }, /* ensemble */
    { WAVE_SQUARE,    4, 220, 176, 190, 195 }, /* brass */
    { WAVE_PULSE,     5, 240, 176, 220, 200 }, /* reed */
    { WAVE_TRIANGLE, 10, 280, 184, 300, 220 }, /* pipe */
    { WAVE_SAW,       2, 180, 192, 180, 175 }, /* synth lead */
    { WAVE_PULSE,    22, 420, 200, 460, 190 }, /* synth pad */
    { WAVE_SQUARE,    5, 260, 168, 260, 185 }, /* synth effects */
    { WAVE_TRIANGLE,  3, 200, 184, 180, 215 }, /* ethnic */
    { WAVE_PULSE,     2, 160, 160, 140, 205 }, /* percussive */
    { WAVE_SAW,      10, 300, 176, 300, 180 }  /* sound effects */
};

/* Q0.32 phase increments for MIDI notes 0..11 at 24 kHz. */
static const uint32_t base_step[12] =
{
    1463116, 1550118, 1642292, 1739948, 1843411, 1953026,
    2069159, 2192197, 2322552, 2460658, 2606977, 2761996
};

static synth_channel_t channels[SYNTH_CHANNELS];
static synth_voice_t voices[SYNTH_VOICES];
static unsigned int voice_age;
static int initialized;
static int16_t last_sample;
static int duplicate_pending;

static int clamp_127(int value)
{
    if (value < 0) return 0;
    if (value > 127) return 127;
    return value;
}

static int envelope_step(int distance, int milliseconds)
{
    int samples;
    if (milliseconds <= 0) return distance;
    samples = milliseconds * (SYNTH_RATE / 1000);
    if (samples <= 0) return distance;
    distance = (distance + samples - 1) / samples;
    return distance > 0 ? distance : 1;
}

static uint32_t note_step(int note, int bend)
{
    uint32_t step;
    int adjustment;

    if (note < 0) note = 0;
    if (note > 127) note = 127;
    step = base_step[note % 12] << (note / 12);

    /* Approximate MUS's +/- two-semitone bend outside the sample loop. */
    adjustment = (int)(step >> 8) * bend / 2;
    if (adjustment < 0 && (uint32_t)(-adjustment) >= step) return 1;
    return step + adjustment;
}

static void update_level(synth_voice_t *voice)
{
    int level = voice->velocity * channels[voice->channel].volume / 127;
    voice->level = level * voice->gain / 255;
}

static void release_voice(synth_voice_t *voice)
{
    if (!voice->active || voice->releasing) return;
    voice->releasing = 1;
    voice->env_state = ENV_RELEASE;
    voice->decay_step = envelope_step(voice->env, voice->release_ms);
}

static synth_voice_t *allocate_voice(void)
{
    synth_voice_t *oldest = NULL;
    int i;

    for (i = 0; i < SYNTH_VOICES; i++)
        if (!voices[i].active) return &voices[i];
    for (i = 0; i < SYNTH_VOICES; i++)
        if (voices[i].releasing
            && (!oldest || voices[i].age < oldest->age))
            oldest = &voices[i];
    if (!oldest)
        for (i = 0; i < SYNTH_VOICES; i++)
            if (!oldest || voices[i].age < oldest->age)
                oldest = &voices[i];
    return oldest;
}

static int percussion_waveform(int note)
{
    if (note == 35 || note == 36 || (note >= 41 && note <= 47))
        return WAVE_TRIANGLE;
    if (note == 37 || note == 39 || note == 54 || note == 56)
        return WAVE_PULSE;
    return WAVE_NOISE;
}

void doom_softsynth_note_off(int channel, int note)
{
    int i;
    if (!initialized || channel < 0 || channel >= SYNTH_CHANNELS) return;
    for (i = 0; i < SYNTH_VOICES; i++)
        if (voices[i].active && voices[i].channel == channel
            && voices[i].key == note)
            release_voice(&voices[i]);
}

void doom_softsynth_note_on(int channel, int note, int velocity)
{
    synth_voice_t *voice;
    const synth_preset_t *preset;
    int synth_note = note;

    if (!initialized || channel < 0 || channel >= SYNTH_CHANNELS
        || note < 0 || note > 127)
        return;
    doom_softsynth_note_off(channel, note);
    velocity = clamp_127(velocity);
    if (!velocity) return;

    preset = &presets[channels[channel].program >> 3];
    voice = allocate_voice();
    memset(voice, 0, sizeof(*voice));
    voice->active = 1;
    voice->channel = channel;
    voice->key = note;
    voice->note = note;
    voice->velocity = velocity;
    voice->waveform = preset->waveform;
    voice->gain = preset->gain;
    voice->release_ms = preset->release_ms;
    voice->sustain = preset->sustain * ENV_MAX / 255;
    voice->attack_step = envelope_step(ENV_MAX, preset->attack_ms);
    voice->decay_step = envelope_step(ENV_MAX - voice->sustain,
                                      preset->decay_ms);
    voice->env_state = ENV_ATTACK;
    voice->age = ++voice_age;
    voice->noise = 0x9e3779b9U ^ voice->age ^ ((unsigned int)note << 16);

    if (channel == 15)
    {
        voice->waveform = percussion_waveform(note);
        voice->gain = 240;
        voice->release_ms = note == 35 || note == 36 ? 180 : 70;
        voice->sustain = 0;
        voice->decay_step = envelope_step(ENV_MAX, voice->release_ms);
        synth_note = note == 35 || note == 36 ? 35 : note;
    }
    voice->synth_note = synth_note;
    voice->step = note_step(synth_note, channels[channel].bend);
    update_level(voice);
}

void doom_softsynth_all_notes_off(void)
{
    int i;
    for (i = 0; i < SYNTH_VOICES; i++) voices[i].active = 0;
}

void doom_softsynth_reset_song(void)
{
    int i;
    if (!initialized) return;
    memset(voices, 0, sizeof(voices));
    voice_age = 0;
    last_sample = 0;
    duplicate_pending = 0;
    for (i = 0; i < SYNTH_CHANNELS; i++)
    {
        channels[i].program = 0;
        channels[i].volume = 100;
        channels[i].bend = 0;
    }
}

int doom_softsynth_init(unsigned int sample_rate)
{
    (void)sample_rate;
    initialized = 1;
    doom_softsynth_reset_song();
    printf("doom550d: light softsynth ready (24 voices, 24 kHz)\n");
    return 1;
}

void doom_softsynth_shutdown(void)
{
    if (!initialized) return;
    doom_softsynth_all_notes_off();
    initialized = 0;
}

void doom_softsynth_pitch(int channel, int value)
{
    int i;
    if (!initialized || channel < 0 || channel >= SYNTH_CHANNELS) return;
    channels[channel].bend = clamp_127(value >> 1) - 64;
    for (i = 0; i < SYNTH_VOICES; i++)
        if (voices[i].active && voices[i].channel == channel)
            voices[i].step = note_step(voices[i].synth_note,
                                       channels[channel].bend);
}

void doom_softsynth_controller(int channel, int controller, int value)
{
    int i;
    if (!initialized || channel < 0 || channel >= SYNTH_CHANNELS) return;
    value = clamp_127(value);
    switch (controller)
    {
        case 0:
            channels[channel].program = value;
            break;
        case 3:
            channels[channel].volume = value;
            for (i = 0; i < SYNTH_VOICES; i++)
                if (voices[i].active && voices[i].channel == channel)
                    update_level(&voices[i]);
            break;
        case 4:
        default:
            break;
    }
}

void doom_softsynth_system_event(int channel, int controller)
{
    int i;
    if (!initialized || channel < 0 || channel >= SYNTH_CHANNELS) return;
    switch (controller)
    {
        case 10:
        case 11:
            for (i = 0; i < SYNTH_VOICES; i++)
                if (voices[i].active && voices[i].channel == channel)
                    release_voice(&voices[i]);
            break;
        case 14:
            channels[channel].volume = 100;
            channels[channel].bend = 0;
            for (i = 0; i < SYNTH_VOICES; i++)
                if (voices[i].active && voices[i].channel == channel)
                {
                    update_level(&voices[i]);
                    voices[i].step = note_step(voices[i].synth_note, 0);
                }
            break;
        default:
            break;
    }
}

static int voice_sample(synth_voice_t *voice)
{
    unsigned int position;
    int wave;
    int sample;

    if (voice->env_state == ENV_ATTACK)
    {
        voice->env += voice->attack_step;
        if (voice->env >= ENV_MAX)
        {
            voice->env = ENV_MAX;
            voice->env_state = ENV_DECAY;
        }
    }
    else if (voice->env_state == ENV_DECAY)
    {
        voice->env -= voice->decay_step;
        if (voice->env <= voice->sustain)
        {
            voice->env = voice->sustain;
            voice->env_state = voice->sustain ? ENV_SUSTAIN : ENV_RELEASE;
        }
    }
    else if (voice->env_state == ENV_RELEASE)
    {
        voice->env -= voice->decay_step;
        if (voice->env <= 0)
        {
            voice->active = 0;
            return 0;
        }
    }

    position = voice->phase >> 24;
    switch (voice->waveform)
    {
        case WAVE_PULSE:
            wave = position < 64 ? 127 : -127;
            break;
        case WAVE_TRIANGLE:
            wave = position < 128 ? (int)position * 2 - 127
                                  : 383 - (int)position * 2;
            break;
        case WAVE_SAW:
            wave = (int)position - 128;
            break;
        case WAVE_NOISE:
            voice->noise ^= voice->noise << 13;
            voice->noise ^= voice->noise >> 17;
            voice->noise ^= voice->noise << 5;
            wave = (int)((voice->noise >> 24) & 0xff) - 128;
            break;
        case WAVE_SQUARE:
        default:
            wave = position < 128 ? 127 : -127;
            break;
    }
    voice->phase += voice->step;
    sample = (wave * voice->level) >> 3;
    return (sample * voice->env) >> 15;
}

static int16_t synth_sample(void)
{
    int mixed = 0;
    int i;
    for (i = 0; i < SYNTH_VOICES; i++)
        if (voices[i].active) mixed += voice_sample(&voices[i]);

    /* Preserve headroom for the upper half of Doom's music slider. */
    if (mixed > 16000) mixed = 16000;
    if (mixed < -16000) mixed = -16000;
    return (int16_t)mixed;
}

void doom_softsynth_render_48k(int16_t *buffer, unsigned int samples)
{
    if (!buffer) return;
    if (!initialized)
    {
        memset(buffer, 0, samples * sizeof(*buffer));
        return;
    }

    if (duplicate_pending && samples)
    {
        *buffer++ = last_sample;
        samples--;
        duplicate_pending = 0;
    }
    while (samples)
    {
        last_sample = synth_sample();
        *buffer++ = last_sample;
        samples--;
        if (samples)
        {
            *buffer++ = last_sample;
            samples--;
        }
        else
        {
            duplicate_pending = 1;
        }
    }
}
