#include "app_rtos.h"
#include "app_config.h"
#include "app_logic.h"
#include "air_sensor.h"
#include "can_link.h"
#include "dual_logic.h"
#include "FreeRTOS.h"
#include "key.h"
#include "led_effect.h"
#include "semphr.h"
#include "sht30.h"
#include "ssd1306.h"
#include "task.h"
#include "tri_can_logic.h"
#include "wifi_link.h"
#include <stdio.h>
#include <string.h>

#define SENSOR_TASK_STACK 512U
#define COMM_TASK_STACK 768U
#define DISPLAY_TASK_STACK 768U
#define HEALTH_TASK_STACK 256U
#define HEARTBEAT_TIMEOUT_MS 10000U

static StaticTask_t sensor_task_tcb, comm_task_tcb, display_task_tcb, health_task_tcb;
static StackType_t sensor_task_stack[SENSOR_TASK_STACK];
static StackType_t comm_task_stack[COMM_TASK_STACK];
static StackType_t display_task_stack[DISPLAY_TASK_STACK];
static StackType_t health_task_stack[HEALTH_TASK_STACK];
static StaticSemaphore_t state_mutex_buffer, wifi_mutex_buffer;
static SemaphoreHandle_t state_mutex, wifi_mutex;
static StaticTask_t idle_task_tcb;
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

static DualSample local_sample;
static DualSample can_sample;
static DualRemote wifi_remote;
static TriCanPair can_pair;
#if APP_BOARD_ID != APP_COLLECTOR_ID
static uint8_t can_sample_ready;
static uint32_t can_sample_at;
#endif
static uint32_t local_sequence;
static uint8_t sharing_enabled;
static uint8_t sharing_dirty = 1U;
static uint8_t display_dirty = 1U;
static uint8_t display_visible;
static uint8_t wifi_was_ready;
static volatile uint8_t sensor_heartbeat, comm_heartbeat, display_heartbeat;
static volatile uint32_t sensor_beat_at, comm_beat_at, display_beat_at;
static IWDG_HandleTypeDef watchdog;

static int LockState(void)
{
    return state_mutex != NULL && xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100U)) == pdPASS;
}

static void UnlockState(void)
{
    if (state_mutex != NULL) (void)xSemaphoreGive(state_mutex);
}

static int LockWiFi(void)
{
    return wifi_mutex != NULL && xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(100U)) == pdPASS;
}

static void UnlockWiFi(void)
{
    if (wifi_mutex != NULL) (void)xSemaphoreGive(wifi_mutex);
}

static void Beat(volatile uint8_t *heartbeat, volatile uint32_t *timestamp)
{
    *heartbeat = (uint8_t)(*heartbeat + 1U);
    *timestamp = HAL_GetTick();
}

static uint8_t Fresh(uint32_t now, uint32_t timestamp, uint32_t timeout)
{
    return (uint32_t)(now - timestamp) < timeout;
}

static void BuildLocalSample(uint16_t temperature_raw, uint16_t humidity_raw,
                             uint16_t light_adc, uint16_t co2_ppm,
                             uint16_t hcho_centi, uint8_t valid, uint32_t now)
{
    if (!LockState()) return;
    local_sequence++;
    local_sample.sequence = local_sequence;
    local_sample.uptime_ms = now;
    local_sample.temperature = (int32_t)(((int64_t)temperature_raw * 17500LL) / 65535LL - 4500LL);
    local_sample.humidity = (int32_t)(((int64_t)humidity_raw * 10000LL) / 65535LL);
    local_sample.light_adc = light_adc;
    local_sample.co2_ppm = co2_ppm;
    local_sample.hcho_centi = hcho_centi;
    local_sample.valid = valid;
    local_sample.sharing = sharing_enabled;
    display_dirty = 1U;
    UnlockState();
}

static void SensorTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    (void)argument;
    for (;;) {
        uint16_t temperature_raw = 0U, humidity_raw = 0U;
        uint16_t light = 0U, co2 = APP_CO2_MIN_PPM, hcho = 0U;
        uint8_t valid = 0U;
        if (SHT30_ReadRaw(&temperature_raw, &humidity_raw) == HAL_OK) valid |= 1U;
        if (Air_Read(&light, &co2, &hcho) == HAL_OK) valid |= 0x0EU;
        BuildLocalSample(temperature_raw, humidity_raw, light, co2, hcho, valid, HAL_GetTick());
        Beat(&sensor_heartbeat, &sensor_beat_at);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_SAMPLE_MS));
    }
}

