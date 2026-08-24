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
    // Los que dependen del largo tienen un record por tira: sin decir cual es,
    // el numero no se entiende (y en el LCD no hay columnas para aclararlo).
    String tira = RECORDS[i].porLargo ? (" <small>(tira " + String(LARGO_TIRA) + ")</small>") : "";
    h += "<li>" + String(RECORDS[i].nombre) + ": " + textoRecord(i) +
         (firma.length() ? (" <b>" + firma + "</b>") : "") + tira + "</li>";
  }
  h += "</ul>";
  return h;
}

static String paginaHtml() {
  String h;
  h.reserve(13000);
  h += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  h += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  h += "<title>PixeLED</title></head>";
  h += "<body style=\"font-family:sans-serif;margin:16px;max-width:480px\">";
  h += "<h1>PixeLED</h1>";

  // --- Estado en vivo (lo repinta el script del final) ---
  h += "<div id=\"vivo\">" + bloqueVivo() + "</div>";

  // --- Ajustes de la consola ---
  // Van en un formulario aparte y con desplegables porque no son numeros a
  // tunear sino opciones cerradas, y porque cambiar el largo no es escribir una
  // variable: apaga la cola de la tira, avisa a FastLED, pisa el brillo y trae
  // de la NVS los records de esa tira. Son los mismos tres ajustes de la
  // entrada "Ajustes" del selector.
  h += "<h2>Consola</h2><form method=\"GET\" action=\"/set\">";
  h += "<label>Largo de la tira: <select name=\"largoTira\">";
  h += String("<option value=\"100\"") + (LARGO_TIRA < 200 ? " selected" : "") + ">100 LEDs</option>";
  h += String("<option value=\"200\"") + (LARGO_TIRA >= 200 ? " selected" : "") + ">200 LEDs</option>";
  h += "</select></label><br>";
  h += "<label>Orientacion: <select name=\"orient\">";
  h += String("<option value=\"0\"") + (ORIENTACION == TIRA_VERTICAL ? " selected" : "") + ">vertical (parada)</option>";
  h += String("<option value=\"1\"") + (ORIENTACION == TIRA_HORIZONTAL ? " selected" : "") + ">horizontal (acostada)</option>";
  h += "</select></label><br>";
  h += "<label>Silenciar: <select name=\"silencio\">";
  h += String("<option value=\"0\"") + (!SILENCIO ? " selected" : "") + ">no</option>";
  h += String("<option value=\"1\"") + (SILENCIO ? " selected" : "") + ">si</option>";
  h += "</select></label><br>";
  h += "<p><input type=\"submit\" value=\"Aplicar\"></p></form>";

  // --- Pista de Carrera dibujada a mano ---
  // Ocho filas fijas en vez de un campo de texto: en el celular saltan los
  // teclados numericos y no hay sintaxis que equivocar. Las ocho van SIEMPRE en
  // la query, asi que poner una fila en "--" es como se borra un tramo.
  h += "<h2>Pista de Carrera</h2><form method=\"GET\" action=\"/set\">";
  h += "<p><small>Posiciones en LEDs, de 0 a " + String(LARGO_TIRA - 1) +
       ". Las filas en \"--\" no cuentan; con las ocho vacias cada carrera sortea "
       "sus cuestas como siempre.</small></p>";
  for (uint8_t k = 0; k < CAR_MAX_TRAMOS; k++) {
    String n     = String(k);
    // Se muestran los numeros aunque la fila este en "--": si alguien cargo el
    // tramo y se olvido de elegir subida o bajada, al volver los encuentra ahi
    // en vez de tener que tipearlos de nuevo.
    bool   usado = (CAR_PISTA[k].tipo != TRAMO_NADA || CAR_PISTA[k].fin != 0);
    h += "<label>" + String(k + 1) + ": ";
    h += "<input type=\"number\" min=\"0\" style=\"width:4.5em\" name=\"pt" + n + "i\" value=\"" +
         (usado ? String(CAR_PISTA[k].ini) : String("")) + "\"> a ";
    h += "<input type=\"number\" min=\"0\" style=\"width:4.5em\" name=\"pt" + n + "f\" value=\"" +
         (usado ? String(CAR_PISTA[k].fin) : String("")) + "\"> ";
    h += "<select name=\"pt" + n + "t\">";
    h += String("<option value=\"0\"") + (CAR_PISTA[k].tipo == TRAMO_NADA   ? " selected" : "") + ">--</option>";
    h += String("<option value=\"1\"") + (CAR_PISTA[k].tipo == TRAMO_SUBIDA ? " selected" : "") + ">subida</option>";
    h += String("<option value=\"2\"") + (CAR_PISTA[k].tipo == TRAMO_BAJADA ? " selected" : "") + ">bajada</option>";
    h += "</select></label><br>";
  }
  h += "<p><input type=\"submit\" value=\"Guardar pista\"></p></form>";

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
    // Los ajustes van primero: cambiar el largo pisa el brillo con el que le
    // corresponde a esa tira, y asi un brillo que venga en la MISMA query
    // (imposible hoy, son dos formularios) seguiria ganando.
    if (request->hasParam("largoTira")) {
      ponerLargoTira((uint16_t)request->getParam("largoTira")->value().toInt());
    }
    if (request->hasParam("orient")) {
      ponerOrientacion((uint8_t)request->getParam("orient")->value().toInt());
    }
    if (request->hasParam("silencio")) {
      ponerSilencio(request->getParam("silencio")->value().toInt() != 0);
    }

    // La pista viene entera o no viene: alcanza con mirar si llego la primera
    // fila para saber que el formulario que se aplico es el de la pista.
    if (request->hasParam("pt0t")) {
      for (uint8_t k = 0; k < CAR_MAX_TRAMOS; k++) {
        String base = "pt" + String(k);
        uint16_t ini = request->hasParam(base + "i") ? (uint16_t)request->getParam(base + "i")->value().toInt() : 0;
        uint16_t fin = request->hasParam(base + "f") ? (uint16_t)request->getParam(base + "f")->value().toInt() : 0;
        uint8_t  tip = request->hasParam(base + "t") ? (uint8_t)request->getParam(base + "t")->value().toInt()  : 0;
        if (tip > TRAMO_BAJADA) tip = TRAMO_NADA;
        if (ini > fin) { uint16_t x = ini; ini = fin; fin = x; }   // cargado al reves: se ordena
        CAR_PISTA[k].ini  = ini;
        CAR_PISTA[k].fin  = fin;
        CAR_PISTA[k].tipo = tip;
      }
      guardarPistaCarrera();
    }

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
