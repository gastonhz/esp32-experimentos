// ---------- Carrera: OpenLEDRace en 100 LEDs ----------
// Adaptacion del OpenLEDRace de gbarbarov. Cada pulsacion suma un empujon de
// velocidad, la friccion frena en todo momento y el auto avanza solo mientras
// le quede inercia. Es el unico juego de la consola donde el boton no dispara
// un evento sino que alimenta un sistema fisico: martillar rapido sube la
// velocidad de crucero, dejar de martillar la deja caer sola.
//
// Corren de dos a cuatro autos, la cantidad se elige antes de largar (tener los
// cuatro controles enchufados no quiere decir que siempre haya cuatro personas).
// Cada uno con su color. En una tira de 100 LEDs cuatro autos van a estar encimados casi todo
// el tiempo, asi que el orden de dibujo importa: se pintan de ultimo a primero,
// para que el que va ganando quede SIEMPRE arriba y el peloton no lo tape.
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
// Cada cuesta es subida + un llano en la cima + bajada, con las dos rampas del
// mismo largo, asi que a lo largo de una vuelta la pendiente no regala ni roba
// nada: lo que cambia es DONDE conviene gastar el esfuerzo. Martillar en la
// bajada es desperdiciarlo porque la velocidad ya viene sola; lo que rinde es
// llegar lanzado al pie de la cuesta y no aflojar mientras se sube. El llano de
// la cima es el que cobra los errores: el que corona sin inercia se queda ahi
// arriba sin nada que lo empuje.

#include "juego_carrera.h"

// ---------- Parametros ----------
uint16_t CAR_IMPULSO     = 6;     // LEDs/s que suma cada pulsacion
uint16_t CAR_FRICCION    = 180;   // frenado por segundo, en centesimas (180 = 1.80/s)
uint16_t CAR_GRAVEDAD    = 22;    // LEDs/s^2 de la pendiente maxima
uint16_t CAR_VUELTAS_MIN = 3;     // vueltas con el pote al minimo
uint16_t CAR_VUELTAS_MAX = 9;     // vueltas con el pote al maximo
uint16_t CAR_MESETA_MIN  = 4;     // llano mas corto en la cima de una cuesta (LEDs)
uint16_t CAR_MESETA_MAX  = 8;     // llano mas largo en la cima (LEDs)

const uint16_t CAR_LARGADA_MS = 3200;  // semaforo: tres tiempos y a correr
const uint16_t CAR_FIN_MS     = 4000;
const uint8_t  CAR_ESTELA     = 2;     // largo de la cola de cada auto
const int16_t  CAR_MARGEN     = 8;     // LEDs llanos garantizados en la largada

// En la carrera el terreno va tenue para no competir con los autos. En la
// pantalla de Ajustes, en cambio, la pista ES lo que se esta mirando, y muchas
// veces desde el otro lado de la habitacion: ahi se dibuja a pleno.
static const CRGB COL_SUBIDA_VER = CRGB(255,  76,   0);
static const CRGB COL_BAJADA_VER = CRGB(  0, 190,  50);

static const CRGB COL_SUBIDA = CRGB(60, 18, 0);   // tenue: es terreno, no un objeto
static const CRGB COL_BAJADA = CRGB( 0, 45, 12);

// ---------- Estado ----------
enum EstadoCar { CAR_ELIGIENDO, CAR_LARGADA, CAR_CORRIENDO, CAR_FIN };
static EstadoCar estadoCar;
static uint32_t  faseDesde;

static bool     jugando[NUM_CONTROLES];   // controles que largaron esta carrera
static uint8_t  numJugadores;
static float    pos[NUM_CONTROLES];       // posicion continua dentro de la vuelta (0..LARGO_TIRA)
static float    vel[NUM_CONTROLES];       // LEDs/s, nunca negativa: los autos no van marcha atras
static uint8_t  vueltas[NUM_CONTROLES];
static uint32_t vueltaDesde[NUM_CONTROLES];  // millis() en que empezo la vuelta en curso
static uint32_t mejorVuelta;     // la mejor de la carrera, en ms (0 = ninguna cerrada)
static uint8_t  mejorVueltaDe;   // quien la hizo: el record es suyo aunque gane otro
static uint8_t  vueltasMeta;
static uint8_t  ganador;         // indice de control, valido en CAR_FIN
static bool     esRecord;
static uint32_t ultimoFrame;
static uint8_t  ultimoTiempo;    // ultimo escalon del semaforo que ya sono

