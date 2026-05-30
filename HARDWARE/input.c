#include "main.h"
#include "synth.h"
#include "input.h"
#include "usart.h"

#define COMBO_WAVE_HOLD_MS 600U
#define MIDI_RX_BUF_SIZE 64U
#define MIDI_ACTIVE_HOLD_MS 500U

static const uint8_t kNoteMap[8] = {60, 62, 64, 65, 67, 69, 71, 72};

static uint8_t s_last_note = 0xFF;
static uint8_t s_prev_key_mask = 0;

static uint8_t s_pa0_prev = 0;
static uint8_t s_pa1_prev = 0;
static uint8_t s_pa0_candidate = 0;
static uint8_t s_pa1_candidate = 0;

static uint8_t s_combo_active = 0;
static uint8_t s_combo_done = 0;
static uint32_t s_combo_start_ms = 0;

static uint8_t s_midi_inited = 0;
static volatile uint8_t s_midi_rx_buf[MIDI_RX_BUF_SIZE];
static volatile uint8_t s_midi_rx_head = 0;
static volatile uint8_t s_midi_rx_tail = 0;
static volatile uint32_t s_midi_last_rx_ms = 0U;

static uint8_t s_midi_running_status = 0;
static uint8_t s_midi_need = 0;
static uint8_t s_midi_pos = 0;
static uint8_t s_midi_data[2];

static uint8_t midi_next_idx(uint8_t idx)
{
    idx++;
    if (idx >= MIDI_RX_BUF_SIZE) {
        idx = 0;
    }
    return idx;
}

static void midi_push_byte(uint8_t b)
{
    uint8_t next = midi_next_idx(s_midi_rx_head);
    if (next != s_midi_rx_tail) {
        s_midi_rx_buf[s_midi_rx_head] = b;
        s_midi_rx_head = next;
    }
}

static uint8_t midi_pop_byte(uint8_t *out)
{
    if (s_midi_rx_tail == s_midi_rx_head) {
        return 0U;
    }

    *out = s_midi_rx_buf[s_midi_rx_tail];
    s_midi_rx_tail = midi_next_idx(s_midi_rx_tail);
    return 1U;
}

static uint8_t midi_expected_data_bytes(uint8_t status)
{
    switch (status & 0xF0U) {
    case 0x80U:
    case 0x90U:
    case 0xA0U:
    case 0xB0U:
    case 0xE0U:
        return 2U;
    case 0xC0U:
    case 0xD0U:
        return 1U;
    default:
        return 0U;
    }
}

static void midi_dispatch_message(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint8_t msg = status & 0xF0U;

    if (msg == 0x90U) {
        if (d2 == 0U) {
            synth_note_off_key(d1);
        } else {
            synth_note_on(d1, d2);
        }
    } else if (msg == 0x80U) {
        synth_note_off_key(d1);
    }
}

static void midi_process_byte(uint8_t b)
{
    if ((b & 0x80U) != 0U) {
        if (b >= 0xF8U) {
            return; /* real-time message */
        }

        if (b >= 0xF0U) {
            s_midi_running_status = 0U;
            s_midi_need = 0U;
            s_midi_pos = 0U;
            return;
        }

        s_midi_running_status = b;
        s_midi_need = midi_expected_data_bytes(b);
        s_midi_pos = 0U;
        return;
    }

    if (s_midi_running_status == 0U || s_midi_need == 0U) {
        return;
    }

    if (s_midi_pos < sizeof(s_midi_data)) {
        s_midi_data[s_midi_pos] = b;
    }
    s_midi_pos++;

    if (s_midi_pos >= s_midi_need) {
        uint8_t d1 = s_midi_data[0];
        uint8_t d2 = (s_midi_need >= 2U) ? s_midi_data[1] : 0U;

        midi_dispatch_message(s_midi_running_status, d1, d2);
        s_midi_pos = 0U;
    }
}

static void handle_midi_rx(void)
{
    uint8_t b;

    while (midi_pop_byte(&b)) {
        midi_process_byte(b);
    }
}

