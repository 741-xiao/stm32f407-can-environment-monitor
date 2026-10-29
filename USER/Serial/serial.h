#ifndef APP_SERIAL_H
#define APP_SERIAL_H
#include <stdint.h>
enum { SERIAL_CONSOLE, SERIAL_WIFI };
void Serial_Init(void);
int Serial_Read(uint8_t port, uint8_t *byte);
int Serial_Write(uint8_t port, const char *text);
int Serial_TakeError(uint8_t port);
#endif
