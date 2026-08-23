/*********
  PRUEBA DE CONTROLES — los cuatro, completos.

  Sketch de verificacion del cableado de los cuatro controles (joystick HW-504
  de dos ejes + boton arcade, ficha DB9). NO es parte de la consola: se compila
  solo, con su propio entorno de PlatformIO.

    ~/.platformio/penv/bin/pio run -e test-controles -t upload -t monitor

  Ojo: esto PISA el firmware de la consola. Para volver, "pio run -t upload".

  Que contesta, de un vistazo:
    1. Aparecen los dos ADS1115 en el bus?
    2. Cada control mueve SU fila y no la del vecino? (DB9 bien cableado)
    3. Cada stick barre de 0 a 4095 en los dos ejes?
    4. Responden los ocho botones, cada uno en su control?

  Cableado que asume (ver la nota de hardware):
    Bus I2C dedicado: SDA=GPIO23, SCL=GPIO26, 3.3V, 400 kHz
    Modulo 0x48 -> C1 y C2      Modulo 0x49 -> C3 y C4
    De cada modulo: A0/A1 = ejes del primer control, A2/A3 = del segundo
    DB9  1=GND  3=3V3  5=SW del stick  7=VRy(cruz)  8=arcade  9=VRx(tira)
*********/

#include <Arduino.h>
#include <Wire.h>

// ---------- Hardware bajo prueba ----------
#define ADS_SDA   23
#define ADS_SCL   26
#define ADS_FREQ  400000UL

#define NUM_CONTROLES 4
#define EJE_TIRA 0              // VRx: + hacia el final de la tira
#define EJE_CRUZ 1              // VRy: + hacia la derecha

static const uint8_t PIN_ARCADE[NUM_CONTROLES] = { 14, 27, 32, 33 };
static const uint8_t PIN_STICK [NUM_CONTROLES] = {  4, 13, 17, 19 };

static const uint8_t ADS_DIR[2] = { 0x48, 0x49 };
static bool adsPresente[2] = { false, false };

// ---------- Registros del ADS1115 ----------
static const uint8_t REG_CONV = 0x00;
static const uint8_t REG_CONF = 0x01;

// Config fija de cada conversion; lo unico que cambia entre canales es el MUX.
static const uint16_t CONF_BASE =
      0x8000    // OS = 1: arrancar la conversion ahora
    | 0x0200    // PGA = +-4.096 V (el joystick va a 3.3V, entra justo)
    | 0x0100    // MODE = single-shot
    | 0x00E0    // DR = 860 SPS -> 1,16 ms por conversion
    | 0x0003;   // comparador deshabilitado
static const uint16_t CONF_MUX[4] = { 0x4000, 0x5000, 0x6000, 0x7000 };

// Fondo de escala MEDIDO, no teorico: alimentado a 3.3V el HW-504 llega a
// ~25100 cuentas (3,13 V) porque el gimbal no barre la pista entera del pote.
// Escalando por este numero el centro cae en ~2050, o sea el 2048 nominal.
static const int32_t CUENTAS_FS = 25100;

// Un canal al aire flota en ~765 (medido: 754 a 777 en los ocho). Un stick
// agarrado a fondo da ~0 o ~4095, nunca esa franja, asi que preguntar "esta
// flotando?" no confunde un control torcido con uno desenchufado. Mismo criterio
// que usa calibrarJoy() en la consola.
static const uint16_t AIRE_MIN = 600;
static const uint16_t AIRE_MAX = 950;

// ---------- Acceso al chip ----------
static bool escribirReg(uint8_t addr, uint8_t reg, uint16_t v) {
  Wire1.beginTransmission(addr);
  Wire1.write(reg);
  Wire1.write((uint8_t)(v >> 8));
  Wire1.write((uint8_t)(v & 0xFF));
  return Wire1.endTransmission() == 0;
}

static bool leerReg(uint8_t addr, uint8_t reg, uint16_t& out) {
  Wire1.beginTransmission(addr);
  Wire1.write(reg);
  if (Wire1.endTransmission() != 0) return false;
  if (Wire1.requestFrom((int)addr, 2) != 2) return false;
  // Los dos read() van en lineas separadas a proposito: dentro de una misma
  // expresion el orden de evaluacion no esta garantizado y los bytes se darian
  // vuelta.
  uint8_t hi = Wire1.read();
  uint8_t lo = Wire1.read();
  out = ((uint16_t)hi << 8) | lo;
  return true;
}

