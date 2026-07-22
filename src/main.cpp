/*********
  PONG 1D sobre tira WS2812B — primer juego de la maquina de juegos LED.

  La tira es el mundo del juego: la pelota (LED blanco con estela) rebota
  entre los dos extremos. Cada jugador tiene una ZONA de golpe cerca de su
  punta; cuando la pelota entra en ella, la zona se ilumina fuerte ("dale
  ahora"). El jugador golpea apretando su boton mientras la pelota esta en
  su zona. Si la pelota se escapa por su punta, el rival suma punto.
  Cada golpe acelera la pelota. Primero a PUNTOS_GANAR gana.

  Hardware (ver scripts-y-pruebas/setup-hardware-maquina-juegos-led.md):
    Datos:    GPIO16 -> SN74AHCT125N -> 470ohm -> DIN tira (100 LEDs WS2812B)
    Boton P1: GPIO18 a GND (INPUT_PULLUP -> apretado = LOW)
    Boton P2: GPIO19 a GND (INPUT_PULLUP -> apretado = LOW)
    Tira alimentada por fuente externa 5V, masa comun, cap de 1000uF al inicio.
*********/

#include <FastLED.h>

// ---------- Hardware ----------
#define NUM_LEDS    100
#define DATA_PIN    16
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

#define BTN_P1 18
#define BTN_P2 19

CRGB leds[NUM_LEDS];

// ---------- Parametros del juego (tocar aca para balancear) ----------
const uint8_t  BRILLO         = 128;  // techo de seguridad de corriente
const uint8_t  ZONA           = 12;   // largo de la zona de golpe de cada jugador
const uint16_t VEL_INICIAL    = 60;   // ms por LED al sacar (mas grande = mas lento)
const uint16_t VEL_MINIMA     = 16;   // techo de velocidad (ms por LED)
const uint16_t VEL_ACELERA    = 4;    // cuanto baja el intervalo por cada golpe
const uint8_t  PUNTOS_GANAR   = 5;
const uint16_t DEBOUNCE_MS    = 25;
const uint8_t  ESTELA         = 2;    // largo de la cola de la pelota

// ---------- Colores ----------
#define COL_PELOTA  CRGB::White
#define COL_P1      CRGB(0, 255, 0)   // verde  (jugador 1, inicio de la tira)
#define COL_P2      CRGB(0, 60, 255)  // azul   (jugador 2, final de la tira)

// ---------- Estado del juego ----------
enum Estado { SACANDO, JUGANDO, PUNTO, FIN };
Estado   estado;
uint32_t estadoDesde;   // millis() en que entramos al estado actual

int16_t  pelotaPos;     // 0..NUM_LEDS-1
int8_t   pelotaDir;     // +1 hacia P2 (final), -1 hacia P1 (inicio)
uint16_t pelotaVel;     // ms por paso actual
uint32_t ultimoPaso;    // millis() del ultimo movimiento

uint8_t  puntosP1, puntosP2;
uint8_t  saca;          // quien saca: 1 o 2 (saca el que perdio el punto)
uint8_t  ganador;       // 1 o 2, valido en estado FIN

// ---------- Botones: debounce + flanco de bajada ----------
const uint8_t PIN_BTN[2] = { BTN_P1, BTN_P2 };
bool     btnEstable[2] = { false, false };  // true = presionado (estable)
bool     btnPrev[2]    = { false, false };  // ultima lectura cruda
uint32_t btnCambio[2]  = { 0, 0 };          // millis del ultimo cambio crudo
bool     btnFlanco[2]  = { false, false };  // true un frame al recien presionar

void actualizarBotones() {
  uint32_t ahora = millis();
  for (uint8_t i = 0; i < 2; i++) {
    btnFlanco[i] = false;
    bool raw = (digitalRead(PIN_BTN[i]) == LOW);   // apretado = LOW
    if (raw != btnPrev[i]) {
      btnPrev[i]   = raw;
      btnCambio[i] = ahora;
    }
    if (ahora - btnCambio[i] >= DEBOUNCE_MS && raw != btnEstable[i]) {
      btnEstable[i] = raw;
      if (btnEstable[i]) btnFlanco[i] = true;       // flanco: recien presionado
    }
  }
}

// ---------- Utilidades de dibujo ----------
void setLed(int16_t i, const CRGB& c) {
  if (i >= 0 && i < NUM_LEDS) leds[i] = c;
}

// Marcadores: puntos de P1 como LEDs verdes desde el inicio,
// puntos de P2 como LEDs azules desde el final.
void dibujarMarcador() {
  for (uint8_t i = 0; i < puntosP1; i++) setLed(i, COL_P1);
  for (uint8_t i = 0; i < puntosP2; i++) setLed(NUM_LEDS - 1 - i, COL_P2);
}

