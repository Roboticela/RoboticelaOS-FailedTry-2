#ifndef ROBO_INPUT_H
#define ROBO_INPUT_H

#include <robosys/input.h>

#define RoboInputTouchpad    3u
#define RoboInputTouchscreen 4u
#define RoboInputGamepad     5u
#define RoboInputTablet      6u

#define RoboKeyLshift 0xE1
#define RoboKeyLctrl  0xE0
#define RoboKeyUnknown 0x00

int InputInit(void);
int RoboInputPush(const RoboInputEvent *ev);
int InputPop(RoboInputEvent *ev);
unsigned InputOverflows(void);
unsigned InputQueued(void);
int InputSelftest(void);
int InputKeyboardReady(void);
int InputMouseReady(void);
void InputSetKeyboardReady(int v);
void InputSetMouseReady(int v);

#endif
