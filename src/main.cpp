/*********
  MAQUINA DE JUEGOS LED — consola 1D sobre tira WS2812B (ESP32 + FastLED).

  Al encender aparece un SELECTOR: con el eje X del joystick se pasa de juego
  (izquierda/derecha, con wraparound) y con el boton Verde (P1) se arranca el
  resaltado; el Azul (P2) queda libre para las partidas de dos jugadores. El
  pulsador de reset vuelve al selector desde cualquier juego (y al terminar una
  partida vuelve solo).

  Juegos:
    - Pong 1D: pelota que rebota; cada jugador golpea con su boton cuando la
      pelota entra en su zona; cada golpe acelera. El potenciometro fija la
      velocidad de saque.
    - Tira y Afloja: cinchada de botones; un frente Verde|Azul que se empuja a
      martillazos hacia el lado del rival; el que llega al extremo contrario gana.
    - Rompecolores: un solo jugador contra un muro de colores que avanza solo
      hacia la base. Los colores nuevos entran por el extremo LEJANO y empujan
      la fila hacia el jugador, asi se ve venir la secuencia y se puede preparar
      la bala. P1 cicla el color de la bala, P2 dispara (se puede tener varias
      balas en vuelo a la vez, sin esperar al impacto); si el color coincide
      con el frente del muro lo rompe, si no el muro crece de castigo. Si el
      muro toca la base es game over. El pote fija la velocidad inicial.
    - Twang: dungeon 1D de un jugador. Un punto verde se mueve con el joystick
      (la deflexion del eje Y es VELOCIDAD, no pasos, como la inclinacion del
      TWANG original) mientras enemigos rojos bajan desde el fondo. P1 dispara
      un pulso blanco que mata a los que quedan cerca. Cuando la tanda esta
      limpia parpadea la salida azul del ultimo LED: al llegar ahi se sube de
      nivel (mas enemigos y mas rapidos). Tres vidas, y el pote fija la
      velocidad inicial de los enemigos. El nivel tambien tiene terreno: tramos
      de lava naranja que se prenden y apagan en ciclo (pisarla encendida cuesta
      una vida, y avisa parpadeando antes de encenderse) y cintas
      transportadoras cyan que empujan al jugador hacia un lado.
    - Highscores: no es un juego, es la vitrina. Muestra el record guardado de
      cada uno de los cuatro juegos (sobrevive a los reinicios); se pasa de
      juego con el eje X del joystick y con el reset se vuelve al selector.
    - IP: tampoco es un juego. El ESP32 levanta su propia red WiFi y sirve un
      panel web en http://192.168.4.1 para ver el estado de la partida en vivo
      y tunear parametros de balance sin recompilar; esta pantalla muestra el
      nombre de la red y esa IP.

  Hardware (ver scripts-y-pruebas/setup-hardware-maquina-juegos-led.md):
    Datos:    GPIO16 -> SN74AHCT125N -> 470ohm -> DIN tira (100 LEDs WS2812B)
    Boton P1 (Verde): GPIO14 a GND (arcade/microswitch; INPUT_PULLUP, apretado = LOW)
    Boton P2 (Azul):  GPIO27 a GND (idem)
    Buzzer:   GPIO25 (buzzer pasivo, PWM por LEDC) a GND
    LCD 1602: I2C 0x27, SDA=GPIO21, SCL=GPIO22 (marcador + mensajes de estado)
    Reset:    GPIO18 a GND (pulsador; INPUT_PULLUP, apretado = LOW) -> vuelve al menu
    Pote:     GPIO34 (ADC1, input-only) -- B10k: extremos a 3.3V/GND, cursor al pin
    Joystick: PS2 analogico -- VRy=GPIO33 (ADC1), VRx=GPIO32 (ADC1), VCC a 3.3V
    Tira alimentada por fuente externa 5V, masa comun, cap de 1000uF al inicio.
*********/

#include <FastLED.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>   // records en NVS (flash interna): sobreviven al apagon
#include <WiFi.h>
#include <ESPAsyncWebServer.h>   // servidor asincronico: no necesita atencion desde loop()

// ---------- Hardware ----------
#define NUM_LEDS    100
#define DATA_PIN    16
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

// Botones arcade (microswitch) a GND, con pull-up interno. Apretado = LOW.
#define BTN_P1 14
#define BTN_P2 27

#define BUZZER_PIN 25   // buzzer pasivo por LEDC
#define BUZZER_CH  0    // canal LEDC (core arduino-esp32 2.0.x)

#define RESET_PIN  18   // pulsador viejo reusado como reset del partido

#define POT_PIN    34   // potenciometro B10k -> velocidad de saque (ADC1)

// Joystick analogico PS2, alimentado a 3.3V para que la salida entre en el
// rango del ADC sin divisor. La tira va montada vertical, asi que inclinar el
// joystick "hacia arriba/abajo" (eje Y) es lo que mueve al jugador; el eje X,
// que en el juego no hace falta, se aprovecha para navegar el menu.
#define JOY_Y_PIN  33   // VRy -> ADC1_CH5: movimiento del jugador en Twang
#define JOY_X_PIN  32   // VRx -> ADC1_CH4: navegacion del menu y de Highscores

// LCD 1602 por I2C (marcador + mensajes de estado).
#define LCD_ADDR 0x27
#define LCD_SDA  21
#define LCD_SCL  22

// Red propia del ESP32 (modo Access Point, no se conecta a ningun router): asi
// la consola anda en cualquier lado sin depender del WiFi de casa y la IP del
// panel es siempre la misma. Cambiar aca si hace falta (WPA2 pide 8 caracteres
// minimo en la clave).
#define AP_SSID "GastiConsola"
#define AP_PASS "led12345"


String apIP;   // IP del AP como texto, se calcula una sola vez en setup()

CRGB leds[NUM_LEDS];
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// ---------- Parametros del juego (tocar aca para balancear) ----------
// Los que NO son const se pueden cambiar en caliente desde el panel web
// (pantalla IP): son el subset curado que vale la pena tunear jugando.
uint8_t        BRILLO         = 128;  // techo de seguridad de corriente
const uint8_t  ZONA           = 12;   // largo de la zona de golpe de cada jugador
uint16_t       VEL_LENTA      = 90;   // saque mas lento, pote al minimo (ms por LED)
uint16_t       VEL_RAPIDA     = 24;   // saque mas rapido, pote al maximo (ms por LED)
const uint16_t VEL_MINIMA     = 16;   // piso de aceleracion (ms por LED)
uint16_t       VEL_ACELERA    = 4;    // cuanto baja el intervalo por cada golpe
const uint8_t  PUNTOS_GANAR   = 5;
const uint16_t DEBOUNCE_MS    = 25;
const uint8_t  ESTELA         = 2;    // largo de la cola de la pelota
uint8_t        TUG_EMPUJE     = 2;    // LEDs que avanza el frente por pulsacion (Tira y Afloja)

// --- Menu y Highscores: navegacion con el eje X del joystick ---
const uint16_t MENU_JOY_MUERTA = 700; // zona muerta ancha: elegir juego no necesita
                                      // precision y asi el menu no se mueve solo
const uint16_t MENU_REPETIR_MS = 350; // auto-repeat manteniendo el stick a un costado

// --- Rompecolores: balance del muro y del disparo ---
uint16_t       RC_VEL_LENTA   = 900;  // avance mas lento del muro, pote al minimo (ms por LED)
uint16_t       RC_VEL_RAPIDA  = 320;  // avance mas rapido del muro, pote al maximo (ms por LED)
const uint16_t RC_VEL_MINIMA  = 110;  // piso de dificultad: mas rapido que esto no acelera
const uint8_t  RC_PUNTOS_SUBE = 5;    // cada cuantos puntos se acorta el intervalo de avance
uint16_t       RC_PROY_VEL    = 12;   // velocidad del proyectil (ms por LED); mucho mas rapido que el muro
const uint8_t  RC_MAX_PROYECTILES = 5;// balas simultaneas en vuelo: con ESTELA=2 cada una ocupa 3 LEDs,
                                      // mas que esto en una tira de 100 se vuelve un choclo ilegible
const uint8_t  RC_BASE        = 0;    // primer LED de la base (muestra el color cargado)
const uint8_t  RC_BASE_LEDS   = 2;    // ancho de la base en LEDs: mas visible que uno solo
const uint16_t RC_FIN_MS      = 3500; // duracion de la animacion de derrota antes de volver al menu

// --- Twang: dungeon 1D (joystick para moverse, P1 para atacar) ---
const uint16_t TWANG_VEL_LENTA    = 300;  // enemigos mas lentos, pote al minimo (ms por LED)
const uint16_t TWANG_VEL_RAPIDA   = 120;  // enemigos mas rapidos, pote al maximo (ms por LED)
const uint16_t TWANG_VEL_MINIMA   = 55;   // piso de dificultad: por mas niveles que pasen no aceleran mas
const uint8_t  TWANG_VIDAS        = 3;
const uint8_t  TWANG_MAX_ENEMIGOS = 12;   // tope de la tanda (tamano de los arrays paralelos)
const uint8_t  TWANG_ENEMIGOS_N1  = 3;    // enemigos del nivel 1; sube de a uno por nivel
const uint16_t TWANG_APARICION_MS = 900;  // separacion entre apariciones para que no salgan todos pegados
const uint16_t TWANG_ATAQUE_MS    = 200;  // ventana en la que el pulso mata (y en la que se ve creciendo)
uint16_t       TWANG_ATAQUE_ESPERA= 380;  // cooldown entre ataques: agil, pero no spam infinito
uint8_t        TWANG_ATAQUE_RADIO = 6;    // LEDs a cada lado que alcanza el pulso ya expandido
const uint16_t TWANG_INVUL_MS     = 700;  // invulnerabilidad tras un golpe (evita perder varias vidas de un tiron)
const uint16_t TWANG_JOY_CENTRO   = 2048; // centro nominal del eje Y (12 bits)
const uint16_t TWANG_JOY_MUERTA   = 200;  // zona muerta: el centro real del joystick no cae exacto en 2048
uint8_t        TWANG_VEL_JUGADOR  = 45;   // LEDs por segundo con el joystick a fondo
const uint16_t TWANG_SALIDA_MS    = 1200; // festejo al completar un nivel antes de la tanda siguiente
const uint16_t TWANG_FIN_MS       = 3000; // duracion de la animacion de derrota antes de volver al menu
// Terreno del nivel: lava (tramos que se prenden y apagan) y cintas
// transportadoras (tramos que empujan al que este parado encima).
const uint8_t  TWANG_MAX_LAVA     = 4;    // tope de tramos de lava por nivel
const uint8_t  TWANG_MAX_CINTAS   = 3;    // tope de cintas por nivel
const int16_t  TWANG_TERRENO_MARGEN = 12; // LEDs despejados en la base (donde se reaparece)
                                          // y en la salida: nunca es imposible arrancar ni terminar
