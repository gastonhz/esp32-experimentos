// ---------- Pelea: combate cuerpo a cuerpo en 1D ----------
// Los peleadores se mueven con su eje y atacan con su boton. Toda la profundidad
// esta en la DURACION de la pulsacion:
//
//   toque corto   alcance chico, poco dano, se recupera enseguida
//   mantenido     alcance largo, mucho dano, inmovil al cargar y lento al salir
//
// La pieza que hace que esto sea un juego y no un concurso de martillar el boton
// es que la carga se dibuja: mientras alguien carga, la tira muestra tenue hasta
// donde va a llegar el golpe. Es informacion publica a proposito. El que carga
// esta anunciando "voy a pegar hasta aca y no me puedo mover", y el rival tiene
// esa ventana para salirse del rango y entrar a castigarlo en la recuperacion.
// Sin el aviso, el golpe largo seria pura loteria; con el aviso, es una apuesta.
//
// Pelean de dos a cuatro, la cantidad se elige antes de empezar, y dos
// decisiones sostienen que la cosa escale sin volverse otro juego:
//
//   EL CUERPO ES LA VIDA. No hay barra de vida en ninguna punta: cada peleador
//   se dibuja con tantos LEDs como vida le queda. Te pegan y encoges; te quedas
//   sin LEDs y desapareces. Con cuatro no habia donde poner cuatro barras -- la
//   tira tiene dos puntas --, pero ademas resulta mejor que lo que habia: el
//   estado de la pelea se lee de un vistazo y sin mirar el LCD, y encoger es un
//   premio consuelo, porque un cuerpo corto es un blanco mas dificil de tocar.
//
//   EL GOLPE PEGA A LOS DOS LADOS. No hay orientacion: el swing barre el alcance
//   hacia ambos lados y los cuerpos se atraviesan. Antes habia un "P1 mira para
//   arriba, P2 para abajo" que solo se sostenia porque eran exactamente dos y no
//   se cruzaban nunca. Sacarlo no le quita nada al juego -- se sigue jugando con
//   un boton y nada mas -- y agrega algo: un cargado largo puede llevarse dos
//   rivales de una.

#include "juego_pelea.h"

// ---------- Parametros ----------
// Juego PROPORCIONAL: lo que esta en LEDs o en LEDs/s vale para una tira de 100
// y se pasa por escala*() al usarlo, asi en la tira larga todo crece junto y la
// pelea se siente igual. La VIDA no escala: son puntos de vida, y el record
// cuenta golpes conectados. Lo que escala es el CUERPO que se dibuja con ella.
uint16_t PEL_VEL_JUGADOR   = 14;   // LEDs/s caminando
uint16_t PEL_ALCANCE_CORTO = 3;    // alcance del golpe rapido (LEDs)
uint16_t PEL_ALCANCE_LARGO = 10;   // alcance del cargado a full (LEDs)
uint16_t PEL_CARGA_MIN     = 260;  // hasta aca el boton cuenta como toque (ms)
uint16_t PEL_CARGA_MAX     = 900;  // mas alla de esto ya no carga mas (ms)
uint16_t PEL_RECUP_CORTA   = 200;  // inmovil despues del golpe rapido (ms)
uint16_t PEL_RECUP_LARGA   = 650;  // inmovil despues del cargado (ms)

const uint8_t  PEL_VIDA_MAX   = 6;     // vida inicial Y largo del cuerpo, en LEDs
const uint8_t  PEL_DANO_CORTO = 1;
const uint8_t  PEL_DANO_LARGO = 3;
const uint16_t PEL_GOLPE_MS   = 110;   // cuanto se ve el segmento del golpe
const float    PEL_EMPUJE     = 2.0f;  // LEDs que retrocede el que come el golpe
const uint16_t PEL_FIN_MS     = 4000;

// Toda la tira es piso: sin barras de vida en las puntas no hay nada que
// esquivar. El margen es media vida, para que un cuerpo entero entre siempre.
// Medio cuerpo de margen en cada punta, con el cuerpo ya escalado a la tira.
static int16_t pelPisoMin() { return escalaLeds(PEL_VIDA_MAX) / 2; }
// Depende del largo de la tira, que ahora se elige en Ajustes: va como funcion
// y no como constante de archivo, que se calcularia una sola vez al encender.
static int16_t pelPisoMax() { return LARGO_TIRA - 1 - escalaLeds(PEL_VIDA_MAX) / 2; }

