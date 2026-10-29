#include "dual_logic.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>

static int ParseFieldsCount(const char *line, const char *prefix, int64_t *values, size_t count)
{
    size_t index, digits;
    int negative;
    int64_t value;

    if (line == NULL || prefix == NULL || values == NULL) return 0;
    if (strncmp(line, prefix, strlen(prefix)) != 0) return 0;
    line += strlen(prefix);
    for (index = 0U; index < count; index++) {
        negative = *line == '-';
        if (negative) line++;
        value = 0;
        digits = 0U;
        while (*line >= '0' && *line <= '9') {
            if (++digits > 10U) return 0;
            value = value * 10 + (*line++ - '0');
        }
        if (!digits) return 0;
        values[index] = negative ? -value : value;
        if (index + 1U < count) {
            if (*line++ != ',') return 0;
        } else if (*line != '\0') {
            return 0;
        }
    }
    return 1;
}

int Dual_ParseControl(const char *line, uint8_t *board, uint8_t *enabled)
{
    int64_t values[2];
    if (board == NULL || enabled == NULL) return 0;
    if (!ParseFieldsCount(line, "CTRL,", values, 2U)) return 0;
    if (values[0] != APP_COLLECTOR_ID || values[1] < 0 || values[1] > 1) return 0;
    *board = (uint8_t)values[0];
    *enabled = (uint8_t)values[1];
    return 1;
}

int Dual_FormatSample(const DualSample *sample, char *out, size_t capacity)
{
    int length;
    if (sample == NULL || out == NULL || capacity == 0U) return 0;
    length = snprintf(out, capacity, "DATA,%u,%lu,%lu,%ld,%ld,%u,%u,%u,%u,%u\r\n",
                      (unsigned int)APP_BOARD_ID,
                      (unsigned long)sample->sequence,
                      (unsigned long)sample->uptime_ms,
                      (long)sample->temperature,
                      (long)sample->humidity,
                      (unsigned int)sample->light_adc,
                      (unsigned int)sample->co2_ppm,
                      (unsigned int)sample->hcho_centi,
                      (unsigned int)sample->valid,
                      (unsigned int)sample->sharing);
    return length > 0 && (size_t)length < capacity ? length : 0;
}

int Dual_ParseSample(const char *line, uint8_t expected_board, DualSample *sample)
{
    int64_t values[11] = {0};
    size_t fields = 0U;

    if (line == NULL || sample == NULL) return 0;
    if (ParseFieldsCount(line, "DATA,", values, 8U)) fields = 8U;
    else {
        memset(values, 0, sizeof(values));
        if (!ParseFieldsCount(line, "DATA,", values, 10U)) return 0;
        fields = 10U;
    }

    if (values[0] != expected_board || values[0] < 1 || values[0] > 3 ||
        values[1] < 0 || values[1] > UINT32_MAX ||
        values[2] < 0 || values[2] > UINT32_MAX ||
        values[3] < -4000 || values[3] > 12500 ||
        values[4] < 0 || values[4] > 10000 ||
        values[5] < 0 || values[5] > 4095) return 0;

    sample->sequence = (uint32_t)values[1];
    sample->uptime_ms = (uint32_t)values[2];
    sample->temperature = (int32_t)values[3];
    sample->humidity = (int32_t)values[4];
    sample->light_adc = (uint16_t)values[5];
    if (fields == 10U) {
        if (values[6] < 0 || values[6] > 5000 ||
            values[7] < 0 || values[7] > 1000 ||
            values[8] < 0 || values[8] > 15 ||
            values[9] < 0 || values[9] > 1) return 0;
        sample->co2_ppm = (uint16_t)values[6];
        sample->hcho_centi = (uint16_t)values[7];
        sample->valid = (uint8_t)values[8];
        sample->sharing = (uint8_t)values[9];
    } else {
        if (values[6] < 0 || values[6] > 15 ||
            values[7] < 0 || values[7] > 1) return 0;
        sample->co2_ppm = 0U;
        sample->hcho_centi = 0U;
        sample->valid = (uint8_t)values[6] & 3U;
        sample->sharing = (uint8_t)values[7];
    }
    return 1;
}

int Dual_Receive(DualRemote *remote, const char *line, uint32_t now)
{
    DualSample parsed;
    uint8_t board, enabled;

    if (remote == NULL || line == NULL) return 0;
    if (Dual_ParseControl(line, &board, &enabled)) {
        (void)board;
        memset(&remote->sample, 0, sizeof(remote->sample));
        remote->enabled = enabled;
        remote->has_sample = 0U;
        remote->received_at = now;
        return 1;
    }
    if (!Dual_ParseSample(line, APP_COLLECTOR_ID, &parsed)) return 0;
    remote->sample = parsed;
    remote->enabled = parsed.sharing;
    remote->has_sample = 1U;
    remote->received_at = now;
    return 1;
}

int Dual_Expire(DualRemote *remote, uint32_t now)
{
    if (remote == NULL ||
        (uint32_t)(now - remote->received_at) < APP_REMOTE_TIMEOUT_MS) return 0;
    remote->enabled = 0U;
    remote->has_sample = 0U;
    remote->sample.valid = 0U;
    return 1;
}

uint16_t Dual_LightPercent(uint16_t adc)
{
    int32_t span = (int32_t)APP_LIGHT_BRIGHT_ADC - (int32_t)APP_LIGHT_DARK_ADC;
    int32_t percent;
    if (adc > 4095U || !span) return 0U;
    percent = ((int32_t)adc - (int32_t)APP_LIGHT_DARK_ADC) * 10000L / span;
    if (percent < 0) percent = 0;
    if (percent > 10000) percent = 10000;
    return (uint16_t)percent;
}