const uint16_t TWANG_LAVA_ON_MS   = 1400; // cuanto queda encendida (peligrosa)
const uint16_t TWANG_LAVA_OFF_MS  = 1800; // apagada mas tiempo que encendida: siempre hay ventana para cruzar
const uint16_t TWANG_LAVA_AVISO_MS= 500;  // parpadeo de aviso antes de encenderse: la muerte nunca es sorpresa
const uint8_t  TWANG_CINTA_VEL    = 18;   // LEDs/s que suma la cinta; bastante menos que TWANG_VEL_JUGADOR
                                          // para poder caminar contra ella (mas lento) y no quedar atrapado

// ---------- Colores ----------
#define COL_PELOTA  CRGB::White
#define COL_P1      CRGB(0, 255, 0)   // verde  (jugador 1, inicio de la tira)
#define COL_P2      CRGB(0, 60, 255)  // azul   (jugador 2, final de la tira)

// Dorado exclusivo de los records: no lo usa ningun juego, asi que cuando la
// tira chispea de este color se entiende solo que es un record nuevo.
const CRGB COL_RECORD = CRGB(255, 200, 0);

// Paleta propia de Rompecolores: 4 colores bien distinguibles en WS2812.
// No reusa COL_P1/COL_P2 (son de Pong) para poder tunearla aparte.
const CRGB RC_ROJO      = CRGB(255,   0,   0);
const CRGB RC_VERDE     = CRGB(  0, 255,   0);
const CRGB RC_AZUL      = CRGB(  0,  60, 255);
const CRGB RC_AMARILLO  = CRGB(255, 150,   0);
const CRGB RC_COLORES[4]        = { RC_ROJO, RC_VERDE, RC_AZUL, RC_AMARILLO };
const char* RC_NOMBRE_COLOR[4]  = { "ROJO", "VERDE", "AZUL", "AMARILLO" };

// Paleta de Twang: la del juego original (verde vos, rojo el peligro, azul la
// salida). La lava va naranja y la cinta cyan: colores que no usa ningun otro
// elemento, asi el terreno se lee de un vistazo sin confundirlo con un enemigo
// (rojo), con la salida (azul) ni con un record (dorado).
const CRGB TWANG_COL_JUGADOR = CRGB(  0, 255,   0);
const CRGB TWANG_COL_ENEMIGO = CRGB(255,   0,   0);
const CRGB TWANG_COL_SALIDA  = CRGB(  0,  60, 255);
const CRGB TWANG_COL_LAVA    = CRGB(255,  60,   0);
const CRGB TWANG_COL_CINTA   = CRGB(  0, 180, 160);

// ---------- Estado del juego ----------
enum Estado { SACANDO, JUGANDO, PUNTO, FIN };
Estado   estado;
uint32_t estadoDesde;   // millis() en que entramos al estado actual

int16_t  pelotaPos;     // 0..NUM_LEDS-1
int8_t   pelotaDir;     // +1 hacia P2 (final), -1 hacia P1 (inicio)
uint16_t pelotaVel;     // ms por paso actual
uint32_t ultimoPaso;    // millis() del ultimo movimiento

uint8_t  puntosP1, puntosP2;
uint8_t  saca;          // quien saca: 1 o 2 (saca el que perdio el punto)
uint8_t  ganador;       // 1 o 2, valido en estado FIN
uint16_t velSaque = VEL_LENTA;  // velocidad de saque actual, la fija el potenciometro
uint32_t pongGolpesTotales;     // golpes exitosos de los DOS jugadores en la partida (metrica del record)
bool     pongEsRecord;          // solo tiene sentido con ganador != 0

// ---------- Consola: selector de juegos ----------
enum Pantalla { MENU, JUEGO };
Pantalla pantalla = MENU;               // arranca en el selector

enum Juego { JUEGO_PONG, JUEGO_TUG, JUEGO_ROMPECOLORES, JUEGO_TWANG, JUEGO_HIGHSCORES, JUEGO_IP, NUM_JUEGOS };
uint8_t juegoSel    = 0;                // indice resaltado en el menu
uint8_t juegoActivo = JUEGO_PONG;       // juego que se esta jugando
uint16_t joyCentroX = 2048;             // centro real del eje X, se mide en setup() (ver calibrarJoystick)
const char* NOMBRE_JUEGO[NUM_JUEGOS] = { "Pong", "Tira y afloja", "Rompecolores", "Twang", "Highscores", "IP" };

// Definidas mas abajo; se usan antes en el archivo.
void volverAlMenu();
void iniciarJuego(uint8_t j);
void nuevoTug();
void nuevoRompecolores();
void nuevoTwang();
void nuevoHighscores();
void nuevoIP();
uint8_t twangEnemigosVivos();

// ---------- Estado de Tira y Afloja ----------
enum EstadoTug { TUG_JUGANDO, TUG_FIN };
EstadoTug estadoTug;
int16_t   tugFrente;    // posicion del limite Verde|Azul (0..NUM_LEDS-1)
uint8_t   tugGanador;   // 1 = Verde, 2 = Azul
uint32_t  tugEmpujones; // pulsaciones de los DOS lados en la partida (metrica del record)
bool      tugEsRecord;

// ---------- Estado de Rompecolores ----------
enum EstadoRc { RC_JUGANDO, RC_FIN };
EstadoRc estadoRc;
int16_t  rcFrente;              // LED del muro mas cercano al jugador; NUM_LEDS = muro vacio
CRGB     rcColor[NUM_LEDS];     // color de cada LED del muro (solo importa en [rcFrente, NUM_LEDS-1])
uint16_t rcScore;               // LEDs rotos
uint16_t rcVelAvance;           // intervalo actual de avance del muro (ms por LED)
uint32_t rcUltimoAvance;        // millis() del ultimo avance del muro
uint8_t  rcColorCargado;        // indice en RC_COLORES de la bala lista para disparar
// Proyectiles en vuelo, en arrays paralelos (mismo patron simple que los
// enemigos de Twang): se puede disparar de nuevo sin esperar al impacto.
bool     rcProyectilActivo[RC_MAX_PROYECTILES];  // false = slot libre para un disparo nuevo
int16_t  rcProyectilPos[RC_MAX_PROYECTILES];
uint8_t  rcProyectilColor[RC_MAX_PROYECTILES];   // indice de color con el que salio cada bala
uint32_t rcProyectilPaso[RC_MAX_PROYECTILES];    // millis() del ultimo paso de cada bala
uint16_t rcVelInicial = RC_VEL_LENTA;  // lo fija el pote; se copia a rcVelAvance al empezar
bool     rcEsRecord;

// ---------- Estado de Twang ----------
// Los enemigos van en arrays paralelos (misma idea simple que el muro de
// Rompecolores): posicion, si sigue vivo, cuando dio su ultimo paso y a partir
// de cuando entra a la mazmorra. Un enemigo "vivo" pero con aparicion futura
// todavia no se dibuja ni se mueve, asi la tanda entra escalonada.
enum EstadoTwang { TWANG_JUGANDO, TWANG_NIVEL, TWANG_FIN };
EstadoTwang estadoTwang;
float    twangJugadorPos;                        // posicion continua: el movimiento es por velocidad, no por pasos
int16_t  twangEnemigoPos[TWANG_MAX_ENEMIGOS];
bool     twangEnemigoVivo[TWANG_MAX_ENEMIGOS];
uint32_t twangEnemigoPaso[TWANG_MAX_ENEMIGOS];   // millis() del ultimo paso de cada enemigo
uint32_t twangEnemigoAparece[TWANG_MAX_ENEMIGOS];// millis() en que ese enemigo entra a la mazmorra
uint16_t twangVelEnemigo;                        // intervalo de avance de la tanda actual (ms por LED)
// Terreno del nivel, tambien en arrays paralelos. Es fijo mientras dura el
// nivel y se regenera entero en cada tanda nueva (ver twangGenerarTerreno).
int16_t  twangLavaIni[TWANG_MAX_LAVA];           // tramo de lava [ini, fin], ambos incluidos
int16_t  twangLavaFin[TWANG_MAX_LAVA];
bool     twangLavaOn[TWANG_MAX_LAVA];            // encendida ahora (quema)
uint32_t twangLavaCambio[TWANG_MAX_LAVA];        // millis() del ultimo cambio on/off
uint8_t  twangLavaN;                             // cuantos tramos de lava tiene el nivel actual
int16_t  twangCintaIni[TWANG_MAX_CINTAS];        // tramo de cinta [ini, fin], ambos incluidos
int16_t  twangCintaFin[TWANG_MAX_CINTAS];
int8_t   twangCintaDir[TWANG_MAX_CINTAS];        // +1 empuja hacia la salida, -1 hacia la base
uint8_t  twangCintasN;                           // cuantas cintas tiene el nivel actual
uint8_t  twangNivel;
uint8_t  twangVidas;
uint32_t twangUltimoFrame;                       // para el paso continuo del jugador (dt del frame)
uint32_t twangAtaqueDesde;                       // millis() del ultimo ataque (sirve de ventana y de cooldown)
bool     twangAtacando;                          // true mientras el pulso todavia mata
uint32_t twangInvulDesde;                        // millis() del ultimo golpe recibido
bool     twangInvul;                             // true mientras el jugador parpadea sin recibir dano
int16_t  twangCaida;                             // LED donde cayo el jugador: centro de la animacion de derrota
uint16_t twangVelInicial = TWANG_VEL_LENTA;      // lo fija el pote; se copia a twangVelEnemigo al empezar
bool     twangEsRecord;
uint16_t twangJoyCentro  = TWANG_JOY_CENTRO;      // se recalibra al arrancar cada partida (ver calibrarJoystick)

