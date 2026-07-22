---
title: Pong 1D sobre tira WS2812B
type: nota
category: electronica
tags: [electronica, esp32, ws2812b, fastled, pong, juego, funcionando]
summary: Primer juego de la maquina de juegos LED terminado y andando. Pong 1D sobre 100 LEDs, dos botones, maquina de estados en FastLED. Documenta reglas, diseno del codigo, la puesta a punto del hardware y los parametros para balancear.
created: 2026-07-22
updated: 2026-07-22
---

# Pong 1D sobre tira WS2812B

Primer juego de la maquina de juegos LED, **terminado y funcionando**. Corre en
`src/main.cpp` y se flashea desde PlatformIO. La tira es el mundo del juego: la
pelota rebota entre los dos extremos y cada jugador defiende su punta con un boton.

Relacionado: [[setup-hardware-maquina-juegos-led]] · [[esp32-wroom-32]] · [[tira-led-ws2812b]] · [[level-shifter-sn74ahct125n]]

## Estado

- [x] Hardware validado con sketch de arcoiris
- [x] Logica de Pong escrita y flasheada
- [x] Probado con los dos botones: **anduvo perfecto**

## Como se juega

- **Saque:** la pelota parpadea en la punta del que saca. Se saca apretando el
  boton propio (o auto-saque a los 4 s). **Saca el que perdio el punto anterior.**
- **En juego:** la pelota va y viene. Cuando entra en tu **zona** (P1 = inicio
  verde, P2 = final azul), la zona se ilumina fuerte = golpeas apretando tu boton
  en ese momento. Cada golpe **acelera** la pelota.
- **Punto:** si la pelota se escapa por tu punta, suma el rival (parpadeo en su color).
- **Marcador:** LEDs prendidos en cada extremo (verde P1 al inicio, azul P2 al
  final). **Primero a 5 gana** -> animacion de victoria y nuevo partido.

## Hardware (resumen)

Detalle completo en [[setup-hardware-maquina-juegos-led]].

| Senal | GPIO | Notas |
|---|---|---|
| Datos tira | 16 | -> SN74AHCT125N -> 470ohm -> DIN |
| Boton P1 | 18 | `INPUT_PULLUP`, a GND (apretado = LOW) |
| Boton P2 | 19 | `INPUT_PULLUP`, a GND (apretado = LOW) |

- 100 LEDs WS2812B, orden **GRB**, alimentacion externa 5V, masa comun.
- Brillo global 128 + `setMaxPowerInVoltsAndMilliamps(5, 4500)` como red de seguridad.

## Diseno del codigo

Maquina de estados sencilla en el `loop()`:

```
SACANDO  -> pelota parpadeando en la punta; boton (o timeout) lanza -> JUGANDO
JUGANDO  -> mueve la pelota por tiempo, detecta golpes y puntos
PUNTO    -> parpadeo del color del que sumo, ~1.2 s -> vuelve a SACANDO
FIN      -> chase del ganador, ~3.5 s -> nuevo partido
```

Puntos clave de la implementacion:

- **Movimiento por tiempo, no por `delay()`:** la pelota avanza 1 LED cada
  `pelotaVel` ms usando `millis()`. Asi el juego sigue leyendo botones y
  redibujando a full framerate entre paso y paso.
- **Botones con debounce + flanco de bajada:** se detecta el *momento* en que se
  aprieta (no el mantenido). Un golpe es valido solo si la pelota **viene hacia tu
  punta y esta dentro de tu zona** en el frame del flanco. Mantener apretado no
  sirve: hay que apretar fresco (mecanica de timing, anti-trampa).
- **Aceleracion:** cada golpe baja `pelotaVel` en `VEL_ACELERA` hasta `VEL_MINIMA`.
- **Dibujo:** pelota blanca con estela corta que se desvanece; zonas tenues que se
  encienden fuerte cuando la pelota esta dentro; marcador como LEDs en los extremos.

## Parametros para balancear

Estan todos arriba de `src/main.cpp`:

| Constante | Valor | Que hace |
|---|---|---|
| `ZONA` | 12 | Largo de la ventana de golpe. Mas grande = mas facil. |
| `VEL_INICIAL` | 60 | ms por LED al sacar. Mas grande = mas lento. |
| `VEL_MINIMA` | 16 | Tope de velocidad (piso de ms por LED). |
| `VEL_ACELERA` | 4 | Cuanto acelera por cada golpe. |
| `PUNTOS_GANAR` | 5 | Puntos para ganar el partido. |
| `DEBOUNCE_MS` | 25 | Antirrebote de los botones. |
| `BRILLO` | 128 | Techo de brillo (seguridad de corriente). |

## Puesta a punto del hardware (lecciones)

Aprendizajes de la sesion, utiles para los proximos juegos:

1. **Alimentar el level shifter.** Sin Vcc (pin 14 a +5V) y GND (pin 7), el
   SN74AHCT125N no maneja la salida: solo prende el primer LED (tipico verde) y no
   propaga. Fue el primer sintoma y se resolvio alimentando el chip.
2. **Parpadeo por sectores = alimentacion.** Bajar el brillo lo estabilizaba: era
   caida de tension. Se arreglo con el **capacitor de 1000uF** al inicio de la tira
   e inyectando 5V/GND de la fuente **directo al pad de entrada**, no por los rieles
   finos de la protoboard. Con eso quedo estable a brillo 128.

## Flasheo

El `pio` del sistema esta roto; usar el de la penv de VSCode:

```
~/.platformio/penv/bin/pio run --target upload
```

- Puerto: `/dev/ttyUSB0` (a veces `ttyUSB1`).
- **Cerrar el monitor serie antes de flashear**, si no el puerto queda ocupado.
- Recordatorio de alimentacion: para flashear, USB conectado y pin 5V/VIN del ESP32
  libre. La fuente de 5V alimenta la tira (nunca los dos 5V enfrentados en el VIN).

## Proximos pasos / ideas

- Sonido de golpe y de punto con el buzzer (LEDC, ver [[buzzer-pasivo-ledc-esp32]]).
- Efecto visual al golpear (flash de la zona) y en el punto.
- Los otros tres juegos: Tug of War, Open LED Race, TWANG.
