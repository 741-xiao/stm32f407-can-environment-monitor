#ifndef APP_LOGIC_H
#define APP_LOGIC_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t running, alarm, valid;
    int32_t temperature, humidity, threshold; /* hundredths */
    uint32_t samples;
} AppState;

typedef struct {
    uint8_t candidate, stable;
    uint32_t changed_at;
} AppKeys;

enum { APP_KEY_NONE, APP_KEY_START, APP_KEY_STOP, APP_KEY_ERROR };
void App_Init(AppState *app);
uint8_t App_Key(AppState *app, uint8_t mask);
uint8_t App_Debounce(AppKeys *keys, uint8_t mask, uint32_t now);
int App_Sample(AppState *app, uint16_t temperature_raw, uint16_t humidity_raw);
void App_SensorError(AppState *app);
int App_SetThreshold(AppState *app, const char *command);
void App_FormatValue(char *out, size_t capacity, int32_t value);
int App_FormatFrame(const AppState *app, char *out, size_t capacity);
#endif
