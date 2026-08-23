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
