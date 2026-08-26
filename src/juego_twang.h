// Twang: dungeon 1D, solo o de a dos en cooperativo (se elige al entrar). Un
// punto verde --y uno cian si son dos-- se mueve con el joystick (la deflexion
// del eje Y es VELOCIDAD, no pasos, como la inclinacion del TWANG original)
// mientras los enemigos rojos ocupan la mazmorra: unos bajan a buscarte, otros
// se plantan y otros patrullan su banda. El boton dispara un pulso blanco que
// mata a los que quedan cerca. Con la tanda limpia se abre la salida azul del
// ultimo LED y al llegar ahi --los dos, si son dos-- se sube de nivel. El nivel
// tiene terreno: lava naranja que se prende, apaga y crece, y cintas cyan que
// empujan al que las pisa.
#pragma once

#include "consola.h"

void   nuevoTwang();
void   loopTwang();
void   lcdTwang();
String webTwang();

extern uint16_t TWANG_VEL_JUGADOR;    // LEDs por segundo con el joystick a fondo
extern uint16_t TWANG_ATAQUE_RADIO;   // LEDs a cada lado que alcanza el pulso ya expandido
extern uint16_t TWANG_ATAQUE_ESPERA;  // cooldown entre ataques (ms)
