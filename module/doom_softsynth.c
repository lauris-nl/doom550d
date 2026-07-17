/* Low-cost, fuller MUS synthesizer for the Canon 550D. */

#include <stdio.h>
#include <string.h>

#include "doom_softsynth.h"

#define SYNTH_RATE 24000
#define SYNTH_VOICES 24
#define SYNTH_CHANNELS 16
#define ENV_MAX 32767
#define WAVE_TABLE_SIZE 256
#define WAVE_TABLE_LEVELS 6
#define WAVE_TABLE_AMPLITUDE 1024
#define MODULATION_LFO_INTERVAL 64

enum { WAVE_SQUARE, WAVE_PULSE, WAVE_TRIANGLE, WAVE_SAW, WAVE_NOISE };
enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct
{
    uint8_t waveform;
    uint8_t accent_waveform;
    uint8_t vibrato_depth;
    uint8_t attack_ms;
    uint16_t decay_ms;
    uint8_t sustain;
    uint16_t release_ms;
    uint8_t gain;
    uint16_t accent_ms;
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
    int table_level;
    int accent_waveform;
    int accent;
    int accent_step;
    int vibrato_depth;
    int noise_mix;
    uint32_t phase;
    uint32_t step;
    uint32_t current_step;
    uint32_t pitch_target;
    uint32_t pitch_decay;
    uint32_t noise;
    unsigned int age;
}
synth_voice_t;

/* One enhanced retro character per General MIDI family (program / 8). */
static const synth_preset_t presets[16] =
{
    /* main, accent, vibrato, ADSR, gain, accent length */
    { WAVE_TRIANGLE, WAVE_SAW,    0,
        3, 260, 176, 180, 236,  70 }, /* electric piano */
    { WAVE_SQUARE,   WAVE_SAW,    0,
        2, 180, 152, 130, 210,  45 }, /* chromatic percussion */
    { WAVE_PULSE,    WAVE_SQUARE, 5,
        5, 220, 180, 180, 210,  50 }, /* transistor organ */
    { WAVE_TRIANGLE, WAVE_SAW,    0,
        8, 300, 188, 260, 230,  90 }, /* plucked synth */
    { WAVE_SQUARE,   WAVE_SAW,    0,
        3, 180, 192, 160, 224,  55 }, /* bass */
    { WAVE_SAW,      WAVE_SQUARE, 3,
       28, 360, 208, 420, 170, 130 }, /* strings */
    { WAVE_SAW,      WAVE_PULSE,  4,
       16, 280, 192, 300, 172, 100 }, /* ensemble */
    { WAVE_SQUARE,   WAVE_SAW,    2,
        4, 220, 176, 190, 195,  90 }, /* brass */
    { WAVE_PULSE,    WAVE_SAW,    5,
        5, 240, 176, 220, 200,  75 }, /* reed */
    { WAVE_TRIANGLE, WAVE_PULSE,  6,
       10, 280, 184, 300, 220,  65 }, /* pipe */
    { WAVE_SAW,      WAVE_SQUARE, 5,
        2, 180, 192, 180, 175,  60 }, /* synth lead */
    { WAVE_PULSE,    WAVE_SAW,    4,
       22, 420, 200, 460, 190, 140 }, /* synth pad */
    { WAVE_SQUARE,   WAVE_SAW,    7,
        5, 260, 168, 260, 185,  80 }, /* synth effects */
    { WAVE_TRIANGLE, WAVE_PULSE,  3,
        3, 200, 184, 180, 215,  65 }, /* ethnic */
    { WAVE_PULSE,    WAVE_SAW,    0,
        2, 160, 160, 140, 205,  45 }, /* percussive */
    { WAVE_SAW,      WAVE_SQUARE, 6,
       10, 300, 176, 300, 180, 100 }  /* sound effects */
};

/* Q0.32 phase increments for MIDI notes 0..11 at 24 kHz. */
static const uint32_t base_step[12] =
{
    1463116, 1550118, 1642292, 1739948, 1843411, 1953026,
    2069159, 2192197, 2322552, 2460658, 2606977, 2761996
};

static synth_channel_t channels[SYNTH_CHANNELS];
static synth_voice_t voices[SYNTH_VOICES];
static int16_t wave_tables[4][WAVE_TABLE_LEVELS][WAVE_TABLE_SIZE];
static unsigned int voice_age;
static int initialized;
static int wave_tables_ready;
static int16_t interpolation_sample;
static int interpolation_phase;
static int16_t ensemble_delay[256];
static unsigned int ensemble_position;
static unsigned int modulation_lfo_phase;
static unsigned int modulation_lfo_countdown;

/* One quarter of a sine wave, scaled to 32767. */
static const int16_t quarter_sine[65] =
{
        0,   804,  1608,  2410,  3212,  4011,  4808,  5602,
     6393,  7179,  7962,  8739,  9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767
};

static int sine_sample(unsigned int phase)
{
    unsigned int position = phase & 255;
    if (position <= 64) return quarter_sine[position];
    if (position <= 128) return quarter_sine[128 - position];
    if (position <= 192) return -quarter_sine[position - 128];
    return -quarter_sine[256 - position];
}