static void SendWifiSample(void)
{
    DualSample sample;
    char frame[128];
    if (!LockState()) return;
    sample = local_sample;
    UnlockState();
    if (sample.sequence == 0U) return;
    if (!Dual_FormatSample(&sample, frame, sizeof(frame))) return;
    if (LockWiFi()) {
        (void)WiFi_Submit(frame);
        UnlockWiFi();
    }
}

static void ReadWifiLines(uint32_t now)
{
    char incoming[4][128];
    unsigned int count = 0U, index;
    if (LockWiFi()) {
        while (count < 4U && WiFi_ReadLine(incoming[count], sizeof(incoming[count]))) count++;
        UnlockWiFi();
    }
    for (index = 0U; index < count; index++) {
        DualRemote parsed;
        if (!Dual_Receive(&parsed, incoming[index], now)) continue;
        if (!LockState()) continue;
        if (!parsed.enabled) {
            memset(&wifi_remote, 0, sizeof(wifi_remote));
            wifi_remote.received_at = now;
#if APP_BOARD_ID != APP_COLLECTOR_ID
            can_sample_ready = 0U;
#endif
        } else {
            wifi_remote = parsed;
        }
        display_dirty = 1U;
        UnlockState();
    }
}

static void ProcessCan(uint32_t now)
{
#if APP_BOARD_ID != APP_COLLECTOR_ID
    uint32_t id;
    uint8_t data[8], length;
    DualSample sample;
    while (CanLink_Read(&id, data, &length)) {
        if (!TriCan_Pair(&can_pair, id, data, length, now, &sample)) continue;
        if (!LockState()) continue;
        can_sample_at = now;
        can_sample_ready = sample.sharing ? 1U : 0U;
        if (sample.sharing) can_sample = sample;
        display_dirty = 1U;
        UnlockState();
    }
#else
    (void)now;
#endif
}

static void CommTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t last_can_send = 0U, last_wifi_send = 0U;
    uint8_t have_local = 0U;
    (void)argument;
    for (;;) {
        uint32_t now = HAL_GetTick();
        uint8_t ready = 0U;
        if (LockWiFi()) {
            WiFi_Task(now);
            ready = WiFi_IsReady() ? 1U : 0U;
            UnlockWiFi();
        }
        if (ready && !wifi_was_ready) {
            char hello[40];
            (void)snprintf(hello, sizeof(hello), "HELLO,%u,%lu\r\n",
                           (unsigned int)APP_BOARD_ID, (unsigned long)now);
            if (LockWiFi()) {
                (void)WiFi_SubmitControl(hello);
                UnlockWiFi();
            }
        }
        wifi_was_ready = ready;
        if (ready && (uint32_t)(now - last_wifi_send) >= APP_SAMPLE_MS) {
            SendWifiSample();
            last_wifi_send = now;
        }
        if (ready) ReadWifiLines(now);
        if (APP_BOARD_ID == APP_COLLECTOR_ID) {
            DualSample sample;
            uint8_t data[TRI_CAN_DATA_LENGTH], meta[TRI_CAN_META_LENGTH];
            if (LockState()) {
                have_local = local_sample.sequence != 0U;
                sample = local_sample;
                UnlockState();
            }
            if (have_local && (sharing_dirty || (uint32_t)(now - last_can_send) >= APP_CAN_SEND_MS)) {
                sample.sharing = sharing_enabled;
                TriCan_Encode(&sample, data, meta);
                (void)CanLink_Send(APP_CAN_DATA_ID, data, sizeof(data));
                (void)CanLink_Send(APP_CAN_META_ID, meta, sizeof(meta));
                last_can_send = now;
                if (LockState()) {
                    sharing_dirty = 0U;
                    UnlockState();
                }
            }
        }
        ProcessCan(now);
        Beat(&comm_heartbeat, &comm_beat_at);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20U));
    }
}

static void ApplyKeyEvent(uint8_t event, uint32_t now)
{
    char control[32];
    uint8_t enabled;
    if (APP_BOARD_ID != APP_COLLECTOR_ID ||
        (event != APP_KEY_START && event != APP_KEY_STOP)) return;
    enabled = event == APP_KEY_START ? 1U : 0U;
    if (!LockState()) return;
    sharing_enabled = enabled;
    sharing_dirty = 1U;
    display_dirty = 1U;
    local_sample.sharing = enabled;
#if APP_BOARD_ID != APP_COLLECTOR_ID
    if (!enabled) can_sample_ready = 0U;
#endif
    UnlockState();
    (void)snprintf(control, sizeof(control), "CTRL,1,%u\r\n", (unsigned int)enabled);
    if (LockWiFi()) {
        if (WiFi_IsReady()) (void)WiFi_SubmitControl(control);
        UnlockWiFi();
    }
    (void)now;
}

