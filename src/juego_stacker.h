// Stacker: un bloque se desliza solo de punta a punta y el boton lo fija. Lo
// que queda es la interseccion con el bloque anterior, asi que el bloque se va
// achicando piso a piso hasta que se falla.
#pragma once

#include "consola.h"

void   nuevoStacker();
void   loopStacker();
void   lcdStacker();
String webStacker();

extern uint16_t STK_ANCHO_INI;   // LEDs del bloque en el piso 1
extern uint16_t STK_PISOS;       // pisos para ganar la partida
