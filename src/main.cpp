/*********
  PixeLED — consola de juegos 1D sobre tira WS2812B (ESP32 + FastLED).

  Al encender aparece un SELECTOR: con el eje TRANSVERSAL del control Verde (C1)
  se pasa de juego (izquierda/derecha, con wraparound) y con su boton arcade se
  arranca el resaltado; los otros controles no se usan en el menu. El pulsador de
  reset vuelve al selector desde cualquier juego (y al terminar una partida
  vuelve solo).

  Hay CUATRO controles, cada uno con dos ejes analogicos, un boton arcade y el
  boton del propio joystick. Los juegos de un solo jugador usan siempre el Verde;
  los de varios reparten la partida entre los controles que esten enchufados.

  Este archivo es el ARMADO de la consola, no la logica: la tabla JUEGOS[] de
  abajo es la lista del selector, y cada fila apunta a un juego_*.cpp que se
  ocupa de lo suyo. La infraestructura compartida (tira, botones, buzzer, LCD,
  joystick, pote, records) esta en consola.h / consola.cpp.

    consola.h/.cpp    hardware y servicios comunes
    juego_pong.*      Pong 1D con paletas moviles, media tira para cada uno
    juego_tug.*       Tira y Afloja, cinchada de botones
    juego_rompecolores.*  contra un muro de colores, a uno o a cuatro controles
    juego_twang.*     dungeon 1D con joystick, lava y cintas
    juego_paddle.*    Pong de un jugador con la paleta movil
    juego_stacker.*   apilar bloques sin perder ancho
    juego_esquiva.*   muros que bajan y hay que meterse en los huecos
    juego_lander.*    alunizaje con gravedad, empuje y combustible
    juego_duelo.*     reaccion: de dos a cuatro jugadores, el primero que aprieta
    juego_carrera.*   OpenLEDRace: hasta cuatro autos a fuerza de pulsaciones
    juego_pelea.*     combate cuerpo a cuerpo, de dos a cuatro peleadores
    juego_western.*   Tiros: duelo a distancia con balas lentas y direccionales
    modo_ambiente.*   automata celular elemental (no es un juego)
    pantallas.*       Highscores e IP (no son juegos, pero entran al selector)
    panel_web.*       AP WiFi + pagina de estado y tuneo

  Para agregar un juego: juego_x.h/.cpp con nuevoX/loopX/lcdX/webX, el #include
  aca, una fila en JUEGOS[] y una entrada en el enum Juego de consola.h (en el
  MISMO orden que la tabla).

  Hardware (ver scripts-y-pruebas/setup-hardware-maquina-juegos-led.md):
    Datos:    GPIO16 -> SN74AHCT125N -> 470ohm -> DIN tira (100 LEDs WS2812B)
    Buzzer:   GPIO25 (buzzer pasivo, PWM por LEDC) a GND
    LCD 1602: I2C 0x27, SDA=GPIO21, SCL=GPIO22 (marcador + mensajes de estado)
    Reset:    GPIO18 a GND (pulsador; INPUT_PULLUP, apretado = LOW) -> vuelve al menu
    Pote:     GPIO34 (ADC1, input-only) -- B10k: extremos a 3.3V/GND, cursor al pin
    Tira alimentada por fuente externa 5V, masa comun, cap de 1000uF al inicio.

  Controles: cuatro, iguales, con ficha DB9 y cable de 1,5 m.
    Arcade:   GPIO14 / 27 / 32 / 33   (a GND, INPUT_PULLUP, apretado = LOW)
    Stick SW: GPIO4  / 13 / 17 / 19   (idem)
    Ejes:     2x ADS1115 en bus I2C propio, SDA=GPIO23 SCL=GPIO26 a 400 kHz
              0x48 -> C1 y C2,  0x49 -> C3 y C4  (ADDR del segundo a 3.3V)
              Joysticks alimentados a 3.3V, no a 5V: el SW va directo al ESP32.
*********/

#include "consola.h"
#include "juego_pong.h"
#include "juego_tug.h"
#include "juego_rompecolores.h"
#include "juego_twang.h"
#include "juego_paddle.h"
#include "juego_stacker.h"
#include "juego_esquiva.h"
#include "juego_lander.h"
#include "juego_duelo.h"
#include "juego_carrera.h"
#include "juego_pelea.h"
#include "juego_western.h"
#include "modo_ambiente.h"
#include "pantallas.h"
#include "panel_web.h"

