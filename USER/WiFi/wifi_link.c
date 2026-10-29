#include "wifi_link.h"
#include "serial.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>

typedef enum { BOOT, RESET, ECHO, MODE, JOIN, MUX, NORMAL,
               CONNECT, READY, PROMPT, SENDING, RETRY } WiFiState;
static WiFiState state;
static uint32_t started, timeout, sent;
static char response[512], pending[128], active[128];
static unsigned int used;
static char controls[4][64], lines[4][128], payload_line[128];
static unsigned int control_head, control_tail, line_head, line_tail, payload_used;
static uint32_t payload_remaining, payload_length;
static uint8_t ipd_header, ipd_digits, ipd_match;

static void ResetBuffers(void)
{
    control_head = control_tail = line_head = line_tail = payload_used = 0U;
    payload_remaining = payload_length = 0U;
    ipd_header = ipd_digits = ipd_match = 0U;
}

static void ClearResponse(void) { used = 0U; response[0] = '\0'; }
static int Has(const char *word) { return strstr(response, word) != NULL; }
static void Retry(uint32_t now)
{
    state = RETRY;
    started = now;
    timeout = 5000U;
    pending[0] = active[0] = '\0';
    ResetBuffers();
    ClearResponse();
}
static void Command(const char *command, WiFiState next, uint32_t wait, uint32_t now)
{
    ClearResponse();
    state = next;
    started = now;
    timeout = wait;
    if (!Serial_Write(SERIAL_WIFI, command)) Retry(now);
}
void WiFi_Init(uint32_t now)
{
    state = BOOT;
    started = now;
    timeout = 1500U;
    sent = 0U;
    pending[0] = active[0] = '\0';
    ResetBuffers();
    ClearResponse();
}
int WiFi_IsReady(void) { return state == READY || state == PROMPT || state == SENDING; }
uint32_t WiFi_SentCount(void) { return sent; }
void WiFi_CancelPending(void) { pending[0] = '\0'; }
int WiFi_Submit(const char *frame)
{
    size_t length = strlen(frame);
    if (!WiFi_IsReady() || !length || length >= sizeof(pending)) return 0;
    memcpy(pending, frame, length + 1U); /* Bounded latest sample, no offline backlog. */
    return 1;
}

int WiFi_SubmitControl(const char *frame)
{
    unsigned int next = (control_head + 1U) % 4U;
    size_t length = strlen(frame);
    if (!WiFi_IsReady() || !length || length >= sizeof(controls[0]) || next == control_tail) return 0;
    memcpy(controls[control_head], frame, length + 1U);
    control_head = next;
    return 1;
}

int WiFi_ReadLine(char *out, unsigned int capacity)
{
    size_t length;
    if (line_head == line_tail) return 0;
    length = strlen(lines[line_tail]);
    if (length + 1U > capacity) return 0;
    memcpy(out, lines[line_tail], length + 1U);
    line_tail = (line_tail + 1U) % 4U;
    return 1;
}

static int ResponseByte(uint8_t byte)
{
    if ((byte < 32U && byte != '\r' && byte != '\n') || byte > 126U) return 1;
    if (used + 1U >= sizeof(response)) return 0;
    response[used++] = (char)byte;
    response[used] = '\0';
    return 1;
}

