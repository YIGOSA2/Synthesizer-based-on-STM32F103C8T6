#ifndef WAVETABLE_H
#define WAVETABLE_H

#include <stdint.h>

#define WAVETABLE_SIZE 256

// Sine wave (0-4095, midpoint 2048)
extern const uint16_t sine_table[WAVETABLE_SIZE];

// Square wave
extern const uint16_t square_table[WAVETABLE_SIZE];

// Saw wave
extern const uint16_t saw_table[WAVETABLE_SIZE];

// Triangle wave
extern const uint16_t triangle_table[WAVETABLE_SIZE];

#endif // WAVETABLE_H
