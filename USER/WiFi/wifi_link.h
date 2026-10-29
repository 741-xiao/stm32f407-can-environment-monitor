#ifndef WIFI_LINK_H
#define WIFI_LINK_H
#include <stdint.h>
void WiFi_Init(uint32_t now);
void WiFi_Task(uint32_t now);
int WiFi_IsReady(void);
int WiFi_Submit(const char *frame);
int WiFi_SubmitControl(const char *frame);
int WiFi_ReadLine(char *out, unsigned int capacity);
void WiFi_CancelPending(void);
uint32_t WiFi_SentCount(void);
#endif
