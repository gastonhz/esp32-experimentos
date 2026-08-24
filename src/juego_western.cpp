// ---------- Tiros: duelo a distancia en 1D ----------
// En una tira no hay a donde esquivar: correrse al costado no existe. Asi que la
// defensa no es la posicion, es la ORIENTACION.
//
//   el pistolero mira hacia donde se esta moviendo
//   de frente al rival  -> puede disparar, y las balas le pegan
//   de espaldas         -> las balas lo atraviesan, pero no hace dano
//
// Eso convierte cada metro de la tira en una decision: avanzar es apuntar y
// exponerse a la vez, retroceder es cubrirse y perder terreno.
//
// Cada pistolero se dibuja como una fila de LEDs: el de color es el frente (por
// donde dispara y por donde lo hieren) y los blancos que lo siguen son la
// espalda. Asi la orientacion se lee de un vistazo y en todo momento, que es la
// informacion sobre la que se juega todo el resto.
//
// Y EL LARGO DE ESA FILA ES LA VIDA. No hay barra de vidas en las puntas: te
// pegan y te quedas mas corto, hasta que te queda un solo LED de color -- sin
// espalda, o sea sin nada que te cubra -- y el siguiente tiro te saca. Con hasta
// cuatro pistoleros no habia donde poner cuatro barras (la tira tiene dos
// puntas), pero ademas el largo hace doble trabajo: dice cuanta vida queda Y
// hacia donde mira, en el mismo dibujo.
//
// Juegan de dos a cuatro, la cantidad se elige antes de empezar. Todos se
// atraviesan: pasar
// por encima de otro para quedar de su lado es una jugada valida, y ademas evita
// que el que avanza termine arrastrando a alguien y tapandolo. Nada en las
// reglas depende de quien esta a la izquierda.
//
// Lo demas sostiene el ritmo: las balas viajan lentas (se ven venir y se pueden
// contestar), el cargador se acaba y recargar deja clavado -- inmovil quiere
// decir que tampoco se puede cambiar de orientacion, asi que recargar de frente
// al rival es quedar expuesto todo ese rato --, y dos balas que se cruzan se
// anulan, que es la unica forma de "esquivar" que tiene el juego.
//
// Falta una regla, y es la que evita que el juego se rompa: A QUEMARROPA la
// espalda no salva. Sin eso, cualquiera puede soltar el stick de espaldas (con
// el stick al centro la orientacion no cambia) y quedarse asi para siempre:
// invulnerable, inofensivo, y el rival sin nada que hacer. Un empate eterno.
// Con el tiro a quemarropa, atrincherarse deja de ser gratis: el otro puede
// caminar hasta pegarsele y cobrarselo. Pero acercarse tanto obliga a ir de
// frente -- o sea, expuesto --, asi que el que se atrinchera siempre tiene la
// salida de darse vuelta y tirar primero. Es una decision, no un castigo
// automatico. Esta es la unica regla que castiga al que se cubre: la pared no
// alcanza, porque desde que los pistoleros se atraviesan siempre hay salida.

#include "juego_western.h"

// ---------- Parametros ----------
// Juego PROPORCIONAL: lo que esta en LEDs o en LEDs/s vale para una tira de 100.
// Las VIDAS no escalan --son vidas, y el record cuenta balas anuladas--; lo que
// escala es el cuerpo que se dibuja con ellas.
uint16_t WES_VEL_JUGADOR = 16;    // LEDs/s caminando
uint16_t WES_VEL_BALA    = 40;    // LEDs/s: cruzar la tira entera son ~2,5 s
uint16_t WES_CARGADOR    = 6;     // balas por cargador
uint16_t WES_RECARGA_MS  = 1400;  // cuanto inmoviliza recargar
uint16_t WES_INVUL_MS    = 1400;  // gracia despues de comerse una bala
uint16_t WES_QUEMARROPA  = 4;     // a esta distancia o menos, la espalda no salva
uint16_t WES_GIRO_MIN    = 35;    // deflexion minima para girar, en % del recorrido

const uint8_t  WES_VIDAS     = 3;    // vida inicial Y largo del cuerpo, en LEDs
const uint8_t  WES_MAX_BALAS = 12;   // balas simultaneas en el aire (cuatro que tiran)
const uint16_t WES_FIN_MS    = 4000;

// Sin barras en las puntas, toda la tira es piso.
const int16_t WES_PISO_MIN = 0;
// Depende del largo de la tira, que ahora se elige en Ajustes: va como funcion
// y no como constante de archivo, que se calcularia una sola vez al encender.
static int16_t wesPisoMax() { return LARGO_TIRA - 1; }

const uint8_t WES_NADIE = 255;

