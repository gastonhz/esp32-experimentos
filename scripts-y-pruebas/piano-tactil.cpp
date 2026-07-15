/*********
  Piano tactil - cada pad toca una nota en el buzzer pasivo.
  Toca un cable (o una fruta con un cocodrilo) conectado a cada pad.
  Buzzer pasivo: una pata a GPIO17, la otra a GND.
  LED opcional: GPIO16 -> resistencia 220 -> LED -> GND (parpadea al tocar).
*********/

#include <Arduino.h>

// --- Salidas ---
const int BUZZ_PIN   = 17;   // buzzer pasivo
const int LEDC_CANAL = 0;    // canal LEDC que genera el tono
const int LED_PIN    = 16;   // LED que se prende con cualquier nota

// --- Teclas ---
// Cada tecla = un pad tactil + su nota (Hz). Usamos solo pads "limpios"
// (evitamos strapping: GPIO0/2/5/12/15). Podes agregar o sacar teclas
// libremente: el codigo se adapta al tamano del array.
struct Tecla {
  int  pad;         // pad tactil (T0..T9)
  int  frecuencia;  // Hz de la nota
  const char* nombre;
};

Tecla teclas[] = {
  { T0, 262, "Do" },   // GPIO4
  { T4, 294, "Re" },   // GPIO13
  { T6, 330, "Mi" },   // GPIO14
  { T7, 349, "Fa" },   // GPIO27
  { T8, 392, "Sol" },  // GPIO33
  { T9, 440, "La" },   // GPIO32
};
const int N_TECLAS = sizeof(teclas) / sizeof(teclas[0]);

// Umbral por tecla, calculado solo al arrancar.
int umbral[N_TECLAS];

// Lee un pad 20 veces sin tocar y devuelve la mitad del reposo.
int calibrar(int pad) {
  long base = 0;
  for (int i = 0; i < 20; i++) {
    base += touchRead(pad);
    delay(20);
  }
  return (base / 20) / 2;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  ledcSetup(LEDC_CANAL, 440, 10);
  ledcAttachPin(BUZZ_PIN, LEDC_CANAL);
  ledcWriteTone(LEDC_CANAL, 0);   // silencio

  // NO toques ningun pad durante la calibracion (~1 segundo por tecla).
  Serial.println("Calibrando... no toques nada.");
  for (int i = 0; i < N_TECLAS; i++) {
    umbral[i] = calibrar(teclas[i].pad);
  }
  Serial.println("Listo. Toca las teclas!");
}

void loop() {
  int notaSonando = 0;   // 0 = silencio
  const char* nombre = nullptr;

  // Recorremos las teclas; la primera que este tocada manda.
  for (int i = 0; i < N_TECLAS; i++) {
    if (touchRead(teclas[i].pad) < umbral[i]) {
      notaSonando = teclas[i].frecuencia;
      nombre = teclas[i].nombre;
      break;
    }
  }

  ledcWriteTone(LEDC_CANAL, notaSonando);      // 0 = silencio
  digitalWrite(LED_PIN, notaSonando ? HIGH : LOW);

  if (nombre) Serial.println(nombre);
  delay(20);
}