// ---------- Highscores persistentes (NVS) ----------
// Un record por juego, guardado en flash con Preferences para que sobreviva al
// apagado. Se escribe solo cuando se supera el valor viejo: la NVS tiene ciclos
// de escritura contados, no conviene grabar en cada partida.
Preferences prefs;
uint32_t hsPong, hsTug, hsRc, hsTwang;
uint8_t  hsVerIndice = 0;   // juego que se esta mirando en la pantalla Highscores (0..3)

bool intentarRecord(uint32_t &record, const char* clave, uint32_t valor) {
  if (valor <= record) return false;
  record = valor;
  prefs.putUInt(clave, valor);
  return true;
}

// ---------- Botones: debounce + flanco de bajada ----------
const uint8_t PIN_BTN[2] = { BTN_P1, BTN_P2 };
bool     btnEstable[2] = { false, false };  // true = presionado (estable)
bool     btnPrev[2]    = { false, false };  // ultima lectura cruda
uint32_t btnCambio[2]  = { 0, 0 };          // millis del ultimo cambio crudo
bool     btnFlanco[2]  = { false, false };  // true un frame al recien presionar

void actualizarBotones() {
  uint32_t ahora = millis();
  for (uint8_t i = 0; i < 2; i++) {
    btnFlanco[i] = false;
    bool raw = (digitalRead(PIN_BTN[i]) == LOW);   // apretado = LOW
    if (raw != btnPrev[i]) {
      btnPrev[i]   = raw;
      btnCambio[i] = ahora;
    }
    if (ahora - btnCambio[i] >= DEBOUNCE_MS && raw != btnEstable[i]) {
      btnEstable[i] = raw;
      if (btnEstable[i]) btnFlanco[i] = true;       // flanco: recien presionado
    }
  }
}

// ---------- Potenciometro: parametro inicial de cada juego ----------
// Promedia unas lecturas para suavizar el jitter del ADC. Cada juego tiene su
// propia variable de salida: en Pong el pote fija la velocidad del SAQUE (la
// aceleracion por golpes sigue igual), en Rompecolores la velocidad inicial del
// muro y en Twang la de los enemigos (estos dos lo leen una sola vez, al
// arrancar la partida).
void leerPote() {
  uint32_t s = 0;
  for (uint8_t i = 0; i < 4; i++) s += analogRead(POT_PIN);
  uint16_t lectura = s / 4;
  velSaque        = map(lectura, 0, 4095, VEL_LENTA, VEL_RAPIDA);
  rcVelInicial    = map(lectura, 0, 4095, RC_VEL_LENTA, RC_VEL_RAPIDA);
  twangVelInicial = map(lectura, 0, 4095, TWANG_VEL_LENTA, TWANG_VEL_RAPIDA);
}

// ---------- Joystick: eje Y como velocidad (Twang), eje X para navegar ----------
// El centro real de un joystick barato no cae exacto en TWANG_JOY_CENTRO (2048):
// el desvio varia de modulo a modulo y puede superar la zona muerta, lo que se
// siente como que el personaje "cae" solo aunque nadie toque el stick. Por eso
// se mide el centro real de cada eje en vez de asumir 2048 fijo: el X una sola
// vez en setup() (el stick esta suelto al encender) y el Y al arrancar cada
// partida de Twang.
uint16_t calibrarJoystick(uint8_t pin) {
  uint32_t s = 0;
  for (uint8_t i = 0; i < 8; i++) s += analogRead(pin);
  return s / 8;
}

// Devuelve LEDs por segundo con signo (+ hacia el final de la tira). Fuera de
// la zona muerta la deflexion arranca en 0 y crece hasta 1, asi no hay salto
// de velocidad al cruzar el borde.
float leerJoystickY() {
  int16_t d = (int16_t)analogRead(JOY_Y_PIN) - (int16_t)twangJoyCentro;
  int16_t m = (d < 0) ? -d : d;
  if (m <= TWANG_JOY_MUERTA) return 0;
  float f = (float)(m - TWANG_JOY_MUERTA) / (float)(twangJoyCentro - TWANG_JOY_MUERTA);
  if (f > 1.0f) f = 1.0f;
  return (d > 0 ? f : -f) * TWANG_VEL_JUGADOR;
}

// Convierte la deflexion del eje X en pasos discretos: devuelve -1/0/+1.
// Da un paso al salir de la zona muerta y, si se mantiene el stick, repite cada
// MENU_REPETIR_MS. Sin esto el menu se iria de largo a 60 pasos por segundo.
// El estado es compartido entre el menu y Highscores porque nunca estan activos
// a la vez, asi que con un solo juego de variables alcanza.
int8_t joyPasoPrev = 0;         // sentido en el que estaba el stick el frame anterior
uint32_t joyPasoDesde = 0;      // millis() del ultimo paso entregado

int8_t joystickPaso() {
  int16_t d = (int16_t)analogRead(JOY_X_PIN) - (int16_t)joyCentroX;
  int8_t  dir = 0;
  if (d >  (int16_t)MENU_JOY_MUERTA) dir = +1;
  if (d < -(int16_t)MENU_JOY_MUERTA) dir = -1;

  if (dir == 0) { joyPasoPrev = 0; return 0; }        // stick al centro: listo para el proximo paso

  uint32_t ahora = millis();
  if (dir != joyPasoPrev) {                           // recien salio de la zona muerta
    joyPasoPrev  = dir;
    joyPasoDesde = ahora;
    return dir;
  }
  if (ahora - joyPasoDesde >= MENU_REPETIR_MS) {      // lo mantiene: auto-repeat
    joyPasoDesde = ahora;
    return dir;
  }
  return 0;
}

// ---------- Buzzer: efectos no bloqueantes (LEDC) ----------
// tone() no anda bien -> usamos LEDC (ledcSetup/ledcAttachPin/ledcWriteTone).
// Este core es arduino-esp32 2.0.x, asi que ledcWriteTone toma el CANAL.
// Un beep simple y un pequeno secuenciador de melodias, ambos guiados por
// millis() para no frenar el juego.
struct Nota { uint16_t freq; uint16_t dur; };   // freq 0 = silencio (pausa)

const Nota JINGLE_PUNTO[] = { {494, 120}, {330, 180} };                       // descendente
const Nota JINGLE_WIN[]   = { {523, 120}, {659, 120}, {784, 120}, {1047, 260} }; // C-E-G-C
// Derrota de Rompecolores: descendente y lento, para que no se confunda con JINGLE_WIN.
const Nota JINGLE_GAMEOVER[] = { {392, 180}, {330, 180}, {262, 180}, {196, 500} }; // G-E-C-G(bajo)
// Twang: nivel superado (ascendente y corto) y derrota (mas grave y arrastrada
// que la de Rompecolores, para distinguir de oido en que juego estas).
const Nota JINGLE_TWANG_NIVEL[] = { {784, 90}, {988, 90}, {1319, 220} };            // G-B-E(alto)
const Nota JINGLE_TWANG_FIN[]   = { {330, 170}, {294, 170}, {247, 170}, {165, 520} }; // E-D-B-E(bajo)
// Record nuevo: mas largo y con dos escalones ascendentes + remate repetido, para
// que no se confunda de oido con la victoria normal de una partida cualquiera.
const Nota JINGLE_RECORD[] = {
  {523, 90}, {659, 90}, {784, 90}, {1047, 160},
  {880, 90}, {1047, 90}, {1319, 200},
  {0,   80}, {1319, 120}, {1568, 120}, {2093, 420}
};

bool     buzzerSonando  = false;   // hay un beep simple en curso
uint32_t buzzerHasta    = 0;       // millis() en que corta ese beep
const Nota* jingle      = nullptr; // melodia en curso (nullptr = ninguna)
uint8_t  jingleLen      = 0;
uint8_t  jingleIdx      = 0;
uint32_t jingleNotaHasta = 0;

void beep(uint16_t freq, uint16_t dur) {
  jingleLen = 0;                    // un beep corta cualquier melodia
  ledcWriteTone(BUZZER_CH, freq);
  buzzerHasta   = millis() + dur;
  buzzerSonando = true;
}

void tocarJingle(const Nota* notas, uint8_t len) {
  buzzerSonando   = false;
  jingle          = notas;
  jingleLen       = len;
  jingleIdx       = 0;
  jingleNotaHasta = 0;              // arranca en el proximo actualizarBuzzer()
}

void actualizarBuzzer() {
  uint32_t ahora = millis();
  if (jingleLen > 0) {              // una melodia tiene prioridad
    if (ahora >= jingleNotaHasta) {
      if (jingleIdx < jingleLen) {
        Nota n = jingle[jingleIdx++];
        ledcWriteTone(BUZZER_CH, n.freq);
        jingleNotaHasta = ahora + n.dur;
      } else {
        ledcWriteTone(BUZZER_CH, 0);   // termino
        jingleLen = 0;
      }
    }
    return;
  }
  if (buzzerSonando && ahora >= buzzerHasta) {
    ledcWriteTone(BUZZER_CH, 0);
    buzzerSonando = false;
  }
}

// Efectos concretos del juego.
void sonarGolpe() {                  // agudo, sube con la velocidad de la pelota
  uint16_t freq = 1000 + (VEL_LENTA - pelotaVel) * 10;
  beep(freq, 30);
}
void sonarSaque()    { beep(900, 45); }
void sonarPunto()    { tocarJingle(JINGLE_PUNTO, 2); }
void sonarVictoria() { tocarJingle(JINGLE_WIN, 4); }
void sonarRecord()   { tocarJingle(JINGLE_RECORD, 11); }   // reemplaza al sonido de fin cuando hubo record

