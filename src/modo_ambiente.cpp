// ---------- Modo ambiente: automata celular elemental ----------
// En una pantalla, un automata elemental se dibuja con una generacion por fila
// y se ve el triangulo entero de la historia. En una tira de una dimension solo
// existe la generacion ACTUAL, y el tiempo transcurre de verdad: el patron
// parece moverse y mutar solo. Es el uso mas nativo que tiene una tira 1D.
//
// La regla es un numero de 8 bits: los tres vecinos (izquierda, propia,
// derecha) forman un indice de 0 a 7 y el bit correspondiente de la regla dice
// si la celda vive en la generacion siguiente. Todo el automata son estas dos
// lineas de generar():
//
//     uint8_t idx = (izq << 2) | (yo << 1) | der;
//     nueva[i] = (regla >> idx) & 1;
//
// Controles:
//   Pote      elige la regla (0-255) en vivo
//   Joystick  velocidad de las generaciones
//   Verde     resiembra (alterna semilla de una celda / semilla al azar)
//   Azul      cambia la paleta
//
// Reglas que valen la pena: 30 (caos), 90 (Sierpinski), 110 (Turing completa),
// 105, 150, 22.

#include "modo_ambiente.h"

const uint16_t AMB_PASO_LENTO  = 220;  // ms por generacion con el stick abajo
const uint16_t AMB_PASO_RAPIDO = 35;   // ms por generacion con el stick a fondo
const uint8_t  AMB_EDAD_MAX    = 40;   // a partir de aca la celda ya no cambia de tono
const uint8_t  AMB_PALETAS     = 4;
const uint8_t  AMB_ESTANCADO   = 8;    // generaciones iguales seguidas antes de resembrar
// Cada escalon de regla son 16 cuentas del ADC y el jitter del ESP32 es de ese
// orden: sin histeresis la regla bailaria sola entre dos valores vecinos en
// cada frame, el automata no se asentaria nunca y el LCD escribiria por I2C sin
// parar. Solo se acepta un valor nuevo si la perilla se movio de verdad.
const uint16_t AMB_POTE_HISTERESIS = 40;

// ---------- Estado ----------
static uint8_t  cel[NUM_LEDS];        // 0 / 1: la generacion actual
static uint8_t  nueva[NUM_LEDS];
static uint8_t  edad[NUM_LEDS];       // generaciones que lleva viva: da el degrade de color
static uint8_t  regla;
static uint16_t poteEstable;          // ultima lectura del pote que se acepto como cambio
static uint32_t ultimaGen;
static uint8_t  paleta;
static bool     semillaCentro;        // alterna con el boton Verde
static uint8_t  igualesSeguidas;      // detector de patron muerto o congelado

static void sembrar() {
  for (int16_t i = 0; i < NUM_LEDS; i++) {
    cel[i]  = semillaCentro ? (i == NUM_LEDS / 2 ? 1 : 0) : (random8() < 100 ? 1 : 0);
    edad[i] = cel[i];
  }
  igualesSeguidas = 0;
}

void nuevoAmbiente() {
  poteEstable   = leerPoteCrudo();
  regla         = (uint8_t)(poteEstable >> 4);
  paleta        = 0;
  semillaCentro = true;
  ultimaGen     = millis();
  sembrar();
  lcdForzarRefresh();
}

// Una generacion. Los bordes son circulares (el LED 99 es vecino del 0): si no,
// el patron se apagaria desde las puntas y el modo se quedaria vacio.
static void generar() {
  bool cambio = false;
  for (int16_t i = 0; i < NUM_LEDS; i++) {
    uint8_t izq = cel[(i - 1 + NUM_LEDS) % NUM_LEDS];
    uint8_t der = cel[(i + 1) % NUM_LEDS];
    uint8_t idx = (izq << 2) | (cel[i] << 1) | der;
    nueva[i] = (regla >> idx) & 1;
    if (nueva[i] != cel[i]) cambio = true;
  }

  uint16_t vivas = 0;
  for (int16_t i = 0; i < NUM_LEDS; i++) {
    cel[i] = nueva[i];
    if (cel[i]) {
      vivas++;
      if (edad[i] < AMB_EDAD_MAX) edad[i]++;
    } else {
      edad[i] = 0;
    }
  }

  // Muchas reglas mueren o se congelan (la 0 apaga todo, la 255 prende todo).
  // Sin esto el modo ambiente se quedaria en una tira fija hasta que alguien
  // toque un boton, que es justo lo contrario de lo que tiene que hacer.
  if (!cambio || vivas == 0 || vivas == NUM_LEDS) {
    if (++igualesSeguidas >= AMB_ESTANCADO) sembrar();
  } else {
    igualesSeguidas = 0;
  }
}

// El color sale de la edad de la celda, no solo de si esta viva: las
// estructuras estables quedan de un tono y los frentes que avanzan de otro, y
// ahi se ve la diferencia entre la regla 90 y la 30 de un vistazo.
static CRGB colorCelda(uint8_t e) {
  uint8_t f = (uint8_t)((uint16_t)e * 255 / AMB_EDAD_MAX);
  switch (paleta) {
    case 0:  return CHSV(160 - f / 2, 255, 90 + f / 2);        // azul -> cyan
    case 1:  return CHSV(0   + f / 3, 255, 110 + f / 3);       // rojo -> naranja
    case 2:  return CHSV(96  + f / 2, 200, 80 + f / 2);        // verde -> celeste
    default: return CHSV(f, 255, 200);                          // arcoiris por edad
  }
}

void loopAmbiente() {
  uint32_t ahora = millis();

  // El pote elige la regla. Se lee siempre, asi se puede barrer el espacio de
  // reglas girando la perilla y ver como cambia el comportamiento en vivo.
  uint16_t pote = leerPoteCrudo();
  int16_t  dif  = (int16_t)pote - (int16_t)poteEstable;
  if (dif < 0) dif = -dif;
  if (dif > (int16_t)AMB_POTE_HISTERESIS) {
    poteEstable = pote;
    regla = (uint8_t)(pote >> 4);            // 0..4095 -> 0..255
  }

  if (btnFlanco[0]) {                        // resiembra
    semillaCentro = !semillaCentro;
    sembrar();
    beep(1500, 25);
  }
  if (btnFlanco[1]) {                        // cambia la paleta
    paleta = (paleta + 1) % AMB_PALETAS;
    beep(1100, 25);
  }

  // El stick controla la velocidad: al centro va lento, hacia arriba acelera.
  float mando = leerJoyYNorm();
  if (mando < 0) mando = 0;
  uint16_t paso = AMB_PASO_LENTO - (uint16_t)(mando * (AMB_PASO_LENTO - AMB_PASO_RAPIDO));

  if (ahora - ultimaGen >= paso) {
    ultimaGen = ahora;
    generar();
  }

  for (int16_t i = 0; i < NUM_LEDS; i++) {
    leds[i] = cel[i] ? colorCelda(edad[i]) : CRGB::Black;
  }
  FastLED.show();
}

void lcdAmbiente() {
  lcdLinea(0, "Regla " + String(regla));
  lcdLinea(1, "P1 nueva  P2 col");
}

String webAmbiente() {
  uint16_t vivas = 0;
  for (int16_t i = 0; i < NUM_LEDS; i++) if (cel[i]) vivas++;
  return "Regla " + String(regla) + ", " + String(vivas) + " celdas vivas";
}