// ---------- Estado ----------
enum EstadoWes { WES_ELIGIENDO, WES_JUGANDO, WES_FIN };
static EstadoWes estadoWes;
static uint32_t  faseDesde;         // millis() de entrada a WES_FIN

// `quemarropa` se decide al salir el tiro, no al impactar: si se evaluara en el
// momento del impacto, cualquier bala terminaria cumpliendo la condicion al
// acercarse a su blanco y la orientacion no protegeria nunca de nada.
// `deQuien` evita que una bala a quemarropa lastime al que la disparo: el tiro
// pegado saltea el chequeo de orientacion, y sin esta marca el propio tirador
// entraba en el barrido de su primer frame de vuelo.
struct BalaW { float pos; int8_t dir; bool viva; bool quemarropa; uint8_t deQuien; };
static BalaW bala[WES_MAX_BALAS];

static bool     jugando[NUM_CONTROLES];   // controles que entraron al duelo
static uint8_t  numJugadores;
static float    pos[NUM_CONTROLES];
static int8_t   mirando[NUM_CONTROLES];   // +1 hacia el final de la tira, -1 hacia el inicio
static uint8_t  vidas[NUM_CONTROLES];
static uint8_t  municion[NUM_CONTROLES];
static uint32_t recargaHasta[NUM_CONTROLES];  // 0 = no esta recargando
static uint32_t invulHasta[NUM_CONTROLES];

static uint8_t  anuladas;           // balas que se cruzaron en el aire
static uint8_t  ganador;            // indice de control, valido en WES_FIN
static bool     esRecord;
static uint32_t ultimoFrame;

static inline bool vivo(uint8_t j) { return jugando[j] && vidas[j] > 0; }

// ---------- Sonido ----------
static const uint16_t WES_TONO[NUM_CONTROLES] = { 1700, 1400, 1950, 1150 };
static void sonarTiro(uint8_t j) { beep(WES_TONO[j], 25); }
static void sonarCruce()         { beep(2400, 40); }
static void sonarImpacto()       { beep(200, 200); }
static void sonarRecarga()       { beep(400, 90); }
static void sonarListo()         { beep(1500, 60); }

// ---------- Reglas ----------
// Unico limite de posicion: el piso jugable. Los pistoleros se atraviesan sin
// empujarse -- que uno arrastre al otro y lo tape es peor que dejarlos cruzar.
static void limitarPosiciones() {
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (pos[j] < WES_PISO_MIN) pos[j] = WES_PISO_MIN;
    if (pos[j] > wesPisoMax()) pos[j] = wesPisoMax();
  }
}

// La distancia al rival vivo mas cercano: es contra ese que se mide si el tiro
// sale a quemarropa.
static float distanciaAlMasCercano(uint8_t j) {
  float mejor = (float)LARGO_TIRA;
  for (uint8_t o = 0; o < NUM_CONTROLES; o++) {
    if (o == j || !vivo(o)) continue;
    float d = pos[j] - pos[o];
    if (d < 0.0f) d = -d;
    if (d < mejor) mejor = d;
  }
  return mejor;
}

static bool dispararBala(uint8_t j) {
  for (uint8_t k = 0; k < WES_MAX_BALAS; k++) {
    if (bala[k].viva) continue;
    bala[k].viva       = true;
    bala[k].dir        = mirando[j];
    bala[k].pos        = pos[j] + mirando[j];   // sale del cano, no de adentro del cuerpo
    bala[k].quemarropa = (distanciaAlMasCercano(j) <= (float)escalaLeds(WES_QUEMARROPA));
    bala[k].deQuien    = j;
    return true;
  }
  return false;                                 // el aire esta lleno: no sale el tiro
}

static void actualizarPistolero(uint8_t j, uint32_t ahora, float dt) {
  if (recargaHasta[j] != 0) {                // recargando: clavado y sin poder girar
    if (ahora >= recargaHasta[j]) {
      recargaHasta[j] = 0;
      municion[j]     = (uint8_t)WES_CARGADOR;
      sonarListo();
    }
    return;
  }

  // La orientacion es la ultima direccion en la que se movio, pero GIRAR pide
  // bastante mas deflexion que caminar. El motivo es mecanico: al soltar el
  // stick, el resorte lo pasa de largo y devuelve un pico corto del signo
  // contrario. Con el mismo umbral para las dos cosas, soltar el joystick
  // despues de avanzar te dejaba mirando al reves -- justo el momento en que uno
  // deja de moverse porque ya esta donde queria estar, y de golpe esta expuesto
  // para el otro lado. El pico del resorte no llega a WES_GIRO_MIN; una
  // caminata deliberada, si.
  float m    = leerJoyNorm(j);
  float giro = (float)WES_GIRO_MIN / 100.0f;
  if      (m >=  giro) mirando[j] = +1;
  else if (m <= -giro) mirando[j] = -1;
  pos[j] += m * (float)escalaVel(WES_VEL_JUGADOR) * dt;

  if (!btnFlanco[j]) return;

  if (municion[j] > 0) {
    // Se puede disparar de espaldas: la bala sale para el lado inutil y se
    // pierde. Es un error que se paga en municion, no una accion prohibida.
    if (dispararBala(j)) { municion[j]--; sonarTiro(j); }
  } else {
    recargaHasta[j] = ahora + WES_RECARGA_MS;
    sonarRecarga();
  }
}

