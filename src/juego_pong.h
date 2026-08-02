// Pong 1D: pelota que rebota; cada jugador golpea con su boton cuando la pelota
// entra en su zona; cada golpe acelera. El potenciometro fija la velocidad de
// saque. Gana el primero que llega a PUNTOS_GANAR.
#pragma once

#include "consola.h"

void   nuevoPong();
void   loopPong();
void   lcdPong();
String webPong();

// Parametros tuneables en caliente desde el panel web.
extern uint16_t VEL_LENTA;      // saque mas lento, pote al minimo (ms por LED)
extern uint16_t VEL_RAPIDA;     // saque mas rapido, pote al maximo (ms por LED)
extern uint16_t VEL_ACELERA;    // cuanto baja el intervalo por cada golpe
