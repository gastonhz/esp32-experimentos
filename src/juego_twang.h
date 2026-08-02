// Twang: dungeon 1D de un jugador. Un punto verde se mueve con el joystick (la
// deflexion del eje Y es VELOCIDAD, no pasos, como la inclinacion del TWANG
// original) mientras enemigos rojos bajan desde el fondo. P1 dispara un pulso
// blanco que mata a los que quedan cerca. Con la tanda limpia se abre la salida
// azul del ultimo LED y al llegar ahi se sube de nivel. El nivel tiene terreno:
// lava naranja que se prende y apaga, y cintas cyan que empujan al jugador.
#pragma once

#include "consola.h"

void   nuevoTwang();
void   loopTwang();
void   lcdTwang();
String webTwang();

extern uint16_t TWANG_VEL_JUGADOR;    // LEDs por segundo con el joystick a fondo
extern uint16_t TWANG_ATAQUE_RADIO;   // LEDs a cada lado que alcanza el pulso ya expandido
extern uint16_t TWANG_ATAQUE_ESPERA;  // cooldown entre ataques (ms)
