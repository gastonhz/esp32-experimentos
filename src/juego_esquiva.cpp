// ---------- Esquiva: muros solidos y salto apuntado ----------
// OJO con la idea intuitiva de este juego, porque no funciona: "meterse en un
// hueco y despues cruzar al siguiente" es imposible en una dimension. Si el
// muro es solido, pasar del hueco de abajo al de arriba obliga a ocupar los
// LEDs del muro, o sea a morir. Y como el flujo baja sin parar, el hueco en el
// que estas termina saliendose por la base y no queda ningun lugar al que ir:
// el jugador sobrevive los pocos segundos que tarda el primer muro en llegarle
// y despues muere haga lo que haga. En 1D, un obstaculo solido que se mueve no
// se puede rodear.
//
// La salida no es ablandar los muros, es darle al jugador una forma de NO
// recorrer el espacio intermedio: el SALTO.
//
//   mantener Verde   aparece una marca ESQ_SALTO_MIN LEDs mas arriba y se
//                    estira hasta ESQ_SALTO_MAX, donde se queda
//   soltar Verde     el jugador aparece en la marca, sin pasar por el medio
//
// El salto es corto a proposito. Una mira que barre la tira entera se siente
// como frenar un misil: el destino pasa volando por el hueco y soltar a tiempo
// es azar. Acotado a una decena de LEDs, la punteria gruesa la hace el stick
// (caminar hasta quedar debajo del muro) y la mira solo ajusta lo fino. Y la
// carga es monotona, no rebota: si te pasaste no hay que esperar a que la marca
// vuelva, cosa que era el otro motivo por el que se sentia fuera de control.
//
// La marca se pinta blanca si el destino esta libre y parpadea en rojo si cae
// sobre un muro, asi el salto se falla por mal timing y nunca por no haber
// visto. Se puede seguir caminando mientras se apunta: el costo del salto es el
// descanso posterior, no quedarse clavado.
//
// El grosor de los muros se recorta solo para que nunca superen al salto largo
// (ver grosorMaximo), asi ninguna combinacion de parametros deja el juego sin
// solucion. Aun asi los muros gruesos piden estar bien parado abajo y cargar,
// que es donde queda la dificultad.
//
// Los muros se generan de a uno arriba de todo, con un espacio libre garantizado
// respecto del anterior, y todos comparten un unico reloj de bajada: por eso el
// conjunto se lee como una sola pared que se desplaza.

#include "juego_esquiva.h"

// ---------- Parametros ----------
uint16_t ESQ_VEL_JUGADOR   = 60;  // LEDs por segundo caminando
uint16_t ESQ_SALTO_MIN     = 10;  // distancia del salto soltando enseguida (LEDs)
uint16_t ESQ_SALTO_MAX     = 15;  // distancia con la carga completa (LEDs)
uint16_t ESQ_SALTO_CARGA_MS = 550;// cuanto tarda la mira en ir de MIN a MAX. Se
                                  // estiro junto con el rango para que cada LED
                                  // de la carga siga durando mas o menos lo mismo
                                  // y se pueda soltar en el que uno quiere.
uint16_t ESQ_HUECO_MIN     = 7;   // hueco mas chico que puede generarse (LEDs)
uint16_t ESQ_HUECO_MAX     = 16;  // hueco mas grande
uint16_t ESQ_MURO_MIN      = 4;   // muro mas fino
uint16_t ESQ_MURO_MAX      = 10;  // muro mas grueso (grosorMaximo() lo recorta
                                  // si no entrara dentro del salto largo)
uint16_t ESQ_SALTO_ESPERA  = 350; // descanso despues de caer, antes de apuntar de nuevo

const uint8_t  ESQ_MAX_MUROS   = 10;  // tope de muros vivos en la tira a la vez
const uint16_t ESQ_VEL_LENTA   = 130; // bajada mas lenta, pote al minimo (ms por LED)
const uint16_t ESQ_VEL_RAPIDA  = 70;  // bajada mas rapida, pote al maximo (ms por LED)
const uint16_t ESQ_VEL_MINIMA  = 32;  // piso de dificultad
const uint8_t  ESQ_ACELERA_CADA = 4;  // cada cuantos muros esquivados se acelera
const uint16_t ESQ_FIN_MS      = 3000;
// Piso del hueco entre muros. Con hueco 0 dos muros quedarian pegados y se
// leerian como uno solo del doble de grueso, que es justo lo que grosorMaximo()
// trata de evitar. No se tunea: es la condicion para que el recorte sirva.
const int16_t  ESQ_HUECO_PISO  = 2;
// Techo del jugador y de la marca. Existe para que los muros nuevos, que entran
// por la punta de la tira, JAMAS puedan aparecer encima suyo: seria una muerte
// imposible de ver venir.
const int16_t  ESQ_TECHO       = NUM_LEDS - 16;

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

