// ---------- Pong 1D: dos jugadores con paleta movil ----------
// Antes cada jugador tenia una ZONA fija pegada a su punta: el boton golpeaba si
// la pelota entraba ahi, y no habia nada mas que decidir que CUANDO apretar.
// Ahora la zona es una PALETA que se mueve con el eje de la tira, y cada uno la
// puede llevar a cualquier lado de SU MITAD.
//
// Eso agrega la decision que le faltaba: adelantarse a buscar la pelota acorta
// el tiempo de reaccion pero deja la punta descubierta, y quedarse atras es
// seguro pero regala todo el terreno. El boton sigue siendo el golpe -- como en
// Paddle -- asi que llegar con la paleta no alcanza: hay que llegar Y apretar.
//
// El saque sale de la propia paleta y no de la pared. Es obligatorio: con la
// paleta adelantada, una pelota que saliera del LED 1 aparecia DETRAS de ella y
// el punto se perdia sin que hubiera nada que hacer.

#include "juego_pong.h"

// ---------- Parametros ----------
uint16_t       VEL_LENTA    = 90;   // saque mas lento, pote al minimo (ms por LED)
uint16_t       VEL_RAPIDA   = 24;   // saque mas rapido, pote al maximo (ms por LED)
uint16_t       VEL_ACELERA  = 4;    // cuanto baja el intervalo por cada golpe
uint16_t       PON_PALETA_LARGO = 9;   // largo de la paleta de cada jugador (LEDs)
uint16_t       PON_VEL_PALETA   = 55;  // velocidad de la paleta (LEDs/s)

const uint8_t  PON_LARGO_MIN = 3;    // piso por si el panel web lo deja en cero
const uint16_t VEL_MINIMA   = 16;   // piso de aceleracion (ms por LED)
const uint8_t  PUNTOS_GANAR = 5;

// La frontera: P1 se mueve en [0, MITAD-1] y P2 en [MITAD, NUM_LEDS-1].
const int16_t  PON_MITAD = NUM_LEDS / 2;

// ---------- Estado ----------
enum Estado { SACANDO, JUGANDO, PUNTO, FIN };
static Estado   estado;
static uint32_t estadoDesde;   // millis() en que entramos al estado actual

static int16_t  pelotaPos;     // 0..NUM_LEDS-1
static int8_t   pelotaDir;     // +1 hacia P2 (final), -1 hacia P1 (inicio)
static uint16_t pelotaVel;     // ms por paso actual
static uint32_t ultimoPaso;    // millis() del ultimo movimiento

static float    paletaCentro[2];
static uint32_t ultimoFrame;   // para mover las paletas por tiempo y no por frame

static uint8_t  puntosP1, puntosP2;
static uint8_t  saca;          // quien saca: 1 o 2 (saca el que perdio el punto)
static uint8_t  ganador;       // 1 o 2, valido en estado FIN
static uint16_t velSaque;      // velocidad de saque actual, la fija el potenciometro
static uint32_t golpesTotales; // golpes exitosos de los DOS jugadores (metrica del record)
static bool     esRecord;      // solo tiene sentido con ganador != 0

// ---------- Geometria de la paleta ----------
static inline int16_t paletaLargo() {
  return (int16_t)max<uint16_t>(PON_LARGO_MIN, PON_PALETA_LARGO);
}
static inline int16_t paletaIni(uint8_t j) {
  return (int16_t)(paletaCentro[j] + 0.5f) - paletaLargo() / 2;
}
static inline int16_t paletaFin(uint8_t j) { return paletaIni(j) + paletaLargo() - 1; }

// Cada uno encerrado en su mitad, y sin poder salirse de la tira por la punta.
// Los limites se calculan con el MISMO desplazamiento entero que usa paletaIni()
// (largo/2, no largo/2.0): con la mitad real, una paleta de 9 LEDs se pasaba un
// LED de la frontera por el redondeo y se metia en la mitad ajena.
static void limitarPaletas() {
  int16_t largo = paletaLargo();
  int16_t off   = largo / 2;                 // el mismo que aplica paletaIni()
  float min0 = (float)off;
  float max0 = (float)(PON_MITAD - 1 - (largo - 1) + off);
  float min1 = (float)(PON_MITAD + off);
  float max1 = (float)(NUM_LEDS - 1 - (largo - 1) + off);
  if (max0 < min0) max0 = min0;
  if (max1 < min1) max1 = min1;

  if (paletaCentro[0] < min0) paletaCentro[0] = min0;
  if (paletaCentro[0] > max0) paletaCentro[0] = max0;
  if (paletaCentro[1] < min1) paletaCentro[1] = min1;
  if (paletaCentro[1] > max1) paletaCentro[1] = max1;
}

