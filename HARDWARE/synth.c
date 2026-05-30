#include "synth.h"
#include "wavetable.h"

#include <math.h>
#include <string.h>

#define PHASE_MAX            4294967296.0f//32bit

#define TREMOLO_RATE_HZ      5U
#define TREMOLO_PHASE_INC    ((uint32_t)(((uint64_t)TREMOLO_RATE_HZ << 32) / SAMPLE_RATE))//计算相位步进值

#define DELAY_SAMPLES        3308U   /* ~150 ms @ 22.05 kHz */
#define DELAY_MIX_PCT        35
#define DELAY_FB_PCT         40
#define DELAY_LP_SHIFT       2U    /* one-pole LPF: lower value => stronger smoothing */
#define DELAY_NOISE_GATE     3     /* small-signal gate in sample domain */
#define DELAY_IDLE_GATE      2

#define VOICE_COUNT          4U
#define ENV_MAX              32767U//16bit
#define ATTACK_MS            8U
#define RELEASE_MS           400U
#define ATTACK_SAMPLES       ((SAMPLE_RATE * ATTACK_MS) / 1000U)
#define RELEASE_SAMPLES      ((SAMPLE_RATE * RELEASE_MS) / 1000U)
#define ATTACK_STEP          ((ATTACK_SAMPLES > 0U) ? ((ENV_MAX + ATTACK_SAMPLES - 1U) / ATTACK_SAMPLES) : ENV_MAX)
#define RELEASE_STEP         ((RELEASE_SAMPLES > 0U) ? ((ENV_MAX + RELEASE_SAMPLES - 1U) / RELEASE_SAMPLES) : ENV_MAX)

typedef struct {
    uint32_t phase;  //振荡器相位
    uint32_t phase_increment;//相位步进值
    uint16_t amplitude;//映射音量
    uint16_t env_level;//包络值
    uint8_t active;//通道使用否
    uint8_t gate;//按键松开或按下
    uint8_t midi_note;//midi信号对应音符编码
    uint32_t age;//音符年龄
} Voice;

/* Legacy symbol kept for compatibility with existing code paths. */
Oscillator osc = {0};

static Voice s_voices[VOICE_COUNT];
static uint32_t s_voice_age = 1U;

//波形菜单
static const uint16_t *const wave_tables[WAVE_COUNT] = {
    sine_table,
    square_table,
    saw_table,
    triangle_table
};

static WaveType current_wave_type = WAVE_SINE;
static const uint16_t *current_wave = sine_table;

//效果器开关
static uint8_t s_tremolo_enabled = 0U;
static uint8_t s_delay_enabled = 0U;

static uint32_t s_tremolo_phase = 0U;
static int16_t s_delay_buf[DELAY_SAMPLES];
static uint16_t s_delay_idx = 0U;
static int32_t s_delay_lp = 0;//低通

// 弹复音多音时音量衰减更柔和 
static const uint16_t k_poly_gain_q10[VOICE_COUNT + 1U] = {
    0U,
    1024U,
    760U,
    620U,
    540U
};