// Perfil de la pista: -100 es la cuesta mas empinada hacia arriba, +100 la
// bajada mas pronunciada, 0 es llano. Un byte por LED alcanza y sobra.
static int8_t   pendiente[LEDS_MAX];

// ---------- Sonido ----------
static const Nota JINGLE_META[] = { {784, 90}, {988, 90}, {1319, 240} };

// Un tono por auto: con cuatro martillando a la vez, el oido es la unica forma
// de saber si el empujon propio entro.
static const uint16_t CAR_TONO[NUM_CONTROLES] = { 1300, 1050, 1550, 850 };
static void sonarEmpujon(uint8_t i) { beep(CAR_TONO[i], 14); }
static void sonarVuelta()           { beep(1800, 40); }
static void sonarSemaforo()         { beep( 700, 120); }
static void sonarLargada()          { beep(1400, 260); }
static void sonarMeta()             { tocarJingle(JINGLE_META, 3); }

// ---------- Pista ----------
// Una o dos cuestas por carrera, generadas al azar en cada partida: el circuito
// se aprende durante la carrera, que es la mitad de la gracia de correr varias
// vueltas sobre el mismo trazado.
//
// Cada cuesta es subida + MESETA + bajada, con las dos rampas del mismo largo.
// Que midan igual es lo que hace que a lo largo de una vuelta la pendiente no
// regale ni robe nada; lo unico que cambia es donde conviene gastar el esfuerzo.
// La meseta es llano puro, asi que no rompe ese balance, pero cambia bastante
// como se corre la cuesta: sin ella, subida y bajada quedan pegadas y coronar es
// instantaneo -- se llega arriba y ya te esta devolviendo la velocidad. Con un
// tramo llano en la cima, el que llego sin inercia se queda ahi arriba pedaleando
// en falso, y el que dosifico bien lo cruza lanzado. El llano es lo que le da
// consecuencia a haber subido mal.
// ---------- Pista fija, cargada desde el panel web ----------
TramoPista CAR_PISTA[CAR_MAX_TRAMOS];

void iniciarPistaCarrera() {
  memset(CAR_PISTA, 0, sizeof(CAR_PISTA));      // sin clave en NVS: pista al azar
  leerBlob("pistaCar", CAR_PISTA, sizeof(CAR_PISTA));
}

void guardarPistaCarrera() { guardarBlob("pistaCar", CAR_PISTA, sizeof(CAR_PISTA)); }

// Un tramo cuenta si tiene tipo, esta bien ordenado y ARRANCA dentro de la tira
// que esta puesta. El que la cruza se recorta; el que empieza despues del final
// no existe para esta tira.
static bool tramoValido(const TramoPista& t) {
  return t.tipo != TRAMO_NADA && t.ini <= t.fin && t.ini < LARGO_TIRA;
}

static int16_t tramoFin(const TramoPista& t) {
  return min<int16_t>((int16_t)t.fin, (int16_t)LARGO_TIRA - 1);
}

uint8_t tramosCargados() {
  uint8_t n = 0;
  for (uint8_t k = 0; k < CAR_MAX_TRAMOS; k++) if (tramoValido(CAR_PISTA[k])) n++;
  return n;
}

bool hayPistaFija() { return tramosCargados() > 0; }

void dibujarPistaCargada() {
  for (uint8_t k = 0; k < CAR_MAX_TRAMOS; k++) {
    const TramoPista& t = CAR_PISTA[k];
    if (!tramoValido(t)) continue;
    CRGB c = (t.tipo == TRAMO_SUBIDA) ? COL_SUBIDA_VER : COL_BAJADA_VER;
    for (int16_t i = (int16_t)t.ini; i <= tramoFin(t); i++) setLed(i, c);
  }
}

