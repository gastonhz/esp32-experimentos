// Las pantallas que no son juegos.
//
//   Highscores: la vitrina. Muestra el record guardado de cada juego (sobrevive
//               a los reinicios); se pasa de juego con el eje X del joystick.
//   Ajustes:    como esta montada la consola --largo de la tira, orientacion y
//               silencio-- mas la ficha de la red WiFi, que antes era una
//               entrada propia del selector llamada "IP".
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

void   nuevoAjustes();
void   loopAjustes();
void   lcdAjustes();
String webAjustes();

// Pantalla de las tres letras. No tiene web() porque no es una entrada de
// JUEGOS[]: la enciende volverAlMenu() y la despacha el loop() de main.cpp.
void   nuevoNombre();
void   loopNombre();
void   lcdNombre();
