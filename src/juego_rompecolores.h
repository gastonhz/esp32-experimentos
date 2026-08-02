// Rompecolores: un solo jugador contra un muro de colores que avanza hacia la
// base. P1 cicla el color de la bala, P2 dispara. Si el color coincide con el
// frente del muro lo rompe; si no, el muro crece de castigo.
#pragma once

#include "consola.h"

void   nuevoRompecolores();
void   loopRompecolores();
void   lcdRompecolores();
String webRompecolores();

extern uint16_t RC_VEL_LENTA;   // avance mas lento del muro, pote al minimo (ms por LED)
extern uint16_t RC_VEL_RAPIDA;  // avance mas rapido del muro, pote al maximo (ms por LED)
extern uint16_t RC_PROY_VEL;    // velocidad del proyectil (ms por LED)
