// Las dos entradas del selector que no son juegos.
//
//   Highscores: la vitrina. Muestra el record guardado de cada juego (sobrevive
//               a los reinicios); se pasa de juego con el eje X del joystick.
//   IP:         el ESP32 levanta su propia red WiFi y sirve un panel web para
//               ver el estado de la partida en vivo y tunear parametros sin
//               recompilar; esta pantalla muestra el nombre de la red y la IP.
//
// Las dos vuelven al selector con el pulsador de reset.
#pragma once

#include "consola.h"

void   nuevoHighscores();
void   loopHighscores();
void   lcdHighscores();
String webHighscores();

void   nuevoIP();
void   loopIP();
void   lcdIP();
String webIP();