static void normalize_table(int waveform, int level, const int32_t *source)
{
    int maximum = 1;
    int absolute;
    int i;

    for (i = 0; i < WAVE_TABLE_SIZE; i++)
    {
        absolute = source[i] < 0 ? -source[i] : source[i];
        if (absolute > maximum) maximum = absolute;
    }
    for (i = 0; i < WAVE_TABLE_SIZE; i++)
        wave_tables[waveform][level][i] =
            (int16_t)(source[i] * WAVE_TABLE_AMPLITUDE / maximum);
}

static void build_wave_tables(void)
{
    static const int harmonic_limit[WAVE_TABLE_LEVELS] =
        { 32, 16, 8, 4, 2, 1 };
    int32_t source[WAVE_TABLE_SIZE];
    int harmonics;
    int harmonic;
    int level;
    int position;
    int sign;

    if (wave_tables_ready) return;
    for (level = 0; level < WAVE_TABLE_LEVELS; level++)
    {
        harmonics = harmonic_limit[level];

        memset(source, 0, sizeof(source));
        for (position = 0; position < WAVE_TABLE_SIZE; position++)
            for (harmonic = 1; harmonic <= harmonics; harmonic++)
                source[position] += sine_sample(position * harmonic)
                                    / harmonic;
        normalize_table(WAVE_SAW, level, source);

        for (position = 0; position < WAVE_TABLE_SIZE; position++)
            source[position] =
                (int32_t)wave_tables[WAVE_SAW][level][position]
                - wave_tables[WAVE_SAW][level][(position + 192) & 255];
        normalize_table(WAVE_PULSE, level, source);

        memset(source, 0, sizeof(source));
        for (position = 0; position < WAVE_TABLE_SIZE; position++)
            for (harmonic = 1; harmonic <= harmonics; harmonic += 2)
                source[position] += sine_sample(position * harmonic)
                                    / harmonic;
        normalize_table(WAVE_SQUARE, level, source);

        memset(source, 0, sizeof(source));
        for (position = 0; position < WAVE_TABLE_SIZE; position++)
            for (harmonic = 1; harmonic <= harmonics; harmonic += 2)
            {
                sign = (harmonic & 2) ? -1 : 1;
                source[position] += sign * sine_sample(position * harmonic)
                                    / (harmonic * harmonic);
            }
        normalize_table(WAVE_TRIANGLE, level, source);
    }
    wave_tables_ready = 1;
}

static int wave_table_level(uint32_t step)
{
    if (step <= (1U << 26)) return 0;
    if (step <= (1U << 27)) return 1;
    if (step <= (1U << 28)) return 2;
    if (step <= (1U << 29)) return 3;
    if (step <= (1U << 30)) return 4;
    return 5;
}

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

static void set_voice_step(synth_voice_t *voice, uint32_t step)
{
    voice->step = step;
    voice->current_step = step;
    voice->table_level = wave_table_level(step);
}

static void update_modulation_lfo(void)
{
    unsigned int position = modulation_lfo_phase & 255;
    int triangle = position < 128 ? (int)position * 2 - 127
                                  : 383 - (int)position * 2;
    int adjustment;
    int i;

    modulation_lfo_phase += 3; /* About 4.4 Hz at 24 kHz. */
    for (i = 0; i < SYNTH_VOICES; i++)
        if (voices[i].active)
        {
            voices[i].current_step = voices[i].step;
            if (voices[i].vibrato_depth)
            {
                adjustment = (int)(voices[i].step >> 12)
                    * triangle * voices[i].vibrato_depth >> 7;
                if (adjustment < 0)
                    voices[i].current_step -= (uint32_t)(-adjustment);
                else
                    voices[i].current_step += (uint32_t)adjustment;
            }
        }
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
    if (note == 37 || note == 38 || note == 39 || note == 40)
        return WAVE_SQUARE;
    if (note == 42 || note == 44 || note == 46 || note >= 49)
        return WAVE_PULSE;
    return WAVE_NOISE;
}

static int percussion_noise_mix(int note)
{
    if (note == 37 || note == 38 || note == 39 || note == 40) return 40;
    if (note == 42 || note == 44 || note == 46) return 54;
    if (note >= 49) return 48;
    return 0;
}