static float    jugadorPos;      // posicion continua
static uint32_t ultimoFrame;
static uint16_t velBajada;       // ms por LED, comun a todos los muros
static uint32_t ultimaBajada;
static uint16_t esquivados;      // muros que salieron por la base (el score)
static bool     esRecord;
static int16_t  caida;           // donde lo agarraron: centro de la animacion de derrota

static bool     apuntando;       // Verde mantenido: la mira se esta cargando
static uint32_t apuntarDesde;    // millis() en que empezo la carga
static int16_t  destino;         // LED al que se saltaria ahora mismo
static uint32_t saltoDesde;      // millis() del ultimo aterrizaje (descanso)

// ---------- Sonido ----------
static const Nota JINGLE_FIN[] = { {349, 160}, {294, 160}, {233, 160}, {147, 500} };

static void sonarPasar()    { beep(1700,  18); }
static void sonarApuntar()  { beep( 600,  30); }
static void sonarSaltar()   { beep(1900,  45); }
static void sonarChoque()   { beep( 130, 260); }
static void sonarGameOver() { tocarJingle(JINGLE_FIN, 4); }

// ---------- Helpers ----------
// Devuelve el LED mas alto ocupado por algun muro; -1 si la tira esta limpia.
static int16_t techoOcupado() {
  int16_t techo = -1;
  for (uint8_t i = 0; i < ESQ_MAX_MUROS; i++) {
    if (muroVivo[i] && muroFin[i] > techo) techo = muroFin[i];
  }
  return techo;
}

static bool hayMuroEn(int16_t led) {
  for (uint8_t i = 0; i < ESQ_MAX_MUROS; i++) {
    if (muroVivo[i] && led >= muroIni[i] && led <= muroFin[i]) return true;
  }
  return false;
}

// Grosor maximo que puede tener un muro para que siga siendo pasable.
// Parado justo debajo de un muro que arranca en `a`, un salto de `d` cae en
// `a - 1 + d` y el muro termina en `a + grosor - 1`, asi que hace falta
// d >= grosor + 1. Con el salto largo como unico techo, un muro de
// ESQ_SALTO_MAX o mas no se pasa NUNCA y el juego se vuelve imposible.
// Se recorta aca en vez de dejarlo como advertencia en un comentario porque
// ESQ_SALTO_MAX se tunea desde el panel web: asi no hay combinacion de valores
// que deje el juego sin solucion.
static int16_t grosorMaximo() {
  int16_t techo = (int16_t)ESQ_SALTO_MAX - 1;
  if (techo < 1) techo = 1;
  int16_t g = (int16_t)ESQ_MURO_MAX;
  if (g > techo) g = techo;      // el techo de seguridad gana siempre, incluso
  if (g < 1)     g = 1;          // sobre un ESQ_MURO_MIN mal puesto desde el panel
  return g;
}

