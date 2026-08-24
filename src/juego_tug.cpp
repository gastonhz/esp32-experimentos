// ---------- Tira y Afloja: cinchada de botones ----------

#include "juego_tug.h"

uint16_t TUG_EMPUJE = 2;      // LEDs que avanza el frente por pulsacion

enum EstadoTug { TUG_JUGANDO, TUG_FIN };
static EstadoTug estadoTug;
static int16_t   frente;      // posicion del limite Verde|Azul (0..LARGO_TIRA-1)
static uint8_t   ganador;     // 1 = Verde, 2 = Azul
static uint32_t  empujones;   // pulsaciones de los DOS lados (metrica del record)
static bool      esRecord;
static uint32_t  finDesde;    // millis() en que arranco el festejo

void nuevoTug() {
  frente    = LARGO_TIRA / 2;   // el frente arranca en el centro
  empujones = 0;
  ganador   = 0;
  esRecord  = false;
  estadoTug = TUG_JUGANDO;
}

static void terminar(uint8_t quien) {
  ganador   = quien;
  estadoTug = TUG_FIN;
  finDesde  = millis();
  esRecord  = intentarRecord(REC_TUG, empujones, quien - 1);
  esRecord ? sonarRecord() : sonarVictoria();
}

void loopTug() {
  if (estadoTug == TUG_JUGANDO) {
    // Cada pulsacion nueva (flanco) empuja el frente hacia el lado del rival.
    if (btnFlanco[0]) { frente += TUG_EMPUJE; empujones++; beep(1400, 18); }   // Verde empuja al final
    if (btnFlanco[1]) { frente -= TUG_EMPUJE; empujones++; beep(1050, 18); }   // Azul empuja al inicio

    if      (frente >= LARGO_TIRA - 1) terminar(1);   // Verde conquisto la tira
    else if (frente <= 0)            terminar(2);   // Azul conquisto la tira

    // Render: Verde [0, frente), Azul [frente, fin).
    for (int16_t i = 0; i < LARGO_TIRA; i++) leds[i] = (i < frente) ? COL_P1 : COL_P2;
    FastLED.show();
  } else {  // TUG_FIN: festejo del ganador, ~3 s, y al menu.
    CRGB c = (ganador == 1) ? COL_P1 : COL_P2;
    uint16_t t = millis() - finDesde;
    fill_solid(leds, LARGO_TIRA, ((t / 150) % 2 == 0) ? c : CRGB::Black);
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > 3000) volverAlMenu();
  }
}

// ---------- LCD: etiquetas de lado + barra de posicion del frente ----------
void lcdTug() {
  if (estadoTug == TUG_FIN) {
    lcdLinea(0, "Tira-Afloja");
    if (esRecord) lcdLinea(1, "*NUEVO RECORD!*");
    else          lcdLinea(1, (ganador == 1) ? "** GANA VERDE **" : "** GANA AZUL **");
    return;
  }
  lcdLinea(0, "Verde       Azul");        // Verde a la izquierda, Azul a la derecha
  uint8_t p = map(frente, 0, LARGO_TIRA - 1, 0, 15);
  String bar;
  for (uint8_t i = 0; i < 16; i++) bar += (i == p) ? '|' : '-';
  lcdLinea(1, bar);
}

String webTug() {
  return "Frente en el LED " + String(frente) + " de " + String(LARGO_TIRA);
}