// Efectos de Rompecolores: cuatro pitidos bien distintos entre si para que el
// oido solo alcance a distinguir cambio de color / disparo / acierto / fallo.
void sonarRcColor()    { beep(1900,  25); }  // corto y agudo: cambio de bala
void sonarRcDisparo()  { beep( 700,  35); }  // mas grave que el cambio de color
void sonarRcAcierto()  { beep(2300,  70); }  // el mas agudo y largo: rompio un LED
void sonarRcFallo()    { beep( 160, 200); }  // grave y largo: penalidad
void sonarRcGameOver() { tocarJingle(JINGLE_GAMEOVER, 4); }

// Efectos de Twang: el ataque suena seco, la muerte de un enemigo bien aguda y
// el golpe recibido grave y largo, para no confundir "mate" con "me pegaron".
void sonarTwangAtaque()   { beep(1500,  30); }
void sonarTwangMuerte()   { beep(2100,  45); }
void sonarTwangGolpe()    { beep( 140, 220); }
void sonarTwangNivel()    { tocarJingle(JINGLE_TWANG_NIVEL, 3); }
void sonarTwangGameOver() { tocarJingle(JINGLE_TWANG_FIN, 4); }

// ---------- LCD 1602: marcador + mensajes ----------
// El I2C es lento (decenas de ms por refresco completo). Para no frenar el
// juego, cada fila se escribe SOLO cuando su texto cambia (cache por fila).
// Durante el peloteo el marcador no cambia -> cero escrituras -> sin stutter.
String lcdCache[2] = { "\x01", "\x01" };   // valor imposible -> fuerza el 1er dibujo

// Centra en 16 y escribe la fila solo si cambio respecto de la cache.
void lcdLinea(uint8_t fila, const String& txt) {
  String t = txt;
  if (t.length() > 16) t = t.substring(0, 16);
  while (t.length() < 16) {                 // centrar: reparte el relleno
    t = (t.length() % 2 == 0) ? (" " + t) : (t + " ");
  }
  if (t == lcdCache[fila]) return;
  lcdCache[fila] = t;
  lcd.setCursor(0, fila);
  lcd.print(t);
}

// Fuerza que la proxima actualizacion repinte ambas filas (al cambiar de pantalla).
void lcdForzarRefresh() { lcdCache[0] = "\x01"; lcdCache[1] = "\x01"; }

// --- Pong: marcador + mensaje segun estado ---
void lcdPong() {
  lcdLinea(0, "Verde " + String(puntosP1) + " - " + String(puntosP2) + " Azul");
  String m;
  switch (estado) {
    case SACANDO: {                       // muestra el nivel de velocidad del pote
      uint8_t nivel = map(velSaque, VEL_LENTA, VEL_RAPIDA, 1, 9);
      m = String(saca == 1 ? "Saca Verde v" : "Saca Azul v") + nivel;
      break;
    }
    case JUGANDO: m = "- jugando -"; break;
    case PUNTO:   m = (saca == 1) ? "Punto Azul!" : "Punto Verde!"; break;  // sumo el rival del que saca
    case FIN:
      if (pongEsRecord) m = "*NUEVO RECORD!*";
      else              m = (ganador == 1) ? "** GANA VERDE **" : "** GANA AZUL **";
      break;
  }
  lcdLinea(1, m);
}

// --- Tira y Afloja: etiquetas de lado + barra de posicion del frente ---
void lcdTug() {
  if (estadoTug == TUG_FIN) {
    lcdLinea(0, "  Tira y Afloja   ");
    if (tugEsRecord) lcdLinea(1, "*NUEVO RECORD!*");
    else             lcdLinea(1, (tugGanador == 1) ? "** GANA VERDE **" : "** GANA AZUL **");
    return;
  }
  lcdLinea(0, "Verde       Azul");        // Verde a la izquierda, Azul a la derecha
  uint8_t p = map(tugFrente, 0, NUM_LEDS - 1, 0, 15);
  String bar;
  for (uint8_t i = 0; i < 16; i++) bar += (i == p) ? '|' : '-';
  lcdLinea(1, bar);
}

// --- Rompecolores: score en vivo + color cargado (o score final al perder) ---
void lcdRompecolores() {
  if (estadoRc == RC_FIN) {
    lcdLinea(0, "** GAME OVER **");
    if (rcEsRecord) lcdLinea(1, "*NUEVO RECORD!*");
    else            lcdLinea(1, "Score final: " + String(rcScore));
    return;
  }
  lcdLinea(0, "Score: " + String(rcScore));
  lcdLinea(1, "Bala: " + String(RC_NOMBRE_COLOR[rcColorCargado]));
}

// --- Twang: nivel y vidas arriba, estado de la tanda abajo ---
void lcdTwang() {
  if (estadoTwang == TWANG_FIN) {
    lcdLinea(0, "** GAME OVER **");
    if (twangEsRecord) lcdLinea(1, "*NUEVO RECORD!*");
    else               lcdLinea(1, "Llegaste al Nv" + String(twangNivel));
    return;
  }
  lcdLinea(0, "Nivel " + String(twangNivel) + "  Vidas " + String(twangVidas));
  uint8_t vivos = twangEnemigosVivos();
  lcdLinea(1, vivos ? ("Enemigos: " + String(vivos)) : "Salida abierta!");
}

// --- Highscores: un juego por pantalla, con la unidad que le corresponde ---
// "Rec:" en vez de "Record:" porque con la unidad al lado no entra en 16 columnas.
void lcdHighscores() {
  switch (hsVerIndice) {
    case 0:
      lcdLinea(0, "Pong");
      lcdLinea(1, "Rec: " + String(hsPong) + " golpes");
      break;
    case 1:
      lcdLinea(0, "Tira y afloja");
      lcdLinea(1, "Rec: " + String(hsTug) + " empujes");
      break;
    case 2:
      lcdLinea(0, "Rompecolores");
      lcdLinea(1, "Rec: " + String(hsRc) + " pts");
      break;
    default:
      lcdLinea(0, "Twang");
      lcdLinea(1, "Rec: Nivel " + String(hsTwang));
      break;
  }
}

// --- IP: datos para entrar al panel web desde el celular ---
// El SSID va solo (sin prefijo) porque "GastiConsola" ya come 12 de las 16
// columnas. La clave no se muestra: la sabe el que armo la consola.
void lcdIP() {
  lcdLinea(0, AP_SSID);
  lcdLinea(1, apIP);
}

// --- Menu: juego resaltado + ayuda de controles ---
// Las flechas alrededor del nombre invitan a mover el joystick a los costados
// (lcdLinea ya se encarga de centrar el texto en las 16 columnas).
void lcdMenu() {
  lcdLinea(0, "< " + String(NOMBRE_JUEGO[juegoSel]) + " >");
  lcdLinea(1, "Verde = jugar");
}

// Dispatcher: elige que mostrar segun la pantalla/juego actual. Se llama cada
// loop, pero lcdLinea solo toca el I2C cuando el texto realmente cambia.
void actualizarLCD() {
  if (pantalla == MENU) { lcdMenu(); return; }
  if      (juegoActivo == JUEGO_PONG)          lcdPong();
  else if (juegoActivo == JUEGO_TUG)           lcdTug();
  else if (juegoActivo == JUEGO_ROMPECOLORES)  lcdRompecolores();
  else if (juegoActivo == JUEGO_TWANG)         lcdTwang();
  else if (juegoActivo == JUEGO_HIGHSCORES)    lcdHighscores();
  else if (juegoActivo == JUEGO_IP)            lcdIP();
}

// ---------- Utilidades de dibujo ----------
void setLed(int16_t i, const CRGB& c) {
  if (i >= 0 && i < NUM_LEDS) leds[i] = c;
}

// Marcadores: puntos de P1 como LEDs verdes desde el inicio,
// puntos de P2 como LEDs azules desde el final.
void dibujarMarcador() {
  for (uint8_t i = 0; i < puntosP1; i++) setLed(i, COL_P1);
  for (uint8_t i = 0; i < puntosP2; i++) setLed(NUM_LEDS - 1 - i, COL_P2);
}

// Pelota con estela que se desvanece detras (en sentido contrario a la marcha).
void dibujarPelota() {
  setLed(pelotaPos, COL_PELOTA);
  for (uint8_t k = 1; k <= ESTELA; k++) {
    CRGB c = COL_PELOTA;
    c.nscale8(255 / (k + 1));               // cada paso mas tenue
    setLed(pelotaPos - pelotaDir * k, c);
  }
}

// ---------- Transiciones de estado ----------
void irA(Estado e) {
  estado = e;
  estadoDesde = millis();
}

void prepararSaque() {
  pelotaVel = velSaque;
  if (saca == 1) {                 // P1 saca desde el inicio, hacia P2
    pelotaPos = 1;
    pelotaDir = +1;
  } else {                         // P2 saca desde el final, hacia P1
    pelotaPos = NUM_LEDS - 2;
    pelotaDir = -1;
  }
  irA(SACANDO);
}

void nuevoPartido() {
  puntosP1 = puntosP2 = 0;
  saca = 1;
  pongGolpesTotales = 0;
  pongEsRecord      = false;
  prepararSaque();
}

// ---------- Estados ----------
void loopSacando() {
  // La zona del que saca late; el saque arranca con su boton (o auto a los 4 s).
  uint8_t idx = saca - 1;
  bool lanzar = btnFlanco[idx] || (millis() - estadoDesde > 4000);

  FastLED.clear();
  dibujarMarcador();
  // Pelota parpadeando en el punto de saque.
  if ((millis() / 300) % 2 == 0) dibujarPelota();
  FastLED.show();

  if (lanzar) {
    sonarSaque();
    ultimoPaso = millis();
    irA(JUGANDO);
  }
}

