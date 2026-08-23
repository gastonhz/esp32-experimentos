// Rompecolores: contra un muro de colores que avanza hacia la base. Antes de
// jugar se elige el modo: con UN control (el SW cicla el color de la bala y el
// arcade dispara) o con LOS CUATRO (cada arcade dispara el color de su propio
// control). Si el color coincide con el frente del muro lo rompe; si no, el muro
// crece de castigo.
#pragma once

#include "consola.h"

void   nuevoRompecolores();
void   loopRompecolores();
void   lcdRompecolores();
String webRompecolores();

extern uint16_t RC_VEL_LENTA;   // avance mas lento del muro, pote al minimo (ms por LED)
extern uint16_t RC_VEL_RAPIDA;  // avance mas rapido del muro, pote al maximo (ms por LED)
extern uint16_t RC_PROY_VEL;    // velocidad del proyectil (ms por LED)