//限制钳位
static int32_t clamp_i32(int32_t x, int32_t lo, int32_t hi)
{
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

//软削波
static int32_t soft_clip_12b(int32_t x)
{
    int32_t ax = (x >= 0) ? x : -x;

    if (ax <= 2047) {
        return x;
    }

    x = (x * 4096) / (ax + 2048);
    return clamp_i32(x, -2048, 2047);
}
//判断是否有音使能，更新标志位
static void update_legacy_active_flag(void)
{
    uint8_t i;
    osc.active = 0U;
    for (i = 0U; i < VOICE_COUNT; i++) {
        if (s_voices[i].active) {
            osc.active = 1U;
            break;
        }
    }
}

static uint32_t midi_to_phase_inc(uint8_t midi_note)
{
    float freq = 440.0f * powf(2.0f, ((float)midi_note - 69.0f) / 12.0f);
    return (uint32_t)((freq * PHASE_MAX) / SAMPLE_RATE);
}

void synth_init(void)
{
    memset(s_voices, 0, sizeof(s_voices));
    s_voice_age = 1U;

    osc.phase = 0U;
    osc.phase_increment = 0U;
    osc.amplitude = 0U;
    osc.active = 0U;

    current_wave_type = WAVE_SINE;
    current_wave = sine_table;

    s_tremolo_enabled = 0U;
    s_delay_enabled = 0U;
    s_tremolo_phase = 0U;
	
    s_delay_idx = 0U;
    s_delay_lp = 0;
    memset(s_delay_buf, 0, sizeof(s_delay_buf));
}

void synth_note_on(uint8_t midi_note, uint8_t velocity)
{
    uint8_t i;
    int free_idx = -1;
    int replace_idx = -1;
    uint32_t oldest_age = 0xFFFFFFFFU;
    uint16_t amp;

    if (midi_note < 21U || midi_note > 108U) {
        return;
    }

    amp = (uint16_t)((velocity * 2047U) / 127U);
    if (amp == 0U) {
        amp = 1U;
    }

    for (i = 0U; i < VOICE_COUNT; i++) {
        if (s_voices[i].active && s_voices[i].midi_note == midi_note) {
            s_voices[i].phase = 0U;
            s_voices[i].phase_increment = midi_to_phase_inc(midi_note);//返回振荡器增量
            s_voices[i].amplitude = amp;
            s_voices[i].gate = 1U;
            s_voices[i].age = s_voice_age++;
            update_legacy_active_flag();
            return;
        }
    }

    for (i = 0U; i < VOICE_COUNT; i++) {
        if (!s_voices[i].active && free_idx < 0) {
            free_idx = (int)i;
        }
        if (s_voices[i].active && s_voices[i].age < oldest_age) {
            oldest_age = s_voices[i].age;
            replace_idx = (int)i;
        }
    }

    if (free_idx >= 0) {
        i = (uint8_t)free_idx;
    } else {
        i = (uint8_t)replace_idx;
    }

    s_voices[i].phase = 0U;
    s_voices[i].phase_increment = midi_to_phase_inc(midi_note);
    s_voices[i].amplitude = amp;
    s_voices[i].env_level = 0U;
    s_voices[i].active = 1U;
    s_voices[i].gate = 1U;
    s_voices[i].midi_note = midi_note;
    s_voices[i].age = s_voice_age++;

    update_legacy_active_flag();
}

void synth_note_off_key(uint8_t midi_note)
{
    uint8_t i;

    for (i = 0U; i < VOICE_COUNT; i++) {
        if (s_voices[i].active && s_voices[i].midi_note == midi_note) {
            s_voices[i].gate = 0U;
        }
    }

    update_legacy_active_flag();
}

void synth_note_off(void)
{
    uint8_t i;

    for (i = 0U; i < VOICE_COUNT; i++) {
        if (s_voices[i].active) {
            s_voices[i].gate = 0U;
        }
    }

    update_legacy_active_flag();
}

uint16_t synth_get_sample(void)
{
    uint8_t i;
    uint8_t active_count = 0U;
    int32_t sample = 0;

    for (i = 0U; i < VOICE_COUNT; i++) {
        int32_t v;

        if (!s_voices[i].active) {
            continue;
        }

        if (s_voices[i].gate) {
            uint32_t next = (uint32_t)s_voices[i].env_level + ATTACK_STEP;
            s_voices[i].env_level = (next >= ENV_MAX) ? (uint16_t)ENV_MAX : (uint16_t)next;
        } else {
            if (s_voices[i].env_level > RELEASE_STEP) {
                s_voices[i].env_level = (uint16_t)(s_voices[i].env_level - RELEASE_STEP);
            } else {
                s_voices[i].env_level = 0U;
                s_voices[i].active = 0U;
                continue;
            }
        }

        s_voices[i].phase += s_voices[i].phase_increment;
        v = (int32_t)current_wave[(uint8_t)(s_voices[i].phase >> 24)] - 2048;
        v = (v * (int32_t)s_voices[i].amplitude) / 2047;
        v = (v * (int32_t)s_voices[i].env_level) / (int32_t)ENV_MAX;

        sample += v;
        active_count++;
    }

    update_legacy_active_flag();

    if (active_count == 0U && !s_delay_enabled) {
        return 0U;
    }

    if (active_count > VOICE_COUNT) {
        active_count = VOICE_COUNT;
    }
    sample = (sample * (int32_t)k_poly_gain_q10[active_count]) / 1024;

    if (s_tremolo_enabled) {
        uint8_t lfo_idx = (uint8_t)(s_tremolo_phase >> 24);
        int32_t lfo = (int32_t)sine_table[lfo_idx];
        int32_t gain_q12 = 2048 + ((lfo * 2048) / 4095); /* 0.5 .. 1.0 */
        sample = (sample * gain_q12) / 4096;
        s_tremolo_phase += TREMOLO_PHASE_INC;
    }

    sample = soft_clip_12b(sample);

    if (s_delay_enabled) {
        int32_t delayed_raw = s_delay_buf[s_delay_idx];
        int32_t delayed;
        int32_t mixed;
        int32_t fb;

        /* Delay branch low-pass: suppress high-frequency PWM/quantization hiss. */
        s_delay_lp += (delayed_raw - s_delay_lp) >> DELAY_LP_SHIFT;
        delayed = s_delay_lp;

        if (delayed < DELAY_NOISE_GATE && delayed > -DELAY_NOISE_GATE) {
            delayed = 0;
        }

        mixed = sample + ((delayed * DELAY_MIX_PCT) / 100);
        fb = sample + ((delayed * DELAY_FB_PCT) / 100);

        fb = clamp_i32(fb, -32768, 32767);
        s_delay_buf[s_delay_idx] = (int16_t)fb;

        s_delay_idx++;
        if (s_delay_idx >= DELAY_SAMPLES) {
            s_delay_idx = 0U;
        }

        sample = mixed;

        if (active_count == 0U && sample < DELAY_IDLE_GATE && sample > -DELAY_IDLE_GATE) {
            sample = 0;
        }
    }

    sample = soft_clip_12b(sample);
    return (uint16_t)(sample + 2048);
}

void synth_set_waveform(WaveType type)
{
    if ((uint32_t)type >= (uint32_t)WAVE_COUNT) {
        return;
    }

    current_wave_type = type;
    current_wave = wave_tables[type];
}

WaveType synth_get_waveform(void)
{
    return current_wave_type;
}

void synth_set_tremolo(uint8_t enable)
{
    s_tremolo_enabled = (enable != 0U) ? 1U : 0U;
}

uint8_t synth_get_tremolo(void)
{
    return s_tremolo_enabled;
}

void synth_set_delay(uint8_t enable)
{
    s_delay_enabled = (enable != 0U) ? 1U : 0U;
    if (!s_delay_enabled) {
        memset(s_delay_buf, 0, sizeof(s_delay_buf));
        s_delay_idx = 0U;
        s_delay_lp = 0;
    }
}

uint8_t synth_get_delay(void)
{
    return s_delay_enabled;
}