static void actualizarBalas(uint32_t ahora, float dt) {
  float antes[WES_MAX_BALAS];

  for (uint8_t k = 0; k < WES_MAX_BALAS; k++) {
    if (!bala[k].viva) continue;
    antes[k]    = bala[k].pos;
    bala[k].pos += bala[k].dir * (float)escalaVel(WES_VEL_BALA) * dt;
    if (bala[k].pos < 0.0f || bala[k].pos > (float)(LARGO_TIRA - 1)) bala[k].viva = false;
  }

  // Dos balas de sentido opuesto que se cruzan se anulan. Se detecta por cambio
  // de signo de la diferencia entre el frame anterior y este: con un dt largo
  // pueden haberse atravesado sin llegar a estar cerca en ningun frame.
  for (uint8_t a = 0; a < WES_MAX_BALAS; a++) {
    if (!bala[a].viva) continue;
    for (uint8_t b = a + 1; b < WES_MAX_BALAS; b++) {
      if (!bala[b].viva || bala[a].dir == bala[b].dir) continue;
      float d0 = antes[a]     - antes[b];
      float d1 = bala[a].pos  - bala[b].pos;
      if (d0 * d1 <= 0.0f) {
        bala[a].viva = bala[b].viva = false;
        anuladas++;
        sonarCruce();
        break;
      }
    }
  }

  // Impactos. El tramo recorrido este frame se testea entero (no solo la
  // posicion final) para que una bala rapida no atraviese a nadie sin tocarlo.
  for (uint8_t k = 0; k < WES_MAX_BALAS; k++) {
    if (!bala[k].viva) continue;
    for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
      if (!vivo(j) || j == bala[k].deQuien) continue;
      if (ahora < invulHasta[j]) continue;
      // De espaldas la bala lo atraviesa, salvo que le hayan disparado pegado.
      if (!bala[k].quemarropa && mirando[j] != -bala[k].dir) continue;

      float lo = (antes[k] < bala[k].pos) ? antes[k] : bala[k].pos;
      float hi = (antes[k] < bala[k].pos) ? bala[k].pos : antes[k];
      if (pos[j] < lo - 0.5f || pos[j] > hi + 0.5f) continue;

      bala[k].viva  = false;
      if (vidas[j] > 0) vidas[j]--;
      invulHasta[j] = ahora + WES_INVUL_MS;
      sonarImpacto();
      break;
    }
  }
}

// ---------- Dibujo ----------
// Suma en vez de pisar. Ahora que los pistoleros se atraviesan, sus LEDs se
// solapan al cruzarse: con asignacion, el que se dibuja segundo borraria al
// otro y volveria a pasar lo de "desaparecio un jugador". Sumando, el solape se
// ve como mezcla (verde + azul da cian) y se entiende que estan los dos ahi.
static void sumarLed(int16_t i, const CRGB& c) {
  if (i >= 0 && i < LARGO_TIRA) leds[i] += c;
}

static void dibujarWestern(uint32_t ahora) {
  FastLED.clear();

  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!vivo(j)) continue;
    if (ahora < invulHasta[j] && (ahora / 90) % 2 == 0) continue;   // parpadeo de gracia

    int16_t p = (int16_t)(pos[j] + 0.5f);
    CRGB    c = CONTROLES[j].color;
    CRGB    espalda = CRGB::White;

    if (recargaHasta[j] != 0) {                 // late: esta clavado recargando
      uint8_t f = beatsin8(240, 60, 255);
      c.nscale8(f);
      espalda.nscale8(f);
    }

    // El frente va del color del jugador y la espalda es blanca; el largo total
    // es la vida que le queda. Con una sola vida no queda espalda: el ultimo LED
    // que sobrevive es siempre el frente, o sea que quedarse en uno se ve como
    // "ya no tengo con que cubrirme".
    sumarLed(p, c);
    for (int16_t k = 1; k < escalaLeds(vidas[j]); k++) sumarLed(p - mirando[j] * k, espalda);
  }

  for (uint8_t k = 0; k < WES_MAX_BALAS; k++) {
    if (bala[k].viva) setLed((int16_t)(bala[k].pos + 0.5f), CRGB::White);
  }

  FastLED.show();
}

