#ifndef DOOM_SOFTSYNTH_H
#define DOOM_SOFTSYNTH_H

#include <stdint.h>

int doom_softsynth_init(unsigned int sample_rate);
void doom_softsynth_shutdown(void);
void doom_softsynth_reset_song(void);
void doom_softsynth_note_on(int channel, int note, int velocity);
void doom_softsynth_note_off(int channel, int note);
void doom_softsynth_pitch(int channel, int value);
void doom_softsynth_controller(int channel, int controller, int value);
void doom_softsynth_system_event(int channel, int controller);
void doom_softsynth_all_notes_off(void);
void doom_softsynth_render_48k(int16_t *buffer, unsigned int samples);

#endif
