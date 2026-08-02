// ---------- Pantallas informativas: Highscores e IP ----------

#include "pantallas.h"
#include "panel_web.h"

// ---------- Highscores: vitrina de records ----------
// Un record por pantalla. Se pasa de juego con el joystick, igual que en el
// menu, y el pulsador de reset vuelve al selector (eso lo maneja main.cpp).
static uint8_t verIndice = 0;   // que record se esta mirando (0..NUM_RECORDS-1)

void nuevoHighscores() {
  verIndice = 0;
  lcdForzarRefresh();
}

void loopHighscores() {
  int8_t paso = joystickPasoX();
  if (paso) {
    verIndice = (verIndice + NUM_RECORDS + paso) % NUM_RECORDS;
    beep(1200, 25);
  }

  uint8_t b = beatsin8(20, 10, 60);      // dorado respirando, sin prisa
  fill_solid(leds, NUM_LEDS, COL_RECORD);
  nscale8(leds, NUM_LEDS, b);
  FastLED.show();
}

// "Rec:" en vez de "Record:" porque con la unidad al lado no entra en 16 columnas.
void lcdHighscores() {
  lcdLinea(0, RECORDS[verIndice].nombre);
  lcdLinea(1, "Rec: " + textoRecord(verIndice));
}

String webHighscores() {
  return "(vitrina de records)";
}

// ---------- IP: como entrar al panel web ----------
// El LCD muestra la red y la IP (que ya se calcularon al levantar el AP) y la
// tira queda en un azul tenue, sin animacion, para que se note que aca no hay
// nada que jugar.
void nuevoIP() {
  lcdForzarRefresh();
}

void loopIP() {
  fill_solid(leds, NUM_LEDS, CRGB(0, 60, 255));
  nscale8(leds, NUM_LEDS, 30);
  FastLED.show();
}

// El SSID va solo (sin prefijo) porque "GastiConsola" ya come 12 de las 16
// columnas. La clave no se muestra: la sabe el que armo la consola.
void lcdIP() {
  lcdLinea(0, AP_SSID);
  lcdLinea(1, apIP);
}

String webIP() {
  return "(pantalla informativa)";
}
