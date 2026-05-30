#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

#define SAMPLE_RATE 22050U

typedef enum {
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_SAW,
    WAVE_TRIANGLE,
    WAVE_COUNT
} WaveType;

typedef struct {
    uint32_t phase;
    uint32_t phase_increment;
    uint16_t amplitude;
    uint8_t active;
} Oscillator;

void synth_init(void);

void synth_note_on(uint8_t midi_note, uint8_t velocity);
void synth_note_off(void);
void synth_note_off_key(uint8_t midi_note);

uint16_t synth_get_sample(void);

void synth_set_waveform(WaveType type);
WaveType synth_get_waveform(void);

void synth_set_tremolo(uint8_t enable);
uint8_t synth_get_tremolo(void);
void synth_set_delay(uint8_t enable);
uint8_t synth_get_delay(void);

/* Kept for compatibility with legacy code paths. */
extern Oscillator osc;

#endif /* SYNTH_H */
