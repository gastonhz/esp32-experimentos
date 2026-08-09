// ---------- Western: duelo a distancia en 1D ----------
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
// Cada pistolero se dibuja con DOS LEDs: el de color es el frente (por donde
// dispara y por donde lo hieren) y el blanco es la espalda. Asi la orientacion
// de los dos se lee de un vistazo y en todo momento, que es la informacion sobre
// la que se juega todo el resto.
//
// Los dos se atraviesan: pasar por encima del rival para quedar de su lado es
// una jugada valida, y ademas evita que el que avanza termine arrastrando al
// otro y tapandolo. Nada en las reglas depende de quien esta a la izquierda.
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
uint16_t WES_VEL_JUGADOR = 16;    // LEDs/s caminando
uint16_t WES_VEL_BALA    = 40;    // LEDs/s: cruzar la tira entera son ~2,5 s
uint16_t WES_CARGADOR    = 6;     // balas por cargador
uint16_t WES_RECARGA_MS  = 1400;  // cuanto inmoviliza recargar
uint16_t WES_INVUL_MS    = 1400;  // gracia despues de comerse una bala
uint16_t WES_QUEMARROPA  = 4;     // a esta distancia o menos, la espalda no salva
uint16_t WES_GIRO_MIN    = 35;    // deflexion minima para girar, en % del recorrido

const uint8_t  WES_VIDAS     = 3;    // tambien es el largo de la barra, en LEDs
const uint8_t  WES_MAX_BALAS = 8;    // balas simultaneas en el aire
const uint16_t WES_FIN_MS    = 4000;

// Las vidas ocupan las puntas; los pistoleros se mueven en el medio, con un LED
// de aire para no confundir el cuerpo con la barra.
const int16_t WES_PISO_MIN = WES_VIDAS + 1;
const int16_t WES_PISO_MAX = NUM_LEDS - WES_VIDAS - 2;

// ---------- Estado ----------
enum EstadoWes { WES_JUGANDO, WES_FIN };
static EstadoWes estadoWes;
static uint32_t  faseDesde;         // millis() de entrada a WES_FIN

// `quemarropa` se decide al salir el tiro, no al impactar: si se evaluara en el
// momento del impacto, cualquier bala terminaria cumpliendo la condicion al
// acercarse a su blanco y la orientacion no protegeria nunca de nada.
struct BalaW { float pos; int8_t dir; bool viva; bool quemarropa; };
static BalaW bala[WES_MAX_BALAS];

static float    pos[2];
static int8_t   mirando[2];         // +1 hacia el final de la tira, -1 hacia el inicio
static uint8_t  vidas[2];
static uint8_t  municion[2];
static uint32_t recargaHasta[2];    // 0 = no esta recargando
static uint32_t invulHasta[2];

static uint8_t  anuladas;           // balas que se cruzaron en el aire
static uint8_t  ganador;            // 1 o 2, valido en WES_FIN
static bool     esRecord;
static uint32_t ultimoFrame;

// ---------- Sonido ----------
static void sonarTiro(uint8_t j) { beep(j == 0 ? 1700 : 1400, 25); }
static void sonarCruce()         { beep(2400, 40); }
static void sonarImpacto()       { beep(200, 200); }
static void sonarRecarga()       { beep(400, 90); }
static void sonarListo()         { beep(1500, 60); }

// ---------- Reglas ----------
// Unico limite de posicion: el piso jugable. Los pistoleros se atraviesan sin
// empujarse -- que uno arrastre al otro y lo tape es peor que dejarlos cruzar.
static void limitarPosiciones() {
  for (uint8_t j = 0; j < 2; j++) {
    if (pos[j] < WES_PISO_MIN) pos[j] = WES_PISO_MIN;
    if (pos[j] > WES_PISO_MAX) pos[j] = WES_PISO_MAX;
  }
}

