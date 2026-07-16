/*********
  LCD 1602 por I2C - frase que desfila (efecto cartel de cancha).
  Direccion 0x27 (confirmada con el scanner).
  Conexion: GND->GND, VCC->5V, SDA->GPIO21, SCL->GPIO22.
*********/

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Mensaje que desfila en la fila de arriba.
String mensaje = "INGLATERRA LA CONCHA DE TU MADRE";

// Se le agregan 16 espacios a cada lado para que entre desde la derecha
// y salga por la izquierda de forma limpia.
String scroller;
int pos = 0;

// Velocidad del scroll: mas chico = mas rapido.
const int VELOCIDAD_MS = 400;

void setup() {
  Wire.begin(21, 22);   // SDA, SCL
  lcd.init();
  lcd.backlight();

  scroller = "                " + mensaje + "                ";

  // Fila de abajo, texto fijo (max 16 caracteres).
  lcd.setCursor(0, 1);
  lcd.print(" DALE MESSI");
}

void loop() {
  // Mostramos una ventana de 16 caracteres que avanza de a uno.
  lcd.setCursor(0, 0);
  lcd.print(scroller.substring(pos, pos + 16));

  pos++;
  if (pos > (int)scroller.length() - 16) pos = 0;   // reinicia el ciclo

  delay(VELOCIDAD_MS);
}
