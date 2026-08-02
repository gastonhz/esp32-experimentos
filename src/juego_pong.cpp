// ---------- Pong 1D: dos jugadores, un boton cada uno ----------

#include "juego_pong.h"

// ---------- Parametros ----------
uint16_t       VEL_LENTA    = 90;   // saque mas lento, pote al minimo (ms por LED)
uint16_t       VEL_RAPIDA   = 24;   // saque mas rapido, pote al maximo (ms por LED)
uint16_t       VEL_ACELERA  = 4;    // cuanto baja el intervalo por cada golpe
const uint8_t  ZONA         = 12;   // largo de la zona de golpe de cada jugador
const uint16_t VEL_MINIMA   = 16;   // piso de aceleracion (ms por LED)
const uint8_t  PUNTOS_GANAR = 5;

// ---------- Estado ----------
enum Estado { SACANDO, JUGANDO, PUNTO, FIN };
static Estado   estado;
static uint32_t estadoDesde;   // millis() en que entramos al estado actual

static int16_t  pelotaPos;     // 0..NUM_LEDS-1
static int8_t   pelotaDir;     // +1 hacia P2 (final), -1 hacia P1 (inicio)
static uint16_t pelotaVel;     // ms por paso actual
static uint32_t ultimoPaso;    // millis() del ultimo movimiento

static uint8_t  puntosP1, puntosP2;
static uint8_t  saca;          // quien saca: 1 o 2 (saca el que perdio el punto)
static uint8_t  ganador;       // 1 o 2, valido en estado FIN
static uint16_t velSaque;      // velocidad de saque actual, la fija el potenciometro
static uint32_t golpesTotales; // golpes exitosos de los DOS jugadores (metrica del record)
static bool     esRecord;      // solo tiene sentido con ganador != 0

// ---------- Sonido ----------
static const Nota JINGLE_PUNTO[] = { {494, 120}, {330, 180} };   // descendente

static void sonarGolpe() {                  // agudo, sube con la velocidad de la pelota
  beep(1000 + (VEL_LENTA - pelotaVel) * 10, 30);
}
static void sonarSaque() { beep(900, 45); }
static void sonarPunto() { tocarJingle(JINGLE_PUNTO, 2); }

// ---------- Dibujo ----------
// Marcadores: puntos de P1 como LEDs verdes desde el inicio, puntos de P2 como
// LEDs azules desde el final.
static void dibujarMarcador() {
  for (uint8_t i = 0; i < puntosP1; i++) setLed(i, COL_P1);
  for (uint8_t i = 0; i < puntosP2; i++) setLed(NUM_LEDS - 1 - i, COL_P2);
}

static void dibujarPelota() {
  dibujarPuntoConEstela(pelotaPos, pelotaDir, COL_PELOTA);
}

// ---------- Transiciones ----------
static void irA(Estado e) {
  estado = e;
  estadoDesde = millis();
}

static void prepararSaque() {
  pelotaVel = velSaque;
  if (saca == 1) {                 // P1 saca desde el inicio, hacia P2
    pelotaPos = 1;
    pelotaDir = +1;
  } else {                         // P2 saca desde el final, hacia P1
    pelotaPos = NUM_LEDS - 2;
    pelotaDir = -1;
  }
  irA(SACANDO);
}

// Cierra la partida: compara el total de golpes contra el record y elige el
// sonido. Los dos puntos donde se pierde la pelota hacen exactamente lo mismo.
static void terminarPunto() {
  if (ganador) esRecord = intentarRecord(REC_PONG, golpesTotales);
  if (!ganador)      sonarPunto();
  else if (esRecord) sonarRecord();
  else               sonarVictoria();
  irA(ganador ? FIN : PUNTO);
}

void nuevoPong() {
  puntosP1 = puntosP2 = 0;
  saca          = 1;
  ganador       = 0;
  golpesTotales = 0;
  esRecord      = false;
  velSaque      = map(leerPoteCrudo(), 0, 4095, VEL_LENTA, VEL_RAPIDA);
  prepararSaque();
}

