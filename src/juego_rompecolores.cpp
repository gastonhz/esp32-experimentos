// ---------- Rompecolores: contra el muro de colores ----------
// El muro es una FILA contigua que ocupa [rcFrente, NUM_LEDS-1]: los colores
// nuevos entran por el extremo lejano (NUM_LEDS-1) y empujan a los viejos hacia
// el jugador, como una cinta transportadora. Por eso el frente (rcFrente) es
// siempre el color mas VIEJO: se lo ve venir desde lejos y se puede tener la
// bala del color correcto lista. rcFrente == NUM_LEDS significa "muro vacio"
// (asi arranca la partida y asi queda si se rompe el ultimo LED).
// La base ocupa [RC_BASE, RC_BASE + baseLeds - 1]; si el muro llega ahi se
// pierde. random() en este core usa el RNG por hardware, no hace falta semilla.
//
// Antes de jugar se elige entre dos formas de disparar, que son dos juegos
// bastante distintos:
//
//   UN CONTROL     el SW del joystick cicla el color cargado y el arcade
//                  dispara. Es el juego de siempre: hay que PREPARAR la bala,
//                  y equivocarse de color es culpa de haber ciclado mal.
//   CUATRO         cada arcade dispara el color de SU control. Con los cuatro
//                  mandos delante no hay nada que ciclar: se ve el frente del
//                  muro y se manotea el control de ese color. Mucho mas rapido
//                  y mucho mas facil de arruinar con las manos cruzadas.
//
// La paleta del muro es exactamente la de los cuatro controles, asi que en el
// modo de cuatro no hay ninguna correspondencia que aprender: tu bala sale del
// color de tu propio mando.

#include "juego_rompecolores.h"

// ---------- Parametros ----------
uint16_t       RC_VEL_LENTA  = 900;   // avance mas lento del muro, pote al minimo (ms por LED)
uint16_t       RC_VEL_RAPIDA = 320;   // avance mas rapido del muro, pote al maximo (ms por LED)
uint16_t       RC_PROY_VEL   = 12;    // velocidad del proyectil (ms por LED)
const uint16_t RC_VEL_MINIMA = 110;   // piso de dificultad: mas rapido que esto no acelera
const uint8_t  RC_PUNTOS_SUBE = 5;    // cada cuantos puntos se acorta el intervalo de avance
const uint8_t  RC_MAX_PROYECTILES = 5;// balas simultaneas en vuelo: con ESTELA=2 cada una ocupa 3 LEDs,
                                      // mas que esto en una tira de 100 se vuelve un choclo ilegible
const uint8_t  RC_BASE       = 0;     // primer LED de la base (muestra el color cargado)
const uint8_t  RC_BASE_LEDS  = 2;     // ancho de la base en LEDs: mas visible que uno solo
const uint16_t RC_FIN_MS     = 3500;  // duracion de la animacion de derrota antes de volver al menu

// La paleta es la MISMA que la de los cuatro controles (ver CONTROLES[] en
// consola.cpp): es lo que hace que en el modo de cuatro la bala de cada jugador
// salga de su propio color. Si se toca una, hay que tocar la otra.
static const CRGB RC_ROJO     = CRGB(255,   0,   0);
static const CRGB RC_VERDE    = CRGB(  0, 255,   0);
static const CRGB RC_AZUL     = CRGB(  0,  60, 255);
static const CRGB RC_AMARILLO = CRGB(255, 235,   0);
static const CRGB RC_COLORES[4]       = { RC_ROJO, RC_VERDE, RC_AZUL, RC_AMARILLO };
static const char* RC_NOMBRE_COLOR[4] = { "ROJO", "VERDE", "AZUL", "AMARILLO" };

// Indice en RC_COLORES del color de cada control: C1 verde, C2 azul, C3 rojo,
// C4 amarillo.
static const uint8_t RC_COLOR_DE_CONTROL[NUM_CONTROLES] = { 1, 2, 0, 3 };

