// Pelea: combate cuerpo a cuerpo en 1D. Barra de vida en cada punta y los dos
// peleadores en el medio. Todo pasa por cuanto se mantiene apretado el boton:
// un toque saca un golpe corto y barato, mantenerlo carga uno largo y caro que
// deja inmovil mientras se prepara. Como la carga se dibuja tenue en la tira, el
// rival VE venir el golpe: el juego es de distancia y de leer el windup.
#pragma once

#include "consola.h"

void   nuevoPelea();
void   loopPelea();
void   lcdPelea();
String webPelea();

extern uint16_t PEL_VEL_JUGADOR;    // LEDs/s caminando
extern uint16_t PEL_ALCANCE_CORTO;  // alcance del golpe rapido (LEDs)
extern uint16_t PEL_ALCANCE_LARGO;  // alcance del cargado a full (LEDs)
extern uint16_t PEL_CARGA_MIN;      // hasta aca el boton cuenta como toque (ms)
extern uint16_t PEL_CARGA_MAX;      // mas alla de esto ya no carga mas (ms)
extern uint16_t PEL_RECUP_CORTA;    // inmovil despues del golpe rapido (ms)
extern uint16_t PEL_RECUP_LARGA;    // inmovil despues del cargado (ms)
