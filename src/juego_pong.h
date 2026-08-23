// Pong 1D con paletas moviles: cada jugador mueve su paleta con el eje de la
// tira, dentro de SU mitad, y golpea con el boton cuando la pelota entra en la
// paleta. Cada golpe acelera. El potenciometro fija la velocidad de saque. Gana
// el primero que llega a PUNTOS_GANAR.
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
extern uint16_t PON_PALETA_LARGO; // largo de la paleta de cada jugador (LEDs)
extern uint16_t PON_VEL_PALETA;   // velocidad de la paleta (LEDs/s)
