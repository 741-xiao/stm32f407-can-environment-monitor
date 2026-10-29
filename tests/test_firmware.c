#include "app_logic.h"
#include "wifi_link.h"
#include "serial.h"
#include "dual_logic.h"
#include "tri_can_logic.h"
#include "app_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char incoming[2048], outgoing[256];
static size_t cursor;
static int serial_error;
int Serial_Read(uint8_t port, uint8_t *byte)
{
    (void)port;
    if (!incoming[cursor]) return 0;
    *byte = (uint8_t)incoming[cursor++];
    return 1;
}
int Serial_Write(uint8_t port, const char *text)
{
    assert(port == SERIAL_WIFI);
    assert(strlen(text) < sizeof(outgoing));
    strcpy(outgoing, text);
    return 1;
}
int Serial_TakeError(uint8_t port)
{
    int error = serial_error;
    (void)port;
    serial_error = 0;
    return error;
}
static void Reply(const char *response, uint32_t now)
{
    assert(strlen(response) < sizeof(incoming));
    strcpy(incoming, response);
    cursor = 0;
    outgoing[0] = '\0';
    WiFi_Task(now);
}
static uint16_t Raw(int32_t centi)
{
    return (uint16_t)(((centi + 4500L) * 65535L + 8750L) / 17500L);
}
static void ApplicationTests(void)
{
    AppState app;
    AppKeys keys = {0};
    char frame[96], value[16];
    unsigned int key;
    App_Init(&app);
    assert(!app.running && !app.alarm && app.threshold == 2600);
    assert(!App_Sample(&app, Raw(3000), 32768));
    assert(!App_FormatFrame(&app, frame, sizeof(frame)));
    assert(App_Key(&app, 2) == APP_KEY_START);
    assert(App_Sample(&app, Raw(2601), 32768) && app.alarm);
    assert(App_Sample(&app, Raw(2600), 32768) && app.alarm);
    assert(App_Sample(&app, Raw(2599), 32768) && !app.alarm);
    assert(App_Sample(&app, Raw(2600), 32768) && !app.alarm);
    assert(App_SetThreshold(&app, "TH=25.00") && app.alarm);
    assert(App_SetThreshold(&app, "TH=28") && !app.alarm);
    assert(App_SetThreshold(&app, "TH=-0.25") && app.threshold == -25);
    assert(!App_SetThreshold(&app, "TH=NaN"));
    assert(!App_SetThreshold(&app, "TH=26.001"));
    assert(!App_SetThreshold(&app, "TH=26garbage"));
    assert(!App_SetThreshold(&app, "TH=125.01"));
    assert(!App_SetThreshold(&app, "TH=-40.01"));
    assert(!App_SetThreshold(&app, "TH=9999999999999"));
    assert(app.threshold == -25);
    assert(App_SetThreshold(&app, "TH=26.00"));
    for (key = 1; key < 16; key++) {
        if (key == 2 || key == 4) continue;
        assert(App_Key(&app, (uint8_t)key) == APP_KEY_ERROR && app.running);
    }
    assert(App_Sample(&app, Raw(-25), 32768));
    assert(App_FormatFrame(&app, frame, sizeof(frame)));
    assert(strcmp(frame, "SHT30,-0.25,50.00,26.00,0\r\n") == 0);
    assert(!App_FormatFrame(&app, frame, 8));
    App_FormatValue(value, sizeof(value), -1);
    assert(strcmp(value, "-0.01") == 0);
    assert(App_Sample(&app, Raw(3000), 32768) && app.alarm);
    assert(App_Key(&app, 4) == APP_KEY_STOP && !app.running && !app.alarm);
    assert(!App_Sample(&app, Raw(3000), 32768));
    assert(!App_FormatFrame(&app, frame, sizeof(frame)));
    App_Key(&app, 2);
    assert(!app.valid);
    assert(App_Sample(&app, Raw(3000), 32768));
    App_SensorError(&app);
    assert(!app.valid && !app.alarm && !App_FormatFrame(&app, frame, sizeof(frame)));
    assert(!App_Sample(&app, 0, 32768)); /* Outside SHT30 rated range. */
    assert(!App_Debounce(&keys, 2, 100));
    assert(!App_Debounce(&keys, 0, 105));
    assert(!App_Debounce(&keys, 2, 110));
    assert(!App_Debounce(&keys, 2, 129));
    assert(App_Debounce(&keys, 2, 130) == 2);
    assert(!App_Debounce(&keys, 2, 30000)); /* Holding fires once. */
    App_Debounce(&keys, 0, 30001);
    App_Debounce(&keys, 0, 30021);
    App_Debounce(&keys, 4, 0xFFFFFFF0U);
    assert(App_Debounce(&keys, 4, 4U) == 4); /* Tick rollover. */
    puts("PASS: start/stop, invalid keys, debounce, threshold, alarm boundaries, sensor errors, wire format");
}
static void WiFiTests(void)
{
    const char *frame = "SHT30,27.50,60.00,26.00,1\r\n";
    WiFi_Init(0);
    assert(!WiFi_Submit(frame));
    Reply("", 1500); assert(strcmp(outgoing, "AT+RST\r\n") == 0);
    serial_error = 1; /* Boot ROM switches baud rates before AT firmware starts. */
    Reply("", 1550); assert(!outgoing[0]);
    serial_error = 1; /* A valid ready line can already be in the ring buffer. */
    Reply("\r\nready\r\n", 1600); assert(strcmp(outgoing, "ATE0\r\n") == 0);
    Reply("\r\nO", 1610); assert(!outgoing[0]);
    Reply("K\r\n", 1620); assert(strcmp(outgoing, "AT+CWMODE=1\r\n") == 0);
    Reply("\r\nOK\r\n", 1630); assert(strstr(outgoing, "AT+CWJAP="));
    Reply("WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n", 1640);
    assert(strcmp(outgoing, "AT+CIPMUX=0\r\n") == 0);
    Reply("\r\nOK\r\n", 1650); assert(strcmp(outgoing, "AT+CIPMODE=0\r\n") == 0);
    Reply("\r\nOK\r\n", 1660); assert(strstr(outgoing, "AT+CIPSTART=\"TCP\""));
    Reply("CONNECT\r\n\r\nOK\r\n", 1670); assert(WiFi_IsReady());
    assert(WiFi_Submit(frame));
    Reply("", 1680); assert(strstr(outgoing, "AT+CIPSEND="));
    Reply("\r\nOK\r\n> ", 1690); assert(strcmp(outgoing, frame) == 0);
    assert(WiFi_SentCount() == 0);
    Reply("Recv 28 bytes\r\nSEND ", 1700); assert(!WiFi_SentCount());
    Reply("OK\r\n", 1710); assert(WiFi_SentCount() == 1);
    assert(WiFi_Submit(frame));
    WiFi_CancelPending();
    Reply("", 1720); assert(!outgoing[0]);
    Reply("CLO", 1730); assert(WiFi_IsReady());
    Reply("SED\r\n", 1740); assert(!WiFi_IsReady());
    Reply("", 6740); assert(strcmp(outgoing, "AT+RST\r\n") == 0);
    Reply("", 11740); assert(!WiFi_IsReady()); /* AT timeout doesn't block caller. */
    Reply("", 16740); assert(strcmp(outgoing, "AT+RST\r\n") == 0);
    Reply("\r\nready\r\n", 16741);
    serial_error = 1;
    Reply("", 16742); assert(!WiFi_IsReady());
    puts("PASS: AT connection, split replies, CIPSEND, cancel pending, disconnect, timeout, UART overflow");
}
static void DualTests(void)
{
    DualRemote remote = {0};
    DualSample sample = {1U, 2000U, -25, 6000, 2048U, 812U, 27U, 15U, 1U};
    char line[128], control[32], expected[128];
    (void)snprintf(control, sizeof(control), "CTRL,%u,1", (unsigned int)APP_COLLECTOR_ID);
    assert(Dual_Receive(&remote, control, 100U) && remote.enabled && !remote.sample.valid);
    (void)snprintf(line, sizeof(line), "DATA,%u,42,2345,-25,6000,2048,812,27,15,1", (unsigned int)APP_COLLECTOR_ID);
    assert(Dual_Receive(&remote, line, 200U) && remote.sample.temperature == -25);
    assert(remote.sample.sequence == 42U && remote.sample.valid == 15U && remote.enabled);
    assert(remote.sample.co2_ppm == 812U && remote.sample.hcho_centi == 27U);
    assert(!Dual_Receive(&remote, "DATA,3,42,2345,2500,6000,2048,812,27,15,1", 201U));
    assert(!Dual_Receive(&remote, "DATA,1,42,2345,2500,6000,2048,5001,27,15,1", 201U));
    assert(!Dual_Receive(&remote, "DATA,1,42,2345,2500,6000,2048,812,1001,15,1", 201U));
    assert(!Dual_Receive(&remote, "DATA,1,42,2345,9999999999999999999,6000,2048,812,27,15,1", 201U));
    assert(Dual_Receive(&remote, "DATA,1,43,2346,2500,6000,2048,812,27,15,1", 202U));
    remote.received_at = 8198U;
    assert(!Dual_Expire(&remote, 8199U));
    assert(!Dual_Expire(&remote, 16197U));
    assert(Dual_Expire(&remote, 16198U) && !remote.enabled && !remote.sample.valid);
    assert(Dual_Receive(&remote, line, 0xFFFFFFF0U));
    assert(Dual_Expire(&remote, 7984U));
    (void)snprintf(control, sizeof(control), "CTRL,%u,0", (unsigned int)APP_COLLECTOR_ID);
    assert(Dual_Receive(&remote, control, 100U) && !remote.enabled);
    assert(Dual_FormatSample(&sample, line, sizeof(line)));
    (void)snprintf(expected, sizeof(expected), "DATA,%u,1,2000,-25,6000,2048,812,27,15,1\r\n", (unsigned int)APP_BOARD_ID);
    assert(strcmp(line, expected) == 0);
    assert(!Dual_FormatSample(&sample, line, 4U));
    assert(Dual_LightPercent(4095U) == 0U && Dual_LightPercent(0U) == 10000U);
    puts("PASS: V1/V2 parsing, board identity, remote start/stop, invalid frames, timeout, rollover");
}