// Coloca un muro que arranca un hueco al azar por encima de `base`. Devuelve
// false si no entra en la tira (no hay lugar todavia, o no quedan slots). El
// extremo de arriba puede pasarse del LED 99 a proposito: el muro entra
// deslizandose desde afuera en vez de aparecer entero de golpe.
static bool colocarMuro(int16_t base) {
  // Todo lo que viene del panel web se ordena antes de usarlo: los cuatro
  // valores son independientes y nada impide que queden cruzados.
  int16_t hmin = (int16_t)ESQ_HUECO_MIN;
  if (hmin < ESQ_HUECO_PISO) hmin = ESQ_HUECO_PISO;
  int16_t hmax = (int16_t)ESQ_HUECO_MAX;
  if (hmax < hmin) hmax = hmin;

  int16_t ini = base + 1 + (int16_t)random(hmin, hmax + 1);
  if (ini > NUM_LEDS - 1) return false;

  int16_t gmax = grosorMaximo();
  int16_t gmin = (int16_t)ESQ_MURO_MIN;
  if (gmin > gmax) gmin = gmax;
  if (gmin < 1)    gmin = 1;

  for (uint8_t i = 0; i < ESQ_MAX_MUROS; i++) {
    if (muroVivo[i]) continue;
    muroVivo[i] = true;
    muroIni[i]  = ini;
    muroFin[i]  = ini + (int16_t)random(gmin, gmax + 1) - 1;
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
  apuntando    = false;
  apuntarDesde = 0;
  destino      = 0;
  saltoDesde   = 0;
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

  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;
  if (dt > 0.1f) dt = 0.1f;

  // --- Caminar ---
  // Se camina siempre, tambien mientras se apunta: la punteria gruesa es esta.
  jugadorPos += leerJoyYNorm() * ESQ_VEL_JUGADOR * dt;
  if (jugadorPos < 0)         jugadorPos = 0;
  if (jugadorPos > ESQ_TECHO) jugadorPos = ESQ_TECHO;
  int16_t jugadorLed = (int16_t)(jugadorPos + 0.5f);

  // --- Apuntar y saltar ---
  if (btnFlanco[0] && !apuntando && (ahora - saltoDesde) >= ESQ_SALTO_ESPERA) {
    apuntando    = true;
    apuntarDesde = ahora;
    sonarApuntar();
  }

  if (apuntando) {
    // Carga lineal de MIN a MAX y ahi se queda. En int16 porque MIN y MAX se
    // tunean desde el panel web y nada impide que queden al reves.
    int16_t dmin = (int16_t)ESQ_SALTO_MIN;
    int16_t dmax = (int16_t)ESQ_SALTO_MAX;
    if (dmax < dmin) dmax = dmin;
    float f = (ESQ_SALTO_CARGA_MS == 0) ? 1.0f
                                        : (float)(ahora - apuntarDesde) / (float)ESQ_SALTO_CARGA_MS;
    if (f > 1.0f) f = 1.0f;

    destino = jugadorLed + dmin + (int16_t)((dmax - dmin) * f + 0.5f);
    if (destino > ESQ_TECHO) destino = ESQ_TECHO;

    if (!btnEstable[0]) {                    // solto: aparece en la marca
      jugadorPos = destino;
      jugadorLed = destino;                  // la colision de este frame ya juega en el destino
      apuntando  = false;
      saltoDesde = ahora;
      sonarSaltar();
    }
  }

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

  // --- Colision: un solo toque y se termina ---
  // Cubre los dos casos con el mismo chequeo: que un muro te alcance parado y
  // que caigas adentro de uno por haber soltado el boton en el momento errado.
  if (hayMuroEn(jugadorLed)) { perder(jugadorLed); return; }

  // --- Dibujo ---
  FastLED.clear();
  for (uint8_t i = 0; i < ESQ_MAX_MUROS; i++) {
    if (!muroVivo[i]) continue;
    for (int16_t p = muroIni[i]; p <= muroFin[i]; p++) setLed(p, COL_MURO);
  }

  if (apuntando) {
    // Blanca si el destino esta libre, parpadeando si cae sobre un muro: el
    // salto se falla por timing, nunca por no haber podido ver.
    CRGB c = hayMuroEn(destino) ? (((ahora / 70) % 2 == 0) ? CRGB(255, 0, 0) : CRGB(60, 0, 0))
                                : CRGB::White;
    setLed(destino, c);
  }

  // El jugador va ultimo (siempre visible) y se dibuja apagado mientras dura el
  // descanso, para que se vea que todavia no puede volver a apuntar.
  bool listo = (ahora - saltoDesde) >= ESQ_SALTO_ESPERA;
  CRGB cj = COL_JUGADOR;
  if (!listo) cj.nscale8(70);
  setLed(jugadorLed, cj);
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
  // La unica instruccion que necesita el juego.
  // Apuntando conviene mostrar la distancia cargada, que es el dato que se esta
  // eligiendo; el resto del tiempo, como se salta.
  lcdLinea(1, apuntando ? ("Salto: " + String(destino - (int16_t)(jugadorPos + 0.5f)) + " LEDs")
                        : "Verde = saltar");
}

String webEsquiva() {
  return "Muros esquivados: " + String(esquivados);
}
