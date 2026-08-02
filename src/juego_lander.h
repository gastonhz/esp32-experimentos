// Lunar Lander vertical: gravedad constante hacia la base, el joystick aplica
// EMPUJE (no posicion) y el combustible es limitado. Hay que posarse en la
// plataforma con la velocidad por debajo del umbral. Es el juego que mejor
// justifica que la tira este montada en vertical.
#pragma once

#include "consola.h"

void   nuevoLander();
void   loopLander();
void   lcdLander();
String webLander();

extern uint16_t LND_GRAVEDAD;     // LEDs/s^2 hacia la base
extern uint16_t LND_EMPUJE;       // LEDs/s^2 del motor a fondo
extern uint16_t LND_VEL_SEGURA;   // maxima velocidad de contacto que se banca (LEDs/s)
extern uint16_t LND_COMBUSTIBLE;  // ms de motor a fondo por intento
