// Infraestructura compartida de la consola: tira, botones, pote, joystick,
// buzzer, LCD y records. Nada de aca sabe que juegos existen.

#include "consola.h"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <esp_system.h>

// ---------- Tira ----------
CRGB     leds[LEDS_MAX];
uint16_t LARGO_TIRA  = 100;     // arranca en la corta; iniciarAjustes() lo pisa con lo guardado
uint8_t  ORIENTACION = TIRA_VERTICAL;
bool     SILENCIO    = false;   // no se guarda: cada encendido arranca con sonido
uint16_t BRILLO = 128;

const CRGB COL_RECORD = CRGB(255, 200, 0);

// Los dos primeros reusan COL_P1/COL_P2 para que no haya dos definiciones del
// verde y el azul que se puedan ir desincronizando. Es la misma paleta de
// cuatro que usa el muro de Rompecolores, asi que ahi la bala de cada jugador
// puede salir del color de su propio control.
//
// El amarillo es limon (255,235,0) y no ambar: el dorado de los records es
// (255,200,0) y con un amarillo mas calido los dos se confundirian justo en la
// animacion de fin de partida, que es donde aparecen juntos.
const ControlDef CONTROLES[NUM_CONTROLES] = {
  { "Verde",    "Ve", COL_P1              },
  { "Azul",     "Az", COL_P2              },
  { "Rojo",     "Ro", CRGB(255,   0,   0) },
  { "Amarillo", "Am", CRGB(255, 235,   0) },
};

String textoGana(uint8_t jugador) {
  String s = String("** GANA ") + CONTROLES[jugador].nombre + " **";
  if (s.length() <= 16) return s;
  return String("GANA ") + CONTROLES[jugador].nombre + "!";
}

// ---------- Dibujo ----------
void setLed(int16_t i, const CRGB& c) {
  if (i >= 0 && i < LARGO_TIRA) leds[i] = c;
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
    if (random8() < 60) setLed(random16(LARGO_TIRA), COL_RECORD);
  }
}

// ---------- Botones: debounce + flanco de bajada ----------
const uint16_t DEBOUNCE_MS = 25;

static const uint8_t PIN_BTN[NUM_CONTROLES]   = { BTN_C1, BTN_C2, BTN_C3, BTN_C4 };
static const uint8_t PIN_STICK[NUM_CONTROLES] = { SW_C1,  SW_C2,  SW_C3,  SW_C4  };

bool btnEstable[NUM_CONTROLES]      = { false };  // true = presionado (estable)
bool btnFlanco[NUM_CONTROLES]       = { false };  // true un frame al presionar
bool btnStickEstable[NUM_CONTROLES] = { false };
bool btnStickFlanco[NUM_CONTROLES]  = { false };

// Un boton cualquiera: la ultima lectura cruda y cuando cambio. Como ahora hay
// ocho botones con el mismo tratamiento, el filtro vive en un solo lugar.
struct EstadoBoton { bool prev; uint32_t cambio; };
static EstadoBoton estBtn[NUM_CONTROLES]   = {};
static EstadoBoton estStick[NUM_CONTROLES] = {};

static void filtrarBoton(uint8_t pin, EstadoBoton& est, bool& estable, bool& flanco) {
  uint32_t ahora = millis();
  flanco = false;
  bool raw = (digitalRead(pin) == LOW);            // apretado = LOW
  if (raw != est.prev) {
    est.prev   = raw;
    est.cambio = ahora;
  }
  if (ahora - est.cambio >= DEBOUNCE_MS && raw != estable) {
    estable = raw;
    if (estable) flanco = true;                    // flanco: recien presionado
  }
}

void actualizarBotones() {
  for (uint8_t i = 0; i < NUM_CONTROLES; i++) {
    filtrarBoton(PIN_BTN[i],   estBtn[i],   btnEstable[i],      btnFlanco[i]);
    filtrarBoton(PIN_STICK[i], estStick[i], btnStickEstable[i], btnStickFlanco[i]);
  }
}

// ---------- Potenciometro ----------
// Promedia unas lecturas para suavizar el jitter del ADC. Los extremos del pote
// quedaron soldados al reves respecto de como gira la perilla, asi que se
// invierte aca: todo el resto del codigo sigue viendo 0 = minimo, 4095 = maximo.
static const bool POTE_INVERTIDO = true;

