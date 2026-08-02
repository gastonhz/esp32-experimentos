// ---------- Esquiva: cruzar los muros cuando estan apagados ----------
// OJO con la idea intuitiva de este juego, porque no funciona: "meterse en un
// hueco y despues cruzar al siguiente" es imposible en una dimension. Si el
// muro es solido, pasar del hueco de abajo al de arriba obliga a ocupar los
// LEDs del muro, o sea a morir. Y como el flujo baja sin parar, el hueco en el
// que estas termina saliendose por la base y no queda ningun lugar al que ir:
// el jugador sobrevive los pocos segundos que tarda el primer muro en llegarle
// y despues muere haga lo que haga.
//
// En 1D un obstaculo solido que se mueve no se esquiva en el espacio. Solo en
// el tiempo. Por eso cada muro PARPADEA con su propio ciclo mientras baja:
//
//   rojo brillante   peligroso, tocarlo mata
//   rojo muy tenue   apagado, se puede atravesar
//   parpadeo rapido  aviso de que esta por encenderse
//
// El juego es leer los ciclos y cruzar en la ventana oscura. Como no se puede
// bajar mas alla del LED 0, el flujo obliga a trepar todo el tiempo: quedarse
// quieto abajo no es una estrategia, es la muerte con demora.
//
// Los muros se generan de a uno arriba de todo, con un espacio libre garantizado
// respecto del anterior, y todos comparten un unico reloj de bajada: por eso el
// conjunto se lee como una sola pared que se desplaza.

#include "juego_esquiva.h"

// ---------- Parametros ----------
uint16_t ESQ_VEL_JUGADOR = 60;   // LEDs por segundo del jugador
uint16_t ESQ_HUECO_MIN   = 7;    // hueco mas chico que puede generarse (LEDs)

const uint8_t  ESQ_MAX_MUROS   = 10;  // tope de muros vivos en la tira a la vez
const uint8_t  ESQ_HUECO_MAX   = 16;
const uint8_t  ESQ_MURO_MIN    = 5;   // largo de cada muro
const uint8_t  ESQ_MURO_MAX    = 14;
const uint16_t ESQ_VEL_LENTA   = 130; // bajada mas lenta, pote al minimo (ms por LED)
const uint16_t ESQ_VEL_RAPIDA  = 70;  // bajada mas rapida, pote al maximo (ms por LED)
const uint16_t ESQ_VEL_MINIMA  = 32;  // piso de dificultad
const uint8_t  ESQ_ACELERA_CADA = 4;  // cada cuantos muros esquivados se acelera
const uint16_t ESQ_FIN_MS      = 3000;
// Techo del jugador. Existe por dos razones: para que no pueda quedarse
// campeando arriba de todo donde nunca lo alcanza nada, y sobre todo para que
// los muros nuevos, que entran por la punta, JAMAS puedan aparecer encima suyo
// (seria una muerte imposible de ver venir).
const int16_t  ESQ_TECHO       = NUM_LEDS - 16;
// Ciclo de encendido de cada muro. Apagado dura mas que encendido: siempre hay
// ventana para cruzar. El aviso existe por la misma razon que en la lava de
// Twang: una muerte que no se pudo ver venir no ensena nada.
const uint16_t ESQ_ON_MS       = 1500;
const uint16_t ESQ_OFF_MS      = 2300;
const uint16_t ESQ_AVISO_MS    = 450;

static const CRGB COL_MURO    = CRGB(255,  30,   0);
static const CRGB COL_JUGADOR = CRGB(  0, 255,  80);

// ---------- Estado ----------
enum EstadoEsq { ESQ_JUGANDO, ESQ_FIN };
static EstadoEsq estadoEsq;
static uint32_t  faseDesde;

// Muros en arrays paralelos, mismo patron que los enemigos de Twang. Ocupan
// [ini, fin] con los dos extremos incluidos.
static int16_t  muroIni[ESQ_MAX_MUROS];
static int16_t  muroFin[ESQ_MAX_MUROS];
static bool     muroVivo[ESQ_MAX_MUROS];
static bool     muroOn[ESQ_MAX_MUROS];      // encendido ahora (mata)
static uint32_t muroCambio[ESQ_MAX_MUROS];  // millis() del ultimo cambio on/off

