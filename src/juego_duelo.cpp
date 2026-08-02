// ---------- Duelo de reaccion ----------
// El unico juego de la consola que no usa ni el joystick ni la tira como espacio:
// la tira es puro semaforo. Lo que lo hace jugable no es la senal, son las dos
// reglas que castigan adivinar:
//
//   - apretar antes de la senal pierde la ronda en el acto (salida en falso)
//   - de vez en cuando destella ROJO, que no vale: el que reacciona al color
//     equivocado tambien pierde
//
// Sin eso, la estrategia optima seria martillar el boton y el juego no existiria.

#include "juego_duelo.h"

// ---------- Parametros ----------
uint16_t DUE_ESPERA_MIN = 2000;   // retardo minimo antes de la senal (ms)
uint16_t DUE_ESPERA_MAX = 8000;   // retardo maximo antes de la senal (ms)

const uint8_t  DUE_RONDAS_GANAR = 3;    // al mejor de cinco
const uint16_t DUE_FALSA_MS     = 160;  // cuanto dura un destello falso
const uint8_t  DUE_FALSA_PROB   = 45;   // de 255: probabilidad por chequeo
const uint16_t DUE_FALSA_CADA   = 700;  // cada cuanto se tira el dado de senal falsa
const uint16_t DUE_RESULTADO_MS = 2600; // cuanto se muestra el resultado de la ronda
const uint16_t DUE_FIN_MS       = 3800;
const uint16_t DUE_TIEMPO_TOPE  = 2000; // reacciones mas lentas que esto no cuentan para el record

// ---------- Estado ----------
enum EstadoDue { DUE_ESPERA, DUE_SENAL, DUE_RESULTADO, DUE_FIN };
static EstadoDue estadoDue;
static uint32_t  faseDesde;

static uint32_t senalEn;         // millis() en que aparece la senal de esta ronda
static uint32_t falsaHasta;      // millis() hasta el que dura el destello falso en curso
static uint32_t falsaProximoDado;// millis() del proximo sorteo de senal falsa
static uint8_t  puntosP1, puntosP2;
static uint8_t  ganadorRonda;    // 1 o 2
static uint16_t tiempoRonda;     // ms de reaccion del que gano (0 si fue por salida en falso)
static bool     porFalso;        // la ronda se decidio por salida en falso del rival
static uint8_t  ganador;         // 1 o 2, valido en DUE_FIN
static uint16_t mejorTiempo;     // mejor reaccion de la partida (para el record)
static bool     esRecord;

// ---------- Sonido ----------
static const Nota JINGLE_FALSO[] = { {220, 200}, {165, 320} };

static void sonarSenal()  { beep(2000, 60); }
static void sonarGana()   { beep(1500, 90); }
static void sonarFalso()  { tocarJingle(JINGLE_FALSO, 2); }

// ---------- Rondas ----------
static void nuevaRonda() {
  estadoDue        = DUE_ESPERA;
  faseDesde        = millis();
  senalEn          = faseDesde + random(DUE_ESPERA_MIN, DUE_ESPERA_MAX + 1);
  falsaHasta       = 0;
  falsaProximoDado = faseDesde + DUE_FALSA_CADA;
}

void nuevoDuelo() {
  puntosP1 = puntosP2 = 0;
  ganador      = 0;
  ganadorRonda = 0;
  tiempoRonda  = 0;
  porFalso     = false;
  mejorTiempo  = 0;
  esRecord     = false;
  nuevaRonda();
}

// Cierra la ronda y, si alguien llego a DUE_RONDAS_GANAR, la partida.
static void terminarRonda(uint8_t quien, uint16_t ms, bool falso) {
  ganadorRonda = quien;
  tiempoRonda  = ms;
  porFalso     = falso;
  if (quien == 1) puntosP1++; else puntosP2++;

  // Solo las reacciones de verdad entran al record; las rondas ganadas porque
  // el otro se adelanto no miden nada.
  if (!falso && ms > 0 && ms < DUE_TIEMPO_TOPE) {
    if (mejorTiempo == 0 || ms < mejorTiempo) mejorTiempo = ms;
  }

  falso ? sonarFalso() : sonarGana();

  if (puntosP1 >= DUE_RONDAS_GANAR || puntosP2 >= DUE_RONDAS_GANAR) {
    ganador   = (puntosP1 >= DUE_RONDAS_GANAR) ? 1 : 2;
    estadoDue = DUE_FIN;
    faseDesde = millis();
    esRecord  = intentarRecord(REC_DUELO, mejorTiempo);
    if (esRecord) sonarRecord();
    else          sonarVictoria();
    return;
  }
  estadoDue = DUE_RESULTADO;
  faseDesde = millis();
}

