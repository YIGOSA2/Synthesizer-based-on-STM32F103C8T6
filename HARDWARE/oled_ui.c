#include "main.h"
#include "synth.h"
#include "input.h"
#include "oled_ui.h"
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define OLED_ADDR_7BIT 0x3C
#define OLED_ADDR      (OLED_ADDR_7BIT << 1)

static uint8_t s_inited = 0;
static uint32_t s_last_ms = 0;
static char s_last_wave[12] = "";
static char s_last_note[8] = "";
static char s_last_trm[4] = "";
static char s_last_dly[4] = "";
static char s_last_midi[4] = "";

static HAL_StatusTypeDef oled_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    return HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, 20);
}

static HAL_StatusTypeDef oled_write_data(const uint8_t *data, uint16_t len)
{
    uint8_t tx[17];
    uint16_t i = 0;

    tx[0] = 0x40;
    while (i < len) {
        uint16_t chunk = (len - i > 16U) ? 16U : (len - i);
        memcpy(&tx[1], &data[i], chunk);
        if (HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, tx, (uint16_t)(chunk + 1U), 20) != HAL_OK) {
            return HAL_ERROR;
        }
        i += chunk;
    }
    return HAL_OK;
}

static void oled_set_pos(uint8_t page, uint8_t col)
{
    oled_write_cmd((uint8_t)(0xB0U + page));
    oled_write_cmd((uint8_t)(0x00U + (col & 0x0FU)));
    oled_write_cmd((uint8_t)(0x10U + ((col >> 4) & 0x0FU)));
}

static void oled_clear(void)
{
    uint8_t page, i;
    uint8_t z[16];
    memset(z, 0x00, sizeof(z));

    for (page = 0; page < 8; page++) {
        oled_set_pos(page, 0);
        for (i = 0; i < 8; i++) {
            oled_write_data(z, sizeof(z));
        }
    }
}

static const uint8_t* glyph(char c)
{
    static const uint8_t sp[5]={0,0,0,0,0}, co[5]={0,0x36,0x36,0,0}, hy[5]={0x08,0x08,0x08,0x08,0x08};
    static const uint8_t n0[5]={0x3E,0x51,0x49,0x45,0x3E}, n1[5]={0,0x42,0x7F,0x40,0}, n2[5]={0x42,0x61,0x51,0x49,0x46};
    static const uint8_t n3[5]={0x21,0x41,0x45,0x4B,0x31}, n4[5]={0x18,0x14,0x12,0x7F,0x10}, n5[5]={0x27,0x45,0x45,0x45,0x39};
    static const uint8_t n6[5]={0x3C,0x4A,0x49,0x49,0x30}, n7[5]={0x01,0x71,0x09,0x05,0x03}, n8[5]={0x36,0x49,0x49,0x49,0x36};
    static const uint8_t n9[5]={0x06,0x49,0x49,0x29,0x1E}, sh[5]={0x14,0x7F,0x14,0x7F,0x14};

    static const uint8_t A[5]={0x7E,0x11,0x11,0x11,0x7E}, B[5]={0x7F,0x49,0x49,0x49,0x36}, C[5]={0x3E,0x41,0x41,0x41,0x22};
    static const uint8_t D[5]={0x7F,0x41,0x41,0x22,0x1C}, E[5]={0x7F,0x49,0x49,0x49,0x41};
    static const uint8_t F[5]={0x7F,0x09,0x09,0x09,0x01}, G[5]={0x3E,0x41,0x49,0x49,0x3A};
    static const uint8_t I[5]={0,0x41,0x7F,0x41,0}, L[5]={0x7F,0x40,0x40,0x40,0x40};
    static const uint8_t M[5]={0x7F,0x02,0x04,0x02,0x7F}, N[5]={0x7F,0x02,0x0C,0x10,0x7F};
    static const uint8_t O[5]={0x3E,0x41,0x41,0x41,0x3E}, Q[5]={0x3E,0x41,0x51,0x21,0x5E};
    static const uint8_t R[5]={0x7F,0x09,0x19,0x29,0x46}, S[5]={0x46,0x49,0x49,0x49,0x31};
    static const uint8_t T[5]={0x01,0x01,0x7F,0x01,0x01}, U[5]={0x3F,0x40,0x40,0x40,0x3F};
    static const uint8_t V[5]={0x1F,0x20,0x40,0x20,0x1F}, W[5]={0x7F,0x20,0x18,0x20,0x7F};
    static const uint8_t Y[5]={0x03,0x04,0x78,0x04,0x03};

    switch (c) {
    case '0': return n0; case '1': return n1; case '2': return n2; case '3': return n3; case '4': return n4;
    case '5': return n5; case '6': return n6; case '7': return n7; case '8': return n8; case '9': return n9;
    case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D; case 'E': return E; case 'F': return F;
    case 'G': return G; case 'I': return I; case 'L': return L; case 'M': return M; case 'N': return N;
    case 'O': return O; case 'Q': return Q; case 'R': return R; case 'S': return S; case 'T': return T;
    case 'U': return U; case 'V': return V; case 'W': return W; case 'Y': return Y;
    case '#': return sh; case ':': return co; case '-': return hy;
    default: return sp;
    }
}