void loopJugando() {
  // --- Golpe: valido si la pelota viene hacia tu punta y esta en tu zona ---
  if (pelotaDir == -1 && pelotaPos <= ZONA - 1 && btnFlanco[0]) {
    pelotaDir = +1;
    pelotaVel = max<int>(VEL_MINIMA, pelotaVel - VEL_ACELERA);
    pongGolpesTotales++;
    sonarGolpe();
  }
  if (pelotaDir == +1 && pelotaPos >= NUM_LEDS - ZONA && btnFlanco[1]) {
    pelotaDir = -1;
    pelotaVel = max<int>(VEL_MINIMA, pelotaVel - VEL_ACELERA);
    pongGolpesTotales++;
    sonarGolpe();
  }

  // --- Movimiento por tiempo (1 LED cada pelotaVel ms) ---
  if (millis() - ultimoPaso >= pelotaVel) {
    ultimoPaso += pelotaVel;
    pelotaPos += pelotaDir;

    if (pelotaPos < 0) {          // se escapo por P1 -> punto de P2
      puntosP2++;
      ganador = (puntosP2 >= PUNTOS_GANAR) ? 2 : 0;
      saca = 1;                   // saca el que perdio
      // La partida termina recien cuando hay ganador: ahi se cierra el peloteo
      // y se compara el total de golpes contra el record.
      if (ganador) pongEsRecord = intentarRecord(hsPong, "hsPong", pongGolpesTotales);
      if (!ganador)          sonarPunto();
      else if (pongEsRecord) sonarRecord();
      else                   sonarVictoria();
      irA(ganador ? FIN : PUNTO);
      return;
    }
    if (pelotaPos > NUM_LEDS - 1) { // se escapo por P2 -> punto de P1
      puntosP1++;
      ganador = (puntosP1 >= PUNTOS_GANAR) ? 1 : 0;
      saca = 2;
      if (ganador) pongEsRecord = intentarRecord(hsPong, "hsPong", pongGolpesTotales);
      if (!ganador)          sonarPunto();
      else if (pongEsRecord) sonarRecord();
      else                   sonarVictoria();
      irA(ganador ? FIN : PUNTO);
      return;
    }
  }

  // --- Dibujo ---
  FastLED.clear();
  bool p1Activa = (pelotaDir == -1 && pelotaPos <= ZONA - 1);
  bool p2Activa = (pelotaDir == +1 && pelotaPos >= NUM_LEDS - ZONA);
  for (uint8_t i = 0; i < ZONA; i++) {
    setLed(i, p1Activa ? COL_P1 : CRGB(0, 20, 0));                // zona P1
    setLed(NUM_LEDS - 1 - i, p2Activa ? COL_P2 : CRGB(0, 5, 20)); // zona P2
  }
  dibujarPelota();
  FastLED.show();
}

void loopPunto() {
  // Parpadeo en el color del que sumo, ~1.2 s, y a sacar de nuevo.
  CRGB c = (saca == 1) ? COL_P2 : COL_P1;   // sumo el rival del que saca
  bool on = (millis() / 150) % 2 == 0;
  fill_solid(leds, NUM_LEDS, on ? c : CRGB::Black);
  dibujarMarcador();
  FastLED.show();

  if (millis() - estadoDesde > 1200) prepararSaque();
}

void loopFin() {
  // Animacion de victoria en el color del ganador, ~3.5 s, y vuelta al menu.
  CRGB c = (ganador == 1) ? COL_P1 : COL_P2;
  uint16_t t = (millis() - estadoDesde);
  FastLED.clear();
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    if ((i + t / 40) % 4 == 0) leds[i] = c;   // chase
  }
  if (pongEsRecord) {                          // chispeo dorado ENCIMA del festejo normal
    for (uint8_t s = 0; s < 3; s++) {
      if (random8() < 60) setLed(random16(NUM_LEDS), COL_RECORD);
    }
  }
  FastLED.show();

  if (t > 3500) volverAlMenu();
}

// Despacha el frame de Pong segun su estado interno.
void loopPong() {
  switch (estado) {
    case SACANDO: loopSacando(); break;
    case JUGANDO: loopJugando(); break;
    case PUNTO:   loopPunto();   break;
    case FIN:     loopFin();     break;
  }
}

// ---------- Tira y Afloja: cinchada de botones ----------
void nuevoTug() {
  tugFrente    = NUM_LEDS / 2;   // el frente arranca en el centro
  tugEmpujones = 0;
  tugEsRecord  = false;
  estadoTug    = TUG_JUGANDO;
}

void loopTug() {
  if (estadoTug == TUG_JUGANDO) {
    // Cada pulsacion nueva (flanco) empuja el frente hacia el lado del rival.
    if (btnFlanco[0]) { tugFrente += TUG_EMPUJE; tugEmpujones++; beep(1400, 18); }   // Verde empuja al final
    if (btnFlanco[1]) { tugFrente -= TUG_EMPUJE; tugEmpujones++; beep(1050, 18); }   // Azul empuja al inicio

    if (tugFrente >= NUM_LEDS - 1) {        // Verde conquisto la tira
      tugGanador = 1; estadoTug = TUG_FIN; estadoDesde = millis();
      tugEsRecord = intentarRecord(hsTug, "hsTug", tugEmpujones);
      tugEsRecord ? sonarRecord() : sonarVictoria();
    } else if (tugFrente <= 0) {            // Azul conquisto la tira
      tugGanador = 2; estadoTug = TUG_FIN; estadoDesde = millis();
      tugEsRecord = intentarRecord(hsTug, "hsTug", tugEmpujones);
      tugEsRecord ? sonarRecord() : sonarVictoria();
    }

    // Render: Verde [0, frente), Azul [frente, fin).
    for (int16_t i = 0; i < NUM_LEDS; i++) leds[i] = (i < tugFrente) ? COL_P1 : COL_P2;
    FastLED.show();
  } else {  // TUG_FIN: festejo del ganador, ~3 s, y al menu.
    CRGB c = (tugGanador == 1) ? COL_P1 : COL_P2;
    uint16_t t = millis() - estadoDesde;
    fill_solid(leds, NUM_LEDS, ((t / 150) % 2 == 0) ? c : CRGB::Black);
    if (tugEsRecord) {
      for (uint8_t s = 0; s < 3; s++) {
        if (random8() < 60) setLed(random16(NUM_LEDS), COL_RECORD);
      }
    }
    FastLED.show();
    if (t > 3000) volverAlMenu();
  }
}

// ---------- Rompecolores: un jugador contra el muro de colores ----------
// El muro es una FILA contigua que ocupa [rcFrente, NUM_LEDS-1]: los colores
// nuevos entran por el extremo lejano (NUM_LEDS-1) y empujan a los viejos hacia
// el jugador, como una cinta transportadora. Por eso el frente (rcFrente) es
// siempre el color mas VIEJO: se lo ve venir desde lejos y se puede tener la
// bala del color correcto lista. rcFrente == NUM_LEDS significa "muro vacio"
// (asi arranca la partida y asi queda si se rompe el ultimo LED).
// La base ocupa [RC_BASE, RC_BASE + RC_BASE_LEDS - 1]; si el muro llega ahi se
// pierde. random() en este core usa el RNG por hardware, no hace falta semilla.

void nuevoRompecolores() {
  rcFrente = NUM_LEDS;              // sin muro al empezar
  for (int16_t i = 0; i < NUM_LEDS; i++) rcColor[i] = CRGB::Black;
  rcScore           = 0;
  rcVelAvance       = rcVelInicial; // el pote se lee una sola vez, al arrancar
  rcUltimoAvance    = millis();
  rcColorCargado    = 0;            // arranca con Rojo cargado
  rcEsRecord        = false;
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) {
    rcProyectilActivo[p] = false;                  // todos los slots libres
    rcProyectilPos[p]    = RC_BASE + RC_BASE_LEDS; // sale justo delante de la base
    rcProyectilColor[p]  = 0;
  }
  estadoRc          = RC_JUGANDO;
}

void rcPerder() {
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) rcProyectilActivo[p] = false;
  estadoRc    = RC_FIN;
  estadoDesde = millis();
  rcEsRecord  = intentarRecord(hsRc, "hsRc", rcScore);
  rcEsRecord ? sonarRecord() : sonarRcGameOver();
}

// Acierto: rompe el frente y le devuelve terreno al jugador; cada RC_PUNTOS_SUBE
// puntos el muro acelera un 10%, con piso en RC_VEL_MINIMA.
void rcAcertar() {
  rcFrente++;
  rcScore++;
  sonarRcAcierto();
  if (rcScore % RC_PUNTOS_SUBE == 0) {
    rcVelAvance = max<int>(RC_VEL_MINIMA, (rcVelAvance * 9) / 10);
  }
}

// Fallo: se agrega una capa nueva del color que erraste, un LED mas cerca de la
// base. Para pelarla hay que acertarle a ESE color antes de ver la de abajo.
// Recibe el color de la bala que fallo porque puede haber varias en vuelo.
void rcFallar(uint8_t colorBala) {
  rcFrente--;
  rcColor[rcFrente] = RC_COLORES[colorBala];
  sonarRcFallo();
}

