// ---------- Panel web: estado en vivo + tuneo de parametros ----------
// El servidor es asincronico (corre en su propia tarea, avisado por eventos),
// asi que el loop() del juego no tiene que atenderlo: no hay handleClient().
// Sin login ni HTTPS a proposito: la red la crea el propio ESP32 y no toca
// internet, es un panel para el que esta armando la consola.

#include "panel_web.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "juego_pong.h"
#include "juego_tug.h"
#include "juego_rompecolores.h"
#include "juego_twang.h"
#include "juego_paddle.h"
#include "juego_stacker.h"
#include "juego_esquiva.h"
#include "juego_lander.h"
#include "juego_duelo.h"

String apIP;
static AsyncWebServer server(80);

// ---------- Tabla de parametros tuneables ----------
// Todos son uint16_t para poder apuntarlos desde una sola tabla; los rangos
// reales los conoce cada juego. Agregar un parametro nuevo es agregar una fila.
struct Param {
  const char* grupo;      // encabezado bajo el que se agrupa en el formulario
  const char* clave;      // nombre del campo en la query de /set
  const char* etiqueta;   // texto visible
  uint16_t*   valor;
};

static const Param PARAMS[] = {
  { "Global",        "brillo",            "Brillo (0-255)",        &BRILLO              },
  { "Pong",          "velLenta",          "Saque lento (ms/LED)",  &VEL_LENTA           },
  { "Pong",          "velRapida",         "Saque rapido (ms/LED)", &VEL_RAPIDA          },
  { "Pong",          "velAcelera",        "Aceleracion por golpe", &VEL_ACELERA         },
  { "Tira-Afloja",   "tugEmpuje",         "LEDs por pulsacion",    &TUG_EMPUJE          },
  { "Rompecolores",  "rcVelLenta",        "Muro lento (ms/LED)",   &RC_VEL_LENTA        },
  { "Rompecolores",  "rcVelRapida",       "Muro rapido (ms/LED)",  &RC_VEL_RAPIDA       },
  { "Rompecolores",  "rcProyVel",         "Bala (ms/LED)",         &RC_PROY_VEL         },
  { "Twang",         "twangVelJugador",   "Jugador (LEDs/s)",      &TWANG_VEL_JUGADOR   },
  { "Twang",         "twangAtaqueRadio",  "Radio del ataque",      &TWANG_ATAQUE_RADIO  },
  { "Twang",         "twangAtaqueEspera", "Cooldown ataque (ms)",  &TWANG_ATAQUE_ESPERA },
  { "Paddle",        "padVelJugador",     "Paleta (LEDs/s)",       &PAD_VEL_JUGADOR     },
  { "Paddle",        "padLargoIni",       "Largo inicial (LEDs)",  &PAD_LARGO_INI       },
  { "Paddle",        "padAchicaCada",     "Achica cada N golpes",  &PAD_ACHICA_CADA     },
  { "Stacker",       "stkAnchoIni",       "Ancho inicial (LEDs)",  &STK_ANCHO_INI       },
  { "Stacker",       "stkPisos",          "Pisos para ganar",      &STK_PISOS           },
  { "Esquiva",       "esqVelJugador",     "Caminar (LEDs/s)",      &ESQ_VEL_JUGADOR     },
  { "Esquiva",       "esqSaltoMin",       "Salto corto (LEDs)",    &ESQ_SALTO_MIN       },
  { "Esquiva",       "esqSaltoMax",       "Salto largo (LEDs)",    &ESQ_SALTO_MAX       },
  { "Esquiva",       "esqSaltoCarga",     "Carga del salto (ms)",  &ESQ_SALTO_CARGA_MS  },
  { "Esquiva",       "esqDescanso",       "Descanso salto (ms)",   &ESQ_SALTO_ESPERA    },
  { "Esquiva",       "esqHuecoMin",       "Hueco minimo (LEDs)",   &ESQ_HUECO_MIN       },
  { "Esquiva",       "esqHuecoMax",       "Hueco maximo (LEDs)",   &ESQ_HUECO_MAX       },
  { "Esquiva",       "esqMuroMin",        "Muro fino (LEDs)",      &ESQ_MURO_MIN        },
  { "Esquiva",       "esqMuroMax",        "Muro grueso (LEDs)",    &ESQ_MURO_MAX        },
  { "Lander",        "lndGravedad",       "Gravedad (LEDs/s2)",    &LND_GRAVEDAD        },
  { "Lander",        "lndEmpuje",         "Empuje (LEDs/s2)",      &LND_EMPUJE          },
  { "Lander",        "lndVelSegura",      "Vel. de posado (LEDs/s)", &LND_VEL_SEGURA    },
  { "Duelo",         "dueEsperaMin",      "Espera minima (ms)",    &DUE_ESPERA_MIN      },
  { "Duelo",         "dueEsperaMax",      "Espera maxima (ms)",    &DUE_ESPERA_MAX      },
};
static const uint8_t NUM_PARAMS = sizeof(PARAMS) / sizeof(PARAMS[0]);