static uint8_t KeyMask(void)
{
    uint8_t mask = 0U;
    if (KEY1 == 0U) mask |= APP_START_KEY_MASK;
    if (KEY2 == 0U) mask |= APP_STOP_KEY_MASK;
    return mask;
}

#if APP_BOARD_ID != APP_COLLECTOR_ID
static void ShowNoRemote(void)
{
    if (display_visible) {
        SSD1306_DisplayOff();
        display_visible = 0U;
    }
    LED_EffectSet(LED_EFFECT_OFF);
}
#endif

static void ShowWait(void)
{
    if (display_visible) return;
    SSD1306_DisplayOn();
    display_visible = 1U;
    SSD1306_Clear();
    if (APP_BOARD_ID == APP_COLLECTOR_ID) {
        SSD1306_Text(0U, 0U, "BOARD 1 STARTING");
        SSD1306_Text(0U, 2U, "WAIT FOR SENSOR");
    } else {
        SSD1306_Text(0U, 0U, "REMOTE BOARD 1");
        SSD1306_Text(0U, 2U, "WAIT FOR SAMPLE");
    }
    SSD1306_Flush();
    LED_EffectSet(LED_EFFECT_OFF);
}

static void ShowSample(const DualSample *sample, const char *source)
{
    SSD1306_DisplayOn();
    display_visible = 1U;
    SSD1306_Clear();
    SSD1306_Text(0U, 0U, source);
    SSD1306_Value(78U, 0U, (int32_t)sample->sequence, 0U, "");
    SSD1306_Value(0U, 1U, sample->temperature, 2U, "C ");
    SSD1306_Value(64U, 1U, sample->humidity, 2U, "%");
    SSD1306_Value(0U, 2U, (int32_t)sample->light_adc, 0U, " ADC");
    SSD1306_Value(0U, 3U, (int32_t)sample->co2_ppm, 0U, " ppm");
    SSD1306_Value(0U, 4U, (int32_t)sample->hcho_centi, 2U, " mg/m3");
    SSD1306_Text(0U, 5U, (sample->valid & 1U) ? "TH:OK" : "TH:ERR");
    SSD1306_Text(48U, 5U, (sample->valid & 2U) ? "LIGHT:OK" : "LIGHT:ERR");
    SSD1306_Text(0U, 6U, (sample->valid & 4U) ? "CO2:OK" : "CO2:ERR");
    SSD1306_Text(48U, 6U, (sample->valid & 8U) ? "HCHO:OK" : "HCHO:ERR");
    SSD1306_Text(0U, 7U, sample->sharing ? "SHARE:ON" : "SHARE:OFF");
    SSD1306_Flush();
    if (APP_BOARD_ID == APP_COLLECTOR_ID) LED_EffectSet(LED_EFFECT_OFF);
    else LED_EffectSet(LED_EFFECT_BREATH);
}

static void RenderDisplay(uint32_t now)
{
#if APP_BOARD_ID == APP_COLLECTOR_ID
    DualSample local;
#else
    DualSample remote_sample;
    DualRemote remote;
    uint8_t can_ready;
    uint32_t can_at;
#endif
    uint8_t dirty = 0U;
    char source[16];

    if (!LockState()) return;
#if APP_BOARD_ID == APP_COLLECTOR_ID
    local = local_sample;
#else
    can_ready = can_sample_ready;
    can_at = can_sample_at;
    remote = wifi_remote;
    remote_sample = wifi_remote.sample;
    if (can_ready && !Fresh(now, can_at, APP_CAN_LIVE_TIMEOUT_MS)) {
        can_ready = 0U;
        can_sample_ready = 0U;
        display_dirty = 1U;
    }
    if ((wifi_remote.enabled || wifi_remote.has_sample) && Dual_Expire(&wifi_remote, now)) {
        display_dirty = 1U;
        remote = wifi_remote;
        remote_sample = wifi_remote.sample;
    }
#endif
    dirty = display_dirty;
    display_dirty = 0U;
    UnlockState();

#if APP_BOARD_ID == APP_COLLECTOR_ID
    if (local.sequence == 0U) {
        if (dirty) ShowWait();
        return;
    }
    (void)snprintf(source, sizeof(source), "B%u LOCAL #", (unsigned int)APP_BOARD_ID);
    ShowSample(&local, source);
#else
    if (can_ready) {
        (void)snprintf(source, sizeof(source), "B1 CAN #");
        ShowSample(&can_sample, source);
        return;
    }
    if (remote.enabled && remote.has_sample && remote_sample.sharing) {
        (void)snprintf(source, sizeof(source), "B1 WIFI #");
        ShowSample(&remote_sample, source);
        return;
    }
    if (remote.enabled) {
        if (dirty) ShowWait();
        return;
    }
    ShowNoRemote();
#endif
}