// Modo de disparo, que se elige antes de cada partida.
enum ModoRc { RC_UN_CONTROL, RC_CUATRO_CONTROLES, RC_NUM_MODOS };
static const char* RC_NOMBRE_MODO[RC_NUM_MODOS] = { " 1 control ", " 4 controles " };

// ---------- Estado ----------
enum EstadoRc { RC_ELIGIENDO, RC_JUGANDO, RC_FIN };
static EstadoRc estadoRc;
static uint8_t  modo;                   // ModoRc elegido en la pantalla previa
static uint8_t  baseLeds;               // ancho de la base: 2 con un control, 4 con cuatro
static int16_t  frente;                 // LED del muro mas cercano al jugador; NUM_LEDS = muro vacio
static CRGB     muro[NUM_LEDS];         // color de cada LED del muro (solo importa en [frente, NUM_LEDS-1])
static uint16_t score;                  // LEDs rotos
static uint16_t velAvance;              // intervalo actual de avance del muro (ms por LED)
static uint32_t ultimoAvance;           // millis() del ultimo avance del muro
static uint8_t  colorCargado;           // indice en RC_COLORES de la bala lista para disparar
static bool     esRecord;
static uint32_t finDesde;

// Proyectiles en vuelo, en arrays paralelos: se puede disparar de nuevo sin
// esperar al impacto.
static bool     proyActivo[RC_MAX_PROYECTILES];  // false = slot libre para un disparo nuevo
static int16_t  proyPos[RC_MAX_PROYECTILES];
static uint8_t  proyColor[RC_MAX_PROYECTILES];   // indice de color con el que salio cada bala
static uint32_t proyPaso[RC_MAX_PROYECTILES];    // millis() del ultimo paso de cada bala

// ---------- Sonido ----------
// Derrota descendente y lenta, para que no se confunda con la victoria.
static const Nota JINGLE_GAMEOVER[] = { {392, 180}, {330, 180}, {262, 180}, {196, 500} }; // G-E-C-G(bajo)

// Cuatro pitidos bien distintos entre si para que el oido solo alcance a
// distinguir cambio de color / disparo / acierto / fallo.
static void sonarColor()    { beep(1900,  25); }  // corto y agudo: cambio de bala
static void sonarDisparo()  { beep( 700,  35); }  // mas grave que el cambio de color
static void sonarAcierto()  { beep(2300,  70); }  // el mas agudo y largo: rompio un LED
static void sonarFallo()    { beep( 160, 200); }  // grave y largo: penalidad
static void sonarGameOver() { tocarJingle(JINGLE_GAMEOVER, 4); }

void nuevoRompecolores() {
  calibrarJoy(0);                 // la pantalla de modo se navega con la cruz
  modo     = RC_UN_CONTROL;
  estadoRc = RC_ELIGIENDO;
}

// Arranca la partida de verdad, ya con el modo elegido. El pote se lee ACA y no
// al entrar al juego: asi la dificultad es la que marca la perilla en el momento
// de empezar a jugar, no la que marcaba mientras se elegia el modo.
static void arrancarPartida() {
  frente = NUM_LEDS;              // sin muro al empezar
  for (int16_t i = 0; i < NUM_LEDS; i++) muro[i] = CRGB::Black;
  score        = 0;
  velAvance    = map(leerPoteCrudo(), 0, 4095, RC_VEL_LENTA, RC_VEL_RAPIDA);
  ultimoAvance = millis();
  colorCargado = 0;               // arranca con Rojo cargado
  esRecord     = false;
  // Con cuatro controles la base muestra los cuatro colores en fila, uno por
  // mando: es la chuleta de que arcade dispara que color.
  baseLeds     = (modo == RC_CUATRO_CONTROLES) ? 4 : RC_BASE_LEDS;
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) {
    proyActivo[p] = false;                   // todos los slots libres
    proyPos[p]    = RC_BASE + baseLeds;      // sale justo delante de la base
    proyColor[p]  = 0;
  }
  estadoRc = RC_JUGANDO;
}