static int percussion_release_ms(int note)
{
    if (note == 35 || note == 36) return 190;
    if (note == 42 || note == 44) return 55;
    if (note == 46) return 190;
    if (note >= 49) return 280;
    if (note >= 41 && note <= 47) return 150;
    return 105;
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
    voice->accent_waveform = preset->accent_waveform;
    voice->accent = preset->accent_ms ? ENV_MAX : 0;
    voice->accent_step = envelope_step(ENV_MAX, preset->accent_ms);
    voice->vibrato_depth = preset->vibrato_depth;
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
        voice->accent = 0;
        voice->vibrato_depth = 0;
        voice->noise_mix = percussion_noise_mix(note);
        voice->gain = 240;
        voice->release_ms = percussion_release_ms(note);
        voice->sustain = 0;
        voice->decay_step = envelope_step(ENV_MAX, voice->release_ms);
        synth_note = note == 35 || note == 36 ? 47 : note;
    }
    voice->synth_note = synth_note;
    set_voice_step(voice, note_step(synth_note, channels[channel].bend));
    if (channel == 15 && (note == 35 || note == 36))
    {
        voice->pitch_target = note_step(35, 0);
        voice->pitch_decay = (voice->step - voice->pitch_target)
                             / (SYNTH_RATE * 70 / 1000);
        if (!voice->pitch_decay) voice->pitch_decay = 1;
    }
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
    interpolation_sample = 0;
    interpolation_phase = 0;
    memset(ensemble_delay, 0, sizeof(ensemble_delay));
    ensemble_position = 0;
    modulation_lfo_phase = 0;
    modulation_lfo_countdown = 0;
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
    build_wave_tables();
    initialized = 1;
    doom_softsynth_reset_song();
    printf("doom550d: enhanced softsynth ready "
           "(24 voices, band-limited 24->48 kHz)\n");
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
            set_voice_step(&voices[i],
                           note_step(voices[i].synth_note,
                                     channels[channel].bend));
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
                    set_voice_step(&voices[i],
                                   note_step(voices[i].synth_note, 0));
                }
            break;
        default:
            break;
    }
}

static int voice_sample(synth_voice_t *voice)
{
    unsigned int position;
    int accent_mix;
    int noise_wave;
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
    if (voice->channel == 15)
    {
        if (voice->pitch_target && voice->step > voice->pitch_target)
        {
            if (voice->step - voice->pitch_target <= voice->pitch_decay)
                voice->step = voice->pitch_target;
            else
                voice->step -= voice->pitch_decay;
            voice->current_step = voice->step;
        }

        if (voice->waveform == WAVE_NOISE)
        {
            voice->noise ^= voice->noise << 13;
            voice->noise ^= voice->noise >> 17;
            voice->noise ^= voice->noise << 5;
            wave = ((int)((voice->noise >> 24) & 0xff) - 128) * 8;
        }
        else
        {
            wave = wave_tables[voice->waveform][voice->table_level][position];
            if (voice->noise_mix)
            {
                voice->noise ^= voice->noise << 13;
                voice->noise ^= voice->noise >> 17;
                voice->noise ^= voice->noise << 5;
                noise_wave = ((int)((voice->noise >> 24) & 0xff) - 128) * 8;
                wave += (noise_wave - wave) * voice->noise_mix >> 6;
            }
        }
    }
    else
    {
        wave = wave_tables[voice->waveform][voice->table_level][position];
        if (voice->accent > 0)
        {
            accent_mix = voice->accent >> 9;
            wave += (wave_tables[voice->accent_waveform][voice->table_level]
                                [position] - wave) * accent_mix >> 6;
            voice->accent -= voice->accent_step;
            if (voice->accent < 0) voice->accent = 0;
        }
    }

    voice->phase += voice->current_step;
    sample = (wave * voice->level) >> 6;
    return (sample * voice->env) >> 15;
}

static int16_t soft_saturate(int mixed)
{
    int negative = mixed < 0;
    int magnitude = negative ? -mixed : mixed;

    if (magnitude > 12000)
        magnitude = 12000 + ((magnitude - 12000) >> 2);
    if (magnitude > 16000) magnitude = 16000;
    return (int16_t)(negative ? -magnitude : magnitude);
}

static int16_t synth_sample(void)
{
    int mixed = 0;
    int dry;
    int delayed;
    int i;

    if (!modulation_lfo_countdown)
    {
        update_modulation_lfo();
        modulation_lfo_countdown = MODULATION_LFO_INTERVAL;
    }
    modulation_lfo_countdown--;

    for (i = 0; i < SYNTH_VOICES; i++)
        if (voices[i].active) mixed += voice_sample(&voices[i]);

    /* Soft knee followed by a subtle mono ensemble delay tap. */
    dry = soft_saturate(mixed);
    delayed = ensemble_delay[(ensemble_position + 129) & 255];
    ensemble_delay[ensemble_position] = (int16_t)dry;
    ensemble_position = (ensemble_position + 1) & 255;
    return (int16_t)(dry + ((delayed - dry) >> 3));
}

void doom_softsynth_render_48k(int16_t *buffer, unsigned int samples)
{
    int16_t next_sample;

    if (!buffer) return;
    if (!initialized)
    {
        memset(buffer, 0, samples * sizeof(*buffer));
        return;
    }

    while (samples)
    {
        if (interpolation_phase)
        {
            next_sample = synth_sample();
            *buffer++ = (int16_t)(((int)interpolation_sample
                                   + next_sample) / 2);
            interpolation_sample = next_sample;
            interpolation_phase = 0;
        }
        else
        {
            *buffer++ = interpolation_sample;
            interpolation_phase = 1;
        }
        samples--;
    }
}
