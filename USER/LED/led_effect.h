#ifndef LED_EFFECT_H
#define LED_EFFECT_H
#include <stdint.h>
enum { LED_EFFECT_OFF, LED_EFFECT_BREATH, LED_EFFECT_FLOW };
void LED_EffectInit(void);
void LED_EffectSet(uint8_t effect);
#endif
