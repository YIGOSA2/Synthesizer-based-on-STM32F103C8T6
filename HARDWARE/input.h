#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

void input_init(void);
void input_task_step(void);
void input_midi_irq_handler(void);
uint8_t input_get_current_note(void);
uint8_t input_get_midi_active(void);

#endif

