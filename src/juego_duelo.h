// Duelo de reaccion: dos jugadores, un boton cada uno y nada mas. Todo apagado,
// retardo al azar, destello blanco, gana el primero que aprieta. Apretar antes
// de la senal pierde la ronda, y hay senales falsas rojas para castigar al que
// martilla el boton. Al mejor de cinco.
#pragma once

#include "consola.h"

void   nuevoDuelo();
void   loopDuelo();
void   lcdDuelo();
String webDuelo();

extern uint16_t DUE_ESPERA_MIN;   // retardo minimo antes de la senal (ms)
extern uint16_t DUE_ESPERA_MAX;   // retardo maximo antes de la senal (ms)
