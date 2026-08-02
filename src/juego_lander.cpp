// ---------- Lunar Lander vertical ----------
// El LED 0 es el suelo y el 99 el espacio. La gravedad tira siempre hacia el 0
// y el joystick hacia arriba enciende el motor, que gasta combustible mientras
// este encendido. La plataforma esta a una altura al azar: no alcanza con
// llegar abajo, hay que FRENAR justo ahi.
//
// Todo el juego se lee en la tira sin mirar el LCD:
//
//   verde        la plataforma
//   rojo tenue   el suelo (tocarlo es chocar)
//   nave verde   venis a velocidad de aterrizaje
//   nave amarilla venis al limite
//   nave roja    a esta velocidad te estrellas
//   naranja debajo de la nave: la llama, mas larga cuanto mas empuje
//
// Pasar por la plataforma SUBIENDO no cuenta como contacto (si no, despegar
// seria imposible): el contacto se evalua solo cuando la nave viene bajando.

#include "juego_lander.h"

// ---------- Parametros ----------
uint16_t LND_GRAVEDAD   = 26;   // LEDs/s^2 hacia la base
uint16_t LND_EMPUJE     = 60;   // LEDs/s^2 del motor a fondo
uint16_t LND_VEL_SEGURA = 14;   // maxima velocidad de contacto que se banca (LEDs/s)

uint16_t LND_COMBUSTIBLE = 5000;        // ms de motor a fondo por intento
const uint8_t  LND_INTENTOS     = 3;
const uint8_t  LND_PLAT_INI     = 9;    // largo de la plataforma en el primer aterrizaje
const uint8_t  LND_PLAT_MIN     = 3;
const int16_t  LND_PLAT_MIN_ALT = 8;    // la plataforma nunca se pega al suelo...
const int16_t  LND_PLAT_MAX_ALT = 70;   // ...ni queda tan alta que no se llegue
const uint16_t LND_POSADO_MS    = 1400; // festejo de un aterrizaje antes del siguiente
const uint16_t LND_FIN_MS       = 3200;

static const CRGB COL_PLATAFORMA = CRGB(  0, 255,  70);
static const CRGB COL_SUELO      = CRGB( 60,   0,   0);
static const CRGB COL_LLAMA      = CRGB(255,  90,   0);

// ---------- Estado ----------
enum EstadoLnd { LND_VOLANDO, LND_POSADO, LND_FIN };
static EstadoLnd estadoLnd;
static uint32_t  faseDesde;

static float    pos;             // altura de la nave, en LEDs (0 = suelo)
static float    vel;             // LEDs/s, negativa cuando cae
static float    empujeActual;    // 0..1, para dibujar la llama
static int32_t  combustible;     // ms de motor que quedan
static uint32_t ultimoFrame;

static int16_t  platIni, platFin;
static uint8_t  platLargo;
static uint8_t  aterrizajes;     // el score
static uint8_t  intentos;
static bool     esRecord;
static bool     choco;           // como termino: chocando o sin combustible al caer

// ---------- Sonido ----------
static const Nota JINGLE_POSADO[] = { {659, 100}, {784, 100}, {1047, 240} };
static const Nota JINGLE_FIN[]    = { {330, 170}, {262, 170}, {208, 170}, {131, 520} };

static void sonarChoque()   { beep(110, 320); }
static void sonarPosado()   { tocarJingle(JINGLE_POSADO, 3); }
static void sonarGameOver() { tocarJingle(JINGLE_FIN, 4); }

// ---------- Preparacion ----------
// Cada aterrizaje logrado deja la plataforma mas corta y mas alta: la nafta
// alcanza para menos correcciones y el margen de error se achica.
static void nuevaPlataforma() {
  // En int16 a proposito: con uint8 el largo daria la vuelta a 255 en cuanto
  // hubiera mas aterrizajes que LND_PLAT_INI, y la plataforma pasaria de ser
  // imposible a ocupar la tira entera.
  int16_t largo = (int16_t)LND_PLAT_INI - (int16_t)aterrizajes;
  if (largo < LND_PLAT_MIN) largo = LND_PLAT_MIN;
  platLargo = (uint8_t)largo;

  int16_t maxAlt = LND_PLAT_MAX_ALT;
  if (maxAlt > NUM_LEDS - 1 - platLargo) maxAlt = NUM_LEDS - 1 - platLargo;
  platIni = random(LND_PLAT_MIN_ALT, maxAlt + 1);
  platFin = platIni + platLargo - 1;

  pos          = NUM_LEDS - 1;    // se entra en orbita, arriba de todo
  vel          = 0;
  empujeActual = 0;
  combustible  = LND_COMBUSTIBLE;
  ultimoFrame  = millis();
}

void nuevoLander() {
  calibrarJoyY();
  aterrizajes = 0;
  intentos    = LND_INTENTOS;
  esRecord    = false;
  choco       = false;
  estadoLnd   = LND_VOLANDO;
  nuevaPlataforma();
}

static void perder() {
  estadoLnd = LND_FIN;
  faseDesde = millis();
  esRecord  = intentarRecord(REC_LANDER, aterrizajes);
  esRecord ? sonarRecord() : sonarGameOver();
}

// Un intento fallido: se pierde una nave y, si quedan, se genera todo de nuevo.
static void estrellarse() {
  choco = true;
  sonarChoque();
  intentos--;
  if (intentos == 0) { perder(); return; }
  estadoLnd = LND_POSADO;      // reusa la pausa: el LCD distingue por `choco`
  faseDesde = millis();
}

