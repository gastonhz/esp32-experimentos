// Infraestructura compartida de la consola: tira, botones, pote, joystick,
// buzzer, LCD y records. Nada de aca sabe que juegos existen.

#include "consola.h"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <esp_system.h>

// ---------- Tira ----------
CRGB     leds[NUM_LEDS];
uint16_t BRILLO = 128;

const CRGB COL_RECORD = CRGB(255, 200, 0);

// ---------- Dibujo ----------
void setLed(int16_t i, const CRGB& c) {
  if (i >= 0 && i < NUM_LEDS) leds[i] = c;
}

void dibujarPuntoConEstela(int16_t pos, int8_t dir, const CRGB& col, uint8_t largo) {
  setLed(pos, col);
  for (uint8_t k = 1; k <= largo; k++) {
    CRGB c = col;
    c.nscale8(255 / (k + 1));                 // cada paso mas tenue
    setLed(pos - dir * k, c);
  }
}

void dibujarChispasRecord() {
  for (uint8_t s = 0; s < 3; s++) {
    if (random8() < 60) setLed(random16(NUM_LEDS), COL_RECORD);
  }
}

// ---------- Botones: debounce + flanco de bajada ----------
const uint16_t DEBOUNCE_MS = 25;

const uint8_t PIN_BTN[2] = { BTN_P1, BTN_P2 };
bool     btnEstable[2] = { false, false };  // true = presionado (estable)
bool     btnFlanco[2]  = { false, false };  // true un frame al recien presionar
static bool     btnPrev[2]   = { false, false };  // ultima lectura cruda
static uint32_t btnCambio[2] = { 0, 0 };          // millis del ultimo cambio crudo

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
      if (btnEstable[i]) btnFlanco[i] = true;      // flanco: recien presionado
    }
  }
}

// ---------- Potenciometro ----------
// Promedia unas lecturas para suavizar el jitter del ADC.
uint16_t leerPoteCrudo() {
  uint32_t s = 0;
  for (uint8_t i = 0; i < 4; i++) s += analogRead(POT_PIN);
  return s / 4;
}

// ---------- Joysticks ----------
// Un eje por jugador, indexados igual que los botones: 0 = Verde, 1 = Azul.
const uint8_t PIN_JOY[2] = { JOY_P1_PIN, JOY_P2_PIN };

// Los dos controles son caseros y cada uno pudo quedar montado con el eje para
// el lado contrario. Se corrige aca, en un solo lugar, en vez de repartir
// signos por el codigo de cada juego.
const bool JOY_INVERTIDO[2] = { false, false };

const uint16_t JOY_CENTRO_NOMINAL = 2048;  // centro teorico de 12 bits
const uint16_t JOY_MUERTA_JUEGO   = 200;   // zona muerta moviendo al jugador
// Zona muerta ancha para el menu: elegir juego no necesita precision y asi el
// menu no se mueve solo.
const uint16_t JOY_MUERTA_MENU    = 700;
// Auto-repeat manteniendo el stick al costado. Despues de unos pasos seguidos
// acelera: con doce entradas en el menu, cruzar la lista al ritmo lento se hace
// eterno, pero arrancar rapido haria que un toque suelto se pase de largo.
const uint16_t MENU_REPETIR_MS     = 350;
const uint16_t MENU_REPETIR_RAPIDO = 140;
const uint8_t  MENU_PASOS_ACELERA  = 3;    // pasos seguidos antes de acelerar

// Se miden los dos en setup() y cada juego remide el suyo al empezar la partida.
static uint16_t joyCentro[2] = { JOY_CENTRO_NOMINAL, JOY_CENTRO_NOMINAL };

static uint16_t promediarEje(uint8_t pin) {
  uint32_t s = 0;
  for (uint8_t i = 0; i < 8; i++) s += analogRead(pin);
  return s / 8;
}

void calibrarJoy(uint8_t jugador) { joyCentro[jugador] = promediarEje(PIN_JOY[jugador]); }
void calibrarJoys() { calibrarJoy(0); calibrarJoy(1); }

// Lectura cruda ya centrada y con el sentido corregido: + hacia el final de la
// tira. La comparten leerJoyNorm y joystickPaso, que solo se diferencian en la
// zona muerta y en como traducen la deflexion.
static int16_t desvioJoy(uint8_t jugador) {
  int16_t d = (int16_t)analogRead(PIN_JOY[jugador]) - (int16_t)joyCentro[jugador];
  return JOY_INVERTIDO[jugador] ? (int16_t)-d : d;
}

