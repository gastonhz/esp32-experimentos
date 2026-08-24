// Panel web: el ESP32 levanta su propia red (modo Access Point, no se conecta a
// ningun router) y sirve una pagina para ver el estado de la partida en vivo y
// tunear parametros de balance sin recompilar.
#pragma once

#include "consola.h"

// Cambiar aca si hace falta (WPA2 pide 8 caracteres minimo en la clave).
#define AP_SSID "PixeLED"
#define AP_PASS "led12345"

extern String apIP;             // IP del AP como texto, se calcula una sola vez

void iniciarPanelWeb();         // levanta el AP y arranca el servidor

// ---------- Actualizacion de firmware por WiFi (OTA) ----------
// Se sube el .bin desde la misma pagina del panel: el ESP32 lo escribe en la
// particion de app que NO esta corriendo y reinicia ahi. La tabla de
// particiones ya lo permitia (default.csv trae app0 y app1), asi que no hubo
// que tocar nada ni migrar la NVS: los records siguen donde estaban.
//
// Mientras dura, el loop() no corre juegos ni lee controles --escribir flash
// bloquea de a ratos-- y la consola muestra el progreso en la tira y el LCD.
bool otaActiva();               // true mientras se sube o se muestra el resultado
void loopOTA();                 // dibuja el progreso y reinicia al terminar
