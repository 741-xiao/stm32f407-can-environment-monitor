#ifndef DUAL_LOGIC_H
#define DUAL_LOGIC_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sequence, uptime_ms;
    int32_t temperature, humidity;
    uint16_t light_adc, co2_ppm, hcho_centi;
    uint8_t valid, sharing;
} DualSample;

typedef struct {
    DualSample sample;
    uint32_t received_at;
    uint8_t enabled;
    uint8_t has_sample;
} DualRemote;

int Dual_FormatSample(const DualSample *sample, char *out, size_t capacity);
int Dual_ParseControl(const char *line, uint8_t *board, uint8_t *enabled);
int Dual_ParseSample(const char *line, uint8_t expected_board, DualSample *sample);
int Dual_Receive(DualRemote *remote, const char *line, uint32_t now);
int Dual_Expire(DualRemote *remote, uint32_t now);
uint16_t Dual_LightPercent(uint16_t adc);
#endif
