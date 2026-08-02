// Tira y Afloja: cinchada de botones. Un frente Verde|Azul que se empuja a
// martillazos hacia el lado del rival; el que llega al extremo contrario gana.
#pragma once

#include "consola.h"

void   nuevoTug();
void   loopTug();
void   lcdTug();
String webTug();

extern uint16_t TUG_EMPUJE;   // LEDs que avanza el frente por pulsacion