static void ConnectWiFi(void)
{
    WiFi_Init(0U);
    Reply("", 1500U);
    Reply("ready\r\n", 1501U);
    Reply("\r\nOK\r\n", 1502U);
    Reply("\r\nOK\r\n", 1503U);
    Reply("\r\nOK\r\n", 1504U);
    Reply("\r\nOK\r\n", 1505U);
    Reply("\r\nOK\r\n", 1506U);
    Reply("\r\nOK\r\n", 1507U);
    assert(WiFi_IsReady());
}

static void WiFiReceiveTests(void)
{
    char line[128];
    ConnectWiFi();
    assert(WiFi_Submit("DATA,1,1,2000,2500,6000,2048,3,1\r\n"));
    assert(WiFi_SubmitControl("CTRL,1,0\r\n"));
    Reply("+IP", 1510U);
    assert(strcmp(outgoing, "AT+CIPSEND=10\r\n") == 0);
    Reply("D,10:CTRL,2", 1511U);
    Reply(",1\r\n\r\nOK\r\n>", 1512U);
    assert(strcmp(outgoing, "CTRL,1,0\r\n") == 0);
    assert(WiFi_ReadLine(line, sizeof(line)) && strcmp(line, "CTRL,2,1") == 0);
    Reply("+IPD,7:ERROR\r\nSEND OK\r\n", 1513U);
    assert(WiFi_IsReady());
    assert(WiFi_ReadLine(line, sizeof(line)) && strcmp(line, "ERROR") == 0);
    Reply("+IPD,5:CTRL,+IPD,5:2,0\r\n", 1514U);
    assert(WiFi_ReadLine(line, sizeof(line)) && strcmp(line, "CTRL,2,0") == 0);
    Reply("+IPD,20:CTRL,2,1\r\nCTRL,2,0\r\n", 1515U);
    assert(WiFi_ReadLine(line, sizeof(line)) && strcmp(line, "CTRL,2,1") == 0);
    assert(WiFi_ReadLine(line, sizeof(line)) && strcmp(line, "CTRL,2,0") == 0);
    assert(!WiFi_ReadLine(line, sizeof(line)));
    Reply("+IPD,99999:", 1516U);
    assert(!WiFi_IsReady());
    puts("PASS: priority controls, split IPD header/payload, TCP fragmentation/coalescing, isolated AT replies");
}