void loopRompecolores() {
  if (estadoRc == RC_FIN) {
    // Derrota: alarma roja que se va comiendo la tira desde la base, y al menu.
    uint32_t t = millis() - estadoDesde;
    bool    on = (t / 130) % 2 == 0;
    int16_t comido = t / 30;                  // el muro ya paso por ahi
    FastLED.clear();
    for (int16_t i = comido; i < NUM_LEDS; i++) setLed(i, on ? RC_ROJO : CRGB(40, 0, 0));
    if (rcEsRecord) {
      for (uint8_t s = 0; s < 3; s++) {
        if (random8() < 60) setLed(random16(NUM_LEDS), COL_RECORD);
      }
    }
    FastLED.show();
    if (t > RC_FIN_MS) volverAlMenu();
    return;
  }

  // --- Controles: P1 cicla el color de la bala, P2 dispara ---
  if (btnFlanco[0]) {
    rcColorCargado = (rcColorCargado + 1) % 4;   // Rojo -> Verde -> Azul -> Amarillo
    sonarRcColor();
  }
  if (btnFlanco[1]) {
    // Se puede disparar sin esperar al impacto de la bala anterior: se busca el
    // primer slot libre. Si estan los RC_MAX_PROYECTILES en vuelo el disparo se
    // pierde y listo (caso raro: es un boton, no se puede espamear tan rapido).
    for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) {
      if (rcProyectilActivo[p]) continue;
      rcProyectilActivo[p] = true;
      rcProyectilColor[p]  = rcColorCargado;
      rcProyectilPos[p]    = RC_BASE + RC_BASE_LEDS;   // sale justo delante de la base
      rcProyectilPaso[p]   = millis();
      sonarRcDisparo();
      break;
    }
  }

  // --- Avance natural del muro (la amenaza constante) ---
  // El color nuevo entra SIEMPRE por el extremo lejano y toda la fila se corre
  // un LED hacia el jugador. Es lo que hace al juego jugable: la secuencia se
  // ve venir con anticipacion. (Si el color nuevo apareciera directo en el
  // frente, acertar seria pura suerte.)
  if (millis() - rcUltimoAvance >= rcVelAvance) {
    rcUltimoAvance += rcVelAvance;
    rcFrente--;
    for (int16_t i = rcFrente; i < NUM_LEDS - 1; i++) rcColor[i] = rcColor[i + 1];
    rcColor[NUM_LEDS - 1] = RC_COLORES[(uint8_t)random(4)];
    if (rcFrente < RC_BASE_LEDS) { rcPerder(); return; }   // el muro toco la base
  }

  // --- Movimiento de los proyectiles (mucho mas rapido que el muro) ---
  // Cada bala lleva su propio timestamp, asi vuelan independientes aunque hayan
  // salido en momentos distintos.
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) {
    if (!rcProyectilActivo[p]) continue;
    if (millis() - rcProyectilPaso[p] >= RC_PROY_VEL) {
      rcProyectilPaso[p] += RC_PROY_VEL;
      rcProyectilPos[p]++;
      if (rcProyectilPos[p] > NUM_LEDS - 1) rcProyectilActivo[p] = false;  // no habia muro: sin penalidad
    }
  }

  // --- Colision: solo puede pasar en el frente del muro ---
  // Se resuelve de a una bala por vez contra el rcFrente ACTUALIZADO: si dos
  // llegan en el mismo frame, la segunda ya juega contra la capa que dejo la
  // primera (rompio o agrego), nunca contra el frente viejo.
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) {
    if (!rcProyectilActivo[p]) continue;
    if (rcFrente >= NUM_LEDS || rcProyectilPos[p] < rcFrente) continue;
    if (rcColor[rcFrente] == RC_COLORES[rcProyectilColor[p]]) rcAcertar();
    else                                                      rcFallar(rcProyectilColor[p]);
    rcProyectilActivo[p] = false;               // el proyectil se consume igual
    if (rcFrente < RC_BASE_LEDS) { rcPerder(); return; }  // la penalidad tambien puede perder
  }

  // --- Dibujo: base con el color cargado, muro, proyectiles con estela ---
  FastLED.clear();
  for (uint8_t i = 0; i < RC_BASE_LEDS; i++) setLed(RC_BASE + i, RC_COLORES[rcColorCargado]);
  for (int16_t i = rcFrente; i < NUM_LEDS; i++) setLed(i, rcColor[i]);
  for (uint8_t p = 0; p < RC_MAX_PROYECTILES; p++) {
    if (!rcProyectilActivo[p]) continue;
    setLed(rcProyectilPos[p], RC_COLORES[rcProyectilColor[p]]);
    for (uint8_t k = 1; k <= ESTELA; k++) {     // cola detras, cada paso mas tenue
      CRGB c = RC_COLORES[rcProyectilColor[p]];
      c.nscale8(255 / (k + 1));
      // la estela no pisa la base, que muestra el color cargado
      if (rcProyectilPos[p] - k >= RC_BASE + RC_BASE_LEDS) setLed(rcProyectilPos[p] - k, c);
    }
  }
  FastLED.show();
}

// ---------- Twang: dungeon 1D de un jugador ----------
// El jugador es un punto verde que se mueve con el joystick: la deflexion del
// eje Y es una VELOCIDAD (mas inclinado, mas rapido), no un paso por pulsacion,
// que es lo que le da el tacto del TWANG original. Por eso la posicion se
// guarda en float y el LED que se pinta es esa posicion redondeada.
// Los enemigos entran por el extremo lejano (el de la salida) y caminan hacia
// el jugador a paso fijo, igual que avanza el muro de Rompecolores.
// La salida es el ultimo LED y solo parpadea cuando la tanda esta limpia: asi
// se ve de un vistazo que primero hay que matar a todos y despues cruzar.

uint8_t twangEnemigosVivos() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) if (twangEnemigoVivo[i]) n++;
  return n;
}

// Arma el terreno del nivel: la lava aparece desde el nivel 2 y las cintas
// desde el 3, sumando una mas de cada tipo cada dos niveles hasta el tope. El
// nivel 1 queda limpio a proposito, como introduccion.
// La colocacion recorre la tira UNA sola vez con un cursor que avanza un hueco
// al azar y despues ocupa un tramo al azar: asi por construccion los tramos no
// se pisan y siempre queda camino libre entre ellos. Se alternan los tipos para
// que no queden todas las lavas de un lado y todas las cintas del otro.
void twangGenerarTerreno() {
  twangLavaN   = twangNivel / 2;              // 0 en el nivel 1, 1 en el 2 y 3, 2 en el 4 y 5...
  twangCintasN = (twangNivel - 1) / 2;        // lo mismo corrido un nivel: arranca en el 3
  if (twangLavaN   > TWANG_MAX_LAVA)   twangLavaN   = TWANG_MAX_LAVA;
  if (twangCintasN > TWANG_MAX_CINTAS) twangCintasN = TWANG_MAX_CINTAS;

  uint32_t ahora  = millis();
  int16_t  cursor = TWANG_TERRENO_MARGEN;                    // despues de la zona de reaparicion
  int16_t  limite = NUM_LEDS - 1 - TWANG_TERRENO_MARGEN;     // antes de la salida
  uint8_t  lavas = 0, cintas = 0;
  bool     tocaLava = true;

  while (lavas < twangLavaN || cintas < twangCintasN) {
    if ( tocaLava && lavas  >= twangLavaN)   tocaLava = false;  // ya no quedan de ese tipo
    if (!tocaLava && cintas >= twangCintasN) tocaLava = true;

    cursor += random(3, 9);                   // hueco de piso firme antes del tramo
    int16_t largo = random(4, 8);              // tramos de menos de 4 LEDs casi no se notan
    if (cursor + largo - 1 > limite) break;   // no entra: nos quedamos con lo ya colocado

    if (tocaLava) {
      twangLavaIni[lavas] = cursor;
      twangLavaFin[lavas] = cursor + largo - 1;
      twangLavaOn[lavas]  = false;            // todas arrancan apagadas
      // Desfase por indice hacia ATRAS en el tiempo: cada tramo entra al ciclo
      // en un momento distinto. Si todas prendieran al unisono el nivel seria
      // un unico semaforo y cruzar seria trivial (o imposible) de una.
      twangLavaCambio[lavas] = ahora - (uint32_t)lavas * 450;
      lavas++;
    } else {
      twangCintaIni[cintas] = cursor;
      twangCintaFin[cintas] = cursor + largo - 1;
      twangCintaDir[cintas] = (random(2) == 0) ? -1 : +1;
      cintas++;
    }
    cursor += largo;
    tocaLava = !tocaLava;
  }

  twangLavaN   = lavas;    // puede haber entrado menos de lo pedido si se acabo la tira
  twangCintasN = cintas;
}

// Arma la tanda del nivel actual: mas enemigos y mas rapidos a cada nivel, con
// piso de velocidad para que no se vuelva imposible. Las apariciones se
// escalonan en el tiempo (y un poco en la posicion) para que no salgan pegados.
void twangIniciarTanda() {
  uint8_t n = TWANG_ENEMIGOS_N1 + (twangNivel - 1);
  if (n > TWANG_MAX_ENEMIGOS) n = TWANG_MAX_ENEMIGOS;

  if (twangNivel == 1) twangVelEnemigo = twangVelInicial;   // el pote fija el nivel 1
  else twangVelEnemigo = max<int>(TWANG_VEL_MINIMA, (twangVelEnemigo * 88) / 100);

  uint32_t ahora = millis();
  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) {
    twangEnemigoVivo[i]    = (i < n);
    twangEnemigoPos[i]     = NUM_LEDS - 1 - (int16_t)random(0, 5);
    twangEnemigoAparece[i] = ahora + (uint32_t)i * TWANG_APARICION_MS;
    twangEnemigoPaso[i]    = twangEnemigoAparece[i];
  }

  twangGenerarTerreno();             // lava y cintas nuevas en cada nivel

  twangJugadorPos  = 0;              // cada nivel se arranca de nuevo en la base
  twangUltimoFrame = ahora;
  twangAtacando    = false;
  twangInvul       = false;
}

void nuevoTwang() {
  twangJoyCentro   = calibrarJoystick(JOY_Y_PIN);  // asume el stick soltado en el instante de arrancar
  twangNivel       = 1;
  twangVidas       = TWANG_VIDAS;
  twangAtaqueDesde = 0;              // 0 = ataque disponible desde el primer frame
  twangInvulDesde  = 0;
  twangCaida       = 0;
  twangEsRecord    = false;
  estadoTwang      = TWANG_JUGANDO;
  twangIniciarTanda();
}

void twangPerder() {
  estadoTwang   = TWANG_FIN;
  estadoDesde   = millis();
  twangEsRecord = intentarRecord(hsTwang, "hsTwang", (uint32_t)twangNivel);
  twangEsRecord ? sonarRecord() : sonarTwangGameOver();
}

// Un golpe recibido, venga de un enemigo o de la lava: en los dos casos se
// pierde una vida, se arranca la invulnerabilidad y suena lo mismo. Devuelve
// true si fue el ultimo: el frame tiene que cortar porque ya se entro en
// TWANG_FIN y no hay nada mas que actualizar ni dibujar.
bool twangRecibirGolpe(int16_t jugadorLed) {
  twangVidas--;
  if (twangVidas == 0) {
    twangCaida = jugadorLed;
    twangPerder();
    return true;
  }
  twangInvul      = true;
  twangInvulDesde = millis();
  sonarTwangGolpe();
  return false;
}