// Mete una bala del color pedido, si queda algun slot libre. Se puede disparar
// sin esperar al impacto de la anterior; si estan los RC_MAX_PROYECTILES en
// vuelo el disparo se pierde y listo.
static void disparar(uint8_t colorIdx) {
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) {
    if (proyActivo[p]) continue;
    proyActivo[p] = true;
    proyColor[p]  = colorIdx;
    proyPos[p]    = RC_BASE + baseLeds;      // sale justo delante de la base
    proyPaso[p]   = millis();
    sonarDisparo();
    return;
  }
}

static void perder() {
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) proyActivo[p] = false;
  estadoRc = RC_FIN;
  finDesde = millis();
  // El puntaje es del equipo entero, no de un jugador: lo firma el Verde.
  esRecord = intentarRecord(REC_RC, score);
  esRecord ? sonarRecord() : sonarGameOver();
}

// Acierto: rompe el frente y le devuelve terreno al jugador; cada RC_PUNTOS_SUBE
// puntos el muro acelera un 10%, con piso en RC_VEL_MINIMA.
static void acertar() {
  frente++;
  score++;
  sonarAcierto();
  if (score % RC_PUNTOS_SUBE == 0) {
    velAvance = max<int>(RC_VEL_MINIMA, (velAvance * 9) / 10);
  }
}

// Fallo: se agrega una capa nueva del color que erraste, un LED mas cerca de la
// base. Para pelarla hay que acertarle a ESE color antes de ver la de abajo.
// Recibe el color de la bala que fallo porque puede haber varias en vuelo.
static void fallar(uint8_t colorBala) {
  frente--;
  muro[frente] = RC_COLORES[colorBala];
  sonarFallo();
}