static float    jugadorPos;      // posicion continua
static uint32_t ultimoFrame;
static uint16_t velBajada;       // ms por LED, comun a todos los muros
static uint32_t ultimaBajada;
static uint16_t esquivados;      // muros que salieron por la base (el score)
static bool     esRecord;
static int16_t  caida;           // donde lo agarraron: centro de la animacion de derrota

// ---------- Sonido ----------
static const Nota JINGLE_FIN[] = { {349, 160}, {294, 160}, {233, 160}, {147, 500} };

static void sonarPasar()    { beep(1700, 18); }
static void sonarChoque()   { beep( 130, 260); }
static void sonarGameOver() { tocarJingle(JINGLE_FIN, 4); }

// ---------- Generacion ----------
// Devuelve el LED mas alto ocupado por algun muro; -1 si la tira esta limpia.
static int16_t techoOcupado() {
  int16_t techo = -1;
  for (uint8_t i = 0; i < ESQ_MAX_MUROS; i++) {
    if (muroVivo[i] && muroFin[i] > techo) techo = muroFin[i];
  }
  return techo;
}

// Coloca un muro que arranca un hueco al azar por encima de `base`. Devuelve
// false si no entra en la tira (no hay lugar todavia, o no quedan slots). El
// extremo de arriba puede pasarse del LED 99 a proposito: el muro entra
// deslizandose desde afuera en vez de aparecer entero de golpe.
static bool colocarMuro(int16_t base) {
  int16_t ini = base + 1 + (int16_t)random(ESQ_HUECO_MIN, ESQ_HUECO_MAX + 1);
  if (ini > NUM_LEDS - 1) return false;

  for (uint8_t i = 0; i < ESQ_MAX_MUROS; i++) {
    if (muroVivo[i]) continue;
    muroVivo[i] = true;
    muroIni[i]  = ini;
    muroFin[i]  = ini + (int16_t)random(ESQ_MURO_MIN, ESQ_MURO_MAX + 1) - 1;
    // Nace apagado y con la fase corrida al azar: si todos parpadearan al
    // unisono la tira seria un unico semaforo y cruzar seria trivial (o
    // imposible) de una sola vez para todos los muros a la vez.
    muroOn[i]     = false;
    muroCambio[i] = millis() - (uint32_t)random(0, ESQ_OFF_MS);
    return true;
  }
  return false;
}

// En partida los muros entran siempre por encima del techo del jugador, aunque
// la tira haya quedado despoblada: es lo que garantiza que nunca aparezca uno
// sobre el jugador.
static void generarMuro() {
  int16_t base = techoOcupado();
  if (base < ESQ_TECHO) base = ESQ_TECHO;
  colocarMuro(base);
}

void nuevoEsquiva() {
  calibrarJoyY();
  for (uint8_t i = 0; i < ESQ_MAX_MUROS; i++) muroVivo[i] = false;
  jugadorPos   = 0;
  ultimoFrame  = millis();
  velBajada    = map(leerPoteCrudo(), 0, 4095, ESQ_VEL_LENTA, ESQ_VEL_RAPIDA);
  ultimaBajada = millis();
  esquivados   = 0;
  esRecord     = false;
  caida        = 0;
  estadoEsq    = ESQ_JUGANDO;

  // Se arranca con la tira ya poblada del tercio superior para arriba: el
  // jugador ve venir el patron entero y tiene unos segundos antes del primer
  // cruce, en vez de comerse un muro que aparecio a diez LEDs suyos.
  int16_t base = NUM_LEDS / 3;
  while (colocarMuro(base)) base = techoOcupado();
}

static void perder(int16_t donde) {
  caida     = donde;
  estadoEsq = ESQ_FIN;
  faseDesde = millis();
  sonarChoque();
  esRecord  = intentarRecord(REC_ESQUIVA, esquivados);
  if (esRecord) sonarRecord(); else sonarGameOver();
}

