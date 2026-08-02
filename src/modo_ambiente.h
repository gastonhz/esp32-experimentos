// Modo ambiente: automata celular elemental. No es un juego, es lo que corre
// cuando nadie esta jugando. Cada LED es una celda viva o muerta y cada
// generacion sale de la celda y sus dos vecinas segun una regla de 8 bits, que
// se elige en vivo con el potenciometro.
#pragma once

#include "consola.h"

void   nuevoAmbiente();
void   loopAmbiente();
void   lcdAmbiente();
String webAmbiente();