void loopRompecolores() {
  if (estadoRc == RC_ELIGIENDO) {
    int8_t paso = joystickPaso(0);
    if (paso) {
      modo = (modo + RC_NUM_MODOS + paso) % RC_NUM_MODOS;
      beep(1200, 25);
    }
    if (btnFlanco[0]) { arrancarPartida(); return; }

    // Muestra de que va cada modo, sin texto: una bala sola que cicla los cuatro
    // colores, o los cuatro colores juntos en fila.
    FastLED.clear();
    if (modo == RC_UN_CONTROL) {
      uint8_t c = (millis() / 500) % 4;
      for (uint8_t i = 0; i < RC_BASE_LEDS; i++) setLed(RC_BASE + i, RC_COLORES[c]);
    } else {
      for (uint8_t i = 0; i < 4; i++) setLed(RC_BASE + i, RC_COLORES[i]);
    }
    FastLED.show();
    return;
  }

  if (estadoRc == RC_FIN) {
    // Derrota: alarma roja que se va comiendo la tira desde la base, y al menu.
    uint32_t t = millis() - finDesde;
    bool    on = (t / 130) % 2 == 0;
    int16_t comido = t / 30;                  // el muro ya paso por ahi
    FastLED.clear();
    for (int16_t i = comido; i < NUM_LEDS; i++) setLed(i, on ? RC_ROJO : CRGB(40, 0, 0));
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > RC_FIN_MS) volverAlMenu();
    return;
  }

  // --- Controles, segun el modo elegido ---
  if (modo == RC_UN_CONTROL) {
    if (btnStickFlanco[0]) {
      colorCargado = (colorCargado + 1) % 4;   // Rojo -> Verde -> Azul -> Amarillo
      sonarColor();
    }
    if (btnFlanco[0]) disparar(colorCargado);
  } else {
    for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
      if (btnFlanco[j]) disparar(RC_COLOR_DE_CONTROL[j]);
    }
  }

  // --- Avance natural del muro (la amenaza constante) ---
  // El color nuevo entra SIEMPRE por el extremo lejano y toda la fila se corre
  // un LED hacia el jugador. Es lo que hace al juego jugable: la secuencia se
  // ve venir con anticipacion. (Si el color nuevo apareciera directo en el
  // frente, acertar seria pura suerte.)
  if (millis() - ultimoAvance >= velAvance) {
    ultimoAvance += velAvance;
    frente--;
    for (int16_t i = frente; i < NUM_LEDS - 1; i++) muro[i] = muro[i + 1];
    muro[NUM_LEDS - 1] = RC_COLORES[(uint8_t)random(4)];
    if (frente < baseLeds) { perder(); return; }   // el muro toco la base
  }

  // --- Movimiento de los proyectiles (mucho mas rapido que el muro) ---
  // Cada bala lleva su propio timestamp, asi vuelan independientes aunque hayan
  // salido en momentos distintos.
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) {
    if (!proyActivo[p]) continue;
    if (millis() - proyPaso[p] >= RC_PROY_VEL) {
      proyPaso[p] += RC_PROY_VEL;
      proyPos[p]++;
      if (proyPos[p] > NUM_LEDS - 1) proyActivo[p] = false;  // no habia muro: sin penalidad
    }
  }

  // --- Colision: solo puede pasar en el frente del muro ---
  // Se resuelve de a una bala por vez contra el frente ACTUALIZADO: si dos
  // llegan en el mismo frame, la segunda ya juega contra la capa que dejo la
  // primera (rompio o agrego), nunca contra el frente viejo.
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) {
    if (!proyActivo[p]) continue;
    if (frente >= NUM_LEDS || proyPos[p] < frente) continue;
    if (muro[frente] == RC_COLORES[proyColor[p]]) acertar();
    else                                          fallar(proyColor[p]);
    proyActivo[p] = false;                          // el proyectil se consume igual
    if (frente < baseLeds) { perder(); return; }  // la penalidad tambien puede perder
  }

  // --- Dibujo: base con el color cargado, muro, proyectiles con estela ---
  FastLED.clear();
  if (modo == RC_UN_CONTROL) {
    for (uint8_t i = 0; i < baseLeds; i++) setLed(RC_BASE + i, RC_COLORES[colorCargado]);
  } else {
    for (uint8_t i = 0; i < 4; i++) setLed(RC_BASE + i, RC_COLORES[i]);
  }
  for (int16_t i = frente; i < NUM_LEDS; i++) setLed(i, muro[i]);
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) {
    if (!proyActivo[p]) continue;
    setLed(proyPos[p], RC_COLORES[proyColor[p]]);
    for (uint8_t k = 1; k <= ESTELA; k++) {     // cola detras, cada paso mas tenue
      CRGB c = RC_COLORES[proyColor[p]];
      c.nscale8(255 / (k + 1));
      // la estela no pisa la base, que muestra el color cargado
      if (proyPos[p] - k >= RC_BASE + baseLeds) setLed(proyPos[p] - k, c);
    }
  }
  FastLED.show();
}

// ---------- LCD: score en vivo + color cargado (o score final al perder) ----------
void lcdRompecolores() {
  if (estadoRc == RC_ELIGIENDO) {
    lcdLinea(0, "Rompecolores");
    lcdLinea(1, "<" + String(RC_NOMBRE_MODO[modo]) + ">");
    return;
  }
  if (estadoRc == RC_FIN) {
    lcdLinea(0, "** GAME OVER **");
    if (esRecord) lcdLinea(1, "*NUEVO RECORD!*");
    else          lcdLinea(1, "Score final: " + String(score));
    return;
  }
  lcdLinea(0, "Score: " + String(score));
  // Con cuatro controles no hay bala cargada que mostrar: cada arcade tiene la
  // suya. Se muestra el color del frente del muro, que es el dato que importa.
  if (modo == RC_UN_CONTROL) {
    lcdLinea(1, "Bala: " + String(RC_NOMBRE_COLOR[colorCargado]));
  } else if (frente < NUM_LEDS) {
    uint8_t f = 0;
    for (uint8_t c = 0; c < 4; c++) if (muro[frente] == RC_COLORES[c]) f = c;
    lcdLinea(1, "Muro: " + String(RC_NOMBRE_COLOR[f]));
  } else {
    lcdLinea(1, "- sin muro -");
  }
}

String webRompecolores() {
  return "Score: " + String(score);
}