static bool dispararBala(uint8_t j) {
  for (uint8_t k = 0; k < WES_MAX_BALAS; k++) {
    if (bala[k].viva) continue;
    float d = pos[j] - pos[1 - j];
    if (d < 0.0f) d = -d;
    bala[k].viva       = true;
    bala[k].dir        = mirando[j];
    bala[k].pos        = pos[j] + mirando[j];   // sale del cano, no de adentro del cuerpo
    bala[k].quemarropa = (d <= (float)WES_QUEMARROPA);
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
  pos[j] += m * (float)WES_VEL_JUGADOR * dt;

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
    bala[k].pos += bala[k].dir * (float)WES_VEL_BALA * dt;
    if (bala[k].pos < 0.0f || bala[k].pos > (float)(NUM_LEDS - 1)) bala[k].viva = false;
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
    for (uint8_t j = 0; j < 2; j++) {
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
  if (i >= 0 && i < NUM_LEDS) leds[i] += c;
}

static void dibujarWestern(uint32_t ahora) {
  FastLED.clear();

  for (uint8_t k = 0; k < vidas[0]; k++) setLed(k, COL_P1);
  for (uint8_t k = 0; k < vidas[1]; k++) setLed(NUM_LEDS - 1 - k, COL_P2);

  for (uint8_t j = 0; j < 2; j++) {
    if (ahora < invulHasta[j] && (ahora / 90) % 2 == 0) continue;   // parpadeo de gracia

    int16_t p = (int16_t)(pos[j] + 0.5f);
    CRGB    c = (j == 0) ? COL_P1 : COL_P2;
    CRGB    espalda = CRGB::White;

    if (recargaHasta[j] != 0) {                 // late: esta clavado recargando
      uint8_t f = beatsin8(240, 60, 255);
      c.nscale8(f);
      espalda.nscale8(f);
    }

    // Dos LEDs por pistolero: el de color es el frente (dispara y lo hieren) y
    // el blanco es la espalda. La orientacion queda escrita en la tira todo el
    // tiempo, sin depender de brillos que hay que aprender a interpretar.
    sumarLed(p, c);
    sumarLed(p - mirando[j], espalda);
  }

  for (uint8_t k = 0; k < WES_MAX_BALAS; k++) {
    if (bala[k].viva) setLed((int16_t)(bala[k].pos + 0.5f), CRGB::White);
  }

  FastLED.show();
}

// ---------- Partida ----------
void nuevoWestern() {
  calibrarJoy(0);
  calibrarJoy(1);

  estadoWes = WES_JUGANDO;
  for (uint8_t j = 0; j < 2; j++) {
    vidas[j]        = WES_VIDAS;
    municion[j]     = (uint8_t)WES_CARGADOR;
    recargaHasta[j] = 0;
    invulHasta[j]   = 0;
  }
  // Empiezan lejos y mirandose: la primera bala tarda mas de un segundo en
  // cruzar, que es tiempo de sobra para contestarla o para darse vuelta.
  pos[0]     = WES_PISO_MIN + 15;
  pos[1]     = WES_PISO_MAX - 15;
  mirando[0] = +1;
  mirando[1] = -1;

  for (uint8_t k = 0; k < WES_MAX_BALAS; k++) bala[k].viva = false;

  anuladas    = 0;
  ganador     = 0;
  esRecord    = false;
  ultimoFrame = millis();
}

void loopWestern() {
  uint32_t ahora = millis();

  if (estadoWes == WES_FIN) {
    uint32_t t = ahora - faseDesde;
    CRGB c = (ganador == 1) ? COL_P1 : COL_P2;
    FastLED.clear();
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
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

  for (uint8_t j = 0; j < 2; j++) actualizarPistolero(j, ahora, dt);
  limitarPosiciones();
  actualizarBalas(ahora, dt);

  if (vidas[0] == 0 || vidas[1] == 0) {
    ganador   = (vidas[0] == 0) ? 2 : 1;
    estadoWes = WES_FIN;
    faseDesde = ahora;
    esRecord  = intentarRecord(REC_WESTERN, anuladas);
    if (esRecord) sonarRecord();
    else          sonarVictoria();
    return;
  }

  dibujarWestern(ahora);
}

// ---------- LCD ----------
void lcdWestern() {
  lcdLinea(0, "Verde " + String(vidas[0]) + " - " + String(vidas[1]) + " Azul");

  if (estadoWes == WES_FIN) {
    if (esRecord) lcdLinea(1, "RECORD " + String(anuladas) + " cruces");
    else          lcdLinea(1, (ganador == 1) ? "** GANA VERDE **" : "** GANA AZUL **");
    return;
  }

  String a = (recargaHasta[0] != 0) ? String("REC") : String(municion[0]);
  String b = (recargaHasta[1] != 0) ? String("REC") : String(municion[1]);
  lcdLinea(1, "Balas " + a + " | " + b);
}

String webWestern() {
  return "Verde " + String(vidas[0]) + " - " + String(vidas[1]) + " Azul (" +
         String(anuladas) + " cruces)";
}
