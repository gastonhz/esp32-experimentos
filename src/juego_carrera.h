// Carrera: version del OpenLEDRace (gbarbarov) para esta consola. Dos autos,
// un pulsador cada uno. Cada pulsacion es un empujon de velocidad, la friccion
// frena todo el tiempo y la pista tiene cuestas que cobran al subir y devuelven
// al bajar. Gana el primero que completa las vueltas.
#pragma once

#include "consola.h"

void   nuevoCarrera();
void   loopCarrera();
void   lcdCarrera();
String webCarrera();

extern uint16_t CAR_IMPULSO;      // LEDs/s que suma cada pulsacion
extern uint16_t CAR_FRICCION;     // frenado por segundo, en centesimas (180 = 1.80/s)
extern uint16_t CAR_GRAVEDAD;     // LEDs/s^2 que cobra o devuelve la pendiente maxima
extern uint16_t CAR_VUELTAS_MIN;  // vueltas con el pote al minimo
extern uint16_t CAR_VUELTAS_MAX;  // vueltas con el pote al maximo
