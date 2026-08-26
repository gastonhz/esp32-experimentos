// Twang32: el TWANG canonico de bdring, portado a la consola.
//
// No es una version nuestra de TWANG: es EL TWANG. Veinte niveles hechos a
// mano mas el boss, mundo virtual de 1000 unidades que se escala a la tira,
// enemigos que caminan / se plantan / oscilan en seno, spawners infinitos,
// lava que crece y fluye, cintas, y la salida SIEMPRE abierta -- matar es
// opcional, el nivel es una carrera de obstaculos. Se pierde una vida y se
// reinicia el nivel; las vidas se recargan en cada nivel nuevo y el puntaje es
// la suma de las que sobraron.
//
// El codigo viene del repo original (MIT, ver src/twang32/): las clases estan
// sin tocar y la logica esta portada lo mas literal que se pudo. Lo unico
// adaptado es lo que no podia funcionar tal cual en esta maquina --el ataque
// era un sacudon del acelerometro y aca es el boton-- y esta todo listado
// arriba de juego_twang32.cpp.
#pragma once

#include "consola.h"

void   nuevoTwang32();
void   loopTwang32();
void   lcdTwang32();
String webTwang32();