uint16_t leerPoteCrudo() {
  uint32_t s = 0;
  for (uint8_t i = 0; i < 4; i++) s += analogRead(POT_PIN);
  uint16_t v = s / 4;
  return POTE_INVERTIDO ? (uint16_t)(4095 - v) : v;
}

// ---------- Joysticks: dos ADS1115 en bus propio ----------
// Reparto de canales. Cada modulo se lleva dos controles, y de cada control el
// canal par es el eje de la tira y el impar el transversal:
//
//   0x48  A0=C1 tira  A1=C1 cruz  A2=C2 tira  A3=C2 cruz
//   0x49  A0=C3 tira  A1=C3 cruz  A2=C4 tira  A3=C4 cruz
static const uint8_t ADS_DIR[2] = { ADS_ADDR_A, ADS_ADDR_B };
static bool adsPresente[2] = { false, false };

static const uint8_t ADS_REG_CONV = 0x00;
static const uint8_t ADS_REG_CONF = 0x01;

// Config fija de cada conversion; lo unico que cambia entre canales es el MUX.
static const uint16_t ADS_CONF_BASE =
      0x8000    // OS = 1: arrancar la conversion ahora
    | 0x0200    // PGA = +-4.096 V (el joystick va a 3.3V, entra justo)
    | 0x0100    // MODE = single-shot
    | 0x00E0    // DR = 860 SPS -> 1,16 ms. El default de 128 SPS tarda 8 ms por
                //      canal, o sea 64 ms los ocho ejes: injugable.
    | 0x0003;   // comparador deshabilitado
static const uint16_t ADS_CONF_MUX[4] = { 0x4000, 0x5000, 0x6000, 0x7000 };

static const uint32_t ADS_CONV_US = 1300;   // 1,16 ms de conversion + margen

// Fondo de escala MEDIDO, no teorico: alimentado a 3.3V el HW-504 llega a
// ~25100 cuentas (3,13 V) porque el gimbal no barre la pista entera del pote.
// Escalando por este numero, el centro del stick cae en ~2050 -> el mismo 2048
// que ya usaba el ADC interno, y ninguna constante de juego hay que retocar.
static const int32_t ADS_FONDO_ESCALA = 25100;

// Los controles son caseros y alguno pudo quedar montado para el lado
// contrario. Se corrige aca, en un solo lugar, en vez de repartir signos por el
// codigo de cada juego. Medido con el sketch de puesta a punto: en C1 los dos
// ejes ya crecen en el sentido bueno.
static const bool JOY_INVERTIDO[NUM_CONTROLES][NUM_EJES] = {
  { false, false },   // C1
  { false, false },   // C2
  { false, false },   // C3
  { false, false },   // C4
};

static const uint16_t JOY_CENTRO_NOMINAL = 2048;  // centro teorico de 12 bits

// Banda en la que se acepta un centro recien medido. Los ocho ejes medidos
// reposan entre 1989 y 2107, asi que esto deja margen de sobra y sigue
// descartando una calibracion hecha con el stick agarrado.
static const uint16_t JOY_CENTRO_MIN = 1600;
static const uint16_t JOY_CENTRO_MAX = 2500;

// PRESENCIA es una pregunta distinta de CENTRO VALIDO, y confundirlas costaba
// caro. Un canal sin nada conectado flota en ~765 (medido: 754 a 777 en los
// ocho); un stick agarrado a fondo da ~0 o ~4095, nunca esa franja. Preguntando
// "el centro es plausible?" para decidir presencia, cualquiera que empezara la
// partida con el stick torcido quedaba marcado como desenchufado y afuera del
// juego -- que en una mesa con cuatro personas manoseando los controles es algo
// que pasa siempre. Preguntando "esta flotando?" hace falta que los DOS ejes
// caigan en la franja del aire a la vez, que un joystick real no hace.
static const uint16_t JOY_AIRE_MIN = 600;
static const uint16_t JOY_AIRE_MAX = 950;