// Fuera de la zona muerta la deflexion arranca en 0 y crece hasta 1, asi no hay
// salto de velocidad al cruzar el borde.
float leerJoyNorm(uint8_t jugador) {
  int16_t d = desvioJoy(jugador);
  int16_t m = (d < 0) ? -d : d;
  if (m <= JOY_MUERTA_JUEGO) return 0.0f;
  float f = (float)(m - JOY_MUERTA_JUEGO) / (float)(joyCentro[jugador] - JOY_MUERTA_JUEGO);
  if (f > 1.0f) f = 1.0f;
  return (d > 0) ? f : -f;
}

// Convierte la deflexion del eje en pasos discretos: devuelve -1/0/+1. Da un
// paso al salir de la zona muerta y, si se mantiene el stick, repite cada
// MENU_REPETIR_MS. Sin esto el menu se iria de largo a 60 pasos por segundo.
// El estado va por jugador, pero dentro de cada uno lo comparten el menu y
// Highscores, que nunca estan activos a la vez.
static int8_t   joyPasoPrev[2]      = { 0, 0 };  // sentido del stick el frame anterior
static uint32_t joyPasoDesde[2]     = { 0, 0 };  // millis() del ultimo paso entregado
static uint8_t  joyPasosSeguidos[2] = { 0, 0 };

int8_t joystickPaso(uint8_t jugador) {
  int16_t d = desvioJoy(jugador);
  int8_t  dir = 0;
  if (d >  (int16_t)JOY_MUERTA_MENU) dir = +1;
  if (d < -(int16_t)JOY_MUERTA_MENU) dir = -1;

  if (dir == 0) { joyPasoPrev[jugador] = 0; return 0; }  // al centro: listo para el proximo paso

  uint32_t ahora = millis();
  if (dir != joyPasoPrev[jugador]) {                     // recien salio de la zona muerta
    joyPasoPrev[jugador]      = dir;
    joyPasoDesde[jugador]     = ahora;
    joyPasosSeguidos[jugador] = 1;
    return dir;
  }
  uint16_t espera = (joyPasosSeguidos[jugador] >= MENU_PASOS_ACELERA) ? MENU_REPETIR_RAPIDO
                                                                      : MENU_REPETIR_MS;
  if (ahora - joyPasoDesde[jugador] >= espera) {         // lo mantiene: auto-repeat
    joyPasoDesde[jugador] = ahora;
    if (joyPasosSeguidos[jugador] < 255) joyPasosSeguidos[jugador]++;
    return dir;
  }
  return 0;
}

// ---------- Buzzer: efectos no bloqueantes (LEDC) ----------
// tone() no anda bien -> usamos LEDC (ledcSetup/ledcAttachPin/ledcWriteTone).
// Este core es arduino-esp32 2.0.x, asi que ledcWriteTone toma el CANAL.
static const Nota JINGLE_WIN[] = { {523, 120}, {659, 120}, {784, 120}, {1047, 260} }; // C-E-G-C
// Record nuevo: mas largo y con dos escalones ascendentes + remate repetido, para
// que no se confunda de oido con la victoria normal de una partida cualquiera.
static const Nota JINGLE_RECORD[] = {
  {523, 90}, {659, 90}, {784, 90}, {1047, 160},
  {880, 90}, {1047, 90}, {1319, 200},
  {0,   80}, {1319, 120}, {1568, 120}, {2093, 420}
};

static bool     buzzerSonando   = false;   // hay un beep simple en curso
static uint32_t buzzerHasta     = 0;       // millis() en que corta ese beep
static const Nota* jingle       = nullptr; // melodia en curso
static uint8_t  jingleLen       = 0;
static uint8_t  jingleIdx       = 0;
static uint32_t jingleNotaHasta = 0;

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

void sonarVictoria() { tocarJingle(JINGLE_WIN, 4); }
void sonarRecord()   { tocarJingle(JINGLE_RECORD, 11); }

// ---------- LCD 1602: marcador + mensajes ----------
static LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
static String lcdCache[2] = { "\x01", "\x01" };   // valor imposible -> fuerza el 1er dibujo

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

void lcdForzarRefresh() { lcdCache[0] = "\x01"; lcdCache[1] = "\x01"; }

String barraLCD(uint16_t valor, uint16_t maximo, uint8_t ancho) {
  if (maximo == 0) maximo = 1;
  if (valor > maximo) valor = maximo;
  uint8_t llenos = ((uint32_t)valor * ancho) / maximo;
  String b;
  for (uint8_t i = 0; i < ancho; i++) b += (i < llenos) ? '#' : '-';
  return b;
}

