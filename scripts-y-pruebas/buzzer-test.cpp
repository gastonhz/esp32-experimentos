/*********
  Test MINIMO de buzzer pasivo - sin touch, sin nada mas.
  Alterna dos tonos fuertes para verificar que el buzzer suena.
  Buzzer: una pata a GPIO17, la otra a GND.
*********/

#include <Arduino.h>

const int BUZZ_PIN  = 17;
const int LEDC_CANAL = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  // canal, frecuencia inicial, bits de resolucion
  ledcSetup(LEDC_CANAL, 2000, 10);
  ledcAttachPin(BUZZ_PIN, LEDC_CANAL);
  Serial.println("Sonando... deberias oir dos tonos alternados.");
}

void loop() {
  // Los piezo pasivos suenan MUCHO mas fuerte cerca de su resonancia
  // (tipico 2-3 kHz). Alternamos dos notas para que sea obvio.
  ledcWriteTone(LEDC_CANAL, 2000);
  Serial.println("2000 Hz");
  delay(500);

  ledcWriteTone(LEDC_CANAL, 2700);
  Serial.println("2700 Hz");
  delay(500);
}