uint16_t JOY_MUERTA_JUEGO = 60;    // zona muerta moviendo al jugador
// Zona muerta ancha para el menu: elegir juego no necesita precision y asi el
// menu no se mueve solo.
uint16_t JOY_MUERTA_MENU  = 400;
// Auto-repeat manteniendo el stick al costado. Despues de unos pasos seguidos
// acelera: con doce entradas en el menu, cruzar la lista al ritmo lento se hace
// eterno, pero arrancar rapido haria que un toque suelto se pase de largo.
const uint16_t MENU_REPETIR_MS     = 350;
const uint16_t MENU_REPETIR_RAPIDO = 140;
const uint8_t  MENU_PASOS_ACELERA  = 3;    // pasos seguidos antes de acelerar

// Ultima lectura de cada eje, ya escalada a 0..4095, y su centro. Se miden los
// cuatro en setup() y cada juego remide el suyo al empezar la partida.
static uint16_t joyVal[NUM_CONTROLES][NUM_EJES];
static uint16_t joyCentro[NUM_CONTROLES][NUM_EJES];
// false = ese eje no se puede leer (modulo o control ausente). Devuelve 0
// siempre, asi un control desenchufado no arrastra al jugador.
static bool joyOk[NUM_CONTROLES][NUM_EJES];
// Si el control esta enchufado. Se decide por la franja del aire, no por el
// centro: ver JOY_AIRE_MIN.
static bool joyEnchufado[NUM_CONTROLES];

// ---- Acceso al chip ----
static bool adsEscribirConf(uint8_t addr, uint16_t v) {
  Wire1.beginTransmission(addr);
  Wire1.write(ADS_REG_CONF);
  Wire1.write((uint8_t)(v >> 8));
  Wire1.write((uint8_t)(v & 0xFF));
  return Wire1.endTransmission() == 0;
}

static bool adsLeerConversion(uint8_t addr, int16_t& out) {
  Wire1.beginTransmission(addr);
  Wire1.write(ADS_REG_CONV);
  if (Wire1.endTransmission() != 0) return false;
  if (Wire1.requestFrom((int)addr, 2) != 2) return false;
  // Los dos read() van en lineas separadas a proposito: dentro de una misma
  // expresion el orden de evaluacion no esta garantizado y los bytes se darian
  // vuelta.
  uint8_t hi = Wire1.read();
  uint8_t lo = Wire1.read();
  out = (int16_t)(((uint16_t)hi << 8) | lo);
  return true;
}

// Cuentas del ADS -> el dominio 0..4095 que usa toda la consola. Se recorta
// porque otro joystick puede barrer un poco mas que el que se midio.
static uint16_t adsEscalar(int16_t raw) {
  int32_t v = ((int32_t)raw * 4095) / ADS_FONDO_ESCALA;
  if (v < 0)    v = 0;
  if (v > 4095) v = 4095;
  return (uint16_t)v;
}

// ---- Barrido no bloqueante ----
// Una "ronda" es el mismo numero de canal en los dos chips: se arrancan las dos
// conversiones juntas y despues se recogen las dos, asi los ocho ejes salen en
// cuatro rondas y no en ocho.
static uint8_t  adsRonda   = 0;
static uint32_t adsDesde   = 0;
static bool     adsEnCurso = false;

static void adsGuardar(uint8_t modulo, uint8_t canal, int16_t raw) {
  joyVal[modulo * 2 + canal / 2][canal % 2] = adsEscalar(raw);
}

static void adsArrancarRonda() {
  bool alguno = false;
  for (uint8_t m = 0; m < 2; m++) {
    if (!adsPresente[m]) continue;
    if (adsEscribirConf(ADS_DIR[m], ADS_CONF_BASE | ADS_CONF_MUX[adsRonda])) alguno = true;
  }
  adsEnCurso = alguno;
  adsDesde   = micros();
}

void actualizarJoysticks() {
  if (adsEnCurso) {
    if (micros() - adsDesde < ADS_CONV_US) return;   // todavia convirtiendo
    for (uint8_t m = 0; m < 2; m++) {
      if (!adsPresente[m]) continue;
      int16_t raw;
      if (adsLeerConversion(ADS_DIR[m], raw)) adsGuardar(m, adsRonda, raw);
    }
    adsRonda = (adsRonda + 1) & 3;
  }
  adsArrancarRonda();
}