static void CanTests(void)
{
    TriCanPair pair;
    DualSample sample = {1U, 2000U, -25, 6000, 2048U, 812U, 27U, 15U, 1U};
    DualSample decoded;
    uint8_t data[TRI_CAN_DATA_LENGTH], meta[TRI_CAN_META_LENGTH];

    memset(&pair, 0, sizeof(pair));
    TriCan_Encode(&sample, data, meta);
    assert(data[0] == 0xE7U && data[1] == 0xFFU); /* -25 little endian */
    assert(TriCan_MetaValid(meta, sizeof(meta)));
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 100U, &decoded));
    assert(TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 150U, &decoded));
    assert(decoded.temperature == -25 && decoded.humidity == 6000);
    assert(decoded.light_adc == 2048U && decoded.co2_ppm == 812U && decoded.hcho_centi == 27U);
    assert(decoded.valid == 15U && decoded.sharing == 1U && decoded.sequence == 1U);

    memset(&pair, 0, sizeof(pair));
    assert(!TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 100U, &decoded));
    assert(TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 150U, &decoded));
    assert(decoded.temperature == -25);

    memset(&pair, 0, sizeof(pair));
    assert(!TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 100U, &decoded));
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 201U, &decoded));

    memset(&pair, 0, sizeof(pair));
    TriCan_Encode(&sample, data, meta);
    meta[6] = 0x00U;
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 150U, &decoded));
    TriCan_Encode(&sample, data, meta);
    meta[7] = 0x01U;
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 150U, &decoded));
    TriCan_Encode(&sample, data, meta);
    meta[4] = 2U;
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 150U, &decoded));
    TriCan_Encode(&sample, data, meta);
    meta[2] = 0x10U;
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 150U, &decoded));
    TriCan_Encode(&sample, data, meta);
    meta[3] = 2U;
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 150U, &decoded));
    TriCan_Encode(&sample, data, meta);
    meta[0] = 0xE9U; meta[1] = 0x03U; /* 1001 centi */
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 150U, &decoded));

    memset(&pair, 0, sizeof(pair));
    TriCan_Encode(&sample, data, meta);
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 100U, &decoded));
    data[2] = 0x11U; data[3] = 0x27U; /* 10001 > 10000 */
    assert(!TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 150U, &decoded));

    memset(&pair, 0, sizeof(pair));
    TriCan_Encode(&sample, data, meta);
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 100U, &decoded));
    data[0] = 0xD5U; data[1] = 0x30U; /* 12501 > 12500 */
    assert(!TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 150U, &decoded));
    TriCan_Encode(&sample, data, meta);
    data[6] = 0x89U; data[7] = 0x13U; /* CO2 5001 > 5000 */
    assert(!TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 180U, &decoded));

    memset(&pair, 0, sizeof(pair));
    TriCan_Encode(&sample, data, meta);
    meta[5] = 0xFEU;
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 100U, &decoded));
    assert(TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 150U, &decoded));
    assert(decoded.sequence == 254U);
    assert(!TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 180U, &decoded));
    meta[5] = 0x02U;
    assert(TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 200U, &decoded));
    assert(decoded.sequence == 258U);

    meta[3] = 0U; meta[5] = 0x01U;
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 300U, &decoded));
    assert(TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 310U, &decoded));
    assert(decoded.sequence == 257U && decoded.sharing == 0U);

    memset(&pair, 0, sizeof(pair));
    TriCan_Encode(&sample, data, meta);
    meta[5] = 0x80U;
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 100U, &decoded));
    assert(TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 150U, &decoded));
    assert(decoded.sequence == 128U);
    meta[5] = 0x7FU;
    assert(!TriCan_Pair(&pair, APP_CAN_META_ID, meta, sizeof(meta), 200U, &decoded));
    assert(TriCan_Pair(&pair, APP_CAN_DATA_ID, data, sizeof(data), 250U, &decoded));
    assert(decoded.sequence == 127U);
    puts("PASS: CAN encode/pair/out-of-order/timeout, META magic/version/board/range, sequence assembly");
}

int main(void) { ApplicationTests(); WiFiTests(); DualTests(); WiFiReceiveTests(); CanTests(); return 0; }
