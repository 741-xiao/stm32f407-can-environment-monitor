#include "tri_can_logic.h"
#include "app_config.h"
#include <string.h>

#define TRI_CAN_MAGIC 0xA5U
#define TRI_CAN_VERSION 0x02U

static uint16_t GetU16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static void PutU16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)(value >> 8);
}

static uint32_t ReconstructSequence(TriCanPair *pair, uint8_t low)
{
    uint32_t candidate;
    if (!pair->sequence_valid) {
        pair->sequence = low;
        pair->sequence_valid = 1U;
        return pair->sequence;
    }
    candidate = (pair->sequence & 0xFFFFFF00UL) | low;
    if (candidate + 128UL < pair->sequence) candidate += 256UL;
    else if (candidate > pair->sequence + 128UL && candidate >= 256UL) candidate -= 256UL;
    pair->sequence = candidate;
    return candidate;
}

void TriCan_Encode(const DualSample *sample, uint8_t data[TRI_CAN_DATA_LENGTH],
                   uint8_t meta[TRI_CAN_META_LENGTH])
{
    if (sample == NULL || data == NULL || meta == NULL) return;
    PutU16(&data[0], (uint16_t)(int16_t)sample->temperature);
    PutU16(&data[2], (uint16_t)sample->humidity);
    PutU16(&data[4], sample->light_adc);
    PutU16(&data[6], sample->co2_ppm);
    PutU16(&meta[0], sample->hcho_centi);
    meta[2] = sample->valid;
    meta[3] = sample->sharing;
    meta[4] = APP_BOARD_ID;
    meta[5] = (uint8_t)sample->sequence;
    meta[6] = TRI_CAN_MAGIC;
    meta[7] = TRI_CAN_VERSION;
}

int TriCan_MetaValid(const uint8_t *data, uint8_t length)
{
    return data != NULL && length == TRI_CAN_META_LENGTH &&
           data[4] == APP_COLLECTOR_ID && data[6] == TRI_CAN_MAGIC &&
           data[7] == TRI_CAN_VERSION && (data[2] & 0xF0U) == 0U &&
           data[3] <= 1U && GetU16(&data[0]) <= 1000U;
}

static int Complete(TriCanPair *pair, DualSample *sample)
{
    int16_t temperature;
    uint32_t difference;
    if (!pair->data_ready || !pair->meta_ready) return 0;
    difference = pair->data_at > pair->meta_at ? pair->data_at - pair->meta_at
                                               : pair->meta_at - pair->data_at;
    if (difference > APP_CAN_PAIR_TIMEOUT_MS) {
        pair->data_ready = pair->meta_ready = 0U;
        return 0;
    }
    temperature = (int16_t)pair->data.temperature_raw;
    if (temperature < -4000 || temperature > 12500 ||
        pair->data.humidity_raw > 10000U || pair->data.light_adc > 4095U ||
        pair->data.co2_ppm > 5000U || pair->meta.hcho_centi > 1000U) {
        pair->data_ready = pair->meta_ready = 0U;
        return 0;
    }
    sample->sequence = ReconstructSequence(pair, pair->meta.sequence_low);
    sample->uptime_ms = 0U;
    sample->temperature = temperature;
    sample->humidity = pair->data.humidity_raw;
    sample->light_adc = pair->data.light_adc;
    sample->co2_ppm = pair->data.co2_ppm;
    sample->hcho_centi = pair->meta.hcho_centi;
    sample->valid = pair->meta.valid;
    sample->sharing = pair->meta.sharing;
    pair->data_ready = pair->meta_ready = 0U;
    return 1;
}

int TriCan_Pair(TriCanPair *pair, uint32_t id, const uint8_t *data, uint8_t length,
                uint32_t now, DualSample *sample)
{
    uint32_t difference;
    if (pair == NULL || data == NULL || sample == NULL) return 0;
    if (pair->data_ready) {
        difference = now > pair->data_at ? now - pair->data_at : pair->data_at - now;
        if (difference > APP_CAN_PAIR_TIMEOUT_MS) pair->data_ready = 0U;
    }
    if (pair->meta_ready) {
        difference = now > pair->meta_at ? now - pair->meta_at : pair->meta_at - now;
        if (difference > APP_CAN_PAIR_TIMEOUT_MS) pair->meta_ready = 0U;
    }
    if (id == APP_CAN_DATA_ID && length == TRI_CAN_DATA_LENGTH) {
        pair->data.temperature_raw = GetU16(&data[0]);
        pair->data.humidity_raw = GetU16(&data[2]);
        pair->data.light_adc = GetU16(&data[4]);
        pair->data.co2_ppm = GetU16(&data[6]);
        pair->data_at = now;
        pair->data_ready = 1U;
    } else if (id == APP_CAN_META_ID && length == TRI_CAN_META_LENGTH) {
        if (!TriCan_MetaValid(data, length)) return 0;
        pair->meta.hcho_centi = GetU16(&data[0]);
        pair->meta.valid = data[2];
        pair->meta.sharing = data[3];
        pair->meta.board_id = data[4];
        pair->meta.sequence_low = data[5];
        pair->meta.magic = data[6];
        pair->meta.version = data[7];
        pair->meta_at = now;
        pair->meta_ready = 1U;
    } else {
        return 0;
    }
    return Complete(pair, sample);
}