// ---- Arranque y calibracion ----
void iniciarJoysticks() {
  Wire1.begin(ADS_SDA, ADS_SCL, ADS_FREQ);
  for (uint8_t m = 0; m < 2; m++) {
    Wire1.beginTransmission(ADS_DIR[m]);
    adsPresente[m] = (Wire1.endTransmission() == 0);
    Serial.printf("[joysticks] ADS1115 0x%02X: %s\n",
                  ADS_DIR[m], adsPresente[m] ? "ok" : "AUSENTE");
  }
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    joyEnchufado[j] = false;
    for (uint8_t e = 0; e < NUM_EJES; e++) {
      joyVal[j][e]    = JOY_CENTRO_NOMINAL;
      joyCentro[j][e] = JOY_CENTRO_NOMINAL;
      joyOk[j][e]     = false;
    }
  }
  adsRonda   = 0;
  adsEnCurso = false;
}

// Lectura bloqueante de un canal. Solo la usa la calibracion, que corre entre
// partidas y puede permitirse esperar; el juego lee del barrido no bloqueante.
static bool adsLeerCanalYa(uint8_t modulo, uint8_t canal, int16_t& out) {
  if (!adsEscribirConf(ADS_DIR[modulo], ADS_CONF_BASE | ADS_CONF_MUX[canal])) return false;
  delayMicroseconds(ADS_CONV_US);
  return adsLeerConversion(ADS_DIR[modulo], out);
}

void calibrarJoy(uint8_t jugador) {
  uint8_t modulo = jugador / 2;
  if (!adsPresente[modulo]) {
    joyEnchufado[jugador] = false;
    joyOk[jugador][EJE_TIRA] = joyOk[jugador][EJE_CRUZ] = false;
    return;
  }

  // Medir primero los dos ejes, decidir despues: la presencia se resuelve
  // mirando los dos juntos, no eje por eje.
  uint16_t medido[NUM_EJES];
  bool     hayLectura[NUM_EJES] = { false, false };

  for (uint8_t e = 0; e < NUM_EJES; e++) {
    uint8_t  canal = (jugador % 2) * 2 + e;
    uint32_t suma  = 0;
    uint8_t  n     = 0;
    for (uint8_t i = 0; i < 8; i++) {
      int16_t raw;
      if (adsLeerCanalYa(modulo, canal, raw)) { suma += adsEscalar(raw); n++; }
    }
    if (n == 0) continue;
    medido[e]     = suma / n;
    hayLectura[e] = true;
  }
  adsEnCurso = false;   // el barrido quedo a mitad de camino: que reempiece

  if (!hayLectura[EJE_TIRA] || !hayLectura[EJE_CRUZ]) {
    joyEnchufado[jugador] = false;
    joyOk[jugador][EJE_TIRA] = joyOk[jugador][EJE_CRUZ] = false;
    return;
  }

  // Desenchufado = los DOS ejes flotando. Con uno solo en la franja no alcanza:
  // podria ser un stick de verdad pasando por ahi.
  bool aire = true;
  for (uint8_t e = 0; e < NUM_EJES; e++) {
    if (medido[e] < JOY_AIRE_MIN || medido[e] > JOY_AIRE_MAX) aire = false;
  }
  joyEnchufado[jugador] = !aire;
  if (aire) {
    joyOk[jugador][EJE_TIRA] = joyOk[jugador][EJE_CRUZ] = false;
    return;
  }

  for (uint8_t e = 0; e < NUM_EJES; e++) {
    // Un centro implausible quiere decir que estaban tocando el stick justo
    // ahora. Se DESCARTA la medicion y se deja el centro anterior en pie, que
    // es mejor que caer al nominal: los centros reales van de 1989 a 2107, asi
    // que el nominal puede errarle 60 cuentas -- la zona muerta entera.
    if (medido[e] >= JOY_CENTRO_MIN && medido[e] <= JOY_CENTRO_MAX) {
      joyCentro[jugador][e] = medido[e];
      joyVal[jugador][e]    = medido[e];
    }
    joyOk[jugador][e] = true;
  }
}

void calibrarJoys() {
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) calibrarJoy(j);
}

bool controlPresente(uint8_t jugador) {
  return joyEnchufado[jugador];
}

uint8_t numControles() {
  uint8_t n = 0;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) if (controlPresente(j)) n++;
  return n;
}