void loopEsquiva() {
  uint32_t ahora = millis();

  if (estadoEsq == ESQ_FIN) {
    // Derrota: onda naranja que se expande desde donde lo agarraron.
    uint32_t t  = ahora - faseDesde;
    bool     on = (t / 110) % 2 == 0;
    int16_t  r  = t / 22;
    FastLED.clear();
    for (int16_t k = -r; k <= r; k++) setLed(caida + k, on ? COL_MURO : CRGB(50, 6, 0));
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > ESQ_FIN_MS) volverAlMenu();
    return;
  }

  // --- Movimiento del jugador ---
  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;
  jugadorPos += leerJoyYNorm() * ESQ_VEL_JUGADOR * dt;
  if (jugadorPos < 0)          jugadorPos = 0;
  if (jugadorPos > ESQ_TECHO)  jugadorPos = ESQ_TECHO;
  int16_t jugadorLed = (int16_t)(jugadorPos + 0.5f);

  // --- Bajada del flujo: todos los muros a la vez ---
  if (ahora - ultimaBajada >= velBajada) {
    ultimaBajada += velBajada;
    for (uint8_t i = 0; i < ESQ_MAX_MUROS; i++) {
      if (!muroVivo[i]) continue;
      muroIni[i]--;
      muroFin[i]--;
      if (muroFin[i] < 0) {                    // salio por la base: esquivado
        muroVivo[i] = false;
        esquivados++;
        sonarPasar();
        if (esquivados % ESQ_ACELERA_CADA == 0) {
          velBajada = max<int>(ESQ_VEL_MINIMA, (velBajada * 90) / 100);
        }
      }
    }
    generarMuro();                             // rellena por arriba a medida que baja
  }

  // --- Ciclo de encendido y colision ---
  // Estar dentro de un muro solo mata si esta encendido: atravesarlo apagado es
  // la unica forma de avanzar, y es todo el juego.
  for (uint8_t i = 0; i < ESQ_MAX_MUROS; i++) {
    if (!muroVivo[i]) continue;
    uint16_t dur = muroOn[i] ? ESQ_ON_MS : ESQ_OFF_MS;
    if (ahora - muroCambio[i] >= dur) {
      muroCambio[i] += dur;
      muroOn[i] = !muroOn[i];
    }
    if (muroOn[i] && jugadorLed >= muroIni[i] && jugadorLed <= muroFin[i]) {
      perder(jugadorLed);
      return;
    }
  }

  // --- Dibujo ---
  // Apagado se dibuja tenue (hay que ver donde esta para planear el cruce) y en
  // los ultimos ESQ_AVISO_MS antes de encenderse parpadea rapido.
  FastLED.clear();
  for (uint8_t i = 0; i < ESQ_MAX_MUROS; i++) {
    if (!muroVivo[i]) continue;
    CRGB c = COL_MURO;
    if (!muroOn[i]) {
      bool aviso = (ahora - muroCambio[i] + ESQ_AVISO_MS >= ESQ_OFF_MS);
      if (aviso) c.nscale8(((ahora / 60) % 2 == 0) ? 255 : 30);
      else       c.nscale8(22);
    }
    for (int16_t p = muroIni[i]; p <= muroFin[i]; p++) setLed(p, c);
  }
  setLed(jugadorLed, COL_JUGADOR);   // el jugador va ultimo: siempre visible
  FastLED.show();
}

// ---------- LCD ----------
void lcdEsquiva() {
  if (estadoEsq == ESQ_FIN) {
    lcdLinea(0, "** CHOCASTE **");
    if (esRecord) lcdLinea(1, "*NUEVO RECORD!*");
    else          lcdLinea(1, "Muros: " + String(esquivados));
    return;
  }
  lcdLinea(0, "Muros: " + String(esquivados));
  lcdLinea(1, "Cruza en oscuro");   // la unica instruccion que necesita el juego
}

String webEsquiva() {
  return "Muros esquivados: " + String(esquivados);
}
