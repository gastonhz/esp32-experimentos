// Las pantallas que no son juegos.
//
//   Highscores: la vitrina. Muestra el record guardado de cada juego (sobrevive
//               a los reinicios); se pasa de juego con el eje X del joystick.
//   IP:         el ESP32 levanta su propia red WiFi y sirve un panel web para
//               ver el estado de la partida en vivo y tunear parametros sin
//               recompilar; esta pantalla muestra el nombre de la red y la IP.
//
//   Nombre:     las tres letras del que acaba de batir un record. Esta no se
//               elige del selector: aparece sola al terminar una partida con
//               record y se va al menu cuando se guarda.
//
// Todas vuelven al selector con el pulsador de reset.
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

// Pantalla de las tres letras. No tiene web() porque no es una entrada de
// JUEGOS[]: la enciende volverAlMenu() y la despacha el loop() de main.cpp.
void   nuevoNombre();
void   loopNombre();
void   lcdNombre();
