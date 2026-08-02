// ---------- Stacker: apilar sin perder ancho ----------
// El Stacker de fichin es de dos dimensiones: el bloque va y viene en horizontal
// y la pila crece en vertical. Aca la tira ES el eje horizontal y la altura la
// lleva el tiempo: en pantalla conviven solo dos cosas, el bloque del piso
// ANTERIOR (fijo, tenue) y el bloque que se esta deslizando (brillante).
//
// La lectura visual la resuelve el color del solape:
//
//     azul tenue   el piso de abajo, donde se puede apoyar
//     verde        el bloque que se mueve, en el aire
//     BLANCO       donde los dos coinciden = lo que vas a conservar
//
// Apretar el boton con la franja blanca lo mas larga posible es literalmente
// todo el juego, y el ancho que queda es el que se desliza en el piso siguiente.

#include "juego_stacker.h"

// ---------- Parametros ----------
uint16_t STK_ANCHO_INI = 12;   // LEDs del bloque en el piso 1
uint16_t STK_PISOS     = 15;   // pisos para ganar la partida

const uint16_t STK_VEL_LENTA  = 65;   // pote al minimo (ms por LED)
const uint16_t STK_VEL_RAPIDA = 30;   // pote al maximo (ms por LED)
const uint16_t STK_VEL_MINIMA = 14;   // piso: mas rapido que esto no se ve venir
const uint16_t STK_FIN_MS     = 3000;
const uint16_t STK_GANAR_MS   = 4500; // el festejo de ganar dura mas: cuesta llegar

static const CRGB COL_APOYO = CRGB(0, 40, 140);   // el piso de abajo, tenue

// ---------- Estado ----------
enum EstadoStk { STK_JUGANDO, STK_FIN, STK_GANADO };
static EstadoStk estadoStk;
static uint32_t  faseDesde;

static int16_t  apoyoIni, apoyoFin;   // bloque ya fijado (piso anterior), incluidos
static int16_t  bloqueIni;            // bloque que se desliza; mismo ancho que el apoyo
static int8_t   bloqueDir;
static uint16_t velPaso;              // ms por LED del deslizamiento
static uint32_t ultimoPaso;
static uint8_t  piso;                 // pisos ya apilados
static bool     esRecord;
static uint8_t  hue;                  // color del bloque: rota por piso, da sensacion de avance

// ---------- Sonido ----------
static const Nota JINGLE_GANAR[] = {
  {523, 110}, {659, 110}, {784, 110}, {1047, 110}, {1319, 300}
};
static const Nota JINGLE_FIN[] = { {392, 160}, {311, 160}, {247, 160}, {165, 480} };

static void sonarFijar()    { beep(1400 + piso * 40, 35); }   // sube de tono piso a piso
static void sonarRecorte()  { beep( 900, 60); }               // perdiste ancho
static void sonarGameOver() { tocarJingle(JINGLE_FIN, 4); }
static void sonarGanar()    { tocarJingle(JINGLE_GANAR, 5); }

static uint8_t anchoActual() { return apoyoFin - apoyoIni + 1; }

void nuevoStacker() {
  uint8_t ancho = STK_ANCHO_INI;
  // El primer apoyo es un bloque del ancho inicial en el centro de la tira: la
  // base de la torre. El bloque del piso 1 sale del extremo y ya hay que
  // alinearlo contra ella, no hay piso de regalo.
  apoyoIni   = (NUM_LEDS - ancho) / 2;
  apoyoFin   = apoyoIni + ancho - 1;
  bloqueIni  = 0;
  bloqueDir  = +1;
  velPaso    = map(leerPoteCrudo(), 0, 4095, STK_VEL_LENTA, STK_VEL_RAPIDA);
  ultimoPaso = millis();
  piso       = 0;
  esRecord   = false;
  hue        = 96;                    // arranca en verde
  estadoStk  = STK_JUGANDO;
}

static void terminar(bool ganando) {
  estadoStk = ganando ? STK_GANADO : STK_FIN;
  faseDesde = millis();
  // El record es el piso alcanzado; ganar la partida entera vale STK_PISOS.
  esRecord  = intentarRecord(REC_STACKER, piso);
  if (esRecord)     sonarRecord();
  else if (ganando) sonarGanar();
  else              sonarGameOver();
}