// ---------- Selector de cantidad de jugadores ----------
uint8_t jugadoresSugeridos() {
  uint8_t n = numControles();
  if (n < 2) n = 2;
  if (n > NUM_CONTROLES) n = NUM_CONTROLES;
  return n;
}

bool loopSelectorJugadores(uint8_t& n) {
  int8_t paso = joystickPaso(0);
  if (paso) {
    int8_t v = (int8_t)n + paso;
    if (v < 2)              v = NUM_CONTROLES;   // da la vuelta en los dos sentidos
    if (v > NUM_CONTROLES)  v = 2;
    n = (uint8_t)v;
    beep(1200, 25);
  }
  if (btnFlanco[0]) return true;

  // La tira repartida en n franjas, una del color de cada control que va a
  // jugar: se ve de un vistazo quienes entran, sin leer el LCD.
  FastLED.clear();
  for (uint8_t j = 0; j < n; j++) {
    int16_t ini = (int16_t)(((int32_t)LARGO_TIRA * j) / n);
    int16_t fin = (int16_t)(((int32_t)LARGO_TIRA * (j + 1)) / n) - 1;
    for (int16_t i = ini; i <= fin; i++) setLed(i, CONTROLES[j].color);
  }
  nscale8(leds, LARGO_TIRA, 60);
  FastLED.show();
  return false;
}

void lcdSelectorJugadores(const char* titulo, uint8_t n) {
  lcdLinea(0, titulo);
  lcdLinea(1, "< " + String(n) + " jugadores >");
}

// Lectura ya centrada y con el sentido corregido. La comparten leerJoyNorm,
// leerJoyCruz y joystickPaso, que solo se diferencian en la zona muerta y en
// como traducen la deflexion.
static int16_t desvioJoy(uint8_t jugador, uint8_t eje) {
  if (!joyOk[jugador][eje]) return 0;
  int16_t d = (int16_t)joyVal[jugador][eje] - (int16_t)joyCentro[jugador][eje];
  return JOY_INVERTIDO[jugador][eje] ? (int16_t)-d : d;
}

// Fuera de la zona muerta la deflexion arranca en 0 y crece hasta 1, asi no hay
// salto de velocidad al cruzar el borde.
static float leerEjeNorm(uint8_t jugador, uint8_t eje) {
  int16_t d = desvioJoy(jugador, eje);
  int16_t m = (d < 0) ? -d : d;
  if (m <= (int16_t)JOY_MUERTA_JUEGO) return 0.0f;
  float f = (float)(m - JOY_MUERTA_JUEGO) /
            (float)(joyCentro[jugador][eje] - JOY_MUERTA_JUEGO);
  if (f > 1.0f) f = 1.0f;
  return (d > 0) ? f : -f;
}

// Con la tira acostada, el eje que corre a lo largo de la tira pasa a ser el
// horizontal del stick y el transversal pasa a ser el vertical. La traduccion
// vive SOLO aca, en los dos lectores que usan los juegos: los menus, el
// selector de jugadores y la pantalla de las tres letras siguen con el eje
// fisico transversal, porque el texto del LCD se lee igual este la tira parada
// o acostada. JOY_INVERTIDO no se entera: se aplica sobre el eje fisico.
static uint8_t ejeFisico(uint8_t ejeLogico) {
  if (ORIENTACION != TIRA_HORIZONTAL) return ejeLogico;
  return (ejeLogico == EJE_TIRA) ? EJE_CRUZ : EJE_TIRA;
}

float leerJoyNorm(uint8_t jugador) { return leerEjeNorm(jugador, ejeFisico(EJE_TIRA)); }
float leerJoyCruz(uint8_t jugador) { return leerEjeNorm(jugador, ejeFisico(EJE_CRUZ)); }

// Convierte la deflexion de un eje en pasos discretos: devuelve -1/0/+1. Da un
// paso al salir de la zona muerta y, si se mantiene el stick, repite cada
// MENU_REPETIR_MS. Sin esto el menu se iria de largo a 60 pasos por segundo.
//
// Los menus usan el eje TRANSVERSAL (joystickPaso): mover el stick a los
// costados es lo que se corresponde con recorrer una lista horizontal, y ademas
// deja el eje de la tira libre para lo que hace dentro de cada juego.
//
// El estado va por control y por eje. Entre pantallas se comparte, pero el
// menu, Highscores, el selector de jugadores y el cartel de las tres letras
// nunca estan activos a la vez.
static int8_t   joyPasoPrev[NUM_CONTROLES][NUM_EJES]      = {};  // sentido del stick el frame anterior
static uint32_t joyPasoDesde[NUM_CONTROLES][NUM_EJES]     = {};  // millis() del ultimo paso entregado
static uint8_t  joyPasosSeguidos[NUM_CONTROLES][NUM_EJES] = {};

