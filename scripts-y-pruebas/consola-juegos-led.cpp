/*********
  MAQUINA DE JUEGOS LED — consola 1D sobre tira WS2812B (ESP32 + FastLED).

  Al encender aparece un SELECTOR: con el boton Verde (P1) se pasa de juego y
  con el Azul (P2) se arranca el resaltado. El pulsador de reset vuelve al
  selector desde cualquier juego (y al terminar una partida vuelve solo).

  Juegos:
    - Pong 1D: pelota que rebota; cada jugador golpea con su boton cuando la
      pelota entra en su zona; cada golpe acelera. El potenciometro fija la
      velocidad de saque.
    - Tug of War: cinchada de botones; un frente Verde|Azul que se empuja a
      martillazos hacia el lado del rival; el que llega al extremo contrario gana.

  Hardware (ver scripts-y-pruebas/setup-hardware-maquina-juegos-led.md):
    Datos:    GPIO16 -> SN74AHCT125N -> 470ohm -> DIN tira (100 LEDs WS2812B)
    Boton P1 (Verde): GPIO14 a GND (arcade/microswitch; INPUT_PULLUP, apretado = LOW)
    Boton P2 (Azul):  GPIO27 a GND (idem)
    Buzzer:   GPIO25 (buzzer pasivo, PWM por LEDC) a GND
    LCD 1602: I2C 0x27, SDA=GPIO21, SCL=GPIO22 (marcador + mensajes de estado)
    Reset:    GPIO18 a GND (pulsador; INPUT_PULLUP, apretado = LOW) -> vuelve al menu
    Pote:     GPIO34 (ADC1, input-only) -- B10k: extremos a 3.3V/GND, cursor al pin
    Tira alimentada por fuente externa 5V, masa comun, cap de 1000uF al inicio.
*********/

#include <FastLED.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

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

// LCD 1602 por I2C (marcador + mensajes de estado).
#define LCD_ADDR 0x27
#define LCD_SDA  21
#define LCD_SCL  22

CRGB leds[NUM_LEDS];
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// ---------- Parametros del juego (tocar aca para balancear) ----------
const uint8_t  BRILLO         = 128;  // techo de seguridad de corriente
const uint8_t  ZONA           = 12;   // largo de la zona de golpe de cada jugador
const uint16_t VEL_LENTA      = 90;   // saque mas lento, pote al minimo (ms por LED)
const uint16_t VEL_RAPIDA     = 24;   // saque mas rapido, pote al maximo (ms por LED)
const uint16_t VEL_MINIMA     = 16;   // piso de aceleracion (ms por LED)
const uint16_t VEL_ACELERA    = 4;    // cuanto baja el intervalo por cada golpe
const uint8_t  PUNTOS_GANAR   = 5;
const uint16_t DEBOUNCE_MS    = 25;
const uint8_t  ESTELA         = 2;    // largo de la cola de la pelota
const uint8_t  TUG_EMPUJE     = 2;    // LEDs que avanza el frente por pulsacion (Tug of War)

// ---------- Colores ----------
#define COL_PELOTA  CRGB::White
#define COL_P1      CRGB(0, 255, 0)   // verde  (jugador 1, inicio de la tira)
#define COL_P2      CRGB(0, 60, 255)  // azul   (jugador 2, final de la tira)

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

// ---------- Consola: selector de juegos ----------
enum Pantalla { MENU, JUEGO };
Pantalla pantalla = MENU;               // arranca en el selector

enum Juego { JUEGO_PONG, JUEGO_TUG, NUM_JUEGOS };
uint8_t juegoSel    = 0;                // indice resaltado en el menu
uint8_t juegoActivo = JUEGO_PONG;       // juego que se esta jugando
const char* NOMBRE_JUEGO[NUM_JUEGOS] = { "Pong", "Tug of War" };

// Definidas mas abajo; se usan antes en el archivo.
void volverAlMenu();
void iniciarJuego(uint8_t j);
void nuevoTug();

// ---------- Estado de Tug of War ----------
enum EstadoTug { TUG_JUGANDO, TUG_FIN };
EstadoTug estadoTug;
int16_t   tugFrente;    // posicion del limite Verde|Azul (0..NUM_LEDS-1)
uint8_t   tugGanador;   // 1 = Verde, 2 = Azul

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

// ---------- Potenciometro: velocidad de saque ----------
// Promedia unas lecturas para suavizar el jitter del ADC y mapea a velSaque.
// El pote solo fija la velocidad del SAQUE; la aceleracion por golpes sigue igual.
void leerPote() {
  uint32_t s = 0;
  for (uint8_t i = 0; i < 4; i++) s += analogRead(POT_PIN);
  velSaque = map(s / 4, 0, 4095, VEL_LENTA, VEL_RAPIDA);
}

// ---------- Buzzer: efectos no bloqueantes (LEDC) ----------
// tone() no anda bien -> usamos LEDC (ledcSetup/ledcAttachPin/ledcWriteTone).
// Este core es arduino-esp32 2.0.x, asi que ledcWriteTone toma el CANAL.
// Un beep simple y un pequeno secuenciador de melodias, ambos guiados por
// millis() para no frenar el juego.
struct Nota { uint16_t freq; uint16_t dur; };   // freq 0 = silencio (pausa)

const Nota JINGLE_PUNTO[] = { {494, 120}, {330, 180} };                       // descendente
const Nota JINGLE_WIN[]   = { {523, 120}, {659, 120}, {784, 120}, {1047, 260} }; // C-E-G-C

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
    case FIN:     m = (ganador == 1) ? "** GANA VERDE **" : "** GANA AZUL **"; break;
  }
  lcdLinea(1, m);
}

