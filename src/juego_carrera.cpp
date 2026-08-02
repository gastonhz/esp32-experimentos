// ---------- Carrera: OpenLEDRace en 100 LEDs ----------
// Adaptacion del OpenLEDRace de gbarbarov. Cada pulsacion suma un empujon de
// velocidad, la friccion frena en todo momento y el auto avanza solo mientras
// le quede inercia. Es el unico juego de la consola donde el boton no dispara
// un evento sino que alimenta un sistema fisico: martillar rapido sube la
// velocidad de crucero, dejar de martillar la deja caer sola.
//
// La pista es un ANILLO: el LED 99 conecta con el 0 y cada vuelta suma al
// contador. Con 100 LEDs una vuelta es corta, asi que la carrera se hace larga
// por cantidad de vueltas (las fija el pote antes de largar) y no por distancia.
//
// Lo que hace que no sea solo martillar es la pendiente:
//
//   naranja tenue   cuesta arriba, te resta velocidad mientras estas encima
//   verde tenue     bajada, te la devuelve
//   apagado         llano
//
// Cada cuesta tiene la subida y la bajada del mismo largo, asi que a lo largo
// de una vuelta la pendiente no regala ni roba nada: lo que cambia es DONDE
// conviene gastar el esfuerzo. Martillar en la bajada es desperdiciarlo porque
// la velocidad ya viene sola; lo que rinde es llegar lanzado al pie de la
// cuesta y no aflojar mientras se sube.

#include "juego_carrera.h"

// ---------- Parametros ----------
uint16_t CAR_IMPULSO     = 6;     // LEDs/s que suma cada pulsacion
uint16_t CAR_FRICCION    = 180;   // frenado por segundo, en centesimas (180 = 1.80/s)
uint16_t CAR_GRAVEDAD    = 22;    // LEDs/s^2 de la pendiente maxima
uint16_t CAR_VUELTAS_MIN = 3;     // vueltas con el pote al minimo
uint16_t CAR_VUELTAS_MAX = 9;     // vueltas con el pote al maximo

const uint16_t CAR_LARGADA_MS = 3200;  // semaforo: tres tiempos y a correr
const uint16_t CAR_FIN_MS     = 4000;
const uint8_t  CAR_ESTELA     = 2;     // largo de la cola de cada auto
const int16_t  CAR_MARGEN     = 8;     // LEDs llanos garantizados en la largada

static const CRGB COL_SUBIDA = CRGB(60, 18, 0);   // tenue: es terreno, no un objeto
static const CRGB COL_BAJADA = CRGB( 0, 45, 12);

// ---------- Estado ----------
enum EstadoCar { CAR_LARGADA, CAR_CORRIENDO, CAR_FIN };
static EstadoCar estadoCar;
static uint32_t  faseDesde;

static float    pos[2];          // posicion continua dentro de la vuelta (0..NUM_LEDS)
static float    vel[2];          // LEDs/s, nunca negativa: los autos no van marcha atras
static uint8_t  vueltas[2];
static uint32_t vueltaDesde[2];  // millis() en que empezo la vuelta en curso
static uint32_t mejorVuelta;     // la mejor de la carrera, en ms (0 = ninguna cerrada)
static uint8_t  vueltasMeta;
static uint8_t  ganador;         // 1 o 2, valido en CAR_FIN
static bool     esRecord;
static uint32_t ultimoFrame;
static uint8_t  ultimoTiempo;    // ultimo escalon del semaforo que ya sono

// Perfil de la pista: -100 es la cuesta mas empinada hacia arriba, +100 la
// bajada mas pronunciada, 0 es llano. Un byte por LED alcanza y sobra.
static int8_t   pendiente[NUM_LEDS];

// ---------- Sonido ----------
static const Nota JINGLE_META[] = { {784, 90}, {988, 90}, {1319, 240} };

static void sonarEmpujon(uint8_t i) { beep(i == 0 ? 1300 : 1050, 14); }  // uno por jugador
static void sonarVuelta()           { beep(1800, 40); }
static void sonarSemaforo()         { beep( 700, 120); }
static void sonarLargada()          { beep(1400, 260); }
static void sonarMeta()             { tocarJingle(JINGLE_META, 3); }

