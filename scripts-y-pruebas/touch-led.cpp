/*********
  Touch capacitivo -> LED y Buzzer
  - Toca el cable de GPIO4  (T0) -> se prende el LED (GPIO16).
  - Toca el cable de GPIO13 (T4) -> suena el buzzer (GPIO17).
*********/

#include <Arduino.h>

// --- Entradas tactiles ---
// Cada pad tactil es un GPIO. Evitamos T1=GPIO0, T2=GPIO2, T3=GPIO15 y
// T5=GPIO12 porque son pines de arranque/strapping y dan problemas.
// T4=GPIO13 es el siguiente pad libre y limpio despues de T0.
const int TOUCH_LED  = T0;   // GPIO4  -> controla el LED
const int TOUCH_BUZZ = T4;   // GPIO13 -> controla el buzzer

// --- Salidas ---
const int LED_PIN  = 16;     // GPIO16 -> resistencia 220 -> LED -> GND
const int BUZZ_PIN = 17;     // GPIO17 -> buzzer -> GND

// true  = buzzer ACTIVO  (el alto/sellado: suena con voltaje continuo)
// false = buzzer PASIVO  (el chato/con disco a la vista: necesita tono)
const bool BUZZER_ACTIVO = false;
const int  NOTA_HZ = 1000;   // frecuencia del tono si el buzzer es pasivo
const int  LEDC_CANAL = 0;   // canal LEDC (0-15) que genera el tono

// Umbrales: se calculan solos al arrancar (mitad del valor en reposo).
int umbralLed  = 40;
int umbralBuzz = 40;

// Recuerda el ultimo estado del buzzer para escribir solo cuando cambia.
bool buzzSonando = false;

// Lee el pad 20 veces sin tocar y devuelve la mitad del reposo como umbral.
int calibrar(int pin) {
  long base = 0;
  for (int i = 0; i < 20; i++) {
    base += touchRead(pin);
    delay(20);
  }
  base /= 20;
  return base / 2;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);

  // El buzzer pasivo se maneja con el periferico LEDC del ESP32, que es
  // el que genera la onda cuadrada. El activo es un simple on/off.
  if (BUZZER_ACTIVO) {
    pinMode(BUZZ_PIN, OUTPUT);
  } else {
    // Core 2.x: se configura un canal y se le engancha el pin.
    ledcSetup(LEDC_CANAL, NOTA_HZ, 10);   // canal, frecuencia, bits
    ledcAttachPin(BUZZ_PIN, LEDC_CANAL);  // el pin sale por ese canal
    ledcWriteTone(LEDC_CANAL, 0);         // arranca en silencio
  }

  // NO toques ningun cable durante esta calibracion (~1 segundo).
  umbralLed  = calibrar(TOUCH_LED);
  umbralBuzz = calibrar(TOUCH_BUZZ);
  Serial.printf("Umbral LED (GPIO4): %d | Umbral buzzer (GPIO13): %d\n",
                umbralLed, umbralBuzz);
  Serial.println("Toca los cables...");
}

void loop() {
  bool tocaLed  = touchRead(TOUCH_LED)  < umbralLed;
  bool tocaBuzz = touchRead(TOUCH_BUZZ) < umbralBuzz;

  // LED: on/off directo.
  digitalWrite(LED_PIN, tocaLed ? HIGH : LOW);

  // Buzzer: escribimos solo cuando cambia el estado.
  if (tocaBuzz != buzzSonando) {
    buzzSonando = tocaBuzz;
    if (BUZZER_ACTIVO) {
      digitalWrite(BUZZ_PIN, tocaBuzz ? HIGH : LOW);
    } else {
      ledcWriteTone(LEDC_CANAL, tocaBuzz ? NOTA_HZ : 0);
    }
  }

  delay(30);
}