static void generarPista() {
  for (int16_t i = 0; i < LARGO_TIRA; i++) pendiente[i] = 0;

  // Pista dibujada a mano: se usa tal cual y no se sortea nada. Los tramos se
  // pisan entre si en el orden en que estan cargados, asi que el ultimo manda.
  if (hayPistaFija()) {
    for (uint8_t k = 0; k < CAR_MAX_TRAMOS; k++) {
      const TramoPista& t = CAR_PISTA[k];
      if (!tramoValido(t)) continue;
      int8_t p = (t.tipo == TRAMO_SUBIDA) ? -100 : +100;
      for (int16_t i = (int16_t)t.ini; i <= tramoFin(t); i++) pendiente[i] = p;
    }
    return;
  }

  // En la tira larga entran mas cuestas: si no, las mismas una o dos quedarian
  // perdidas en el doble de pista y la vuelta saldria casi toda llana.
  uint8_t cuestas = (uint8_t)random(1, 3) * (uint8_t)(LARGO_TIRA / 100);
  int16_t cursor  = CAR_MARGEN;                 // la largada siempre es llana
  for (uint8_t k = 0; k < cuestas; k++) {
    int16_t rampa  = (int16_t)random(8, 14);    // largo de CADA rampa, no del total
    int16_t meseta = (int16_t)random(CAR_MESETA_MIN, (int32_t)CAR_MESETA_MAX + 1);

    // Con la meseta tuneada muy larga desde el panel no entraria ninguna cuesta y
    // la pista saldria toda llana. Se recorta para que al menos la primera entre.
    int16_t tope = (LARGO_TIRA - 2 * CAR_MARGEN) - rampa * 2;
    if (meseta > tope) meseta = (tope > 0) ? tope : 0;

    int16_t largo = rampa * 2 + meseta;
    if (cursor + largo > LARGO_TIRA - CAR_MARGEN) break;

    for (int16_t i = 0; i < rampa; i++) {
      pendiente[cursor + i]                   = -100;   // subida
      pendiente[cursor + rampa + meseta + i]  = +100;   // bajada, despues del llano
    }
    cursor += largo + (int16_t)random(12, 25);          // separacion hasta la proxima cuesta
  }
}

void nuevoCarrera() {
  // Se calibra aca y no al largar: durante el selector la gente esta moviendo el
  // stick para elegir, y medir el centro justo en ese momento seria medir mal.
  calibrarJoys();
  numJugadores = jugadoresSugeridos();
  estadoCar    = CAR_ELIGIENDO;
}

// Arranca la carrera con la cantidad ya elegida. El pote se lee ACA: la
// dificultad es la que marca la perilla al largar, no la que marcaba mientras se
// elegia cuantos corren.
static void arrancarCarrera() {
  vueltasMeta = map(leerPoteCrudo(), 0, 4095, CAR_VUELTAS_MIN, CAR_VUELTAS_MAX);
  if (vueltasMeta < 1) vueltasMeta = 1;

  // Corren los PRIMEROS n controles, que es como estan puestos sobre la mesa.
  for (uint8_t i = 0; i < NUM_CONTROLES; i++) {
    jugando[i]     = (i < numJugadores);
    pos[i]         = 0;
    vel[i]         = 0;
    vueltas[i]     = 0;
    vueltaDesde[i] = 0;
  }
  mejorVuelta   = 0;
  mejorVueltaDe = 0;
  ganador       = 0;
  esRecord      = false;
  ultimoTiempo  = 0;
  generarPista();
  estadoCar = CAR_LARGADA;
  faseDesde = millis();
}

// ---------- Dibujo ----------
static void dibujarPista() {
  FastLED.clear();
  for (int16_t i = 0; i < LARGO_TIRA; i++) {
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
    leds[(led - k + LARGO_TIRA) % LARGO_TIRA] = c;
  }
  leds[led] = col;
}

// Distancia total recorrida, para ordenar la carrera de atras hacia adelante.
static inline int32_t progreso(uint8_t j) {
  return (int32_t)vueltas[j] * LARGO_TIRA + (int32_t)pos[j];
}
static inline int16_t ledDe(uint8_t j) { return (int16_t)pos[j] % LARGO_TIRA; }

