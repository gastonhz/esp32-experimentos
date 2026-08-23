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
#include "juego_carrera.h"
#include "juego_pelea.h"
#include "juego_western.h"

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
  { "Global",        "joyMuertaJuego",    "Zona muerta juego",     &JOY_MUERTA_JUEGO    },
  { "Global",        "joyMuertaMenu",     "Zona muerta menu",      &JOY_MUERTA_MENU     },
  { "Pong",          "velLenta",          "Saque lento (ms/LED)",  &VEL_LENTA           },
  { "Pong",          "velRapida",         "Saque rapido (ms/LED)", &VEL_RAPIDA          },
  { "Pong",          "velAcelera",        "Aceleracion por golpe", &VEL_ACELERA         },
  { "Pong",          "ponPaletaLargo",    "Paleta (LEDs)",         &PON_PALETA_LARGO    },
  { "Pong",          "ponVelPaleta",      "Paleta (LEDs/s)",       &PON_VEL_PALETA      },
  { "Tira y Afloja", "tugEmpuje",         "LEDs por pulsacion",    &TUG_EMPUJE          },
  { "Rompecolores",  "rcVelLenta",        "Muro lento (ms/LED)",   &RC_VEL_LENTA        },
  { "Rompecolores",  "rcVelRapida",       "Muro rapido (ms/LED)",  &RC_VEL_RAPIDA       },
  { "Rompecolores",  "rcProyVel",         "Bala (ms/LED)",         &RC_PROY_VEL         },
  { "Twang",         "twangVelJugador",   "Jugador (LEDs/s)",      &TWANG_VEL_JUGADOR   },
  { "Twang",         "twangAtaqueRadio",  "Radio del ataque",      &TWANG_ATAQUE_RADIO  },
  { "Twang",         "twangAtaqueEspera", "Cooldown ataque (ms)",  &TWANG_ATAQUE_ESPERA },
  { "Paddle",        "padVelJugador",     "Paleta (LEDs/s)",       &PAD_VEL_JUGADOR     },
  { "Paddle",        "padLargoIni",       "Largo inicial (LEDs)",  &PAD_LARGO_INI       },
  { "Paddle",        "padAchicaCada",     "Achica cada N golpes",  &PAD_ACHICA_CADA     },
  { "Paddle",        "padFalloMs",        "Castigo al aire (ms)",  &PAD_FALLO_MS        },
  { "Stacker",       "stkAnchoIni",       "Ancho inicial (LEDs)",  &STK_ANCHO_INI       },
  { "Stacker",       "stkPisos",          "Pisos para ganar",      &STK_PISOS           },
  { "Salta Muros",   "esqVelJugador",     "Caminar (LEDs/s)",      &ESQ_VEL_JUGADOR     },
  { "Salta Muros",   "esqSaltoMin",       "Salto corto (LEDs)",    &ESQ_SALTO_MIN       },
  { "Salta Muros",   "esqSaltoMax",       "Salto largo (LEDs)",    &ESQ_SALTO_MAX       },
  { "Salta Muros",   "esqSaltoCarga",     "Carga del salto (ms)",  &ESQ_SALTO_CARGA_MS  },
  { "Salta Muros",   "esqDescanso",       "Descanso salto (ms)",   &ESQ_SALTO_ESPERA    },
  { "Salta Muros",   "esqHuecoMin",       "Hueco minimo (LEDs)",   &ESQ_HUECO_MIN       },
  { "Salta Muros",   "esqHuecoMax",       "Hueco maximo (LEDs)",   &ESQ_HUECO_MAX       },
  { "Salta Muros",   "esqMuroMin",        "Muro fino (LEDs)",      &ESQ_MURO_MIN        },
  { "Salta Muros",   "esqMuroMax",        "Muro grueso (LEDs)",    &ESQ_MURO_MAX        },
  { "Alunizaje",     "lndGravedad",       "Gravedad (LEDs/s2)",    &LND_GRAVEDAD        },
  { "Alunizaje",     "lndEmpuje",         "Empuje (LEDs/s2)",      &LND_EMPUJE          },
  { "Alunizaje",     "lndVelSegura",      "Vel. de posado (LEDs/s)", &LND_VEL_SEGURA    },
  { "Alunizaje",     "lndCombustible",    "Combustible (ms)",      &LND_COMBUSTIBLE     },
  { "Reaccion",      "dueEsperaMin",      "Espera minima (ms)",    &DUE_ESPERA_MIN      },
  { "Reaccion",      "dueEsperaMax",      "Espera maxima (ms)",    &DUE_ESPERA_MAX      },
  { "Carrera",       "carImpulso",        "Impulso x pulsacion",   &CAR_IMPULSO         },
  { "Carrera",       "carFriccion",       "Friccion (x100 /s)",    &CAR_FRICCION        },
  { "Carrera",       "carGravedad",       "Pendiente (LEDs/s2)",   &CAR_GRAVEDAD        },
  { "Carrera",       "carVueltasMin",     "Vueltas (pote min)",    &CAR_VUELTAS_MIN     },
  { "Carrera",       "carVueltasMax",     "Vueltas (pote max)",    &CAR_VUELTAS_MAX     },
  { "Carrera",       "carMesetaMin",      "Cima corta (LEDs)",     &CAR_MESETA_MIN      },
  { "Carrera",       "carMesetaMax",      "Cima larga (LEDs)",     &CAR_MESETA_MAX      },
  { "Pelea",         "pelVelJugador",     "Caminar (LEDs/s)",      &PEL_VEL_JUGADOR     },
  { "Pelea",         "pelAlcanceCorto",   "Alcance toque (LEDs)",  &PEL_ALCANCE_CORTO   },
  { "Pelea",         "pelAlcanceLargo",   "Alcance cargado (LEDs)",&PEL_ALCANCE_LARGO   },
  { "Pelea",         "pelCargaMin",       "Umbral de carga (ms)",  &PEL_CARGA_MIN       },
  { "Pelea",         "pelCargaMax",       "Carga completa (ms)",   &PEL_CARGA_MAX       },
  { "Pelea",         "pelRecupCorta",     "Recup. toque (ms)",     &PEL_RECUP_CORTA     },
  { "Pelea",         "pelRecupLarga",     "Recup. cargado (ms)",   &PEL_RECUP_LARGA     },
  { "Tiros",         "wesVelJugador",     "Caminar (LEDs/s)",      &WES_VEL_JUGADOR     },
  { "Tiros",         "wesVelBala",        "Bala (LEDs/s)",         &WES_VEL_BALA        },
  { "Tiros",         "wesCargador",       "Balas por cargador",    &WES_CARGADOR        },
  { "Tiros",         "wesRecargaMs",      "Recarga (ms)",          &WES_RECARGA_MS      },
  { "Tiros",         "wesInvulMs",        "Gracia tras herida (ms)", &WES_INVUL_MS      },
  { "Tiros",         "wesQuemarropa",     "Quemarropa (LEDs)",     &WES_QUEMARROPA      },
  { "Tiros",         "wesGiroMin",        "Umbral de giro (%)",    &WES_GIRO_MIN        },
};
static const uint8_t NUM_PARAMS = sizeof(PARAMS) / sizeof(PARAMS[0]);