// Fija el bloque en el aire: lo que sobresale del apoyo se cae y el resto pasa
// a ser el apoyo del piso siguiente.
static void fijarBloque() {
  int16_t bloqueFin = bloqueIni + anchoActual() - 1;
  int16_t ini = max(bloqueIni, apoyoIni);
  int16_t fin = min(bloqueFin, apoyoFin);

  if (ini > fin) {                    // no toco nada: la torre se cae
    terminar(false);
    return;
  }

  bool perdioAncho = (fin - ini + 1) < anchoActual();
  apoyoIni = ini;
  apoyoFin = fin;
  piso++;
  hue += 24;
  perdioAncho ? sonarRecorte() : sonarFijar();

  if (piso >= STK_PISOS) { terminar(true); return; }

  // Cada piso se desliza un poco mas rapido, y el bloque nuevo entra por la
  // punta mas lejana al apoyo para que siempre haya que cruzar algo de tira.
  velPaso = max<int>(STK_VEL_MINIMA, (velPaso * 92) / 100);
  if (apoyoIni > NUM_LEDS / 2) { bloqueIni = 0;                        bloqueDir = +1; }
  else                         { bloqueIni = NUM_LEDS - anchoActual(); bloqueDir = -1; }
  ultimoPaso = millis();
}

void loopStacker() {
  uint32_t ahora = millis();

  if (estadoStk == STK_FIN) {
    // Derrota: lo que quedaba de la torre parpadea y se apaga.
    uint32_t t = ahora - faseDesde;
    FastLED.clear();
    if ((t / 140) % 2 == 0) {
      for (int16_t i = apoyoIni; i <= apoyoFin; i++) setLed(i, CRGB(120, 0, 0));
    }
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > STK_FIN_MS) volverAlMenu();
    return;
  }

  if (estadoStk == STK_GANADO) {
    // Victoria: arcoiris que recorre la tira entera.
    uint32_t t = ahora - faseDesde;
    fill_rainbow(leds, NUM_LEDS, (uint8_t)(t / 8), 4);
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > STK_GANAR_MS) volverAlMenu();
    return;
  }

  // --- Deslizamiento del bloque, rebotando en las dos puntas ---
  if (ahora - ultimoPaso >= velPaso) {
    ultimoPaso += velPaso;
    bloqueIni  += bloqueDir;
    if (bloqueIni <= 0)                        { bloqueIni = 0;                        bloqueDir = +1; }
    if (bloqueIni >= NUM_LEDS - anchoActual()) { bloqueIni = NUM_LEDS - anchoActual(); bloqueDir = -1; }
  }

  if (btnFlanco[0]) { fijarBloque(); return; }

  // --- Dibujo: apoyo tenue, bloque en el aire, solape en blanco ---
  int16_t bloqueFin = bloqueIni + anchoActual() - 1;
  CRGB colBloque = CHSV(hue, 220, 255);

  FastLED.clear();
  for (int16_t i = apoyoIni;  i <= apoyoFin;  i++) setLed(i, COL_APOYO);
  for (int16_t i = bloqueIni; i <= bloqueFin; i++) setLed(i, colBloque);
  for (int16_t i = max(bloqueIni, apoyoIni); i <= min(bloqueFin, apoyoFin); i++) {
    setLed(i, CRGB::White);
  }
  FastLED.show();
}

// ---------- LCD ----------
void lcdStacker() {
  if (estadoStk == STK_GANADO) {
    lcdLinea(0, "** GANASTE! **");
    lcdLinea(1, esRecord ? "*NUEVO RECORD!*" : ("Torre de " + String(piso)));
    return;
  }
  if (estadoStk == STK_FIN) {
    lcdLinea(0, "** PERDISTE **");
    lcdLinea(1, esRecord ? "*NUEVO RECORD!*" : ("Llegaste al " + String(piso)));
    return;
  }
  lcdLinea(0, "Piso " + String(piso) + " de " + String(STK_PISOS));
  lcdLinea(1, "Ancho: " + String(anchoActual()) + " LEDs");
}

String webStacker() {
  return "Piso " + String(piso) + " de " + String(STK_PISOS) +
         ", ancho " + String(anchoActual()) + " LEDs";
}
