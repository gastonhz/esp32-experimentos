// ---------- Pelea: combate cuerpo a cuerpo en 1D ----------
// Los dos peleadores se mueven con su eje y atacan con su boton. Toda la
// profundidad esta en la DURACION de la pulsacion:
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
// Por eso los tres tiempos que importan (carga, golpe, recuperacion) son
// distintos y visibles: el cuerpo se apaga a medias mientras se recupera, que es
// la ventana en la que esta indefenso.

#include "juego_pelea.h"

// ---------- Parametros ----------
uint16_t PEL_VEL_JUGADOR   = 14;   // LEDs/s caminando
uint16_t PEL_ALCANCE_CORTO = 3;    // alcance del golpe rapido (LEDs)
uint16_t PEL_ALCANCE_LARGO = 10;   // alcance del cargado a full (LEDs)
uint16_t PEL_CARGA_MIN     = 260;  // hasta aca el boton cuenta como toque (ms)
uint16_t PEL_CARGA_MAX     = 900;  // mas alla de esto ya no carga mas (ms)
uint16_t PEL_RECUP_CORTA   = 200;  // inmovil despues del golpe rapido (ms)
uint16_t PEL_RECUP_LARGA   = 650;  // inmovil despues del cargado (ms)

const uint8_t  PEL_VIDA_MAX   = 6;     // tambien es el largo de la barra, en LEDs
const uint8_t  PEL_DANO_CORTO = 1;
const uint8_t  PEL_DANO_LARGO = 3;
const uint16_t PEL_GOLPE_MS   = 110;   // cuanto se ve el segmento del golpe
const float    PEL_EMPUJE     = 2.0f;  // LEDs que retrocede el que come el golpe
const uint16_t PEL_FIN_MS     = 4000;

// La barra de vida de cada uno ocupa los primeros / ultimos PEL_VIDA_MAX LEDs.
// Los peleadores se mueven en el medio, con un LED de aire para que el cuerpo no
// se confunda con la punta de la barra.
const int16_t PEL_PISO_MIN = PEL_VIDA_MAX + 1;
const int16_t PEL_PISO_MAX = NUM_LEDS - PEL_VIDA_MAX - 2;

// ---------- Estado ----------
enum FasePel   { PEL_LIBRE, PEL_CARGANDO, PEL_GOLPEANDO, PEL_RECUPERANDO };
enum EstadoPel { PEL_PELEANDO, PEL_FIN };

static EstadoPel estadoPel;
static uint32_t  faseDesde;        // millis() de entrada a PEL_FIN

static float    pos[2];
static int8_t   vida[2];
static FasePel  fase[2];
static uint32_t faseCambio[2];     // millis() en que empezo la fase actual
static uint16_t faseDura[2];       // cuanto dura la fase actual (golpe / recuperacion)
static int16_t  alcance[2];        // alcance del golpe en curso o de la carga
static uint8_t  dano[2];

static uint8_t  golpesConectados;  // los de los dos: mide lo peleada que estuvo
static uint8_t  ganador;           // 1 o 2, valido en PEL_FIN
static bool     esRecord;
static uint32_t ultimoFrame;

// P1 arranca del lado del LED 0 y pega hacia arriba; P2 al reves. Los dos no se
// atraviesan nunca (ver ordenar), asi que este sentido vale toda la partida.
static inline int8_t sentido(uint8_t j) { return (j == 0) ? +1 : -1; }

// ---------- Sonido ----------
static void sonarToque(uint8_t j)  { beep(j == 0 ? 1500 : 1200, 18); }
static void sonarCargado(uint8_t j){ beep(j == 0 ?  900 :  750, 70); }
static void sonarImpacto()         { beep(220, 120); }

// ---------- Reglas ----------
// Traduce el tiempo de carga en alcance y dano. Por debajo de PEL_CARGA_MIN es
// un toque; de ahi para arriba interpola lineal hasta el tope.
static int16_t alcanceDeCarga(uint32_t carga, uint8_t* danoSalida) {
  int16_t corto = (int16_t)PEL_ALCANCE_CORTO;
  int16_t largo = (int16_t)PEL_ALCANCE_LARGO;
  if (largo < corto) largo = corto;          // por si el panel web los deja al reves

  if (carga < PEL_CARGA_MIN) { *danoSalida = PEL_DANO_CORTO; return corto; }

  uint32_t tope = (PEL_CARGA_MAX > PEL_CARGA_MIN) ? PEL_CARGA_MAX
                                                  : (uint32_t)PEL_CARGA_MIN + 1;
  if (carga > tope) carga = tope;
  uint32_t f = ((carga - PEL_CARGA_MIN) * 100u) / (tope - PEL_CARGA_MIN);   // 0..100

  *danoSalida = (uint8_t)(PEL_DANO_CORTO + ((PEL_DANO_LARGO - PEL_DANO_CORTO) * f) / 100u);
  return (int16_t)(corto + ((int32_t)(largo - corto) * (int32_t)f) / 100);
}

// Los peleadores no se atraviesan: si se pisan, se separan a la mitad del
// solape. Que P1 quede SIEMPRE del lado bajo es lo que hace valido a sentido().
static void ordenar() {
  if (pos[1] - pos[0] < 1.0f) {
    float medio = (pos[0] + pos[1]) / 2.0f;
    pos[0] = medio - 0.5f;
    pos[1] = medio + 0.5f;
  }
  if (pos[0] < PEL_PISO_MIN) pos[0] = PEL_PISO_MIN;
  if (pos[1] > PEL_PISO_MAX) pos[1] = PEL_PISO_MAX;
  if (pos[0] > PEL_PISO_MAX) pos[0] = PEL_PISO_MAX;
  if (pos[1] < PEL_PISO_MIN) pos[1] = PEL_PISO_MIN;
}

