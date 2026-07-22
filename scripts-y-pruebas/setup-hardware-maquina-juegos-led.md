---
title: Setup de hardware — máquina de juegos LED
type: nota
category: electronica
tags: [electronica, esp32, ws2812b, fastled, pong, handoff]
summary: Referencia del hardware ya montado (ESP32 + tira WS2812B + level shifter) como handoff para escribir el código de los juegos, empezando por Pong.
created: 2026-07-22
updated: 2026-07-22
---

# Setup de hardware — máquina de juegos LED

Documento de referencia del hardware ya montado, pensado también como **handoff
para Claude Code** al escribir el código de los juegos. Empezamos por **Pong**.

Relacionado: [[esp32-wroom-32]] · [[tira-led-ws2812b]] · [[level-shifter-sn74ahct125n]] · [[consumo-tira-ws2812b]] · [[proyecto-pong]]

## Plataforma

- **Micro:** ESP32 WROOM-32 (clásico, 38 pines, USB-C).
- **Tira:** WS2812B, 60 LED/m, 5V, orden de color **GRB**, protocolo 800 kHz.
- **Largo actual:** **100 LEDs** (166,6 cm).
- **Fuente:** conmutada **5V / 5A** externa (la tira NO se alimenta del ESP32).
- **Level shifter:** SN74AHCT125N (buffer TTL, un canal en uso) para pasar la
  señal de datos de 3.3V a 5V de forma confiable.

## Pinout y cadena de señal

| Señal | Origen | Destino |
|---|---|---|
| Datos | GPIO16 del ESP32 (3.3V) | 1A (pin 2) del SN74AHCT125N |
| Datos elevados | 1Y (pin 3) del chip | → 470Ω en serie → DIN de la tira |
| Habilitación | 1OE (pin 1) del chip | GND (activo en bajo, siempre a masa) |
| Alim. chip | Vcc (pin 14) | +5V de la fuente |
| GND chip | GND (pin 7) | GND común |

- **GPIO de datos: `16`.** (Evitados strapping pins 0/2/15 y los solo-entrada
  34–39. Si se cambia, actualizar acá y en el código.)
- Resistencia serie de **470Ω** en la línea de datos, junto al DIN.
- Capacitor de **1000µF / 25V** entre +5V y GND cerca del inicio de la tira
  (respetar polaridad: franja = negativo → GND).

## Alimentación y masa

- +5V y GND de la **fuente** van directos a la tira.
- El riel +5V de la breadboard (que alimenta el chip) viene de los **5V de la fuente**.
- **MASA COMÚN OBLIGATORIA:** GND de la fuente, GND del ESP32 y GND de la tira,
  todos al mismo nodo. Sin esto, la línea de datos no tiene referencia.

## Restricciones para el software (importante para Claude Code)

- **Cap de brillo global por seguridad de corriente:**
  ```cpp
  FastLED.setBrightness(128);  // 50% -> techo ~3 A en 100 LEDs (fuente de 5 A)
  ```
  Opcional, red de seguridad de FastLED:
  ```cpp
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 4500);  // limita a 4,5 A
  ```
- **Nunca** blanco puro a full brillo en los 100 LEDs (≈6 A > fuente).
- Consumo real de los juegos 1D: muy bajo (pelota = 1–3 LEDs). Ver
  [[consumo-tira-ws2812b]] para los números y mediciones.

## Config base sugerida (FastLED)

```cpp
#include <FastLED.h>

#define NUM_LEDS   100
#define DATA_PIN   16
#define LED_TYPE   WS2812B
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(128);
  FastLED.clear(true);
}
```

- Primera prueba recomendada antes de la lógica del juego: un barrido / arcoíris
  simple para validar que la cadena entera (nivel lógico, masa, alimentación)
  está sana. Si eso corre, el hardware está OK.

## Los cuatro juegos (la tira es el mundo del juego)

1. **Pong** — primero. Pelota que rebota entre dos extremos; cada jugador tiene
   un botón para "golpear" cuando la pelota entra en su zona.
2. Tug of War
3. Open LED Race
4. TWANG

## Estado

- [x] Componentes en mano
- [x] Cableado montado en breadboard
- [x] Masa común (fuente ↔ ESP32) y 1OE firme a GND validados
- [x] Riel +5V de la breadboard energizado desde la fuente
- [x] Subir sketch de prueba (arcoíris) con la fuente encendida
- [x] Escribir lógica de Pong — **funcionando** (ver [[pong-1d-ws2812b]])

> [!warning] Alimentación del ESP32
> **Nunca** tener el USB y los 5V de la fuente en el pin 5V/VIN al mismo tiempo
> (dos fuentes de 5V enfrentadas puede dañar el regulador de la placa).
> - Para flashear/debuggear: USB conectado, pin 5V del ESP libre.
> - Para correr standalone: USB desenchufado, fuente al pin **5V/VIN** (nunca al de 3.3V).

## Entradas — botones

Dos pulsadores, uno por jugador. Cableado con `INPUT_PULLUP`, sin resistencias
externas: una pata al GPIO, la otra a GND.

| Botón | Pata A → | Pata B → |
|---|---|---|
| Jugador 1 | GPIO18 | GND |
| Jugador 2 | GPIO19 | GND |

- **GPIOs de botones: `18` y `19`.** (Elegidos lejos del GPIO16 de datos y de los
  strapping pins 0/2/5/12/15, que si están apretados en el arranque confunden el
  booteo.)
- Modo `INPUT_PULLUP` → **lógica invertida**: botón suelto = `HIGH`, apretado = `LOW`.
- Pulsadores de 4 patas: usar **dos patas de esquinas diagonalmente opuestas**
  para tomar el par que abre/cierra (las patas de un mismo lado ya están unidas).
- **Rebote:** los pulsadores mecánicos rebotan; si aparecen lecturas dobles,
  aplicar *debounce* por software (~20–50 ms). Se resuelve en código, el cableado
  no cambia.

### Config de botones (FastLED/Arduino)

```cpp
#define BTN_P1 18
#define BTN_P2 19

void setupButtons() {
  pinMode(BTN_P1, INPUT_PULLUP);
  pinMode(BTN_P2, INPUT_PULLUP);
}

// Lectura: apretado == LOW
bool p1Pressed() { return digitalRead(BTN_P1) == LOW; }
bool p2Pressed() { return digitalRead(BTN_P2) == LOW; }
```