// Pelota con estela que se desvanece detras (en sentido contrario a la marcha).
void dibujarPelota() {
  setLed(pelotaPos, COL_PELOTA);
  for (uint8_t k = 1; k <= ESTELA; k++) {
    CRGB c = COL_PELOTA;
    c.nscale8(255 / (k + 1));               // cada paso mas tenue
    setLed(pelotaPos - pelotaDir * k, c);
  }
}

// ---------- Transiciones de estado ----------
void irA(Estado e) {
  estado = e;
  estadoDesde = millis();
}

void prepararSaque() {
  pelotaVel = VEL_INICIAL;
  if (saca == 1) {                 // P1 saca desde el inicio, hacia P2
    pelotaPos = 1;
    pelotaDir = +1;
  } else {                         // P2 saca desde el final, hacia P1
    pelotaPos = NUM_LEDS - 2;
    pelotaDir = -1;
  }
  irA(SACANDO);
}

void nuevoPartido() {
  puntosP1 = puntosP2 = 0;
  saca = 1;
  prepararSaque();
}

// ---------- Estados ----------
void loopSacando() {
  // La zona del que saca late; el saque arranca con su boton (o auto a los 4 s).
  uint8_t idx = saca - 1;
  bool lanzar = btnFlanco[idx] || (millis() - estadoDesde > 4000);

  FastLED.clear();
  dibujarMarcador();
  // Pelota parpadeando en el punto de saque.
  if ((millis() / 300) % 2 == 0) dibujarPelota();
  FastLED.show();

  if (lanzar) {
    ultimoPaso = millis();
    irA(JUGANDO);
  }
}

void loopJugando() {
  // --- Golpe: valido si la pelota viene hacia tu punta y esta en tu zona ---
  if (pelotaDir == -1 && pelotaPos <= ZONA - 1 && btnFlanco[0]) {
    pelotaDir = +1;
    pelotaVel = max<int>(VEL_MINIMA, pelotaVel - VEL_ACELERA);
  }
  if (pelotaDir == +1 && pelotaPos >= NUM_LEDS - ZONA && btnFlanco[1]) {
    pelotaDir = -1;
    pelotaVel = max<int>(VEL_MINIMA, pelotaVel - VEL_ACELERA);
  }

  // --- Movimiento por tiempo (1 LED cada pelotaVel ms) ---
  if (millis() - ultimoPaso >= pelotaVel) {
    ultimoPaso += pelotaVel;
    pelotaPos += pelotaDir;

    if (pelotaPos < 0) {          // se escapo por P1 -> punto de P2
      puntosP2++;
      ganador = (puntosP2 >= PUNTOS_GANAR) ? 2 : 0;
      saca = 1;                   // saca el que perdio
      irA(ganador ? FIN : PUNTO);
      return;
    }
    if (pelotaPos > NUM_LEDS - 1) { // se escapo por P2 -> punto de P1
      puntosP1++;
      ganador = (puntosP1 >= PUNTOS_GANAR) ? 1 : 0;
      saca = 2;
      irA(ganador ? FIN : PUNTO);
      return;
    }
  }

  // --- Dibujo ---
  FastLED.clear();
  bool p1Activa = (pelotaDir == -1 && pelotaPos <= ZONA - 1);
  bool p2Activa = (pelotaDir == +1 && pelotaPos >= NUM_LEDS - ZONA);
  for (uint8_t i = 0; i < ZONA; i++) {
    setLed(i, p1Activa ? COL_P1 : CRGB(0, 20, 0));                // zona P1
    setLed(NUM_LEDS - 1 - i, p2Activa ? COL_P2 : CRGB(0, 5, 20)); // zona P2
  }
  dibujarPelota();
  FastLED.show();
}

void loopPunto() {
  // Parpadeo en el color del que sumo, ~1.2 s, y a sacar de nuevo.
  CRGB c = (saca == 1) ? COL_P2 : COL_P1;   // sumo el rival del que saca
  bool on = (millis() / 150) % 2 == 0;
  fill_solid(leds, NUM_LEDS, on ? c : CRGB::Black);
  dibujarMarcador();
  FastLED.show();

  if (millis() - estadoDesde > 1200) prepararSaque();
}

void loopFin() {
  // Animacion de victoria en el color del ganador, ~3.5 s, y nuevo partido.
  CRGB c = (ganador == 1) ? COL_P1 : COL_P2;
  uint16_t t = (millis() - estadoDesde);
  FastLED.clear();
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    if ((i + t / 40) % 4 == 0) leds[i] = c;   // chase
  }
  FastLED.show();

  if (t > 3500) nuevoPartido();
}

// ---------- Arduino ----------
void setup() {
  pinMode(BTN_P1, INPUT_PULLUP);
  pinMode(BTN_P2, INPUT_PULLUP);

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRILLO);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 4500);  // red de seguridad: 4.5 A
  FastLED.clear(true);

  nuevoPartido();
}

void loop() {
  actualizarBotones();
  switch (estado) {
    case SACANDO: loopSacando(); break;
    case JUGANDO: loopJugando(); break;
    case PUNTO:   loopPunto();   break;
    case FIN:     loopFin();     break;
  }
}