// ---------- Diagnostico: por que se reinicio la placa ----------
String textoCausaReset() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "Encendido normal";
    case ESP_RST_BROWNOUT: return "BROWNOUT (5V!)";   // se cayo la alimentacion
    case ESP_RST_PANIC:    return "Crash (panic)";    // excepcion en el firmware
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "Watchdog";
    case ESP_RST_SW:       return "Reset software";
    case ESP_RST_EXT:      return "Reset externo";
    case ESP_RST_DEEPSLEEP:return "Deep sleep";
    default:               return "Desconocida";
  }
}

// Escribe una linea de 16 columnas sin pasar por la cache de lcdLinea: se usa
// durante el splash, cuando todavia no hay pantalla activa que cachear.
static void lcdCrudo(uint8_t fila, const String& txt) {
  String t = txt;
  if (t.length() > 16) t = t.substring(0, 16);
  while (t.length() < 16) t += ' ';
  lcd.setCursor(0, fila);
  lcd.print(t);
}

void iniciarLCD() {
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcdCrudo(0, "  gasticonsola");
  lcdCrudo(1, "  LED  -  ESP32");

  // El splash dura lo mismo que antes, pero ahora es una espera activa: si en
  // algun momento de esos 3,6 s se aprieta el pulsador de reset, despues se
  // muestra la causa del ultimo reinicio. Asi el dato esta siempre disponible
  // sin enchufar la compu, pero no le aparece en la cara a nadie que solo
  // quiera jugar.
  bool pedido = false;
  uint32_t hasta = millis() + 3600;
  while ((int32_t)(millis() - hasta) < 0) {
    if (digitalRead(RESET_PIN) == LOW) pedido = true;   // apretado = LOW
    delay(10);
  }

  if (pedido) {
    lcdCrudo(0, "Ultimo reinicio:");
    lcdCrudo(1, textoCausaReset());
    delay(4000);
  }
  lcd.clear();
}

// ---------- Records persistentes (NVS) ----------
// OJO: las claves son las que ya estan grabadas en la flash de la placa. Si se
// cambia una, el record viejo queda huerfano y arranca de cero.
const RecordDef RECORDS[] = {
  { "hsPong",   "Pong",          "",       " golpes",  false },
  { "hsTug",    "Tira y Afloja", "",       " empujes", false },
  { "hsRc",     "Rompecolores",  "",       " pts",     false },
  { "hsTwang",  "Twang",         "Nivel ", "",         false },
  { "hsPaddle", "Paddle",        "",       " pts",     false },
  { "hsStack",  "Stacker",       "Piso ",  "",         false },
  { "hsEsq",    "Salta Muros",       "",       " muros",   false },
  { "hsLander", "Alunizaje",        "",       " aluniza.", false },
  { "hsDuelo",  "Reaccion",      "",       " ms",      true  },   // gana el tiempo mas CHICO
  { "hsCarr",   "Carrera",       "",       " ms/vta",  true  },   // idem: mejor vuelta
  { "hsPelea",  "Pelea",         "",       " golpes",  false },
  { "hsWest",   "Western",       "",       " cruces",  false },   // balas anuladas en el aire
};
static_assert(sizeof(RECORDS) / sizeof(RECORDS[0]) == NUM_RECORDS,
              "RECORDS[] y el enum Record quedaron desincronizados");

uint32_t hsValor[NUM_RECORDS];
static Preferences prefs;

void iniciarRecords() {
  // La primera vez que se enciende la placa no existe la clave todavia y
  // getUInt devuelve el 0 por defecto, que es justamente "sin record".
  prefs.begin("gasti", false);
  for (uint8_t i = 0; i < NUM_RECORDS; i++) {
    hsValor[i] = prefs.getUInt(RECORDS[i].clave, 0);
  }
}

bool intentarRecord(uint8_t rec, uint32_t valor) {
  if (valor == 0) return false;              // 0 esta reservado para "sin record"
  uint32_t actual = hsValor[rec];
  bool mejor = RECORDS[rec].menorEsMejor ? (actual == 0 || valor < actual)
                                         : (valor > actual);
  if (!mejor) return false;
  hsValor[rec] = valor;
  prefs.putUInt(RECORDS[rec].clave, valor);
  return true;
}

String textoRecord(uint8_t rec) {
  if (hsValor[rec] == 0) return "---";
  return String(RECORDS[rec].prefijo) + String(hsValor[rec]) + String(RECORDS[rec].unidad);
}
