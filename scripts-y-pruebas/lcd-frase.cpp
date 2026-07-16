/*********
  LCD 1602 por I2C - muestra una frase en las dos lineas.
  Conexion: GND->GND, VCC->5V, SDA->GPIO21, SCL->GPIO22.
*********/

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Direccion I2C tipica: 0x27. Si no anda, probar 0x3F (ver scanner abajo).
// 16 columnas x 2 filas.
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);   // SDA, SCL (son los default, se puede omitir)

  lcd.init();
  lcd.backlight();      // enciende la luz de fondo

  // Fila de arriba (fila 0) y fila de abajo (fila 1).
  lcd.setCursor(0, 0);  // columna 0, fila 0
  lcd.print("INGLATERRA LA CONCHA DE TU MADRE");
  lcd.setCursor(0, 1);  // columna 0, fila 1
  lcd.print("DALE MESSIIIIIIIII");
}

void loop() {
  // Ejemplo de texto que cambia: un contador de segundos en la fila 1.
  // Descomenta para probar movimiento:
  // lcd.setCursor(0, 1);
  // lcd.print("Segundos: ");
  // lcd.print(millis() / 1000);
  // delay(1000);
}