int8_t joystickPasoEje(uint8_t jugador, uint8_t eje) {
  int16_t d = desvioJoy(jugador, eje);
  int8_t  dir = 0;
  if (d >  (int16_t)JOY_MUERTA_MENU) dir = +1;
  if (d < -(int16_t)JOY_MUERTA_MENU) dir = -1;

  if (dir == 0) { joyPasoPrev[jugador][eje] = 0; return 0; }  // al centro: listo para el proximo paso

  uint32_t ahora = millis();
  if (dir != joyPasoPrev[jugador][eje]) {                     // recien salio de la zona muerta
    joyPasoPrev[jugador][eje]      = dir;
    joyPasoDesde[jugador][eje]     = ahora;
    joyPasosSeguidos[jugador][eje] = 1;
    return dir;
  }
  uint16_t espera = (joyPasosSeguidos[jugador][eje] >= MENU_PASOS_ACELERA) ? MENU_REPETIR_RAPIDO
                                                                           : MENU_REPETIR_MS;
  if (ahora - joyPasoDesde[jugador][eje] >= espera) {         // lo mantiene: auto-repeat
    joyPasoDesde[jugador][eje] = ahora;
    if (joyPasosSeguidos[jugador][eje] < 255) joyPasosSeguidos[jugador][eje]++;
    return dir;
  }
  return 0;
}

int8_t joystickPaso(uint8_t jugador) { return joystickPasoEje(jugador, EJE_CRUZ); }

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
  if (SILENCIO) return;
  jingleLen = 0;                    // un beep corta cualquier melodia
  ledcWriteTone(BUZZER_CH, freq);
  buzzerHasta   = millis() + dur;
  buzzerSonando = true;
}