const uint8_t PEL_NADIE = 255;

// ---------- Estado ----------
enum FasePel   { PEL_LIBRE, PEL_CARGANDO, PEL_GOLPEANDO, PEL_RECUPERANDO };
enum EstadoPel { PEL_ELIGIENDO, PEL_PELEANDO, PEL_FIN };

static EstadoPel estadoPel;
static uint32_t  faseDesde;        // millis() de entrada a PEL_FIN

static bool     jugando[NUM_CONTROLES];   // controles que entraron a la pelea
static uint8_t  numJugadores;
static float    pos[NUM_CONTROLES];
static int8_t   vida[NUM_CONTROLES];
static FasePel  fase[NUM_CONTROLES];
static uint32_t faseCambio[NUM_CONTROLES]; // millis() en que empezo la fase actual
static uint16_t faseDura[NUM_CONTROLES];   // cuanto dura la fase actual (golpe / recuperacion)
static int16_t  alcance[NUM_CONTROLES];    // alcance del golpe en curso o de la carga
static uint8_t  dano[NUM_CONTROLES];

static uint8_t  golpesConectados;  // los de todos: mide lo peleada que estuvo
static uint8_t  ganador;           // indice de control, valido en PEL_FIN
static bool     esRecord;
static uint32_t ultimoFrame;

static inline bool vivo(uint8_t j) { return jugando[j] && vida[j] > 0; }

// El cuerpo va centrado en pos y mide lo que le queda de vida, escalado a la
// tira: en la larga cada punto de vida son dos LEDs. Los golpes se resuelven
// con estas mismas funciones, asi que el alcance y el cuerpo siguen encajando.
static inline int16_t largoCuerpo(uint8_t j) { return escalaLeds(vida[j]); }
static inline int16_t cuerpoIni(uint8_t j) {
  return (int16_t)(pos[j] + 0.5f) - largoCuerpo(j) / 2;
}
static inline int16_t cuerpoFin(uint8_t j) {
  return cuerpoIni(j) + largoCuerpo(j) - 1;
}

// ---------- Sonido ----------
static const uint16_t PEL_TONO_TOQUE[NUM_CONTROLES]   = { 1500, 1200, 1750, 1000 };
static const uint16_t PEL_TONO_CARGADO[NUM_CONTROLES] = {  900,  750, 1050,  620 };

static void sonarToque(uint8_t j)   { beep(PEL_TONO_TOQUE[j], 18); }
static void sonarCargado(uint8_t j) { beep(PEL_TONO_CARGADO[j], 70); }
static void sonarImpacto()          { beep(220, 120); }

// ---------- Reglas ----------
// Traduce el tiempo de carga en alcance y dano. Por debajo de PEL_CARGA_MIN es
// un toque; de ahi para arriba interpola lineal hasta el tope.
static int16_t alcanceDeCarga(uint32_t carga, uint8_t* danoSalida) {
  int16_t corto = escalaLeds((int16_t)PEL_ALCANCE_CORTO);
  int16_t largo = escalaLeds((int16_t)PEL_ALCANCE_LARGO);
  if (largo < corto) largo = corto;          // por si el panel web los deja al reves

  if (carga < PEL_CARGA_MIN) { *danoSalida = PEL_DANO_CORTO; return corto; }

  uint32_t tope = (PEL_CARGA_MAX > PEL_CARGA_MIN) ? PEL_CARGA_MAX
                                                  : (uint32_t)PEL_CARGA_MIN + 1;
  if (carga > tope) carga = tope;
  uint32_t f = ((carga - PEL_CARGA_MIN) * 100u) / (tope - PEL_CARGA_MIN);   // 0..100

  *danoSalida = (uint8_t)(PEL_DANO_CORTO + ((PEL_DANO_LARGO - PEL_DANO_CORTO) * f) / 100u);
  return (int16_t)(corto + ((int32_t)(largo - corto) * (int32_t)f) / 100);
}