void loopTwang() {
  uint32_t ahora = millis();

  if (estadoTwang == TWANG_FIN) {
    // Derrota: onda roja que se expande desde donde cayo el jugador, y al menu.
    uint32_t t  = ahora - estadoDesde;
    bool     on = (t / 120) % 2 == 0;
    int16_t  r  = t / 25;
    FastLED.clear();
    for (int16_t k = -r; k <= r; k++) setLed(twangCaida + k, on ? TWANG_COL_ENEMIGO : CRGB(40, 0, 0));
    if (twangEsRecord) {
      for (uint8_t s = 0; s < 3; s++) {
        if (random8() < 60) setLed(random16(NUM_LEDS), COL_RECORD);
      }
    }
    FastLED.show();
    if (t > TWANG_FIN_MS) volverAlMenu();
    return;
  }

  if (estadoTwang == TWANG_NIVEL) {
    // Nivel superado: barrido azul desde la salida hacia la base y siguiente tanda.
    uint32_t t = ahora - estadoDesde;
    int16_t  frente = NUM_LEDS - 1 - (int16_t)(t / 10);
    FastLED.clear();
    for (int16_t i = frente; i < NUM_LEDS; i++) setLed(i, TWANG_COL_SALIDA);
    FastLED.show();
    if (t > TWANG_SALIDA_MS) {
      twangNivel++;
      twangIniciarTanda();
      estadoTwang = TWANG_JUGANDO;
    }
    return;
  }

  // --- Movimiento del jugador: velocidad * tiempo del frame ---
  // El dt se mide contra el frame anterior porque el loop no tiene periodo fijo.
  float dt = (ahora - twangUltimoFrame) / 1000.0f;
  twangUltimoFrame = ahora;
  twangJugadorPos += leerJoystickY() * dt;

  // Cintas: arrastran al que este parado encima, ademas de lo que haga el
  // joystick. Como TWANG_CINTA_VEL es bastante menor que TWANG_VEL_JUGADOR se
  // puede caminar en contra (mas lento), no es una trampa sin salida.
  int16_t pisando = (int16_t)(twangJugadorPos + 0.5f);
  for (uint8_t i = 0; i < twangCintasN; i++) {
    if (pisando >= twangCintaIni[i] && pisando <= twangCintaFin[i]) {
      twangJugadorPos += twangCintaDir[i] * (float)TWANG_CINTA_VEL * dt;
    }
  }

  if (twangJugadorPos < 0)            twangJugadorPos = 0;
  if (twangJugadorPos > NUM_LEDS - 1) twangJugadorPos = NUM_LEDS - 1;
  int16_t jugadorLed = (int16_t)(twangJugadorPos + 0.5f);

  if (twangInvul && ahora - twangInvulDesde > TWANG_INVUL_MS) twangInvul = false;

  // --- Lava: cada tramo alterna encendida/apagada con su propio reloj ---
  // Estar parado en una encendida cuesta una vida, igual que un enemigo.
  for (uint8_t i = 0; i < twangLavaN; i++) {
    uint16_t dur = twangLavaOn[i] ? TWANG_LAVA_ON_MS : TWANG_LAVA_OFF_MS;
    if (ahora - twangLavaCambio[i] >= dur) {
      twangLavaCambio[i] += dur;
      twangLavaOn[i] = !twangLavaOn[i];
    }
    if (twangLavaOn[i] && !twangInvul &&
        jugadorLed >= twangLavaIni[i] && jugadorLed <= twangLavaFin[i]) {
      if (twangRecibirGolpe(jugadorLed)) return;
    }
  }

  // --- Ataque: pulso que se expande durante una ventana corta, con cooldown ---
  if (btnFlanco[0] && ahora - twangAtaqueDesde >= TWANG_ATAQUE_ESPERA) {
    twangAtaqueDesde = ahora;
    twangAtacando    = true;
    sonarTwangAtaque();
  }
  uint32_t tAtaque = ahora - twangAtaqueDesde;
  if (twangAtacando && tAtaque > TWANG_ATAQUE_MS) twangAtacando = false;
  int16_t radio = twangAtacando ? (1 + (TWANG_ATAQUE_RADIO * (int16_t)tAtaque) / TWANG_ATAQUE_MS) : 0;

  // --- Enemigos: avanzan hacia la base y chocan (o mueren en el pulso) ---
  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) {
    if (!twangEnemigoVivo[i]) continue;
    if (ahora < twangEnemigoAparece[i]) continue;      // todavia no entro a la mazmorra

    if (ahora - twangEnemigoPaso[i] >= twangVelEnemigo) {
      twangEnemigoPaso[i] += twangVelEnemigo;
      twangEnemigoPos[i]--;
      if (twangEnemigoPos[i] < 0) { twangEnemigoVivo[i] = false; continue; }  // se escapo por la base
    }

    int16_t d = twangEnemigoPos[i] - jugadorLed;
    if (d < 0) d = -d;

    if (twangAtacando && d <= radio) {                 // el pulso lo alcanzo
      twangEnemigoVivo[i] = false;
      sonarTwangMuerte();
      continue;
    }

    // Contacto: el enemigo llego (o paso) al jugador. Durante la invulnerabilidad
    // lo atraviesa sin hacer dano, para no comerse varias vidas del mismo choque.
    if (!twangInvul && twangEnemigoPos[i] <= jugadorLed) {
      twangEnemigoVivo[i] = false;
      if (twangRecibirGolpe(jugadorLed)) return;
    }
  }

  // --- Salida: cruzarla con la tanda limpia sube de nivel ---
  bool limpio = (twangEnemigosVivos() == 0);
  if (limpio && jugadorLed >= NUM_LEDS - 1) {
    estadoTwang = TWANG_NIVEL;
    estadoDesde = ahora;
    sonarTwangNivel();
    return;
  }

  // --- Dibujo: salida y terreno abajo, despues enemigos, pulso y jugador ---
  // (el jugador va ultimo para que se vea siempre, aunque este sobre la lava).
  FastLED.clear();
  // La salida parpadea SOLO con la tanda limpia; con enemigos vivos queda fija.
  bool salidaOn = limpio ? ((ahora / 200) % 2 == 0) : true;
  setLed(NUM_LEDS - 1, salidaOn ? TWANG_COL_SALIDA : CRGB(0, 10, 40));

  // Lava: apagada se dibuja muy tenue (hay que ver donde esta para planear el
  // cruce) y en los ultimos TWANG_LAVA_AVISO_MS antes de prenderse parpadea
  // rapido, asi el jugador tiene tiempo de salir.
  for (uint8_t i = 0; i < twangLavaN; i++) {
    CRGB c = TWANG_COL_LAVA;
    if (!twangLavaOn[i]) {
      uint32_t t = ahora - twangLavaCambio[i];
      bool aviso = (t + TWANG_LAVA_AVISO_MS >= TWANG_LAVA_OFF_MS);
      if (aviso) c.nscale8(((ahora / 60) % 2 == 0) ? 255 : 30);
      else       c.nscale8(25);
    }
    for (int16_t p = twangLavaIni[i]; p <= twangLavaFin[i]; p++) setLed(p, c);
  }

  // Cintas: fondo tenue con pixeles brillantes cada 4 LEDs que se desplazan en
  // el sentido del empuje, para que se vea de que lado tira antes de pisarla.
  for (uint8_t i = 0; i < twangCintasN; i++) {
    CRGB tenue = TWANG_COL_CINTA;
    tenue.nscale8(30);
    int16_t fase = (int16_t)((ahora / 80) % 4);
    for (int16_t p = twangCintaIni[i]; p <= twangCintaFin[i]; p++) {
      int16_t k = (twangCintaDir[i] > 0) ? (p - fase) : (p + fase);
      setLed(p, (((k % 4) + 4) % 4 == 0) ? TWANG_COL_CINTA : tenue);
    }
  }

  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) {
    if (twangEnemigoVivo[i] && ahora >= twangEnemigoAparece[i]) {
      setLed(twangEnemigoPos[i], TWANG_COL_ENEMIGO);
    }
  }

  if (twangAtacando) {                       // flash blanco que crece y se apaga
    CRGB c = CRGB::White;
    c.nscale8(255 - (uint8_t)((255UL * tAtaque) / TWANG_ATAQUE_MS));
    for (int16_t k = -radio; k <= radio; k++) setLed(jugadorLed + k, c);
  }

  // Al recibir un golpe el jugador parpadea mientras dura la invulnerabilidad.
  if (!twangInvul || (ahora / 100) % 2 == 0) setLed(jugadorLed, TWANG_COL_JUGADOR);
  FastLED.show();
}

// ---------- Highscores: vitrina de records ----------
// No es un juego: solo muestra un record por pantalla. Se pasa de juego con el
// joystick, igual que en el menu, y el pulsador de reset vuelve al selector
// (eso ya lo maneja chequearReset()).
void nuevoHighscores() {
  hsVerIndice = 0;
  lcdForzarRefresh();
}

void loopHighscores() {
  int8_t paso = joystickPaso();
  if (paso) { hsVerIndice = (hsVerIndice + 4 + paso) % 4; beep(1200, 25); }

  uint8_t b = beatsin8(20, 10, 60);      // dorado respirando, sin prisa
  fill_solid(leds, NUM_LEDS, COL_RECORD);
  nscale8(leds, NUM_LEDS, b);
  FastLED.show();
}

// ---------- IP: como entrar al panel web ----------
// Pantalla informativa: el LCD muestra la red y la IP (que ya se calcularon en
// setup()) y la tira queda en un azul tenue, sin animacion, para que se note
// que aca no hay nada que jugar. El reset vuelve al menu como en Highscores.
void nuevoIP() {
  lcdForzarRefresh();
}

void loopIP() {
  fill_solid(leds, NUM_LEDS, TWANG_COL_SALIDA);
  nscale8(leds, NUM_LEDS, 30);
  FastLED.show();
}

// ---------- Selector de juegos ----------
void iniciarJuego(uint8_t j) {
  juegoActivo = j;
  pantalla = JUEGO;
  lcdForzarRefresh();
  FastLED.clear(true);
  if      (j == JUEGO_PONG)          nuevoPartido();
  else if (j == JUEGO_TUG)           nuevoTug();
  else if (j == JUEGO_ROMPECOLORES)  nuevoRompecolores();
  else if (j == JUEGO_TWANG)         nuevoTwang();
  else if (j == JUEGO_HIGHSCORES)    nuevoHighscores();
  else if (j == JUEGO_IP)            nuevoIP();
}

