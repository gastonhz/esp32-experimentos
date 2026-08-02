// Paddle: Pong de un jugador contra la pared del fondo. A diferencia del Pong,
// la zona de golpe no es fija: es una paleta que se mueve con el joystick por
// casi toda la tira, y el boton Verde es el que golpea.
#pragma once

#include "consola.h"

void   nuevoPaddle();
void   loopPaddle();
void   lcdPaddle();
String webPaddle();

extern uint16_t PAD_VEL_JUGADOR;  // LEDs por segundo de la paleta
extern uint16_t PAD_LARGO_INI;    // largo de la paleta al empezar (LEDs)
extern uint16_t PAD_ACHICA_CADA;  // cada cuantos golpes se acorta la paleta