// El golpe no es un proyectil: se resuelve en el instante en que sale, contra
// las posiciones que tengan los rivales en ese momento. Barre el alcance hacia
// los dos lados del cuerpo, asi que puede alcanzar a mas de uno.
static void resolverGolpe(uint8_t j) {
  int16_t lo = cuerpoIni(j) - alcance[j];
  int16_t hi = cuerpoFin(j) + alcance[j];

  for (uint8_t o = 0; o < NUM_CONTROLES; o++) {
    if (o == j || !vivo(o)) continue;
    if (cuerpoFin(o) < lo || cuerpoIni(o) > hi) continue;   // fuera del barrido

    vida[o] -= (int8_t)dano[j];
    if (vida[o] < 0) vida[o] = 0;
    golpesConectados++;
    pos[o] += (pos[o] >= pos[j] ? +1.0f : -1.0f) * (float)escalaLeds((int16_t)PEL_EMPUJE);  // reabre la distancia
    sonarImpacto();
  }
}

static void actualizarPeleador(uint8_t j, uint32_t ahora, float dt) {
  switch (fase[j]) {
    case PEL_LIBRE:
      pos[j] += leerJoyNorm(j) * (float)escalaVel(PEL_VEL_JUGADOR) * dt;
      if (btnFlanco[j]) {
        fase[j]       = PEL_CARGANDO;
        faseCambio[j] = ahora;
        alcance[j]    = escalaLeds((int16_t)PEL_ALCANCE_CORTO);
        dano[j]       = PEL_DANO_CORTO;
      }
      break;

    case PEL_CARGANDO:
      // Inmovil mientras carga: ese es el precio del golpe largo, y es la
      // ventana que los rivales tienen para reaccionar.
      alcance[j] = alcanceDeCarga(ahora - faseCambio[j], &dano[j]);
      if (!btnEstable[j]) {                  // solto: sale el golpe
        fase[j]       = PEL_GOLPEANDO;
        faseCambio[j] = ahora;
        faseDura[j]   = PEL_GOLPE_MS;
        (dano[j] > PEL_DANO_CORTO) ? sonarCargado(j) : sonarToque(j);
        resolverGolpe(j);
      }
      break;

    case PEL_GOLPEANDO:
      if (ahora - faseCambio[j] >= faseDura[j]) {
        fase[j]       = PEL_RECUPERANDO;
        faseCambio[j] = ahora;
        faseDura[j]   = (dano[j] > PEL_DANO_CORTO) ? PEL_RECUP_LARGA : PEL_RECUP_CORTA;
      }
      break;

    case PEL_RECUPERANDO:
      if (ahora - faseCambio[j] >= faseDura[j]) fase[j] = PEL_LIBRE;
      break;
  }

  if (pos[j] < pelPisoMin()) pos[j] = pelPisoMin();
  if (pos[j] > pelPisoMax()) pos[j] = pelPisoMax();
}

// ---------- Dibujo ----------
// Suma en vez de pisar: ahora que los cuerpos se atraviesan, el solape se ve
// como mezcla de colores en vez de hacer desaparecer a uno de los dos.
static void sumarLed(int16_t i, const CRGB& c) {
  if (i >= 0 && i < LARGO_TIRA) leds[i] += c;
}

// Primero los ataques de todos y despues los cuerpos, para que un golpe que
// llega hasta un rival no le tape el cuerpo: se tiene que ver a quien le pegan.
static void dibujarPelea() {
  FastLED.clear();

  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!vivo(j)) continue;
    if (fase[j] != PEL_CARGANDO && fase[j] != PEL_GOLPEANDO) continue;

    // Cargando: tenue, es el aviso de hasta donde va a llegar. Golpeando: el
    // mismo segmento a pleno.
    CRGB c = CONTROLES[j].color;
    if (fase[j] == PEL_CARGANDO) c.nscale8(40);
    else                         c = CRGB::White;

    for (int16_t k = 1; k <= alcance[j]; k++) {
      sumarLed(cuerpoIni(j) - k, c);
      sumarLed(cuerpoFin(j) + k, c);
    }
  }

  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!vivo(j)) continue;
    CRGB c = CONTROLES[j].color;
    if (fase[j] == PEL_RECUPERANDO) c.nscale8(70);   // indefenso: se nota
    for (int16_t i = cuerpoIni(j); i <= cuerpoFin(j); i++) sumarLed(i, c);
  }

  FastLED.show();
}