void volverAlMenu() {
  pantalla = MENU;
  lcdForzarRefresh();
  FastLED.clear(true);
}

void loopMenu() {
  // El joystick elige (izquierda/derecha, da la vuelta en los dos sentidos) y el
  // Verde entra; el Azul no se usa aca, queda libre para los juegos de a dos.
  int8_t paso = joystickPaso();
  if (paso) {
    juegoSel = (juegoSel + NUM_JUEGOS + paso) % NUM_JUEGOS;   // +NUM_JUEGOS: el modulo de un negativo no da la vuelta
    beep(1200, 25);
  }
  if (btnFlanco[0]) { iniciarJuego(juegoSel); return; }       // jugar

  // Atractor: arcoiris tenue en movimiento mientras se elige.
  static uint8_t  hue = 0;
  static uint32_t ultimo = 0;
  if (millis() - ultimo > 30) { ultimo = millis(); hue++; }
  fill_rainbow(leds, NUM_LEDS, hue, 3);
  nscale8(leds, NUM_LEDS, 40);   // bajar el brillo del atractor
  FastLED.show();
}

// Reset con el pulsador viejo (flanco de bajada): desde un juego vuelve al menu.
bool resetPrev = false;
void chequearReset() {
  bool r = (digitalRead(RESET_PIN) == LOW);   // apretado = LOW
  if (r && !resetPrev) {
    beep(600, 80);
    if (pantalla == JUEGO) volverAlMenu();
  }
  resetPrev = r;
}

// ---------- Panel web: estado en vivo + tuneo de parametros ----------
// El servidor es asincronico (corre en su propia tarea, avisado por eventos),
// asi que el loop() del juego no tiene que atenderlo: no hay handleClient().
// Sin login ni HTTPS a proposito: la red la crea el propio ESP32 y no toca
// internet, es un panel para el que esta armando la consola.
AsyncWebServer server(80);

// Un <input type="number"> con su etiqueta, para no repetir el mismo choclo de
// HTML once veces en paginaHtml().
String campoNumero(const String& nombre, const String& etiqueta, long valor) {
  return "<label>" + etiqueta + ": <input type=\"number\" name=\"" + nombre +
         "\" value=\"" + String(valor) + "\"></label><br>";
}

String paginaHtml() {
  String h;
  h.reserve(3000);
  h += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  h += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  // Recarga sola cada 3 s: alcanza para ver el estado en vivo sin JS ni websockets.
  h += "<meta http-equiv=\"refresh\" content=\"3\">";
  h += "<title>gasticonsola</title></head>";
  h += "<body style=\"font-family:sans-serif;margin:16px;max-width:480px\">";
  h += "<h1>gasticonsola</h1>";

  // --- Estado en vivo ---
  h += "<h2>Ahora</h2><p>";
  if (pantalla == MENU) {
    h += "En el menu, resaltado: <b>" + String(NOMBRE_JUEGO[juegoSel]) + "</b>";
  } else {
    h += "Jugando: <b>" + String(NOMBRE_JUEGO[juegoActivo]) + "</b><br>";
    if      (juegoActivo == JUEGO_PONG)         h += "Verde " + String(puntosP1) + " - " + String(puntosP2) + " Azul";
    else if (juegoActivo == JUEGO_TUG)          h += "Frente en el LED " + String(tugFrente) + " de " + String(NUM_LEDS);
    else if (juegoActivo == JUEGO_ROMPECOLORES) h += "Score: " + String(rcScore);
    else if (juegoActivo == JUEGO_TWANG)        h += "Nivel " + String(twangNivel) + ", vidas " + String(twangVidas);
    else                                        h += "(pantalla informativa)";
  }
  h += "</p>";

  // --- Records guardados ---
  h += "<h2>Records</h2><ul>";
  h += "<li>Pong: "          + String(hsPong)  + " golpes</li>";
  h += "<li>Tira y afloja: " + String(hsTug)   + " empujes</li>";
  h += "<li>Rompecolores: "  + String(hsRc)    + " pts</li>";
  h += "<li>Twang: nivel "   + String(hsTwang) + "</li>";
  h += "</ul>";

  // --- Parametros tuneables en caliente ---
  h += "<h2>Parametros</h2><form method=\"GET\" action=\"/set\">";
  h += "<h3>Global</h3>";
  h += campoNumero("brillo", "Brillo (0-255)", BRILLO);
  h += "<h3>Pong</h3>";
  h += campoNumero("velLenta",  "Saque lento (ms/LED)",  VEL_LENTA);
  h += campoNumero("velRapida", "Saque rapido (ms/LED)", VEL_RAPIDA);
  h += campoNumero("velAcelera","Aceleracion por golpe", VEL_ACELERA);
  h += "<h3>Tira y afloja</h3>";
  h += campoNumero("tugEmpuje", "LEDs por pulsacion", TUG_EMPUJE);
  h += "<h3>Rompecolores</h3>";
  h += campoNumero("rcVelLenta",  "Muro lento (ms/LED)",   RC_VEL_LENTA);
  h += campoNumero("rcVelRapida", "Muro rapido (ms/LED)",  RC_VEL_RAPIDA);
  h += campoNumero("rcProyVel",   "Bala (ms/LED)",         RC_PROY_VEL);
  h += "<h3>Twang</h3>";
  h += campoNumero("twangVelJugador",  "Jugador (LEDs/s)",     TWANG_VEL_JUGADOR);
  h += campoNumero("twangAtaqueRadio", "Radio del ataque",     TWANG_ATAQUE_RADIO);
  h += campoNumero("twangAtaqueEspera","Cooldown ataque (ms)", TWANG_ATAQUE_ESPERA);
  h += "<p><input type=\"submit\" value=\"Aplicar\"></p></form>";

  h += "</body></html>";
  return h;
}

// Aplica lo que haya venido en la query. Sin validar rangos: es un panel de
// tuneo, si se mete un numero raro el juego se pone raro y se corrige de nuevo.
void configurarServidor() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", paginaHtml());
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("brillo")) {
      BRILLO = request->getParam("brillo")->value().toInt();
      FastLED.setBrightness(BRILLO);   // el unico que necesita avisarle a FastLED
    }
    if (request->hasParam("velLenta"))   VEL_LENTA   = request->getParam("velLenta")->value().toInt();
    if (request->hasParam("velRapida"))  VEL_RAPIDA  = request->getParam("velRapida")->value().toInt();
    if (request->hasParam("velAcelera")) VEL_ACELERA = request->getParam("velAcelera")->value().toInt();
    if (request->hasParam("tugEmpuje"))  TUG_EMPUJE  = request->getParam("tugEmpuje")->value().toInt();
    if (request->hasParam("rcVelLenta"))  RC_VEL_LENTA  = request->getParam("rcVelLenta")->value().toInt();
    if (request->hasParam("rcVelRapida")) RC_VEL_RAPIDA = request->getParam("rcVelRapida")->value().toInt();
    if (request->hasParam("rcProyVel"))   RC_PROY_VEL   = request->getParam("rcProyVel")->value().toInt();
    if (request->hasParam("twangVelJugador"))   TWANG_VEL_JUGADOR   = request->getParam("twangVelJugador")->value().toInt();
    if (request->hasParam("twangAtaqueRadio"))  TWANG_ATAQUE_RADIO  = request->getParam("twangAtaqueRadio")->value().toInt();
    if (request->hasParam("twangAtaqueEspera")) TWANG_ATAQUE_ESPERA = request->getParam("twangAtaqueEspera")->value().toInt();
    request->redirect("/");            // vuelve al formulario ya con los valores nuevos
  });

  server.begin();
}

// ---------- Arduino ----------
void setup() {
  // Botones (jugadores + reset) a GND, con pull-up interno.
  pinMode(BTN_P1, INPUT_PULLUP);
  pinMode(BTN_P2, INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);
  ledcSetup(BUZZER_CH, 2000, 8);        // canal, freq base, resolucion (bits)
  ledcAttachPin(BUZZER_PIN, BUZZER_CH); // enganchar el pin al canal

  // Centro del eje X del joystick (el que navega el menu): se mide una sola vez
  // al encender, asumiendo que nadie esta tocando el stick en ese instante.
  joyCentroX = calibrarJoystick(JOY_X_PIN);

  // Records guardados: la primera vez que se enciende la placa no existe la
  // clave todavia y getUInt devuelve el 0 por defecto.
  prefs.begin("gasti", false);
  hsPong  = prefs.getUInt("hsPong",  0);
  hsTug   = prefs.getUInt("hsTug",   0);
  hsRc    = prefs.getUInt("hsRc",    0);
  hsTwang = prefs.getUInt("hsTwang", 0);

  // LCD 1602 por I2C: splash de arranque.
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("  gasticonsola  ");
  lcd.setCursor(0, 1); lcd.print("  LED  -  ESP32 ");
  delay(3600);
  lcd.clear();

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRILLO);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 4700);  // red de seguridad: 4.5 A
  FastLED.clear(true);

  // WiFi al final, despues del splash, para no demorar el arranque visible.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  apIP = WiFi.softAPIP().toString();
  configurarServidor();

  pantalla = MENU;   // arranca en el selector de juegos
}

void loop() {
  leerPote();
  chequearReset();
  actualizarBotones();

  if      (pantalla == MENU)                  loopMenu();
  else if (juegoActivo == JUEGO_PONG)         loopPong();
  else if (juegoActivo == JUEGO_TUG)          loopTug();
  else if (juegoActivo == JUEGO_ROMPECOLORES) loopRompecolores();
  else if (juegoActivo == JUEGO_TWANG)        loopTwang();
  else if (juegoActivo == JUEGO_HIGHSCORES)   loopHighscores();
  else if (juegoActivo == JUEGO_IP)           loopIP();

  actualizarBuzzer();
  actualizarLCD();
}
