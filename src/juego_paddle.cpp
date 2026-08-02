// ---------- Paddle: un jugador contra la pared del fondo ----------
// Por que no alcanza con "mover una paleta": en una dimension no hay angulo, y
// una pelota que viaja por la linea SIEMPRE atraviesa a la paleta, la pongas
// donde la pongas. Una paleta que devuelve sola seria un juego sin derrota.
// Por eso el golpe lo sigue dando el boton (como en el Pong) y el joystick
// decide DONDE esta la paleta, que es la apuesta del juego:
//
//   paleta arriba  -> el viaje de ida y vuelta es corto: menos tiempo para
//                     reaccionar, pero cada golpe vale mas puntos
//   paleta abajo   -> comodo y seguro, pero casi no suma
//
// Ademas la ventana de golpe dura (largo de la paleta x velocidad de la pelota)
// milisegundos, y las dos cosas se achican con los golpes: el juego termina
// siendo de timing puro con la dificultad que vos mismo elegiste.

#include "juego_paddle.h"

// ---------- Parametros ----------
uint16_t PAD_VEL_JUGADOR = 55;   // LEDs por segundo de la paleta
uint16_t PAD_LARGO_INI   = 10;   // largo de la paleta al empezar (LEDs)
uint16_t PAD_ACHICA_CADA = 4;    // cada cuantos golpes se acorta la paleta

const uint8_t  PAD_LARGO_MIN  = 3;    // piso: menos que esto es inhumano
const uint16_t PAD_VEL_LENTA  = 70;   // pelota mas lenta, pote al minimo (ms por LED)
const uint16_t PAD_VEL_RAPIDA = 28;   // pelota mas rapida, pote al maximo (ms por LED)
const uint16_t PAD_VEL_MINIMA = 12;   // piso de aceleracion (ms por LED)
const uint16_t PAD_ACELERA    = 2;    // cuanto baja el intervalo por cada golpe
const uint8_t  PAD_VIDAS      = 3;
const int16_t  PAD_TOPE       = NUM_LEDS - 14;  // la paleta no puede pegarse a la pared:
                                                // sin viaje no hay juego
const uint16_t PAD_PERDIDA_MS = 1200; // parpadeo rojo al perder una vida
const uint16_t PAD_FIN_MS     = 3200;
// Sin esto el juego no existe: si errar no costara nada, la jugada optima seria
// martillar el boton, porque con la ventana ancha de los primeros golpes una
// pulsacion cada 100 ms acierta siempre. Con el castigo, cada golpe al aire deja
// la paleta muerta un cuarto de segundo y martillar pasa a ser la peor idea.
uint16_t PAD_FALLO_MS = 260;

static const CRGB COL_PALETA = CRGB(  0, 255,  60);
static const CRGB COL_PARED  = CRGB(  0,  60, 255);

// ---------- Estado ----------
enum EstadoPad { PAD_SAQUE, PAD_JUGANDO, PAD_PERDIDA, PAD_FIN };
static EstadoPad estadoPad;
static uint32_t  faseDesde;      // millis() en que empezo PAD_SAQUE/PERDIDA/FIN

static float    paletaCentro;    // posicion continua: se mueve por velocidad
static uint8_t  paletaLargo;
static uint32_t ultimoFrame;     // para el dt del movimiento de la paleta

static int16_t  pelotaPos;
static int8_t   pelotaDir;       // +1 hacia la pared, -1 hacia el jugador
static uint16_t pelotaVel;       // ms por paso
static uint16_t velInicial;      // la fija el pote al arrancar la partida
static uint32_t ultimoPaso;

static uint8_t  vidas;
static uint16_t golpes;          // golpes acertados (para achicar la paleta)
static uint32_t score;           // puntos: cada golpe vale mas cuanto mas arriba este la paleta
static bool     esRecord;
static uint32_t brilloGolpe;     // millis() del ultimo golpe: la paleta destella
static uint32_t falloDesde;      // millis() del ultimo golpe al aire: la paleta queda muerta

