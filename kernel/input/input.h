#ifndef ROBO_INPUT_H
#define ROBO_INPUT_H

#include <robosys/input.h>

#define ROBO_INPUT_TOUCHPAD    3u
#define ROBO_INPUT_TOUCHSCREEN 4u
#define ROBO_INPUT_GAMEPAD     5u
#define ROBO_INPUT_TABLET      6u

#define ROBO_KEY_LSHIFT 0xE1
#define ROBO_KEY_LCTRL  0xE0
#define ROBO_KEY_UNKNOWN 0x00

int robo_input_init(void);
int robo_input_push(const RoboInputEvent *ev);
int robo_input_pop(RoboInputEvent *ev);
unsigned robo_input_overflows(void);
unsigned robo_input_queued(void);
int robo_input_selftest(void);
int robo_input_keyboard_ready(void);
int robo_input_mouse_ready(void);
void robo_input_set_keyboard_ready(int v);
void robo_input_set_mouse_ready(int v);

#endif