// ---------- La lista del selector ----------
// El orden tiene que coincidir con el enum Juego de consola.h.
//
// Los nombres van SIN espacios de relleno: los agrega lcdMenu(). Antes cada
// nombre traia los suyos, ajustados a mano para que "< nombre >" entrara en 16
// columnas, y esos espacios se colaban tal cual en el panel web. Este es el
// nombre canonico de cada juego: el LCD, la web y los records salen todos de
// aca (o de RECORDS[], que usa los mismos textos).
const JuegoDef JUEGOS[] = {
  { "Pong",         nuevoPong,          loopPong,          lcdPong,          webPong          },
  { "Tira y Afloja", nuevoTug,           loopTug,           lcdTug,           webTug           },
  { "Rompecolores", nuevoRompecolores,  loopRompecolores,  lcdRompecolores,  webRompecolores  },
  { "Twang",        nuevoTwang,         loopTwang,         lcdTwang,         webTwang         },
  { "Paddle",       nuevoPaddle,        loopPaddle,        lcdPaddle,        webPaddle        },
  { "Stacker",      nuevoStacker,       loopStacker,       lcdStacker,       webStacker       },
  { "Salta Muros",  nuevoEsquiva,       loopEsquiva,       lcdEsquiva,       webEsquiva       },
  { "Alunizaje",    nuevoLander,        loopLander,        lcdLander,        webLander        },
  { "Reaccion",     nuevoDuelo,         loopDuelo,         lcdDuelo,         webDuelo         },
  { "Carrera",      nuevoCarrera,       loopCarrera,       lcdCarrera,       webCarrera       },
  { "Pelea",        nuevoPelea,         loopPelea,         lcdPelea,         webPelea         },
  { "Tiros",        nuevoWestern,       loopWestern,       lcdWestern,       webWestern       },
  { "Ambiente",     nuevoAmbiente,      loopAmbiente,      lcdAmbiente,      webAmbiente      },
  { "Highscores",   nuevoHighscores,    loopHighscores,    lcdHighscores,    webHighscores    },
  { "IP",           nuevoIP,            loopIP,            lcdIP,            webIP            },
};
static_assert(sizeof(JUEGOS) / sizeof(JUEGOS[0]) == NUM_JUEGOS,
              "JUEGOS[] y el enum Juego quedaron desincronizados");

Pantalla pantalla    = MENU;          // arranca en el selector
uint8_t  juegoSel    = 0;             // indice resaltado en el menu
uint8_t  juegoActivo = JUEGO_PONG;    // juego que se esta jugando

// ---------- Selector ----------
void iniciarJuego(uint8_t j) {
  juegoActivo = j;
  pantalla    = JUEGO;
  lcdForzarRefresh();
  FastLED.clear(true);
  JUEGOS[j].nuevo();
}

void volverAlMenu() {
  pantalla = MENU;
  lcdForzarRefresh();
  FastLED.clear(true);
}

// --- Menu: juego resaltado + ayuda de controles ---
// Las flechas alrededor del nombre invitan a mover el joystick a los costados
// (lcdLinea ya se encarga de centrar el texto en las 16 columnas). Los nombres
// largos pierden los espacios de adentro antes que dejarse recortar.
static void lcdMenu() {
  String n = String(JUEGOS[juegoSel].nombre);
  String s = "< " + n + " >";
  if (s.length() > 16) s = "<" + n + ">";
  lcdLinea(0, s);
  lcdLinea(1, "Verde = jugar");
}

static void loopMenu() {
  // El joystick del Verde elige (izquierda/derecha, da la vuelta en los dos
  // sentidos) y su boton entra; el control del Azul no se usa en el menu.
  int8_t paso = joystickPaso(0);
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
static bool resetPrev = false;
static void chequearReset() {
  bool r = (digitalRead(RESET_PIN) == LOW);   // apretado = LOW
  if (r && !resetPrev) {
    beep(600, 80);
    if (pantalla == JUEGO) volverAlMenu();
  }
  resetPrev = r;
}

// ---------- Arduino ----------
void setup() {
  // Lo primero, para que el monitor serie sirva de algo aunque la consola se
  // este reiniciando sola: el ESP32 ya imprime la causa del reset por su cuenta,
  // pero esta linea la deja en castellano y marca donde arranca cada boot.
  Serial.begin(115200);
  Serial.println("\n[PixeLED] ultimo reinicio: " + textoCausaReset());

  // Los ocho botones de los controles (arcade + stick) y el reset, todos a GND
  // con pull-up interno.
  const uint8_t PINES_BOTON[] = { BTN_C1, BTN_C2, BTN_C3, BTN_C4,
                                  SW_C1,  SW_C2,  SW_C3,  SW_C4, RESET_PIN };
  for (uint8_t p : PINES_BOTON) pinMode(p, INPUT_PULLUP);

  ledcSetup(BUZZER_CH, 2000, 8);        // canal, freq base, resolucion (bits)
  ledcAttachPin(BUZZER_PIN, BUZZER_CH); // enganchar el pin al canal

  // Bus de los ADS1115 y centro de los cuatro joysticks: se mide una sola vez
  // al encender, asumiendo que nadie esta tocando los sticks en ese instante.
  iniciarJoysticks();
  calibrarJoys();

  iniciarRecords();
  iniciarLCD();                         // incluye el splash de arranque

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness((uint8_t)BRILLO);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 4700);  // red de seguridad: 4.5 A
  FastLED.clear(true);

  // WiFi al final, despues del splash, para no demorar el arranque visible.
  iniciarPanelWeb();

  pantalla = MENU;   // arranca en el selector de juegos
}

void loop() {
  chequearReset();
  actualizarBotones();
  actualizarJoysticks();   // una ronda del barrido de los ocho ejes

  if (pantalla == MENU) loopMenu();
  else                  JUEGOS[juegoActivo].loop();

  actualizarBuzzer();

  // El LCD se repinta ultimo y solo escribe las filas que cambiaron.
  if (pantalla == MENU) lcdMenu();
  else                  JUEGOS[juegoActivo].lcd();
}
