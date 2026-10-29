#ifndef TRI_CAN_LOGIC_H
#define TRI_CAN_LOGIC_H
#include <stdint.h>
#include "dual_logic.h"

enum {
    TRI_CAN_DATA_LENGTH = 8,
    TRI_CAN_META_LENGTH = 8
};

typedef struct {
    uint16_t temperature_raw;
    uint16_t humidity_raw;
    uint16_t light_adc;
    uint16_t co2_ppm;
} TriCanData;

typedef struct {
    uint16_t hcho_centi;
    uint8_t valid;
    uint8_t sharing;
    uint8_t board_id;
    uint8_t sequence_low;
    uint8_t magic;
    uint8_t version;
} TriCanMeta;

typedef struct {
    uint8_t data_ready;
    uint8_t meta_ready;
    uint8_t sequence_valid;
    uint32_t data_at;
    uint32_t meta_at;
    uint32_t sequence;
    TriCanData data;
    TriCanMeta meta;
} TriCanPair;

void TriCan_Encode(const DualSample *sample, uint8_t data[TRI_CAN_DATA_LENGTH],
                   uint8_t meta[TRI_CAN_META_LENGTH]);
int TriCan_Pair(TriCanPair *pair, uint32_t id, const uint8_t *data, uint8_t length,
                uint32_t now, DualSample *sample);
int TriCan_MetaValid(const uint8_t *data, uint8_t length);
#endif