static void aterrizar() {
  choco = false;
  aterrizajes++;
  sonarPosado();
  estadoLnd = LND_POSADO;
  faseDesde = millis();
}

// Color de la nave segun a que velocidad viene bajando: el indicador mas
// importante del juego y no cuesta ni una linea de LCD.
static CRGB colorNave() {
  float caida = (vel < 0) ? -vel : 0;
  if (caida <= LND_VEL_SEGURA)         return CRGB(  0, 255,  40);   // llegas bien
  if (caida <= LND_VEL_SEGURA * 1.8f)  return CRGB(255, 170,   0);   // al limite
  return CRGB(255, 0, 0);                                            // te estrellas
}

static void dibujarEscena() {
  FastLED.clear();
  setLed(0, COL_SUELO);
  for (int16_t i = platIni; i <= platFin; i++) setLed(i, COL_PLATAFORMA);

  int16_t nave = (int16_t)(pos + 0.5f);
  // Llama: hasta 3 LEDs por debajo de la nave, proporcional al empuje.
  int16_t largoLlama = (int16_t)(empujeActual * 3.0f + 0.5f);
  for (int16_t k = 1; k <= largoLlama; k++) {
    CRGB c = COL_LLAMA;
    c.nscale8(255 / (k + 1));
    setLed(nave - k, c);
  }
  setLed(nave, colorNave());
}

void loopLander() {
  uint32_t ahora = millis();

  if (estadoLnd == LND_FIN) {
    // Derrota: la tira se apaga de arriba hacia abajo hasta el suelo.
    uint32_t t = ahora - faseDesde;
    int16_t  techo = NUM_LEDS - 1 - (int16_t)(t / 28);
    FastLED.clear();
    for (int16_t i = 0; i <= techo; i++) setLed(i, CRGB(50, 0, 0));
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > LND_FIN_MS) volverAlMenu();
    return;
  }

  if (estadoLnd == LND_POSADO) {
    uint32_t t = ahora - faseDesde;
    dibujarEscena();
    if (choco) {                              // explosion naranja en el punto de impacto
      int16_t nave = (int16_t)(pos + 0.5f);
      int16_t r = t / 30;
      for (int16_t k = -r; k <= r; k++) setLed(nave + k, ((t / 90) % 2 == 0) ? COL_LLAMA : CRGB(40, 8, 0));
    } else {                                  // la plataforma festeja parpadeando
      if ((t / 150) % 2 == 0) {
        for (int16_t i = platIni; i <= platFin; i++) setLed(i, CRGB::White);
      }
    }
    FastLED.show();
    if (t > LND_POSADO_MS) {
      nuevaPlataforma();
      estadoLnd = LND_VOLANDO;
    }
    return;
  }

  // --- Fisica ---
  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;
  if (dt > 0.1f) dt = 0.1f;         // por si un refresco del LCD se comio un frame largo

  // Solo empuja hacia arriba: inclinar el stick hacia abajo no hace nada, la
  // gravedad ya se encarga de eso.
  float mando = leerJoyYNorm();
  empujeActual = (mando > 0 && combustible > 0) ? mando : 0.0f;
  if (empujeActual > 0) {
    vel += LND_EMPUJE * empujeActual * dt;
    combustible -= (int32_t)(empujeActual * dt * 1000.0f);
    if (combustible < 0) combustible = 0;
  }
  vel -= LND_GRAVEDAD * dt;
  pos += vel * dt;

  if (pos > NUM_LEDS - 1) {         // techo: no se puede salir de la tira
    pos = NUM_LEDS - 1;
    if (vel > 0) vel = 0;
  }

  int16_t nave = (int16_t)(pos + 0.5f);
  float   caida = (vel < 0) ? -vel : 0;

  if (vel <= 0) {                   // el contacto solo se evalua bajando
    if (nave >= platIni && nave <= platFin) {
      if (caida <= LND_VEL_SEGURA) {
        pos = platIni;              // se apoya prolijo sobre la plataforma
        vel = 0;
        aterrizar();
      } else {
        estrellarse();
      }
      return;
    }
    if (pos <= 0) {                 // llego al suelo por fuera de la plataforma
      pos = 0;
      estrellarse();
      return;
    }
  }

  dibujarEscena();
  FastLED.show();
}

// ---------- LCD ----------
void lcdLander() {
  if (estadoLnd == LND_FIN) {
    lcdLinea(0, "** PERDISTE **");
    if (esRecord) lcdLinea(1, "*NUEVO RECORD!*");
    else          lcdLinea(1, "Aterrizajes: " + String(aterrizajes));
    return;
  }
  if (estadoLnd == LND_POSADO) {
    lcdLinea(0, choco ? "** CHOCASTE **" : "* ATERRIZASTE *");
    lcdLinea(1, choco ? ("Naves: " + String(intentos))
                      : ("Aterrizajes: " + String(aterrizajes)));
    return;
  }
  lcdLinea(0, "Aterr " + String(aterrizajes) + "  Naves " + String(intentos));
  lcdLinea(1, "F" + barraLCD(combustible, LND_COMBUSTIBLE, 15));
}

String webLander() {
  uint16_t tope = LND_COMBUSTIBLE ? LND_COMBUSTIBLE : 1;   // el panel web puede dejarlo en 0
  return "Aterrizajes: " + String(aterrizajes) + ", naves " + String(intentos) +
         ", combustible " + String((combustible * 100) / tope) + "%";
}