void loopDuelo() {
  uint32_t ahora = millis();

  if (estadoDue == DUE_FIN) {
    uint32_t t = ahora - faseDesde;
    CRGB c = (ganador == 1) ? COL_P1 : COL_P2;
    FastLED.clear();
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      if ((i + t / 40) % 4 == 0) leds[i] = c;   // chase en el color del ganador
    }
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > DUE_FIN_MS) volverAlMenu();
    return;
  }

  if (estadoDue == DUE_RESULTADO) {
    uint32_t t = ahora - faseDesde;
    // Media tira en el color del que gano la ronda: se ve quien va ganando sin
    // mirar el LCD.
    CRGB c = (ganadorRonda == 1) ? COL_P1 : COL_P2;
    FastLED.clear();
    bool on = (t / 200) % 2 == 0;
    if (on) {
      if (ganadorRonda == 1) for (int16_t i = 0; i < NUM_LEDS / 2; i++) setLed(i, c);
      else                   for (int16_t i = NUM_LEDS / 2; i < NUM_LEDS; i++) setLed(i, c);
    }
    FastLED.show();
    if (t > DUE_RESULTADO_MS) nuevaRonda();
    return;
  }

  if (estadoDue == DUE_ESPERA) {
    // Cualquier boton apretado antes de la senal es salida en falso: gana el otro.
    if (btnFlanco[0]) { terminarRonda(2, 0, true); return; }
    if (btnFlanco[1]) { terminarRonda(1, 0, true); return; }

    if (ahora >= senalEn) {
      estadoDue = DUE_SENAL;
      faseDesde = ahora;
      sonarSenal();
      fill_solid(leds, NUM_LEDS, CRGB::White);
      FastLED.show();
      return;
    }

    // Senales falsas: destellos rojos cortos que NO valen.
    if (ahora >= falsaProximoDado) {
      falsaProximoDado = ahora + DUE_FALSA_CADA;
      // Nunca justo antes de la senal de verdad: seria imposible distinguirlas.
      if (random8() < DUE_FALSA_PROB && senalEn - ahora > DUE_FALSA_MS * 4) {
        falsaHasta = ahora + DUE_FALSA_MS;
      }
    }

    if (ahora < falsaHasta) fill_solid(leds, NUM_LEDS, CRGB(200, 0, 0));
    else {
      // Un latido tenue en el centro para que se note que la consola espera y
      // no que se colgo.
      FastLED.clear();
      setLed(NUM_LEDS / 2, CRGB(0, 0, beatsin8(30, 4, 26)));
    }
    FastLED.show();
    return;
  }

  // --- DUE_SENAL: gana el primero que aprieta ---
  uint16_t ms = (uint16_t)(ahora - faseDesde);
  if (btnFlanco[0]) { terminarRonda(1, ms, false); return; }
  if (btnFlanco[1]) { terminarRonda(2, ms, false); return; }

  // Si nadie aprieta en 3 s la ronda se anula y se vuelve a empezar, para que
  // la consola no quede clavada en blanco.
  if (ahora - faseDesde > 3000) { nuevaRonda(); return; }

  fill_solid(leds, NUM_LEDS, CRGB::White);
  FastLED.show();
}

// ---------- LCD ----------
void lcdDuelo() {
  lcdLinea(0, "Verde " + String(puntosP1) + " - " + String(puntosP2) + " Azul");

  switch (estadoDue) {
    case DUE_ESPERA:
      lcdLinea(1, "Preparados...");
      break;
    case DUE_SENAL:
      lcdLinea(1, "YA!");
      break;
    case DUE_RESULTADO:
      // "se adelanto" no entra en 16 columnas con "Verde" adelante; "Falso:" si.
      if (porFalso) lcdLinea(1, String("Falso: ") + (ganadorRonda == 1 ? "Azul" : "Verde"));
      else          lcdLinea(1, String(ganadorRonda == 1 ? "Verde " : "Azul ") + String(tiempoRonda) + " ms");
      break;
    case DUE_FIN:
      if (esRecord) lcdLinea(1, "*RECORD " + String(mejorTiempo) + " ms*");
      else          lcdLinea(1, (ganador == 1) ? "** GANA VERDE **" : "** GANA AZUL **");
      break;
  }
}

String webDuelo() {
  String s = "Verde " + String(puntosP1) + " - " + String(puntosP2) + " Azul";
  if (mejorTiempo) s += " (mejor: " + String(mejorTiempo) + " ms)";
  return s;
}