// ---------- Sonido ----------
static const Nota JINGLE_FIN[] = { {440, 170}, {370, 170}, {294, 170}, {196, 520} };

static void sonarGolpe()    { beep(1100 + (PAD_VEL_LENTA - pelotaVel) * 12, 30); }
static void sonarAlAire()   { beep( 300, 55); }   // grave y feo: erraste y quedaste vendido
static void sonarPared()    { beep(1800, 20); }
static void sonarFallo()    { beep( 150, 260); }
static void sonarGameOver() { tocarJingle(JINGLE_FIN, 4); }

// ---------- Helpers ----------
static int16_t paletaIni() { return (int16_t)(paletaCentro + 0.5f) - paletaLargo / 2; }
static int16_t paletaFin() { return paletaIni() + paletaLargo - 1; }

// El bonus por altura es lo que le da sentido al joystick: mantener la paleta
// arriba es incomodo y rinde hasta 4 veces mas por golpe.
static uint8_t valorDelGolpe() {
  return 1 + (uint8_t)(paletaCentro / 25.0f);   // 1 abajo del todo, 4 cerca del tope
}

// Saca de nuevo desde la paleta hacia la pared, sin tocar vidas ni score.
static void prepararSaque() {
  pelotaPos  = paletaFin() + 1;
  pelotaDir  = +1;
  pelotaVel  = velInicial;
  ultimoPaso = millis();
  estadoPad  = PAD_SAQUE;
  faseDesde  = millis();
}

void nuevoPaddle() {
  calibrarJoyY();
  velInicial   = map(leerPoteCrudo(), 0, 4095, PAD_VEL_LENTA, PAD_VEL_RAPIDA);
  paletaCentro = 12;
  paletaLargo  = PAD_LARGO_INI;
  vidas        = PAD_VIDAS;
  golpes       = 0;
  score        = 0;
  esRecord     = false;
  brilloGolpe  = 0;
  falloDesde   = 0;
  ultimoFrame  = millis();
  prepararSaque();
}

static void perder() {
  estadoPad = PAD_FIN;
  faseDesde = millis();
  esRecord  = intentarRecord(REC_PADDLE, score);
  esRecord ? sonarRecord() : sonarGameOver();
}

// La paleta se mueve tambien mientras se saca y mientras se pierde una vida:
// asi el jugador ya la tiene donde quiere cuando arranca el punto siguiente.
static void moverPaleta() {
  uint32_t ahora = millis();
  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;

  paletaCentro += leerJoyYNorm() * PAD_VEL_JUGADOR * dt;
  float minimo = paletaLargo / 2.0f;
  float maximo = (float)PAD_TOPE;
  if (paletaCentro < minimo) paletaCentro = minimo;
  if (paletaCentro > maximo) paletaCentro = maximo;
}

// ---------- Dibujo ----------
static void dibujarEscena() {
  FastLED.clear();
  setLed(NUM_LEDS - 1, COL_PARED);

  // Tres estados en el mismo color: blanco justo despues de acertar, rojo
  // apagado mientras esta muerta por haber errado, verde el resto del tiempo.
  CRGB c;
  if ((millis() - brilloGolpe) < 90)             c = CRGB::White;
  else if ((millis() - falloDesde) < PAD_FALLO_MS) c = CRGB(70, 20, 0);
  else                                            c = COL_PALETA;
  for (int16_t i = paletaIni(); i <= paletaFin(); i++) setLed(i, c);
}