static void moverPaletas() {
  uint32_t ahora = millis();
  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;
  if (dt > 0.1f) dt = 0.1f;

  for (uint8_t j = 0; j < 2; j++) {
    paletaCentro[j] += leerJoyNorm(j) * (float)PON_VEL_PALETA * dt;
  }
  limitarPaletas();
}

// La pelota esta al alcance si viene hacia esa punta y cae dentro de la paleta.
static bool alAlcance(uint8_t j) {
  if (j == 0 && pelotaDir != -1) return false;
  if (j == 1 && pelotaDir != +1) return false;
  return pelotaPos >= paletaIni(j) && pelotaPos <= paletaFin(j);
}

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

// La paleta se enciende a pleno cuando la pelota esta adentro: ese destello es
// el aviso de que ESE es el momento de apretar.
static void dibujarPaletas() {
  for (uint8_t j = 0; j < 2; j++) {
    CRGB c = CONTROLES[j].color;
    if (!alAlcance(j)) c.nscale8(28);
    for (int16_t i = paletaIni(j); i <= paletaFin(j); i++) setLed(i, c);
  }
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
  // Sale de la propia paleta: si saliera de la pared, una paleta adelantada
  // dejaria la pelota naciendo detras suyo.
  if (saca == 1) {
    pelotaPos = paletaFin(0) + 1;
    pelotaDir = +1;
  } else {
    pelotaPos = paletaIni(1) - 1;
    pelotaDir = -1;
  }
  irA(SACANDO);
}

// Cierra la partida: compara el total de golpes contra el record y elige el
// sonido. Los dos puntos donde se pierde la pelota hacen exactamente lo mismo.
static void terminarPunto() {
  // Los golpes del peloteo los pusieron los dos, asi que firma el que gano.
  if (ganador) esRecord = intentarRecord(REC_PONG, golpesTotales, ganador - 1);
  if (!ganador)      sonarPunto();
  else if (esRecord) sonarRecord();
  else               sonarVictoria();
  irA(ganador ? FIN : PUNTO);
}

void nuevoPong() {
  calibrarJoy(0);
  calibrarJoy(1);

  puntosP1 = puntosP2 = 0;
  saca          = 1;
  ganador       = 0;
  golpesTotales = 0;
  esRecord      = false;
  velSaque      = map(leerPoteCrudo(), 0, 4095, VEL_LENTA, VEL_RAPIDA);
  ultimoFrame   = millis();

  // Cada uno arranca pegado a su pared, que es la posicion defensiva.
  paletaCentro[0] = 0;
  paletaCentro[1] = NUM_LEDS - 1;
  limitarPaletas();

  prepararSaque();
}

// ---------- Estados ----------
static void loopSacando() {
  // El pote se sigue leyendo hasta el momento del saque: lo que muestra el LCD
  // es lo que va a salir.
  velSaque  = map(leerPoteCrudo(), 0, 4095, VEL_LENTA, VEL_RAPIDA);
  pelotaVel = velSaque;

  // Las paletas ya se mueven: los dos se acomodan antes de que salga la pelota.
  moverPaletas();
  if (saca == 1) pelotaPos = paletaFin(0) + 1;
  else           pelotaPos = paletaIni(1) - 1;

  // El saque arranca con el boton del que saca (o solo a los 4 s).
  uint8_t idx = saca - 1;
  bool lanzar = btnFlanco[idx] || (millis() - estadoDesde > 4000);

  FastLED.clear();
  dibujarMarcador();
  dibujarPaletas();
  if ((millis() / 300) % 2 == 0) dibujarPelota();   // parpadea en el punto de saque
  FastLED.show();

  if (lanzar) {
    sonarSaque();
    ultimoPaso = millis();
    irA(JUGANDO);
  }
}

static void loopJugando() {
  moverPaletas();

  // --- Golpe: valido si la pelota viene hacia tu punta y esta en tu paleta ---
  for (uint8_t j = 0; j < 2; j++) {
    if (!btnFlanco[j] || !alAlcance(j)) continue;
    pelotaDir = (j == 0) ? +1 : -1;
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
  dibujarPaletas();
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
  CRGB c = CONTROLES[ganador - 1].color;
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
      else          m = textoGana(ganador - 1);
      break;
  }
  lcdLinea(1, m);
}

String webPong() {
  return "Verde " + String(puntosP1) + " - " + String(puntosP2) + " Azul";
}