// ---------- Partida ----------
void nuevoWestern() {
  // Se calibra aca y no al empezar el duelo: durante el selector la gente esta
  // moviendo el stick para elegir, y medir el centro ahi seria medir mal.
  calibrarJoys();
  numJugadores = jugadoresSugeridos();
  estadoWes    = WES_ELIGIENDO;
}

static void arrancarTiros() {
  // Juegan los PRIMEROS n controles, que es como estan puestos sobre la mesa.
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) jugando[j] = (j < numJugadores);

  estadoWes = WES_JUGANDO;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    vidas[j]        = jugando[j] ? WES_VIDAS : 0;
    municion[j]     = (uint8_t)WES_CARGADOR;
    recargaHasta[j] = 0;
    invulHasta[j]   = 0;
    pos[j]          = 0;
    mirando[j]      = +1;
  }

  // Repartidos parejo y con un margen en las puntas, cada uno mirando hacia el
  // centro: la primera bala tarda mas de un segundo en cruzar la tira, que es
  // tiempo de sobra para contestarla o para darse vuelta.
  const int16_t margen = 12;
  uint8_t k = 0;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    pos[j] = margen + (float)(LARGO_TIRA - 1 - 2 * margen) * (float)k / (float)(numJugadores - 1);
    mirando[j] = (pos[j] < LARGO_TIRA / 2) ? +1 : -1;
    k++;
  }

  for (uint8_t b = 0; b < WES_MAX_BALAS; b++) bala[b].viva = false;

  anuladas    = 0;
  ganador     = WES_NADIE;
  esRecord    = false;
  ultimoFrame = millis();
}

void loopWestern() {
  uint32_t ahora = millis();

  if (estadoWes == WES_ELIGIENDO) {
    if (loopSelectorJugadores(numJugadores)) arrancarTiros();
    return;
  }

  if (estadoWes == WES_FIN) {
    uint32_t t = ahora - faseDesde;
    CRGB c = CONTROLES[ganador].color;
    FastLED.clear();
    for (uint16_t i = 0; i < LARGO_TIRA; i++) {
      if ((i + t / 40) % 4 == 0) leds[i] = c;
    }
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > WES_FIN_MS) volverAlMenu();
    return;
  }

  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;
  if (dt > 0.1f) dt = 0.1f;

  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (vivo(j)) actualizarPistolero(j, ahora, dt);
  }
  limitarPosiciones();
  actualizarBalas(ahora, dt);

  // Gana el ultimo en pie.
  uint8_t vivos = 0, ultimo = WES_NADIE;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (vivo(j)) { vivos++; ultimo = j; }
  }
  if (vivos <= 1) {
    ganador   = (ultimo != WES_NADIE) ? ultimo : 0;
    estadoWes = WES_FIN;
    faseDesde = ahora;
    esRecord  = intentarRecord(REC_WESTERN, anuladas, ganador);
    if (esRecord) sonarRecord();
    else          sonarVictoria();
    return;
  }

  dibujarWestern(ahora);
}

// ---------- LCD ----------
void lcdWestern() {
  if (estadoWes == WES_ELIGIENDO) {
    lcdSelectorJugadores("Tiros", numJugadores);
    return;
  }
  // Vidas arriba abreviadas ("Ve3 Az2 Ro0 Am3") y municion abajo en el MISMO
  // orden, con * para el que esta recargando. No se repite la abreviatura en la
  // segunda fila: alcanza con que las dos columnas esten alineadas.
  String vid, bal;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    if (vid.length()) { vid += " "; bal += " "; }
    vid += CONTROLES[j].abrev;
    vid += (char)('0' + (vidas[j] > 9 ? 9 : vidas[j]));
    bal += "  ";
    bal += (recargaHasta[j] != 0) ? '*' : (char)('0' + (municion[j] > 9 ? 9 : municion[j]));
  }
  lcdLinea(0, vid);

  if (estadoWes == WES_FIN) {
    if (esRecord) lcdLinea(1, "RECORD " + String(anuladas) + " cruces");
    else          lcdLinea(1, textoGana(ganador));
    return;
  }
  lcdLinea(1, bal);
}

String webWestern() {
  String s;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    if (s.length()) s += " - ";
    s += String(CONTROLES[j].nombre) + " " + String(vidas[j]);
  }
  return s + " (" + String(anuladas) + " cruces)";
}
