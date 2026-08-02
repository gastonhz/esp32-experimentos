// Esquiva: flujo continuo de muros que bajan por la tira con huecos entre ellos.
// El jugador se mueve con el joystick y tiene que ir saltando de hueco en hueco.
// Un solo toque y se termina: es un juego de precision, no de aguante.
#pragma once

#include "consola.h"

void   nuevoEsquiva();
void   loopEsquiva();
void   lcdEsquiva();
String webEsquiva();

extern uint16_t ESQ_VEL_JUGADOR;  // LEDs por segundo del jugador
extern uint16_t ESQ_HUECO_MIN;    // hueco mas chico que puede generarse (LEDs)