// --- Tug of War: etiquetas de lado + barra de posicion del frente ---
void lcdTug() {
  if (estadoTug == TUG_FIN) {
    lcdLinea(0, "Tug of War");
    lcdLinea(1, (tugGanador == 1) ? "** GANA VERDE **" : "** GANA AZUL **");
    return;
  }
  lcdLinea(0, "Verde       Azul");        // Verde a la izquierda, Azul a la derecha
  uint8_t p = map(tugFrente, 0, NUM_LEDS - 1, 0, 15);
  String bar;
  for (uint8_t i = 0; i < 16; i++) bar += (i == p) ? '|' : '-';
  lcdLinea(1, bar);
}

// --- Menu: juego resaltado + ayuda de botones ---
void lcdMenu() {
  lcdLinea(0, "> " + String(NOMBRE_JUEGO[juegoSel]));
  lcdLinea(1, "V=sig   A=jugar");
}

// Dispatcher: elige que mostrar segun la pantalla/juego actual. Se llama cada
// loop, pero lcdLinea solo toca el I2C cuando el texto realmente cambia.
void actualizarLCD() {
  if (pantalla == MENU) { lcdMenu(); return; }
  if (juegoActivo == JUEGO_PONG) lcdPong();
  else                           lcdTug();
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
    sonarGolpe();
  }
  if (pelotaDir == +1 && pelotaPos >= NUM_LEDS - ZONA && btnFlanco[1]) {
    pelotaDir = -1;
    pelotaVel = max<int>(VEL_MINIMA, pelotaVel - VEL_ACELERA);
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
      ganador ? sonarVictoria() : sonarPunto();
      irA(ganador ? FIN : PUNTO);
      return;
    }
    if (pelotaPos > NUM_LEDS - 1) { // se escapo por P2 -> punto de P1
      puntosP1++;
      ganador = (puntosP1 >= PUNTOS_GANAR) ? 1 : 0;
      saca = 2;
      ganador ? sonarVictoria() : sonarPunto();
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

// ---------- Tug of War: cinchada de botones ----------
void nuevoTug() {
  tugFrente = NUM_LEDS / 2;   // el frente arranca en el centro
  estadoTug = TUG_JUGANDO;
}

void loopTug() {
  if (estadoTug == TUG_JUGANDO) {
    // Cada pulsacion nueva (flanco) empuja el frente hacia el lado del rival.
    if (btnFlanco[0]) { tugFrente += TUG_EMPUJE; beep(1400, 18); }   // Verde empuja al final
    if (btnFlanco[1]) { tugFrente -= TUG_EMPUJE; beep(1050, 18); }   // Azul empuja al inicio

    if (tugFrente >= NUM_LEDS - 1) {        // Verde conquisto la tira
      tugGanador = 1; estadoTug = TUG_FIN; estadoDesde = millis(); sonarVictoria();
    } else if (tugFrente <= 0) {            // Azul conquisto la tira
      tugGanador = 2; estadoTug = TUG_FIN; estadoDesde = millis(); sonarVictoria();
    }

    // Render: Verde [0, frente), Azul [frente, fin).
    for (int16_t i = 0; i < NUM_LEDS; i++) leds[i] = (i < tugFrente) ? COL_P1 : COL_P2;
    FastLED.show();
  } else {  // TUG_FIN: festejo del ganador, ~3 s, y al menu.
    CRGB c = (tugGanador == 1) ? COL_P1 : COL_P2;
    uint16_t t = millis() - estadoDesde;
    fill_solid(leds, NUM_LEDS, ((t / 150) % 2 == 0) ? c : CRGB::Black);
    FastLED.show();
    if (t > 3000) volverAlMenu();
  }
}

// ---------- Selector de juegos ----------
void iniciarJuego(uint8_t j) {
  juegoActivo = j;
  pantalla = JUEGO;
  lcdForzarRefresh();
  FastLED.clear(true);
  if (j == JUEGO_PONG) nuevoPartido();
  else                 nuevoTug();
}

void volverAlMenu() {
  pantalla = MENU;
  lcdForzarRefresh();
  FastLED.clear(true);
}

void loopMenu() {
  if (btnFlanco[0]) { juegoSel = (juegoSel + 1) % NUM_JUEGOS; beep(1200, 25); }  // siguiente
  if (btnFlanco[1]) { iniciarJuego(juegoSel); return; }                          // jugar

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

// ---------- Arduino ----------
void setup() {
  // Botones (jugadores + reset) a GND, con pull-up interno.
  pinMode(BTN_P1, INPUT_PULLUP);
  pinMode(BTN_P2, INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);
  ledcSetup(BUZZER_CH, 2000, 8);        // canal, freq base, resolucion (bits)
  ledcAttachPin(BUZZER_PIN, BUZZER_CH); // enganchar el pin al canal

  // LCD 1602 por I2C: splash de arranque.
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print(" MAQUINA JUEGOS ");
  lcd.setCursor(0, 1); lcd.print("  LED  -  ESP32 ");
  delay(1200);
  lcd.clear();

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRILLO);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 4500);  // red de seguridad: 4.5 A
  FastLED.clear(true);

  pantalla = MENU;   // arranca en el selector de juegos
}

void loop() {
  leerPote();
  chequearReset();
  actualizarBotones();

  if (pantalla == MENU)               loopMenu();
  else if (juegoActivo == JUEGO_PONG) loopPong();
  else                                loopTug();

  actualizarBuzzer();
  actualizarLCD();
}