static int ReceiveByte(uint8_t byte)
{
    unsigned int next;
    uint8_t index;
    static const char prefix[] = "+IPD,";
    if (payload_remaining) {
        payload_remaining--;
        if (byte == '\n') {
            if (payload_used) {
                next = (line_head + 1U) % 4U;
                if (next == line_tail) return 0;
                payload_line[payload_used] = '\0';
                strcpy(lines[line_head], payload_line);
                line_head = next;
            }
            payload_used = 0U;
        } else if (byte != '\r') {
            if (byte < 32U || byte > 126U || payload_used + 1U >= sizeof(payload_line)) return 0;
            payload_line[payload_used++] = (char)byte;
        }
        return 1;
    }
    if (ipd_header) {
        if (byte >= '0' && byte <= '9' && ipd_digits < 5U) {
            payload_length = payload_length * 10U + byte - '0';
            ipd_digits++;
        } else if (byte == ':' && ipd_digits && payload_length && payload_length <= 4096U) {
            payload_remaining = payload_length;
            ipd_header = 0U;
        } else return 0;
        return 1;
    }
    if (byte == (uint8_t)prefix[ipd_match]) {
        if (++ipd_match == 5U) {
            ipd_match = 0U;
            ipd_header = 1U;
            ipd_digits = 0U;
            payload_length = 0U;
        }
        return 1;
    }
    for (index = 0U; index < ipd_match; index++) {
        if (!ResponseByte((uint8_t)prefix[index])) return 0;
    }
    ipd_match = 0U;
    if (byte == '+') { ipd_match = 1U; return 1; }
    return ResponseByte(byte);
}

void WiFi_Task(uint32_t now)
{
    uint8_t byte;
    char command[256];
    int length;
    if (Serial_TakeError(SERIAL_WIFI)) {
        /* ESP8266 ROM boot text uses 74880 baud. Ignore framing errors while
           resetting; wait for the AT firmware's 115200-baud ready line. */
        ClearResponse();
        if (state != BOOT && state != RESET && state != RETRY) { Retry(now); return; }
    }
    while (Serial_Read(SERIAL_WIFI, &byte)) {
        if (!ReceiveByte(byte)) { Retry(now); return; }
    }
    if (state == BOOT || state == RETRY) {
        ClearResponse();
        if ((uint32_t)(now - started) >= timeout)
            Command("AT+RST\r\n", RESET, 5000U, now);
        return;
    }
    /* Asynchronous link loss must take precedence over an OK in the same burst. */
    if (Has("ERROR") || Has("FAIL") || Has("CLOSED") || Has("busy") ||
        (state != RESET && (Has("WIFI DISCONNECT") || Has("ready\r\n")))) { Retry(now); return; }
    if (state == READY) {
        if (control_head != control_tail || pending[0]) {
            if (control_head != control_tail) {
                strcpy(active, controls[control_tail]);
                control_tail = (control_tail + 1U) % 4U;
            } else {
                strcpy(active, pending);
                pending[0] = '\0';
            }
            (void)snprintf(command, sizeof(command), "AT+CIPSEND=%u\r\n", (unsigned int)strlen(active));
            Command(command, PROMPT, 2000U, now);
        }
        return;
    }
    if (state == PROMPT && Has(">")) {
        Command(active, SENDING, 5000U, now);
        return;
    }
    if (state == SENDING && Has("SEND OK")) {
        sent++;
        state = READY;
        ClearResponse();
        return;
    }
    if ((state == RESET && Has("ready\r\n")) ||
        (state >= ECHO && state <= CONNECT && Has("\r\nOK\r\n"))) {
        switch (state) {
        case RESET: Command("ATE0\r\n", ECHO, 2000U, now); break;
        case ECHO: Command("AT+CWMODE=1\r\n", MODE, 2000U, now); break;
        case MODE:
            length = snprintf(command, sizeof(command), "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
            if (length <= 0 || (size_t)length >= sizeof(command)) Retry(now);
            else Command(command, JOIN, 20000U, now);
            break;
        case JOIN: Command("AT+CIPMUX=0\r\n", MUX, 2000U, now); break;
        case MUX: Command("AT+CIPMODE=0\r\n", NORMAL, 2000U, now); break;
        case NORMAL:
            length = snprintf(command, sizeof(command), "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                              WIFI_SERVER_IP, (unsigned int)WIFI_SERVER_PORT);
            if (length <= 0 || (size_t)length >= sizeof(command)) Retry(now);
            else Command(command, CONNECT, 10000U, now);
            break;
        case CONNECT: state = READY; ClearResponse(); break;
        default: break;
        }
        return;
    }
    if ((uint32_t)(now - started) >= timeout) Retry(now);
}