void input_init(void)
{
    if (s_midi_inited) {
        return;
    }
    s_midi_inited = 1U;

    s_midi_rx_head = 0U;
    s_midi_rx_tail = 0U;
    s_midi_running_status = 0U;
    s_midi_need = 0U;
    s_midi_pos = 0U;
    s_midi_last_rx_ms = 0U;

    (void)huart1.Instance->SR;
    (void)huart1.Instance->DR;

    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
}

void input_midi_irq_handler(void)
{
    USART_TypeDef *uart = huart1.Instance;
    uint32_t sr = uart->SR;

    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) {
        uint8_t b = (uint8_t)uart->DR;
        if (sr & USART_SR_RXNE) {
            midi_push_byte(b);
            s_midi_last_rx_ms = HAL_GetTick();
        }
    }
}

static uint8_t read_key_mask(void)
{
    uint8_t mask = 0;

    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_RESET) mask |= (1U << 0);
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_RESET) mask |= (1U << 1);
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10) == GPIO_PIN_RESET) mask |= (1U << 2);
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) == GPIO_PIN_RESET) mask |= (1U << 3);
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_RESET) mask |= (1U << 4);
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13) == GPIO_PIN_RESET) mask |= (1U << 5);
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14) == GPIO_PIN_RESET) mask |= (1U << 6);
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15) == GPIO_PIN_RESET) mask |= (1U << 7);

    return mask;
}

static int8_t select_note_idx(uint8_t mask)
{
    int8_t i;
    for (i = 7; i >= 0; i--) {
        if (mask & (1U << i)) {
            return i;
        }
    }
    return -1;
}

static void handle_effect_keys(void)
{
    uint8_t pa0 = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) ? 1U : 0U;
    uint8_t pa1 = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET) ? 1U : 0U;
    uint8_t combo = (uint8_t)(pa0 && pa1);
    uint32_t now = HAL_GetTick();

    if (combo) {
        if (!s_combo_active) {
            s_combo_active = 1U;
            s_combo_done = 0U;
            s_combo_start_ms = now;
            s_pa0_candidate = 0U;
            s_pa1_candidate = 0U;
        } else if (!s_combo_done && (now - s_combo_start_ms >= COMBO_WAVE_HOLD_MS)) {
            WaveType next = (WaveType)((synth_get_waveform() + 1U) % WAVE_COUNT);
            synth_set_waveform(next);
            s_combo_done = 1U;
        }
    } else {
        s_combo_active = 0U;
        s_combo_done = 0U;

        if (pa0 && !s_pa0_prev && !pa1) {
            s_pa0_candidate = 1U;
        }
        if (!pa0 && s_pa0_prev) {
            if (s_pa0_candidate) {
                synth_set_tremolo((uint8_t)!synth_get_tremolo());
            }
            s_pa0_candidate = 0U;
        }

        if (pa1 && !s_pa1_prev && !pa0) {
            s_pa1_candidate = 1U;
        }
        if (!pa1 && s_pa1_prev) {
            if (s_pa1_candidate) {
                synth_set_delay((uint8_t)!synth_get_delay());
            }
            s_pa1_candidate = 0U;
        }
    }

    s_pa0_prev = pa0;
    s_pa1_prev = pa1;
}

static void handle_note_keys(void)
{
    uint8_t mask = read_key_mask();
    uint8_t changed = (uint8_t)(mask ^ s_prev_key_mask);
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        uint8_t bit = (uint8_t)(1U << i);
        if (changed & bit) {
            uint8_t note = kNoteMap[i];
            if (mask & bit) {
                synth_note_on(note, 100);
            } else {
                synth_note_off_key(note);
            }
        }
    }

    {
        int8_t idx = select_note_idx(mask);
        if (idx < 0) {
            s_last_note = 0xFF;
        } else {
            s_last_note = kNoteMap[(uint8_t)idx];
        }
    }

    s_prev_key_mask = mask;
}

void input_task_step(void)
{
    handle_midi_rx();
    handle_effect_keys();
    handle_note_keys();
}

uint8_t input_get_midi_active(void)
{
    if (!s_midi_inited) {
        return 0U;
    }

    return ((HAL_GetTick() - s_midi_last_rx_ms) <= MIDI_ACTIVE_HOLD_MS) ? 1U : 0U;
}

uint8_t input_get_current_note(void)
{
    return s_last_note;
}

