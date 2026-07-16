/*********
  Scanner I2C - recorre todas las direcciones y dice cuales responden.
  Sirve para saber la direccion real del LCD 1602.
  Mira el monitor serie a 115200.
*********/

#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(21, 22);   // SDA=GPIO21, SCL=GPIO22
  Serial.println("\nScanner I2C iniciado...");
}

void loop() {
  int encontrados = 0;
  Serial.println("Buscando dispositivos...");

  for (byte dir = 1; dir < 127; dir++) {
    Wire.beginTransmission(dir);
    if (Wire.endTransmission() == 0) {
      Serial.print("  Dispositivo encontrado en 0x");
      if (dir < 16) Serial.print("0");
      Serial.println(dir, HEX);
      encontrados++;
    }
  }

  if (encontrados == 0)
    Serial.println("  Nada. Revisar cableado SDA/SCL y alimentacion.");
  else
    Serial.println("Fin del scan.");

  delay(3000);
}
