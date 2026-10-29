#include "app_logic.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>

static void UpdateAlarm(AppState *app)
{
    if (!app->running || !app->valid) app->alarm = 0U;
    else if (app->temperature > app->threshold) app->alarm = 1U;
    else if (app->temperature < app->threshold) app->alarm = 0U;
    /* Equality keeps the previous alarm state. */
}

void App_Init(AppState *app)
{
    memset(app, 0, sizeof(*app));
    app->threshold = APP_DEFAULT_THRESHOLD_CENTI;
}

uint8_t App_Key(AppState *app, uint8_t mask)
{
    if (!mask) return APP_KEY_NONE;
    if (mask == APP_START_KEY_MASK) {
        if (!app->running) app->valid = 0U;
        app->running = 1U;
        return APP_KEY_START;
    }
    if (mask == APP_STOP_KEY_MASK) {
        app->running = 0U;
        app->alarm = 0U;
        return APP_KEY_STOP;
    }
    return APP_KEY_ERROR;
}

uint8_t App_Debounce(AppKeys *keys, uint8_t mask, uint32_t now)
{
    if (mask != keys->candidate) {
        keys->candidate = mask;
        keys->changed_at = now;
    }
    if ((uint32_t)(now - keys->changed_at) >= APP_KEY_DEBOUNCE_MS &&
        keys->stable != keys->candidate) {
        keys->stable = keys->candidate;
        return keys->stable;
    }
    return 0U;
}

int App_Sample(AppState *app, uint16_t temperature_raw, uint16_t humidity_raw)
{
    int32_t temperature;
    if (!app->running) return 0;
    temperature = (17500L * temperature_raw + 32767L) / 65535L - 4500L;
    if (temperature < -4000 || temperature > 12500) {
        App_SensorError(app);
        return 0;
    }
    app->temperature = temperature;
    app->humidity = (10000L * humidity_raw + 32767L) / 65535L;
    app->valid = 1U;
    app->samples++;
    UpdateAlarm(app);
    return 1;
}

void App_SensorError(AppState *app)
{
    app->valid = 0U;
    app->alarm = 0U;
}

int App_SetThreshold(AppState *app, const char *command)
{
    const char *p;
    int32_t value = 0, fraction = 0;
    int sign = 1, digits = 0, decimals = 0;
    if (strncmp(command, "TH=", 3U) != 0) return 0;
    p = command + 3;
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') {
        if (++digits > 3) return 0;
        value = value * 10 + (*p++ - '0');
    }
    if (!digits) return 0;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            if (++decimals > 2) return 0;
            fraction = fraction * 10 + (*p++ - '0');
        }
        if (!decimals) return 0;
    }
    if (*p != '\0') return 0;
    if (decimals == 1) fraction *= 10;
    value = sign * (value * 100 + fraction);
    if (value < -4000 || value > 12500) return 0;
    app->threshold = value;
    UpdateAlarm(app);
    return 1;
}

void App_FormatValue(char *out, size_t capacity, int32_t value)
{
    uint32_t absolute = value < 0 ? (uint32_t)(-value) : (uint32_t)value;
    (void)snprintf(out, capacity, "%s%lu.%02lu", value < 0 ? "-" : "",
                   (unsigned long)(absolute / 100U), (unsigned long)(absolute % 100U));
}

int App_FormatFrame(const AppState *app, char *out, size_t capacity)
{
    char temperature[12], humidity[12], threshold[12];
    int length;
    if (!app->running || !app->valid) return 0;
    App_FormatValue(temperature, sizeof(temperature), app->temperature);
    App_FormatValue(humidity, sizeof(humidity), app->humidity);
    App_FormatValue(threshold, sizeof(threshold), app->threshold);
    length = snprintf(out, capacity, "SHT30,%s,%s,%s,%u\r\n",
                      temperature, humidity, threshold, (unsigned int)app->alarm);
    return length > 0 && (size_t)length < capacity ? length : 0;
}