// El golpe no es un proyectil: se resuelve en el instante en que sale, contra la
// posicion que tiene el rival en ese momento.
static void resolverGolpe(uint8_t j) {
  uint8_t o = 1 - j;
  int8_t  s = sentido(j);

  float d = (pos[o] - pos[j]) * s;           // distancia hacia adelante
  if (d <= 0.0f || d > (float)alcance[j]) return;

  vida[o] -= (int8_t)dano[j];
  if (vida[o] < 0) vida[o] = 0;
  golpesConectados++;
  pos[o] += s * PEL_EMPUJE;                  // empujon: reabre la distancia
  sonarImpacto();
}

static void actualizarPeleador(uint8_t j, uint32_t ahora, float dt) {
  switch (fase[j]) {
    case PEL_LIBRE:
      pos[j] += leerJoyNorm(j) * (float)PEL_VEL_JUGADOR * dt;
      if (btnFlanco[j]) {
        fase[j]       = PEL_CARGANDO;
        faseCambio[j] = ahora;
        alcance[j]    = (int16_t)PEL_ALCANCE_CORTO;
        dano[j]       = PEL_DANO_CORTO;
      }
      break;

    case PEL_CARGANDO:
      // Inmovil mientras carga: ese es el precio del golpe largo, y es la
      // ventana que el rival tiene para reaccionar.
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
}

// ---------- Dibujo ----------
// Primero los ataques de los dos y despues los cuerpos, para que un golpe que
// llega hasta el rival no le tape el cuerpo: se tiene que ver a quien le pegan.
static void dibujarPelea() {
  FastLED.clear();

  for (int8_t k = 0; k < vida[0]; k++) setLed(k, COL_P1);
  for (int8_t k = 0; k < vida[1]; k++) setLed(NUM_LEDS - 1 - k, COL_P2);

  for (uint8_t j = 0; j < 2; j++) {
    int16_t p = (int16_t)(pos[j] + 0.5f);
    int8_t  s = sentido(j);
    CRGB    c = (j == 0) ? COL_P1 : COL_P2;

    if (fase[j] == PEL_CARGANDO) {           // windup: hasta donde va a llegar
      CRGB tenue = c;
      tenue.nscale8(40);
      for (int16_t k = 1; k <= alcance[j]; k++) setLed(p + s * k, tenue);
    }
    if (fase[j] == PEL_GOLPEANDO) {          // el mismo segmento, a pleno
      for (int16_t k = 1; k <= alcance[j]; k++) setLed(p + s * k, CRGB::White);
    }
  }

  for (uint8_t j = 0; j < 2; j++) {
    CRGB c = (j == 0) ? COL_P1 : COL_P2;
    if (fase[j] == PEL_RECUPERANDO) c.nscale8(70);   // indefenso: se nota
    setLed((int16_t)(pos[j] + 0.5f), c);
  }

  FastLED.show();
}

// ---------- Partida ----------
void nuevoPelea() {
  calibrarJoy(0);
  calibrarJoy(1);

  estadoPel = PEL_PELEANDO;
  for (uint8_t j = 0; j < 2; j++) {
    vida[j]       = (int8_t)PEL_VIDA_MAX;
    fase[j]       = PEL_LIBRE;
    faseCambio[j] = 0;
    faseDura[j]   = 0;
    alcance[j]    = (int16_t)PEL_ALCANCE_CORTO;
    dano[j]       = PEL_DANO_CORTO;
  }
  // Arrancan separados: bien lejos del alcance del golpe mas largo, asi la
  // primera decision de los dos es acercarse.
  pos[0] = PEL_PISO_MIN + 12;
  pos[1] = PEL_PISO_MAX - 12;

  golpesConectados = 0;
  ganador          = 0;
  esRecord         = false;
  ultimoFrame      = millis();
}

void loopPelea() {
  uint32_t ahora = millis();

  if (estadoPel == PEL_FIN) {
    uint32_t t = ahora - faseDesde;
    CRGB c = (ganador == 1) ? COL_P1 : COL_P2;
    FastLED.clear();
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
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

  for (uint8_t j = 0; j < 2; j++) actualizarPeleador(j, ahora, dt);
  ordenar();

  if (vida[0] <= 0 || vida[1] <= 0) {
    ganador   = (vida[0] <= 0) ? 2 : 1;
    estadoPel = PEL_FIN;
    faseDesde = ahora;
    esRecord  = intentarRecord(REC_PELEA, golpesConectados);
    if (esRecord) sonarRecord();
    else          sonarVictoria();
    return;
  }

  dibujarPelea();
}

// ---------- LCD ----------
static String estadoCorto(uint8_t j) {
  switch (fase[j]) {
    case PEL_CARGANDO:    return "carga";
    case PEL_GOLPEANDO:   return "GOLPE";
    case PEL_RECUPERANDO: return "lento";
    default:              return "listo";
  }
}

void lcdPelea() {
  lcdLinea(0, "Verde " + String(vida[0]) + " - " + String(vida[1]) + " Azul");

  if (estadoPel == PEL_FIN) {
    if (esRecord) lcdLinea(1, "RECORD " + String(golpesConectados) + " golpes");
    else          lcdLinea(1, (ganador == 1) ? "** GANA VERDE **" : "** GANA AZUL **");
    return;
  }
  lcdLinea(1, estadoCorto(0) + " | " + estadoCorto(1));
}

String webPelea() {
  return "Verde " + String(vida[0]) + " - " + String(vida[1]) + " Azul (" +
         String(golpesConectados) + " golpes)";
}