// Bloqueante a proposito: para verificar cableado alcanza y sobra.
static bool leerCanal(uint8_t addr, uint8_t canal, int16_t& out) {
  if (!escribirReg(addr, REG_CONF, CONF_BASE | CONF_MUX[canal])) return false;

  uint16_t conf;
  uint32_t t0 = millis();
  do {
    if (millis() - t0 > 20) return false;        // se colgo: mejor avisar
    if (!leerReg(addr, REG_CONF, conf)) return false;
  } while ((conf & 0x8000) == 0);                // OS = 0 mientras convierte

  uint16_t v;
  if (!leerReg(addr, REG_CONV, v)) return false;
  out = (int16_t)v;
  return true;
}

// Cuentas del ADS -> el dominio 0..4095 que usa toda la consola.
static uint16_t escalar(int16_t raw) {
  int32_t v = ((int32_t)raw * 4095) / CUENTAS_FS;
  if (v < 0)    v = 0;
  if (v > 4095) v = 4095;
  return (uint16_t)v;
}

// ---------- Estado ----------
static uint16_t val [NUM_CONTROLES][2];
static uint16_t vMin[NUM_CONTROLES][2];
static uint16_t vMax[NUM_CONTROLES][2];
static bool     leido[NUM_CONTROLES][2];

static void resetearExtremos() {
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    for (uint8_t e = 0; e < 2; e++) { vMin[j][e] = 4095; vMax[j][e] = 0; }
  }
  Serial.println(">>> min/max reseteados\n");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    pinMode(PIN_ARCADE[j], INPUT_PULLUP);
    pinMode(PIN_STICK [j], INPUT_PULLUP);
  }

  Wire1.begin(ADS_SDA, ADS_SCL, ADS_FREQ);

  Serial.println("\n=== PRUEBA DE LOS 4 CONTROLES ===");
  Serial.printf("Bus dedicado: SDA=%d SCL=%d @%lu Hz\n", ADS_SDA, ADS_SCL, ADS_FREQ);
  for (uint8_t m = 0; m < 2; m++) {
    Wire1.beginTransmission(ADS_DIR[m]);
    adsPresente[m] = (Wire1.endTransmission() == 0);
    Serial.printf("  ADS1115 0x%02X: %s  (controles C%u y C%u)\n",
                  ADS_DIR[m], adsPresente[m] ? "ok" : "AUSENTE", m * 2 + 1, m * 2 + 2);
  }
  resetearExtremos();
  Serial.println("Cualquier boton de stick resetea los min/max.\n");
}

void loop() {
  // Leer los ocho ejes. Canal c del modulo m -> control m*2 + c/2, eje c%2.
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) leido[j][0] = leido[j][1] = false;

  for (uint8_t m = 0; m < 2; m++) {
    if (!adsPresente[m]) continue;
    for (uint8_t c = 0; c < 4; c++) {
      int16_t raw;
      if (!leerCanal(ADS_DIR[m], c, raw)) continue;
      uint8_t j = m * 2 + c / 2;
      uint8_t e = c % 2;
      uint16_t v = escalar(raw);
      val[j][e]   = v;
      leido[j][e] = true;
      if (v < vMin[j][e]) vMin[j][e] = v;
      if (v > vMax[j][e]) vMax[j][e] = v;
    }
  }

  // Cualquier boton de stick resetea los extremos, para medir un control por
  // vez sin tener que reiniciar la placa.
  bool arcade[NUM_CONTROLES], stick[NUM_CONTROLES];
  static bool stickPrev = false;
  bool algunStick = false;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    arcade[j] = (digitalRead(PIN_ARCADE[j]) == LOW);
    stick [j] = (digitalRead(PIN_STICK [j]) == LOW);
    if (stick[j]) algunStick = true;
  }
  if (algunStick && !stickPrev) resetearExtremos();
  stickPrev = algunStick;

  Serial.println("        TIRA  [ min - max ]     CRUZ  [ min - max ]   ARC  SW");
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!leido[j][EJE_TIRA] && !leido[j][EJE_CRUZ]) {
      Serial.printf("  C%u    -- modulo ausente --\n", j + 1);
      continue;
    }
    // Desenchufado = los DOS ejes flotando a la vez.
    bool centrado = !((val[j][EJE_TIRA] >= AIRE_MIN && val[j][EJE_TIRA] <= AIRE_MAX) &&
                      (val[j][EJE_CRUZ] >= AIRE_MIN && val[j][EJE_CRUZ] <= AIRE_MAX));
    Serial.printf("  C%u    %4u  [%4u -%5u]     %4u  [%4u -%5u]    %c    %c   %s\n",
                  j + 1,
                  val[j][EJE_TIRA], vMin[j][EJE_TIRA], vMax[j][EJE_TIRA],
                  val[j][EJE_CRUZ], vMin[j][EJE_CRUZ], vMax[j][EJE_CRUZ],
                  arcade[j] ? '#' : '.', stick[j] ? '#' : '.',
                  centrado ? "" : "<- sin control?");
  }
  Serial.println();

  delay(250);   // ~4 refrescos por segundo: legible en el monitor serie
}