// ---------- Partida ----------
void nuevoPelea() {
  // Se calibra aca y no al empezar la pelea: durante el selector la gente esta
  // moviendo el stick para elegir, y medir el centro ahi seria medir mal.
  calibrarJoys();
  numJugadores = jugadoresSugeridos();
  estadoPel    = PEL_ELIGIENDO;
}

static void arrancarPelea() {
  // Pelean los PRIMEROS n controles, que es como estan puestos sobre la mesa.
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) jugando[j] = (j < numJugadores);

  estadoPel = PEL_PELEANDO;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    vida[j]       = jugando[j] ? (int8_t)PEL_VIDA_MAX : 0;
    fase[j]       = PEL_LIBRE;
    faseCambio[j] = 0;
    faseDura[j]   = 0;
    alcance[j]    = escalaLeds((int16_t)PEL_ALCANCE_CORTO);
    dano[j]       = PEL_DANO_CORTO;
    pos[j]        = pelPisoMin();
  }

  // Repartidos parejo a lo largo de la tira: la primera decision de todos es
  // hacia cual de los vecinos moverse.
  uint8_t k = 0;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    pos[j] = pelPisoMin() +
             (float)(pelPisoMax() - pelPisoMin()) * (float)k / (float)(numJugadores - 1);
    k++;
  }

  golpesConectados = 0;
  ganador          = PEL_NADIE;
  esRecord         = false;
  ultimoFrame      = millis();
}

void loopPelea() {
  uint32_t ahora = millis();

  if (estadoPel == PEL_ELIGIENDO) {
    if (loopSelectorJugadores(numJugadores)) arrancarPelea();
    return;
  }

  if (estadoPel == PEL_FIN) {
    uint32_t t = ahora - faseDesde;
    CRGB c = CONTROLES[ganador].color;
    FastLED.clear();
    for (uint16_t i = 0; i < LARGO_TIRA; i++) {
      if ((i + t / 40) % 4 == 0) leds[i] = c;        // chase del ganador
    }
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > PEL_FIN_MS) volverAlMenu();
    return;
  }

  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;
  if (dt > 0.1f) dt = 0.1f;

  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (vivo(j)) actualizarPeleador(j, ahora, dt);
  }

  // Gana el ultimo en pie. Si el mismo golpe se llevo a los dos que quedaban,
  // no hay ganador: se muestra el chase del ultimo que habia estado vivo.
  uint8_t vivos = 0, ultimo = PEL_NADIE;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (vivo(j)) { vivos++; ultimo = j; }
  }
  if (vivos <= 1) {
    ganador   = (ultimo != PEL_NADIE) ? ultimo : 0;
    estadoPel = PEL_FIN;
    faseDesde = ahora;
    esRecord  = intentarRecord(REC_PELEA, golpesConectados, ganador);
    if (esRecord) sonarRecord();
    else          sonarVictoria();
    return;
  }

  dibujarPelea();
}

// ---------- LCD ----------
static char estadoChar(uint8_t j) {
  if (!vivo(j)) return 'x';
  switch (fase[j]) {
    case PEL_CARGANDO:    return 'c';
    case PEL_GOLPEANDO:   return 'G';
    case PEL_RECUPERANDO: return 'r';
    default:              return '.';
  }
}

// Con cuatro peleadores no entran los nombres: las dos filas van abreviadas y
// en el mismo orden, "Ve6 Az4 Ro0 Am2" arriba y "Ve. Azc Rox Amr" abajo.
static String filaAbreviada(bool estados) {
  String s;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    if (s.length()) s += " ";
    s += CONTROLES[j].abrev;
    s += estados ? estadoChar(j) : (char)('0' + (vida[j] > 9 ? 9 : vida[j]));
  }
  return s;
}

void lcdPelea() {
  if (estadoPel == PEL_ELIGIENDO) {
    lcdSelectorJugadores("Pelea", numJugadores);
    return;
  }
  lcdLinea(0, filaAbreviada(false));

  if (estadoPel == PEL_FIN) {
    if (esRecord) lcdLinea(1, "RECORD " + String(golpesConectados) + " golpes");
    else          lcdLinea(1, textoGana(ganador));
    return;
  }
  lcdLinea(1, filaAbreviada(true));
}

String webPelea() {
  String s;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    if (s.length()) s += " - ";
    s += String(CONTROLES[j].nombre) + " " + String(vida[j]);
  }
  return s + " (" + String(golpesConectados) + " golpes)";
}
