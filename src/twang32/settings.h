// Recorte de settings.h de TWANG32 (bdring, MIT). El original ademas guardaba
// ajustes en la NVS y los servia por WiFi; de eso ya se ocupa la consola, asi
// que aca quedan solo las constantes que necesitan las clases de al lado
// --Conveyor.h incluye este archivo-- y el juego portado.
#pragma once

#include "Arduino.h"

#define VIRTUAL_LED_COUNT 1000              // el mundo de TWANG mide 1000, no LEDs

const uint8_t MAX_PLAYER_SPEED = 10;        // Max move speed of the player
const uint8_t LIVES_PER_LEVEL  = 3;         // default lives per level