// ---------- Pista ----------
// Una o dos cuestas por carrera, con la subida y la bajada del mismo largo. Se
// generan al azar en cada partida: el circuito se aprende durante la carrera,
// que es la mitad de la gracia de correr varias vueltas sobre el mismo trazado.
static void generarPista() {
  for (int16_t i = 0; i < NUM_LEDS; i++) pendiente[i] = 0;

  uint8_t cuestas = random(1, 3);
  int16_t cursor  = CAR_MARGEN;                 // la largada siempre es llana
  for (uint8_t k = 0; k < cuestas; k++) {
    int16_t largo = (int16_t)random(8, 14) * 2; // par: se parte justo al medio
    if (cursor + largo > NUM_LEDS - CAR_MARGEN) break;
    for (int16_t i = 0;         i < largo / 2; i++) pendiente[cursor + i] = -100;
    for (int16_t i = largo / 2; i < largo;     i++) pendiente[cursor + i] = +100;
    cursor += largo + (int16_t)random(12, 25);
  }
}

void nuevoCarrera() {
  vueltasMeta = map(leerPoteCrudo(), 0, 4095, CAR_VUELTAS_MIN, CAR_VUELTAS_MAX);
  if (vueltasMeta < 1) vueltasMeta = 1;

  for (uint8_t i = 0; i < 2; i++) {
    pos[i]         = 0;
    vel[i]         = 0;
    vueltas[i]     = 0;
    vueltaDesde[i] = 0;
  }
  mejorVuelta  = 0;
  ganador      = 0;
  esRecord     = false;
  ultimoTiempo = 0;
  generarPista();
  estadoCar = CAR_LARGADA;
  faseDesde = millis();
}

// ---------- Dibujo ----------
static void dibujarPista() {
  FastLED.clear();
  for (int16_t i = 0; i < NUM_LEDS; i++) {
    if      (pendiente[i] < 0) leds[i] = COL_SUBIDA;
    else if (pendiente[i] > 0) leds[i] = COL_BAJADA;
  }
}

// La pista es un anillo, asi que la cola tiene que dar la vuelta por el 0 en
// vez de cortarse (setLed descarta los indices negativos).
static void dibujarAuto(int16_t led, const CRGB& col) {
  for (uint8_t k = CAR_ESTELA; k >= 1; k--) {
    CRGB c = col;
    c.nscale8(255 / (k + 1));
    leds[(led - k + NUM_LEDS) % NUM_LEDS] = c;
  }
  leds[led] = col;
}

static void dibujarAutos() {
  int16_t l1 = (int16_t)pos[0] % NUM_LEDS;
  int16_t l2 = (int16_t)pos[1] % NUM_LEDS;
  // El que va atras se dibuja primero para que el puntero se lo lleve el que
  // va ganando; si estan en el mismo LED va blanco, que se lee como "pegados".
  if (vueltas[0] * NUM_LEDS + l1 < vueltas[1] * NUM_LEDS + l2) {
    dibujarAuto(l1, COL_P1); dibujarAuto(l2, COL_P2);
  } else {
    dibujarAuto(l2, COL_P2); dibujarAuto(l1, COL_P1);
  }
  if (l1 == l2) leds[l1] = CRGB::White;
}

// ---------- Fisica ----------
// Empujon del boton, pendiente y friccion, en ese orden. La friccion es
// proporcional a la velocidad (como en el OpenLEDRace original), asi que hay
// una velocidad de crucero para cada ritmo de pulsacion: no se acumula infinito
// por mas rapido que se martille.
static void avanzar(uint8_t i, float dt, uint32_t ahora) {
  if (btnFlanco[i]) {
    vel[i] += CAR_IMPULSO;
    sonarEmpujon(i);
  }

  int16_t led = (int16_t)pos[i] % NUM_LEDS;
  vel[i] += (pendiente[led] / 100.0f) * CAR_GRAVEDAD * dt;
  vel[i] -= vel[i] * (CAR_FRICCION / 100.0f) * dt;
  if (vel[i] < 0) vel[i] = 0;          // cuesta arriba se para, no retrocede

  pos[i] += vel[i] * dt;

  if (pos[i] >= NUM_LEDS) {
    pos[i] -= NUM_LEDS;
    vueltas[i]++;

    uint32_t t = ahora - vueltaDesde[i];
    vueltaDesde[i] = ahora;
    if (mejorVuelta == 0 || t < mejorVuelta) mejorVuelta = t;

    if (vueltas[i] >= vueltasMeta) {
      ganador   = i + 1;
      estadoCar = CAR_FIN;
      faseDesde = ahora;
      esRecord  = intentarRecord(REC_CARRERA, mejorVuelta);
      esRecord ? sonarRecord() : sonarMeta();
      return;
    }
    sonarVuelta();
  }
}