static void oled_draw_char(uint8_t page, uint8_t col, char c)
{
    uint8_t d[6];
    const uint8_t *g = glyph(c);
    d[0] = g[0]; d[1] = g[1]; d[2] = g[2]; d[3] = g[3]; d[4] = g[4]; d[5] = 0;
    oled_set_pos(page, col);
    oled_write_data(d, 6);
}

static void oled_draw_text(uint8_t page, uint8_t col, const char *s)
{
    while (*s && col <= 122U) {
        oled_draw_char(page, col, *s++);
        col = (uint8_t)(col + 6U);
    }
}

static void oled_draw_text_padded(uint8_t page, uint8_t col, const char *s, uint8_t chars)
{
    uint8_t i;
    for (i = 0; i < chars; i++) {
        char c = s[i];
        if (c == '\0') break;
        oled_draw_char(page, (uint8_t)(col + i * 6U), c);
    }
    for (; i < chars; i++) {
        oled_draw_char(page, (uint8_t)(col + i * 6U), ' ');
    }
}

static const char* wave_name(WaveType w)
{
    switch (w) {
    case WAVE_SINE: return "SINE";
    case WAVE_SQUARE: return "SQUARE";
    case WAVE_SAW: return "SAW";
    case WAVE_TRIANGLE: return "TRIANGLE";
    default: return "UNKNOWN";
    }
}

static void note_to_str(uint8_t midi, char *out)
{
    static const char *pc[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

    if (midi == 0xFFU) {
        out[0] = '-';
        out[1] = '-';
        out[2] = '\0';
        return;
    }

    {
        uint8_t p = (uint8_t)(midi % 12U);
        int octave = ((int)midi / 12) - 1;
        const char *n = pc[p];

        if (n[1] == '#') {
            out[0] = n[0];
            out[1] = '#';
            out[2] = (char)('0' + octave);
            out[3] = '\0';
        } else {
            out[0] = n[0];
            out[1] = (char)('0' + octave);
            out[2] = '\0';
        }
    }
}

void oled_ui_init(void)
{
    HAL_Delay(50);
    oled_write_cmd(0xAE);
    oled_write_cmd(0x20); oled_write_cmd(0x00);
    oled_write_cmd(0xB0);
    oled_write_cmd(0xC8);
    oled_write_cmd(0x00);
    oled_write_cmd(0x10);
    oled_write_cmd(0x40);
    oled_write_cmd(0x81); oled_write_cmd(0x7F);
    oled_write_cmd(0xA1);
    oled_write_cmd(0xA6);
    oled_write_cmd(0xA8); oled_write_cmd(0x3F);
    oled_write_cmd(0xA4);
    oled_write_cmd(0xD3); oled_write_cmd(0x00);
    oled_write_cmd(0xD5); oled_write_cmd(0x80);
    oled_write_cmd(0xD9); oled_write_cmd(0xF1);
    oled_write_cmd(0xDA); oled_write_cmd(0x12);
    oled_write_cmd(0xDB); oled_write_cmd(0x40);
    oled_write_cmd(0x8D); oled_write_cmd(0x14);
    oled_write_cmd(0xAF);

    oled_clear();
    oled_draw_text(0, 0, "WAVE:");
    oled_draw_text(2, 0, "NOTE:");
    oled_draw_text(4, 0, "TRM:");
    oled_draw_text(6, 0, "DLY:");
    oled_draw_text(7, 0, "MIDI:");
    s_inited = 1;
}

void oled_ui_task_step(void)
{
    char wave[12];
    char note[8];
    char trm[4];
    char dly[4];
    char midi[4];
    uint32_t now = HAL_GetTick();

    if (!s_inited) return;
    if ((now - s_last_ms) < 80U) return;
    s_last_ms = now;

    strcpy(wave, wave_name(synth_get_waveform()));
    note_to_str(input_get_current_note(), note);
    strcpy(trm, synth_get_tremolo() ? "ON" : "OFF");
    strcpy(dly, synth_get_delay() ? "ON" : "OFF");
    strcpy(midi, input_get_midi_active() ? "IN" : "--");

    if (strcmp(wave, s_last_wave) == 0 &&
        strcmp(note, s_last_note) == 0 &&
        strcmp(trm, s_last_trm) == 0 &&
        strcmp(dly, s_last_dly) == 0 &&
        strcmp(midi, s_last_midi) == 0) {
        return;
    }

    strcpy(s_last_wave, wave);
    strcpy(s_last_note, note);
    strcpy(s_last_trm, trm);
    strcpy(s_last_dly, dly);
    strcpy(s_last_midi, midi);

    oled_draw_text_padded(0, 36, wave, 10);
    oled_draw_text_padded(2, 36, note, 4);
    oled_draw_text_padded(4, 36, trm, 3);
    oled_draw_text_padded(6, 36, dly, 3);
    oled_draw_text_padded(7, 36, midi, 3);
}





