// Tiros: duelo a distancia con balas lentas, de dos a cuatro pistoleros. La
// vida es el LARGO del cuerpo: te pegan y te quedas mas corto. La vuelta de
// tuerca que lo hace jugable en una dimension es que el pistolero es
// DIRECCIONAL: mira hacia donde se mueve, y solo puede disparar -- y solo puede
// ser herido -- del lado hacia el que mira. Dar la espalda es invulnerabilidad
// total, pero mientras tanto no se hace dano y se retrocede hacia la pared.
// Municion limitada, la recarga deja clavado en el lugar, y dos balas que se
// cruzan en el aire se anulan.
#pragma once

#include "consola.h"

void   nuevoWestern();
void   loopWestern();
void   lcdWestern();
String webWestern();

extern uint16_t WES_VEL_JUGADOR;  // LEDs/s caminando
extern uint16_t WES_VEL_BALA;     // LEDs/s que viaja el plomo
extern uint16_t WES_CARGADOR;     // balas por cargador
extern uint16_t WES_RECARGA_MS;   // cuanto inmoviliza recargar
extern uint16_t WES_INVUL_MS;     // gracia despues de comerse una bala
extern uint16_t WES_QUEMARROPA;   // a esta distancia o menos, la espalda no salva
extern uint16_t WES_GIRO_MIN;     // deflexion minima para girar, en % del recorrido