void loopCarrera() {
  uint32_t ahora = millis();

  if (estadoCar == CAR_FIN) {
    // Festejo del ganador recorriendo la pista, con la vuelta rapida marcada
    // en dorado si ademas fue record.
    uint32_t t = ahora - faseDesde;
    CRGB c = (ganador == 1) ? COL_P1 : COL_P2;
    FastLED.clear();
    for (int16_t i = 0; i < NUM_LEDS; i++) {
      if ((i + t / 30) % 5 == 0) leds[i] = c;
    }
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > CAR_FIN_MS) volverAlMenu();
    return;
  }

  if (estadoCar == CAR_LARGADA) {
    // Semaforo: tres destellos rojos, uno por segundo, y el verde de largada.
    uint32_t t = ahora - faseDesde;
    uint8_t  paso = t / 800;                 // 0,1,2 rojos y 3 = verde
    if (paso != ultimoTiempo) {
      ultimoTiempo = paso;
      (paso >= 3) ? sonarLargada() : sonarSemaforo();
    }

    dibujarPista();
    bool destello = (t % 800) < 260;
    if (destello) {
      CRGB luz = (paso >= 3) ? CRGB(0, 120, 0) : CRGB(120, 0, 0);
      for (int16_t i = 0; i < NUM_LEDS; i++) leds[i] += luz;
    }
    dibujarAutos();
    FastLED.show();

    if (t > CAR_LARGADA_MS) {
      estadoCar      = CAR_CORRIENDO;
      ultimoFrame    = ahora;
      vueltaDesde[0] = ahora;
      vueltaDesde[1] = ahora;
    }
    return;
  }

  // --- CAR_CORRIENDO ---
  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;
  if (dt > 0.1f) dt = 0.1f;

  avanzar(0, dt, ahora);
  if (estadoCar != CAR_CORRIENDO) return;    // el primero ya cruzo la meta
  avanzar(1, dt, ahora);
  if (estadoCar != CAR_CORRIENDO) return;

  dibujarPista();
  dibujarAutos();
  FastLED.show();
}

// ---------- LCD ----------
void lcdCarrera() {
  if (estadoCar == CAR_FIN) {
    lcdLinea(0, (ganador == 1) ? "** GANA VERDE **" : "** GANA AZUL **");
    if (esRecord) lcdLinea(1, "*RECORD " + String(mejorVuelta) + " ms*");
    else          lcdLinea(1, "Mejor " + String(mejorVuelta) + " ms");
    return;
  }
  if (estadoCar == CAR_LARGADA) {
    lcdLinea(0, "Carrera: " + String(vueltasMeta) + " vue");
    uint8_t queda = 3 - min<int>(3, (millis() - faseDesde) / 800);
    lcdLinea(1, queda ? ("Listos... " + String(queda)) : "YA!");
    return;
  }
  lcdLinea(0, "V " + String(vueltas[0]) + "/" + String(vueltasMeta) +
              "   A " + String(vueltas[1]) + "/" + String(vueltasMeta));
  lcdLinea(1, mejorVuelta ? ("Mejor " + String(mejorVuelta) + " ms") : "- corriendo -");
}

String webCarrera() {
  return "Verde " + String(vueltas[0]) + "/" + String(vueltasMeta) +
         " - Azul " + String(vueltas[1]) + "/" + String(vueltasMeta) +
         (mejorVuelta ? (", mejor vuelta " + String(mejorVuelta) + " ms") : "");
}