static String paginaHtml() {
  String h;
  h.reserve(6000);
  h += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  h += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  // Recarga sola cada 3 s: alcanza para ver el estado en vivo sin JS ni websockets.
  h += "<meta http-equiv=\"refresh\" content=\"3\">";
  h += "<title>gasticonsola</title></head>";
  h += "<body style=\"font-family:sans-serif;margin:16px;max-width:480px\">";
  h += "<h1>gasticonsola</h1>";

  // --- Estado en vivo ---
  h += "<h2>Ahora</h2><p>";
  if (pantalla == MENU) {
    h += "En el menu, resaltado: <b>" + String(JUEGOS[juegoSel].nombre) + "</b>";
  } else {
    h += "Jugando: <b>" + String(JUEGOS[juegoActivo].nombre) + "</b><br>";
    h += JUEGOS[juegoActivo].web();
  }
  h += "</p>";

  // --- Records guardados ---
  h += "<h2>Records</h2><ul>";
  for (uint8_t i = 0; i < NUM_RECORDS; i++) {
    h += "<li>" + String(RECORDS[i].nombre) + ": " + textoRecord(i) + "</li>";
  }
  h += "</ul>";

  // --- Parametros tuneables en caliente ---
  h += "<h2>Parametros</h2><form method=\"GET\" action=\"/set\">";
  const char* grupoActual = "";
  for (uint8_t i = 0; i < NUM_PARAMS; i++) {
    if (strcmp(PARAMS[i].grupo, grupoActual) != 0) {   // encabezado al cambiar de grupo
      grupoActual = PARAMS[i].grupo;
      h += "<h3>" + String(grupoActual) + "</h3>";
    }
    h += "<label>" + String(PARAMS[i].etiqueta) +
         ": <input type=\"number\" name=\"" + String(PARAMS[i].clave) +
         "\" value=\"" + String(*PARAMS[i].valor) + "\"></label><br>";
  }
  h += "<p><input type=\"submit\" value=\"Aplicar\"></p></form>";

  h += "</body></html>";
  return h;
}

// Aplica lo que haya venido en la query. Sin validar rangos: es un panel de
// tuneo, si se mete un numero raro el juego se pone raro y se corrige de nuevo.
void iniciarPanelWeb() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  apIP = WiFi.softAPIP().toString();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", paginaHtml());
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest* request) {
    for (uint8_t i = 0; i < NUM_PARAMS; i++) {
      if (request->hasParam(PARAMS[i].clave)) {
        *PARAMS[i].valor = request->getParam(PARAMS[i].clave)->value().toInt();
      }
    }
    FastLED.setBrightness((uint8_t)BRILLO);   // el unico parametro que hay que avisarle a FastLED
    request->redirect("/");                   // vuelve al formulario ya con los valores nuevos
  });

  server.begin();
}