static void dibujarAutos() {
  uint8_t orden[NUM_CONTROLES];
  uint8_t n = 0;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) if (jugando[j]) orden[n++] = j;

  // Insercion por progreso ascendente: son cuatro elementos como maximo, asi que
  // el algoritmo no importa, importa que el lider quede ULTIMO en la lista y por
  // lo tanto se dibuje encima de todos los que viene doblando.
  for (uint8_t a = 1; a < n; a++) {
    uint8_t  v  = orden[a];
    int32_t  pv = progreso(v);
    int8_t   b  = (int8_t)a - 1;
    while (b >= 0 && progreso(orden[b]) > pv) { orden[b + 1] = orden[b]; b--; }
    orden[b + 1] = v;
  }

  for (uint8_t k = 0; k < n; k++) dibujarAuto(ledDe(orden[k]), CONTROLES[orden[k]].color);

  // Dos autos en el mismo LED van blancos: se lee como "pegados" y evita que el
  // de abajo desaparezca sin explicacion.
  for (uint8_t a = 0; a < n; a++) {
    for (uint8_t b = a + 1; b < n; b++) {
      if (ledDe(orden[a]) == ledDe(orden[b])) leds[ledDe(orden[a])] = CRGB::White;
    }
  }
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

  int16_t led = (int16_t)pos[i] % LARGO_TIRA;
  vel[i] += (pendiente[led] / 100.0f) * CAR_GRAVEDAD * dt;
  vel[i] -= vel[i] * (CAR_FRICCION / 100.0f) * dt;
  if (vel[i] < 0) vel[i] = 0;          // cuesta arriba se para, no retrocede

  pos[i] += vel[i] * dt;

  if (pos[i] >= LARGO_TIRA) {
    pos[i] -= LARGO_TIRA;
    vueltas[i]++;

    uint32_t t = ahora - vueltaDesde[i];
    vueltaDesde[i] = ahora;
    if (mejorVuelta == 0 || t < mejorVuelta) { mejorVuelta = t; mejorVueltaDe = i; }

    if (vueltas[i] >= vueltasMeta) {
      ganador   = i;
      estadoCar = CAR_FIN;
      faseDesde = ahora;
      esRecord  = intentarRecord(REC_CARRERA, mejorVuelta, mejorVueltaDe);
      esRecord ? sonarRecord() : sonarMeta();
      return;
    }
    sonarVuelta();
  }
}

void loopCarrera() {
  uint32_t ahora = millis();

  if (estadoCar == CAR_ELIGIENDO) {
    if (loopSelectorJugadores(numJugadores)) arrancarCarrera();
    return;
  }

  if (estadoCar == CAR_FIN) {
    // Festejo del ganador recorriendo la pista, con la vuelta rapida marcada
    // en dorado si ademas fue record.
    uint32_t t = ahora - faseDesde;
    CRGB c = CONTROLES[ganador].color;
    FastLED.clear();
    for (int16_t i = 0; i < LARGO_TIRA; i++) {
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
      for (int16_t i = 0; i < LARGO_TIRA; i++) leds[i] += luz;
    }
    dibujarAutos();
    FastLED.show();

    if (t > CAR_LARGADA_MS) {
      estadoCar   = CAR_CORRIENDO;
      ultimoFrame = ahora;
      for (uint8_t j = 0; j < NUM_CONTROLES; j++) vueltaDesde[j] = ahora;
    }
    return;
  }

  // --- CAR_CORRIENDO ---
  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;
  if (dt > 0.1f) dt = 0.1f;

  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    avanzar(j, dt, ahora);
    if (estadoCar != CAR_CORRIENDO) return;  // alguien ya cruzo la meta
  }

  dibujarPista();
  dibujarAutos();
  FastLED.show();
}

// ---------- LCD ----------
// Con cuatro autos las vueltas de todos no entran con nombre: van abreviadas,
// "Ve2 Az1 Ro3 Am0", que en 16 columnas entra hasta con los cuatro corriendo.
static String marcadorVueltas() {
  String s;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    if (s.length()) s += " ";
    s += String(CONTROLES[j].abrev) + String(vueltas[j]);
  }
  return s;
}

void lcdCarrera() {
  if (estadoCar == CAR_ELIGIENDO) {
    lcdSelectorJugadores("Carrera", numJugadores);
    return;
  }
  if (estadoCar == CAR_FIN) {
    lcdLinea(0, textoGana(ganador));
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
  lcdLinea(0, marcadorVueltas());
  lcdLinea(1, String(vueltasMeta) + " vue | " +
              (mejorVuelta ? (String(mejorVuelta) + "ms") : "--"));
}

String webCarrera() {
  String s;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    if (s.length()) s += " - ";
    s += String(CONTROLES[j].nombre) + " " + String(vueltas[j]) + "/" + String(vueltasMeta);
  }
  if (mejorVuelta) s += ", mejor vuelta " + String(mejorVuelta) + " ms";
  s += hayPistaFija() ? (" [pista fija: " + String(tramosCargados()) + " tramos]")
                      : " [pista al azar]";
  return s;
}
