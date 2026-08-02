// Esquiva: flujo continuo de muros SOLIDOS que bajan por la tira. Caminando no
// se cruzan, asi que la herramienta es el salto corto: manteniendo el boton
// Verde aparece una marca unos LEDs mas arriba que se estira con la carga, y al
// soltarlo el jugador aparece ahi sin pasar por el medio. Un solo toque y se
// termina.
#pragma once

#include "consola.h"

void   nuevoEsquiva();
void   loopEsquiva();
void   lcdEsquiva();
String webEsquiva();

extern uint16_t ESQ_VEL_JUGADOR;    // LEDs por segundo caminando
extern uint16_t ESQ_SALTO_MIN;      // distancia del salto soltando enseguida (LEDs)
extern uint16_t ESQ_SALTO_MAX;      // distancia con la carga completa (LEDs)
extern uint16_t ESQ_SALTO_CARGA_MS; // cuanto tarda la mira en ir de MIN a MAX
extern uint16_t ESQ_HUECO_MIN;      // hueco mas chico que puede generarse (LEDs)
extern uint16_t ESQ_HUECO_MAX;      // hueco mas grande
extern uint16_t ESQ_MURO_MIN;       // muro mas fino
extern uint16_t ESQ_MURO_MAX;       // muro mas grueso (se recorta al salto largo)
extern uint16_t ESQ_SALTO_ESPERA;   // descanso despues de caer (ms)