// ---------- Estados ----------
static void loopSacando() {
  // El pote se sigue leyendo hasta el momento del saque: lo que muestra el LCD
  // es lo que va a salir.
  velSaque  = map(leerPoteCrudo(), 0, 4095, VEL_LENTA, VEL_RAPIDA);
  pelotaVel = velSaque;

  // El saque arranca con el boton del que saca (o solo a los 4 s).
  uint8_t idx = saca - 1;
  bool lanzar = btnFlanco[idx] || (millis() - estadoDesde > 4000);

  FastLED.clear();
  dibujarMarcador();
  if ((millis() / 300) % 2 == 0) dibujarPelota();   // parpadea en el punto de saque
  FastLED.show();

  if (lanzar) {
    sonarSaque();
    ultimoPaso = millis();
    irA(JUGANDO);
  }
}

static void loopJugando() {
  // --- Golpe: valido si la pelota viene hacia tu punta y esta en tu zona ---
  if (pelotaDir == -1 && pelotaPos <= ZONA - 1 && btnFlanco[0]) {
    pelotaDir = +1;
    pelotaVel = max<int>(VEL_MINIMA, pelotaVel - VEL_ACELERA);
    golpesTotales++;
    sonarGolpe();
  }
  if (pelotaDir == +1 && pelotaPos >= NUM_LEDS - ZONA && btnFlanco[1]) {
    pelotaDir = -1;
    pelotaVel = max<int>(VEL_MINIMA, pelotaVel - VEL_ACELERA);
    golpesTotales++;
    sonarGolpe();
  }

  // --- Movimiento por tiempo (1 LED cada pelotaVel ms) ---
  if (millis() - ultimoPaso >= pelotaVel) {
    ultimoPaso += pelotaVel;
    pelotaPos += pelotaDir;

    if (pelotaPos < 0) {              // se escapo por P1 -> punto de P2
      puntosP2++;
      ganador = (puntosP2 >= PUNTOS_GANAR) ? 2 : 0;
      saca = 1;                       // saca el que perdio
      terminarPunto();
      return;
    }
    if (pelotaPos > NUM_LEDS - 1) {   // se escapo por P2 -> punto de P1
      puntosP1++;
      ganador = (puntosP1 >= PUNTOS_GANAR) ? 1 : 0;
      saca = 2;
      terminarPunto();
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

static void loopPunto() {
  // Parpadeo en el color del que sumo, ~1.2 s, y a sacar de nuevo.
  CRGB c = (saca == 1) ? COL_P2 : COL_P1;   // sumo el rival del que saca
  bool on = (millis() / 150) % 2 == 0;
  fill_solid(leds, NUM_LEDS, on ? c : CRGB::Black);
  dibujarMarcador();
  FastLED.show();

  if (millis() - estadoDesde > 1200) prepararSaque();
}

static void loopFin() {
  // Animacion de victoria en el color del ganador, ~3.5 s, y vuelta al menu.
  CRGB c = (ganador == 1) ? COL_P1 : COL_P2;
  uint16_t t = (millis() - estadoDesde);
  FastLED.clear();
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    if ((i + t / 40) % 4 == 0) leds[i] = c;   // chase
  }
  if (esRecord) dibujarChispasRecord();       // chispeo dorado ENCIMA del festejo normal
  FastLED.show();

  if (t > 3500) volverAlMenu();
}

void loopPong() {
  switch (estado) {
    case SACANDO: loopSacando(); break;
    case JUGANDO: loopJugando(); break;
    case PUNTO:   loopPunto();   break;
    case FIN:     loopFin();     break;
  }
}

// ---------- LCD: marcador + mensaje segun estado ----------
void lcdPong() {
  lcdLinea(0, "Verde " + String(puntosP1) + " - " + String(puntosP2) + " Azul");
  String m;
  switch (estado) {
    case SACANDO: {                       // muestra el nivel de velocidad del pote
      uint8_t nivel = map(velSaque, VEL_LENTA, VEL_RAPIDA, 1, 9);
      m = String(saca == 1 ? "Saca Verde v" : "Saca Azul v") + nivel;
      break;
    }
    case JUGANDO: m = "- jugando -"; break;
    case PUNTO:   m = (saca == 1) ? "Punto Azul!" : "Punto Verde!"; break;  // sumo el rival del que saca
    case FIN:
      if (esRecord) m = "*NUEVO RECORD!*";
      else          m = (ganador == 1) ? "** GANA VERDE **" : "** GANA AZUL **";
      break;
  }
  lcdLinea(1, m);
}

String webPong() {
  return "Verde " + String(puntosP1) + " - " + String(puntosP2) + " Azul";
}