void tocarJingle(const Nota* notas, uint8_t len) {
  if (SILENCIO) return;
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
  lcdCrudo(0, "    PixeLED");
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
//                                                        menor  por
//   clave      nombre           prefijo   unidad         esMejor largo
const RecordDef RECORDS[] = {
  { "hsPong",   "Pong",          "",       " golpes",   false,  false },
  { "hsTug",    "Tira y Afloja", "",       " empujes",  false,  true  },   // mas tira que empujar
  { "hsRc",     "Rompecolores",  "",       " pts",      false,  true  },   // el muro tarda el doble
  { "hsTwang",  "Twang",         "Nivel ", "",          false,  true  },   // mazmorra del doble
  { "hsTwCoop", "Twang coop",    "Nivel ", "",          false,  true  },   // de a dos se llega mas lejos
  { "hsTw32",   "Twang32",       "",       " pts",      false,  true  },   // el puntaje canonico: vidas que sobran
  { "hsPaddle", "Paddle",        "",       " pts",      false,  false },
  { "hsStack",  "Stacker",       "Piso ",  "",          false,  false },
  { "hsEsq",    "Salta Muros",   "",       " muros",    false,  true  },   // el doble de muros
  { "hsLander", "Alunizaje",     "",       " aluniza.", false,  false },
  { "hsDuelo",  "Reaccion",      "",       " ms",       true,   false },   // gana el tiempo mas CHICO
  { "hsCarr",   "Carrera",       "",       " ms/vta",   true,   true  },   // idem: mejor vuelta
  { "hsPelea",  "Pelea",         "",       " golpes",   false,  false },
  { "hsWest",   "Tiros",         "",       " cruces",   false,  false },   // balas anuladas en el aire
};
static_assert(sizeof(RECORDS) / sizeof(RECORDS[0]) == NUM_RECORDS,
              "RECORDS[] y el enum Record quedaron desincronizados");

uint32_t hsValor[NUM_RECORDS];
char     hsNombre[NUM_RECORDS][LARGO_NOMBRE + 1];
static Preferences prefs;
static bool prefsListas = false;

// La NVS la comparten los records y los ajustes, y begin() se llama una sola
// vez: llamarlo dos veces sobre el mismo handle abre otro y pierde el anterior.
static void abrirPrefs() {
  if (prefsListas) return;
  prefs.begin("gasti", false);      // ojo: "gasti" es el namespace viejo, ver iniciarRecords()
  prefsListas = true;
}

static int8_t  recPend    = -1;  // record batido al que todavia le falta el nombre
static uint8_t recPendJug = 0;   // el control que lo hizo: es el que lo va a firmar

// La clave del valor es la de la tabla en la tira corta, y esa misma con una "L"
// pegada en la larga, solo para los records que dependen del largo. Asi los
// records ya grabados siguen siendo los de la tira de 100, sin migrar nada.
//
// La del nombre es la del valor mas una "N" ("hsPong" -> "hsPongN", "hsCarrL" ->
// "hsCarrLN"). Todo entra holgado en los 15 caracteres que admite la NVS.
static String claveValor(uint8_t rec) {
  String k = RECORDS[rec].clave;
  if (RECORDS[rec].porLargo && LARGO_TIRA >= 200) k += "L";
  return k;
}
static String claveNombre(uint8_t rec) { return claveValor(rec) + "N"; }

// Trae de la NVS el valor y las iniciales de UN record, los del largo actual.
static void cargarRecord(uint8_t i) {
  hsValor[i] = prefs.getUInt(claveValor(i).c_str(), 0);

  // Un record sin firmar no tiene clave de nombre, que es lo normal hasta que
  // alguien lo bate. Se pregunta con isKey() antes de leerlo por dos razones:
  // getString no toca el buffer si la clave falta --habria que vaciarlo igual--
  // y ademas loguea a nivel ERROR, llenando el monitor serie en cada arranque
  // con una linea por cada record sin firmar. Ruido de error que no es error
  // termina en errores de verdad que nadie mira.
  hsNombre[i][0] = '\0';
  String claveN = claveNombre(i);
  if (prefs.isKey(claveN.c_str())) {
    prefs.getString(claveN.c_str(), hsNombre[i], sizeof(hsNombre[i]));
  }
}

void recargarRecords() {
  for (uint8_t i = 0; i < NUM_RECORDS; i++) {
    if (RECORDS[i].porLargo) cargarRecord(i);
  }
}

void iniciarRecords() {
  // La primera vez que se enciende la placa no existe la clave todavia y
  // getUInt devuelve el 0 por defecto, que es justamente "sin record".
  //
  // OJO: "gasti" es el namespace de NVS, NO un nombre visible. Quedo del nombre
  // viejo de la consola y se deja asi a proposito: renombrarlo dejaria
  // huerfanos todos los records ya grabados en la flash.
  abrirPrefs();
  for (uint8_t i = 0; i < NUM_RECORDS; i++) cargarRecord(i);
}

bool intentarRecord(uint8_t rec, uint32_t valor, uint8_t jugador) {
  if (valor == 0) return false;              // 0 esta reservado para "sin record"
  uint32_t actual = hsValor[rec];
  bool mejor = RECORDS[rec].menorEsMejor ? (actual == 0 || valor < actual)
                                         : (valor > actual);
  if (!mejor) return false;
  hsValor[rec] = valor;
  prefs.putUInt(claveValor(rec).c_str(), valor);
  // Las iniciales que habia son del duenio anterior y ya no valen. Se borran
  // aca mismo, y no cuando se guardan las nuevas, para que un corte de luz en
  // el medio deje el record sin nombre y no con el nombre equivocado.
  hsNombre[rec][0] = '\0';
  prefs.putString(claveNombre(rec).c_str(), "");
  recPend    = (int8_t)rec;
  recPendJug = (jugador < NUM_CONTROLES) ? jugador : 0;
  return true;
}

int8_t  recordPendiente()  { return recPend; }
uint8_t jugadorPendiente() { return recPendJug; }

void ponerNombreRecord(uint8_t rec, const char* nombre) {
  strncpy(hsNombre[rec], nombre, LARGO_NOMBRE);
  hsNombre[rec][LARGO_NOMBRE] = '\0';
  prefs.putString(claveNombre(rec).c_str(), hsNombre[rec]);
  recPend = -1;
}

// El valor del record ya quedo grabado cuando se batio: lo unico que se pierde
// al cancelar son las iniciales.
void cancelarNombreRecord() { recPend = -1; }

String textoRecord(uint8_t rec) {
  if (hsValor[rec] == 0) return "---";
  return String(RECORDS[rec].prefijo) + String(hsValor[rec]) + String(RECORDS[rec].unidad);
}

// ---------- Ajustes: largo de la tira, orientacion y silencio ----------
// Con 200 LEDs la misma escena consume el doble, asi que la tira larga arranca
// mas abajo de brillo y el limitador de FastLED se encarga del resto. Cambiar
// el largo pisa el brillo con este valor: si despues se tunea por web, ese
// numero es el que manda hasta el proximo cambio de largo.
static uint16_t brilloDe(uint16_t largo) { return (largo >= 200) ? 100 : 128; }

void iniciarAjustes() {
  abrirPrefs();
  uint16_t largo = prefs.getUShort("largo", 100);
  LARGO_TIRA  = (largo >= 200) ? 200 : 100;      // solo dos valores validos
  ORIENTACION = prefs.getUChar("orient", TIRA_VERTICAL) ? TIRA_HORIZONTAL : TIRA_VERTICAL;
  BRILLO      = brilloDe(LARGO_TIRA);
  SILENCIO    = false;                           // este no se guarda a proposito
}

void ponerLargoTira(uint16_t largo) {
  largo = (largo >= 200) ? 200 : 100;
  if (largo == LARGO_TIRA) return;

  // Al acortar hay que apagar la cola ANTES de cambiar el largo: si no, los
  // LEDs que dejan de usarse se quedan prendidos con lo ultimo que mostraron,
  // porque ya nadie les vuelve a mandar datos.
  fill_solid(leds, LEDS_MAX, CRGB::Black);
  FastLED.show();

  LARGO_TIRA = largo;
  FastLED[0].setLeds(leds, LARGO_TIRA);
  BRILLO = brilloDe(LARGO_TIRA);
  FastLED.setBrightness((uint8_t)BRILLO);
  FastLED.clear(true);

  prefs.putUShort("largo", LARGO_TIRA);
  recargarRecords();     // los records que dependen del largo son otros ahora
}

void ponerOrientacion(uint8_t orient) {
  ORIENTACION = (orient == TIRA_HORIZONTAL) ? TIRA_HORIZONTAL : TIRA_VERTICAL;
  prefs.putUChar("orient", (uint8_t)ORIENTACION);
}

void ponerSilencio(bool s) {
  SILENCIO = s;
  if (!s) return;
  ledcWriteTone(BUZZER_CH, 0);      // corta en seco lo que estuviera sonando
  buzzerSonando = false;
  jingleLen     = 0;
}

// ---------- Escala de los juegos proporcionales ----------
// Todo lo que entra aca esta expresado en LEDs (o en LEDs/s, o en ms por LED)
// de una tira de 100. Con la tira corta las tres funciones son la identidad.
int16_t  escalaLeds(int16_t leds100)      { return (int16_t)(((int32_t)leds100 * LARGO_TIRA) / 100); }

// Sirve igual para LEDs/s y para LEDs/s^2 (la gravedad y el empuje del
// Alunizaje): cualquier magnitud "por LED" crece con el largo en la misma
// proporcion, asi que caer la tira entera sigue tardando lo mismo.
uint16_t escalaVel(uint16_t ledsPorSeg)   { return (uint16_t)(((uint32_t)ledsPorSeg * LARGO_TIRA) / 100); }

// El periodo va al reves que la velocidad: con el doble de LEDs, cada paso dura
// la mitad para que la pelota tarde lo mismo en cruzar. Nunca devuelve 0, que
// seria un movimiento sin espera ninguna.
uint16_t escalaMsPorLed(uint16_t ms) {
  uint32_t r = ((uint32_t)ms * 100) / LARGO_TIRA;
  return (r > 0) ? (uint16_t)r : 1;
}

// ---------- Guardado generico en NVS ----------
size_t guardarBlob(const char* clave, const void* datos, size_t n) {
  abrirPrefs();
  return prefs.putBytes(clave, datos, n);
}

size_t leerBlob(const char* clave, void* datos, size_t n) {
  abrirPrefs();
  return prefs.getBytes(clave, datos, n);
}