void loopPaddle() {
  uint32_t ahora = millis();

  if (estadoPad == PAD_FIN) {
    // Derrota: barrido que se apaga desde la pared hacia la base, y al menu.
    uint32_t t = ahora - faseDesde;
    int16_t  frente = NUM_LEDS - 1 - (int16_t)(t / 25);
    FastLED.clear();
    for (int16_t i = 0; i <= frente; i++) setLed(i, CRGB(60, 0, 0));
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > PAD_FIN_MS) volverAlMenu();
    return;
  }

  moverPaleta();

  if (estadoPad == PAD_PERDIDA) {
    // Parpadeo rojo sobre la escena y despues se saca de nuevo.
    uint32_t t = ahora - faseDesde;
    dibujarEscena();
    if ((t / 150) % 2 == 0) {
      for (int16_t i = 0; i < 8; i++) setLed(i, CRGB(120, 0, 0));
    }
    FastLED.show();
    if (t > PAD_PERDIDA_MS) prepararSaque();
    return;
  }

  if (estadoPad == PAD_SAQUE) {
    // La pelota espera pegada a la paleta y la sigue; sale con el boton o sola
    // a los 2 s, asi el que no entendio igual arranca.
    pelotaPos = paletaFin() + 1;
    bool lanzar = btnFlanco[0] || (ahora - faseDesde > 2000);
    dibujarEscena();
    if ((ahora / 250) % 2 == 0) setLed(pelotaPos, COL_PELOTA);
    FastLED.show();
    if (lanzar) {
      sonarPared();
      ultimoPaso = ahora;
      estadoPad  = PAD_JUGANDO;
    }
    return;
  }

  // --- PAD_JUGANDO ---
  // Golpe: solo cuenta si la pelota viene bajando, esta dentro de la paleta y
  // la paleta no quedo muerta por un golpe al aire anterior.
  if (btnFlanco[0]) {
    bool enVentana = (pelotaDir == -1 && pelotaPos >= paletaIni() && pelotaPos <= paletaFin());
    if (enVentana && (ahora - falloDesde) >= PAD_FALLO_MS) {
      pelotaDir   = +1;
      pelotaVel   = max<int>(PAD_VEL_MINIMA, pelotaVel - PAD_ACELERA);
      golpes++;
      score      += valorDelGolpe();
      brilloGolpe = ahora;
      sonarGolpe();
      if (golpes % PAD_ACHICA_CADA == 0 && paletaLargo > PAD_LARGO_MIN) paletaLargo--;
    } else if (!enVentana) {
      falloDesde = ahora;                 // golpe al aire: la paleta queda muerta un rato
      sonarAlAire();
    }
  }

  // --- Movimiento de la pelota (1 LED cada pelotaVel ms) ---
  if (ahora - ultimoPaso >= pelotaVel) {
    ultimoPaso += pelotaVel;
    pelotaPos  += pelotaDir;

    if (pelotaPos >= NUM_LEDS - 1) {     // rebote en la pared del fondo
      pelotaPos = NUM_LEDS - 1;
      pelotaDir = -1;
      sonarPared();
    } else if (pelotaPos < 0) {          // se escapo por abajo: vida perdida
      sonarFallo();
      vidas--;
      if (vidas == 0) { perder(); return; }
      estadoPad = PAD_PERDIDA;
      faseDesde = ahora;
      return;
    }
  }

  dibujarEscena();
  dibujarPuntoConEstela(pelotaPos, pelotaDir, COL_PELOTA);
  FastLED.show();
}

// ---------- LCD ----------
void lcdPaddle() {
  if (estadoPad == PAD_FIN) {
    lcdLinea(0, "** GAME OVER **");
    if (esRecord) lcdLinea(1, "*NUEVO RECORD!*");
    else          lcdLinea(1, "Puntos: " + String(score));
    return;
  }
  lcdLinea(0, "Pts " + String(score) + "  Vid " + String(vidas));
  if (estadoPad == PAD_PERDIDA) { lcdLinea(1, "Vida perdida!"); return; }
  // El multiplicador es la unica forma de que el jugador entienda que subir la
  // paleta paga: se muestra en vivo mientras la mueve.
  lcdLinea(1, "Paleta " + String(paletaLargo) + "  x" + String(valorDelGolpe()));
}

String webPaddle() {
  return "Puntos: " + String(score) + ", vidas " + String(vidas) +
         ", paleta " + String(paletaLargo) + " LEDs";
}