// Lo unico de la pagina que cambia solo: estado del juego y records. Va aparte
// porque se sirve por dos vias -- dentro de la pagina completa la primera vez, y
// por /estado en cada refresco.
static String bloqueVivo() {
  String h;
  h.reserve(1200);

  h += "<h2>Ahora</h2><p>";
  if (pantalla == MENU) {
    h += "En el menu, resaltado: <b>" + String(JUEGOS[juegoSel].nombre) + "</b>";
  } else if (pantalla == NOMBRE) {
    h += "Firmando un record nuevo";
  } else {
    h += "Jugando: <b>" + String(JUEGOS[juegoActivo].nombre) + "</b><br>";
    h += JUEGOS[juegoActivo].web();
  }
  h += "<br><small>Ultimo reinicio: " + textoCausaReset() + "</small>";
  h += "</p>";

  h += "<h2>Records</h2><ul>";
  for (uint8_t i = 0; i < NUM_RECORDS; i++) {
    String firma = String(hsNombre[i]);
    h += "<li>" + String(RECORDS[i].nombre) + ": " + textoRecord(i) +
         (firma.length() ? (" <b>" + firma + "</b>") : "") + "</li>";
  }
  h += "</ul>";
  return h;
}

static String paginaHtml() {
  String h;
  h.reserve(9000);
  h += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  h += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  h += "<title>PixeLED</title></head>";
  h += "<body style=\"font-family:sans-serif;margin:16px;max-width:480px\">";
  h += "<h1>PixeLED</h1>";

  // --- Estado en vivo (lo repinta el script del final) ---
  h += "<div id=\"vivo\">" + bloqueVivo() + "</div>";

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

  // Refresco del estado sin recargar la pagina. Antes esto era un
  // <meta http-equiv="refresh" content="3">, que era mas simple pero hacia
  // inusable el formulario: cada 3 segundos el navegador recargaba entero y se
  // perdia lo que se estuviera tipeando antes de llegar a apretar Aplicar.
  // Repintando solo el div los campos quedan intactos. Si el navegador no
  // ejecuta el script no se rompe nada: la pagina se ve igual, solo deja de
  // actualizarse sola. Sin librerias ni CDN a proposito -- la red la sirve el
  // propio ESP32 y no tiene salida a internet.
  h += "<script>setInterval(function(){"
       "fetch('/estado').then(function(r){return r.text();})"
       ".then(function(t){document.getElementById('vivo').innerHTML=t;})"
       ".catch(function(){});"
       "},3000);</script>";

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

  // Solo el fragmento que cambia solo: lo pide el script cada 3 s.
  server.on("/estado", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", bloqueVivo());
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