static void DisplayTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    AppKeys keys = {0U, 0U, 0U};
    (void)argument;
    for (;;) {
        uint32_t now = HAL_GetTick();
        uint8_t event = App_Debounce(&keys, KeyMask(), now);
        if (event == APP_KEY_START || event == APP_KEY_STOP) ApplyKeyEvent(event, now);
        RenderDisplay(now);
        Beat(&display_heartbeat, &display_beat_at);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_DISPLAY_REFRESH_MS));
    }
}

static void HealthTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    (void)argument;
    for (;;) {
        uint32_t now = HAL_GetTick();
        if (Fresh(now, sensor_beat_at, HEARTBEAT_TIMEOUT_MS) &&
            Fresh(now, comm_beat_at, HEARTBEAT_TIMEOUT_MS) &&
            Fresh(now, display_beat_at, HEARTBEAT_TIMEOUT_MS)) {
            (void)HAL_IWDG_Refresh(&watchdog);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(250U));
    }
}

void App_RtosStart(void)
{
    TaskHandle_t handle;
    state_mutex = xSemaphoreCreateMutexStatic(&state_mutex_buffer);
    wifi_mutex = xSemaphoreCreateMutexStatic(&wifi_mutex_buffer);
    configASSERT(state_mutex != NULL);
    configASSERT(wifi_mutex != NULL);
    memset(&local_sample, 0, sizeof(local_sample));
    memset(&can_sample, 0, sizeof(can_sample));
    memset(&wifi_remote, 0, sizeof(wifi_remote));
    memset(&can_pair, 0, sizeof(can_pair));
    local_sequence = 0U;
    sharing_enabled = 0U;
    sharing_dirty = 1U;
    display_dirty = 1U;
    display_visible = 0U;
    wifi_was_ready = 0U;
    sensor_heartbeat = comm_heartbeat = display_heartbeat = 0U;
    sensor_beat_at = comm_beat_at = display_beat_at = HAL_GetTick();

    handle = xTaskCreateStatic(SensorTask, "sensor", SENSOR_TASK_STACK, NULL, 3U,
                               sensor_task_stack, &sensor_task_tcb);
    configASSERT(handle != NULL);
    handle = xTaskCreateStatic(CommTask, "comm", COMM_TASK_STACK, NULL, 4U,
                               comm_task_stack, &comm_task_tcb);
    configASSERT(handle != NULL);
    handle = xTaskCreateStatic(DisplayTask, "display", DISPLAY_TASK_STACK, NULL, 2U,
                               display_task_stack, &display_task_tcb);
    configASSERT(handle != NULL);
    handle = xTaskCreateStatic(HealthTask, "health", HEALTH_TASK_STACK, NULL, 1U,
                               health_task_stack, &health_task_tcb);
    configASSERT(handle != NULL);

#if APP_WATCHDOG_ENABLED
    watchdog.Instance = IWDG;
    watchdog.Init.Prescaler = IWDG_PRESCALER_64;
    watchdog.Init.Reload = 1250U;
    configASSERT(HAL_IWDG_Init(&watchdog) == HAL_OK);
#else
    (void)watchdog;
#endif
    vTaskStartScheduler();
    App_RtosFault();
}

void App_RtosFault(void)
{
    __disable_irq();
    HAL_GPIO_WritePin(APP_LED0_GPIO, APP_LED0_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(APP_LED1_GPIO, APP_LED1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(APP_LED2_GPIO, APP_LED2_PIN, GPIO_PIN_SET);
    for (;;) { }
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *size)
{
    *tcb = &idle_task_tcb;
    *stack = idle_task_stack;
    *size = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    App_RtosFault();
}

void vApplicationIdleHook(void)
{
}

void vApplicationMallocFailedHook(void)
{
    App_RtosFault();
}
