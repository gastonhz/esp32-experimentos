// ---------- Panel web: estado en vivo + tuneo de parametros ----------
// El servidor es asincronico (corre en su propia tarea, avisado por eventos),
// asi que el loop() del juego no tiene que atenderlo: no hay handleClient().
// Sin login ni HTTPS a proposito: la red la crea el propio ESP32 y no toca
// internet, es un panel para el que esta armando la consola.

#include "panel_web.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <nvs.h>

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

// ---------- Estado de la actualizacion de firmware ----------
// Lo escribe la tarea del servidor (el upload llega por eventos de AsyncTCP) y
// lo lee el loop(): por eso volatile. Son todos de 32 bits alineados, que en el
// ESP32 se leen y escriben de una sola vez.
static volatile bool     otaEnCurso   = false;
static volatile bool     otaError     = false;
static volatile uint32_t otaRecibido  = 0;
static volatile uint32_t otaTotal     = 0;
static uint32_t          otaReinicioEn = 0;   // millis() en que hay que reiniciar (0 = no)
static uint32_t          otaErrorHasta = 0;   // hasta cuando se muestra el cartel de error

// Por que fallo, en texto, para la pagina y el monitor serie. Buffer fijo y no
// String porque lo escribe la tarea del servidor y lo lee otra.
static char otaMsg[64] = "";
static void otaFalla(const char* msg) {
  snprintf(otaMsg, sizeof(otaMsg), "%s", msg);
  Serial.printf("[OTA] FALLO: %s\n", otaMsg);
}

bool otaActiva() { return otaEnCurso; }

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

// ---------- Estado y diagnostico ----------
// Todo esto se lee EN VIVO de la placa: no hay nada anotado al flashear. El
// tamano del firmware sale de la particion que se esta ejecutando, y la memoria
// del propio asignador, asi que los numeros son los de esta corrida y no los de
// la compilacion.
static String kB(uint32_t bytes) { return String(bytes / 1024.0f, 1) + " kB"; }

static String porcentaje(uint32_t parte, uint32_t total) {
  if (!total) return "?";
  return String((uint32_t)((parte * 100ULL) / total)) + "%";
}

static String tiempoEncendida() {
  uint32_t s = millis() / 1000;
  uint32_t d = s / 86400;  s %= 86400;
  uint32_t h = s / 3600;   s %= 3600;
  uint32_t m = s / 60;     s %= 60;
  String t;
  if (d)           t += String(d) + " d ";
  if (d || h)      t += String(h) + " h ";
  if (d || h || m) t += String(m) + " min ";
  return t + String(s) + " s";
}

// Barra de proporcion en HTML puro, sin imagenes ni CSS externo, que la red del
// ESP32 no tiene salida a internet. Son SPAN y no DIV porque van dentro de un
// <p>: un div ahi adentro le cierra el parrafo al navegador y descoloca todo lo
// que viene despues.
static String barraHtml(uint32_t parte, uint32_t total, const char* color) {
  uint32_t p = total ? (uint32_t)((parte * 100ULL) / total) : 0;
  if (p > 100) p = 100;
  return "<span style=\"display:block;background:#eee;border-radius:3px;height:8px;margin:2px 0 6px\">"
         "<span style=\"display:block;background:" + String(color) + ";width:" + String(p) +
         "%;height:8px;border-radius:3px\"></span></span>";
}

static String bloqueDiagnostico() {
  String h;
  h.reserve(1600);
  h += "<h2>Estado y diagnostico</h2>";

  // --- RAM ---
  // El numero que da PlatformIO al compilar es lo que se reserva ESTATICO; el
  // que importa mientras corre es este, y sobre todo el minimo historico: si
  // ese se acerca a cero, en algun pico la consola se queda sin memoria y se
  // reinicia sola (y ahi el motivo del reinicio de mas abajo dice "Crash").
  uint32_t heapTotal = ESP.getHeapSize();
  uint32_t heapLibre = ESP.getFreeHeap();
  h += "<h3>Memoria RAM</h3>";
  h += "<p>Usada: <b>" + kB(heapTotal - heapLibre) + "</b> de " + kB(heapTotal) +
       " (" + porcentaje(heapTotal - heapLibre, heapTotal) + ")";
  h += barraHtml(heapTotal - heapLibre, heapTotal, "#3a7");
  h += "<small>Minimo libre desde que arranco: <b>" + kB(ESP.getMinFreeHeap()) + "</b>"
       " &middot; bloque contiguo mas grande: " + kB(ESP.getMaxAllocHeap()) + "</small></p>";

  // --- Flash ---
  // El firmware se mide contra la particion en la que esta, no contra el chip:
  // hay dos particiones de app (para que el OTA escriba en la que no corre) y
  // lo que puede crecer el binario es el tamano de UNA.
  const esp_partition_t* corriendo = esp_ota_get_running_partition();
  uint32_t appTotal = corriendo ? corriendo->size : 0;
  uint32_t appUsada = ESP.getSketchSize();
  h += "<h3>Flash</h3>";
  h += "<p>Firmware: <b>" + kB(appUsada) + "</b> de " + kB(appTotal) +
       " de la particion (" + porcentaje(appUsada, appTotal) + ")";
  h += barraHtml(appUsada, appTotal, "#37a");
  h += "<small>Chip de " + kB(ESP.getFlashChipSize()) + " &middot; corriendo en <b>" +
       String(corriendo ? corriendo->label : "?") + "</b></small></p>";

  // --- NVS: donde viven los records y los ajustes ---
  // Si se llenara, los records dejarian de guardarse sin avisar.
  nvs_stats_t nvs;
  if (nvs_get_stats(NULL, &nvs) == ESP_OK) {
    h += "<h3>Memoria guardada (NVS)</h3><p>";
    h += "Entradas usadas: <b>" + String(nvs.used_entries) + "</b> de " +
         String(nvs.total_entries) + " (" +
         porcentaje(nvs.used_entries, nvs.total_entries) + ")";
    h += barraHtml(nvs.used_entries, nvs.total_entries, "#a73");
    h += "<small>Aca viven los records firmados y los ajustes.</small></p>";
  }

  // --- Marcha de la consola ---
  h += "<h3>Marcha</h3><p>";
  h += "Loop: <b>" + String(fpsConsola) + " vueltas/s</b> &middot; tira de " +
       String(LARGO_TIRA) + " LEDs<br>";
  h += "Encendida hace: <b>" + tiempoEncendida() + "</b><br>";
  h += "Ultimo reinicio: <b>" + textoCausaReset() + "</b><br>";
  h += "<small>" + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) +
       ", " + String(ESP.getChipCores()) + " nucleos a " + String(ESP.getCpuFreqMHz()) + " MHz"
       " &middot; " + String(WiFi.softAPgetStationNum()) + " conectado(s) al AP</small></p>";
  return h;
}

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
  h.reserve(15000);
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

  // --- Estado y diagnostico, y abajo la actualizacion ---
  // Van pegados a proposito: los dos numeros que hay que mirar antes y despues
  // de flashear estan ahi arriba. La particion (app0/app1) dice si el OTA
  // realmente cambio de ranura, y la fecha de compilacion dice que version
  // corre, que es la unica forma de confirmar de un vistazo que la
  // actualizacion entro: el numero de version se olvida de subir, la fecha no.
  h += "<div id=\"diag\">" + bloqueDiagnostico() + "</div>";

  h += "<h2>Firmware</h2>";
  h += "<p><small>Compilado el " __DATE__ " a las " __TIME__ "</small></p>";
  h += "<form method=\"POST\" action=\"/actualizar\" enctype=\"multipart/form-data\">";
  h += "<input type=\"file\" name=\"firmware\" accept=\".bin\"> ";
  h += "<input type=\"submit\" value=\"Actualizar\">";
  h += "<p><small>Subi el <code>firmware.bin</code> que deja PlatformIO en "
       "<code>.pio/build/nodemcu-32s/</code>. Se escribe en la particion que no "
       "esta corriendo, asi que si algo falla la consola sigue arrancando con "
       "esta misma version.</small></p></form>";

  // Refresco del estado sin recargar la pagina. Antes esto era un
  // <meta http-equiv="refresh" content="3">, que era mas simple pero hacia
  // inusable el formulario: cada 3 segundos el navegador recargaba entero y se
  // perdia lo que se estuviera tipeando antes de llegar a apretar Aplicar.
  // Repintando solo el div los campos quedan intactos. Si el navegador no
  // ejecuta el script no se rompe nada: la pagina se ve igual, solo deja de
  // actualizarse sola. Sin librerias ni CDN a proposito -- la red la sirve el
  // propio ESP32 y no tiene salida a internet.
  h += "<script>function pinta(url,id){"
       "fetch(url).then(function(r){return r.text();})"
       ".then(function(t){document.getElementById(id).innerHTML=t;})"
       ".catch(function(){});}"
       "setInterval(function(){pinta('/estado','vivo');pinta('/diag','diag');},3000);</script>";

  h += "</body></html>";
  return h;
}

// Aplica lo que haya venido en la query. Sin validar rangos: es un panel de
// tuneo, si se mete un numero raro el juego se pone raro y se corrige de nuevo.
void iniciarPanelWeb() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  apIP = WiFi.softAPIP().toString();

  // Sin ahorro de energia en la radio: son unos mA mas, pero le saca latencia
  // y variabilidad a la conexion, que es justo lo que necesita una subida de
  // casi un mega que no se puede permitir una pausa larga.
  WiFi.setSleep(false);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", paginaHtml());
  });

  // Solo el fragmento que cambia solo: lo pide el script cada 3 s.
  server.on("/estado", HTTP_GET, [](AsyncWebServerRequest* request) {
    // La pagina pide esto cada 3 s, y la subida del firmware sale de esa misma
    // pagina: durante una actualizacion, cada refresco abre otra conexion y
    // arma un bloque de HTML de varios KB en la MISMA tarea que esta
    // escribiendo flash. Mientras dura el OTA se contesta con una linea.
    if (otaActiva()) {
      request->send(200, "text/html", "<p>Actualizando firmware...</p>");
      return;
    }
    request->send(200, "text/html", bloqueVivo());
  });

  // Igual que /estado: durante una actualizacion no se arma HTML, que se
  // estaria construyendo en la misma tarea que escribe la flash.
  server.on("/diag", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (otaActiva()) {
      request->send(200, "text/html", "");
      return;
    }
    request->send(200, "text/html", bloqueDiagnostico());
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

  // Actualizacion de firmware. El primer lambda se llama cuando termino de
  // subir TODO; el segundo, por cada pedazo que va llegando.
  server.on("/actualizar", HTTP_POST,
    [](AsyncWebServerRequest* request) {
      // Lo primero de todo: que llego. Si el Content-Type no arranca con
      // "multipart/form-data", la libreria NO parsea el cuerpo como archivo y
      // el callback de subida no corre nunca, por mas que el POST haya llegado.
      Serial.printf("[OTA] POST /actualizar  tipo=\"%s\"  largo=%u  params=%u  subida=%d\n",
                    request->contentType().c_str(), (unsigned)request->contentLength(),
                    (unsigned)request->params(), (int)otaEnCurso);

      bool ok = otaEnCurso && !otaError && !Update.hasError();

      // Si el callback de subida no corrio ni una vez, el problema no es el
      // firmware: no llego el archivo. Distinguirlo ahorra media hora de
      // buscar del lado equivocado.
      if (!ok && !otaEnCurso) otaFalla("no llego ningun archivo al servidor");
      if (!ok && otaMsg[0] == '\0') otaFalla(Update.errorString());

      Serial.printf("[OTA] fin: ok=%d recibido=%u de %u\n",
                    (int)ok, (unsigned)otaRecibido, (unsigned)otaTotal);

      String pagina = ok
        ? "<h1>Firmware actualizado</h1><p>La consola se esta reiniciando. "
          "Volve al panel en unos segundos.</p>"
        : "<h1>Fallo la actualizacion</h1><p>La consola sigue con el firmware "
          "anterior.</p><p><b>Motivo:</b> " + String(otaMsg) + "</p>"
          "<p>Recibidos " + String(otaRecibido) + " de " + String(otaTotal) + " bytes.</p>";
      AsyncWebServerResponse* res = request->beginResponse(200, "text/html", pagina);
      res->addHeader("Connection", "close");
      request->send(res);

      // Reiniciar aca mismo cortaria la respuesta antes de que salga: se agenda
      // y lo hace el loop(), que ademas ya no esta corriendo ningun juego.
      if (ok) otaReinicioEn = millis() + 800;
      else    otaErrorHasta = millis() + 4000;
    },
    [](AsyncWebServerRequest* request, const String& filename, size_t index,
       uint8_t* data, size_t len, bool final) {
      if (index == 0) {
        // OJO CON EL ORDEN: otaEnCurso va ULTIMO. Es la bandera que habilita a
        // loopOTA(), que corre en la tarea del loop() y en el otro nucleo, y
        // que lee todo lo demas para dibujar el progreso. Publicarla primero
        // deja una ventana en la que el otro nucleo entra a mirar valores que
        // todavia no se escribieron.
        otaError    = false;
        otaRecibido = 0;
        otaTotal    = request->contentLength();
        otaMsg[0]   = '\0';
        otaEnCurso  = true;

        Serial.printf("[OTA] primer bloque: archivo=\"%s\" len=%u byte0=0x%02X "
                      "contentLength=%u heap=%u\n",
                      filename.c_str(), (unsigned)len, (len > 0) ? data[0] : 0,
                      (unsigned)otaTotal, (unsigned)ESP.getFreeHeap());

        // Un firmware de ESP32 siempre arranca con 0xE9. Sin este chequeo,
        // subir cualquier otro archivo por error borra igual la particion.
        if (len < 1 || data[0] != 0xE9) {
          otaError = true;
          otaFalla("el archivo no arranca con 0xE9: no es un firmware");
          return;
        }
        // Por las dudas de que haya quedado a medias un intento anterior: con
        // una actualizacion "corriendo", begin() devuelve false y no dice por que.
        if (Update.isRunning()) Update.abort();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          otaError = true;
          otaFalla(Update.errorString());
          return;
        }
      }
      if (otaError) return;

      if (Update.write(data, len) != len) {
        otaError = true;
        otaFalla(Update.errorString());
        return;
      }
      otaRecibido += len;
      if (final) {
        Serial.printf("[OTA] ultimo bloque: %u bytes escritos\n", (unsigned)otaRecibido);
        if (!Update.end(true)) { otaError = true; otaFalla(Update.errorString()); }
      }
    });

  server.begin();
}

// ---------- Pantalla de la actualizacion ----------
// La tira hace de barra de progreso y el LCD lleva el porcentaje. Se repinta a
// ~10 fps y no en cada pasada: mientras esto dibuja, la otra tarea esta
// escribiendo flash, y no tiene sentido pelearle el bus por cuadros que nadie
// va a ver. Un pixel glitcheado aca no es grave; colgarse, si.
void loopOTA() {
  actualizarBuzzer();          // deja terminar lo que estuviera sonando

  if (otaReinicioEn && millis() >= otaReinicioEn) ESP.restart();

  // El cartel de error se muestra un rato y despues la consola sigue viviendo.
  if (otaError && otaErrorHasta && millis() >= otaErrorHasta) {
    otaEnCurso = otaError = false;
    otaErrorHasta = 0;
    volverAlMenu();
    return;
  }

  // SIN VENCIMIENTO POR AHORA.
  //
  // Habia aca una red de seguridad que abortaba la subida despues de 15 s sin
  // recibir datos, para que una conexion cortada no dejara la consola colgada
  // en la barra de progreso. Se disparaba a los pocos milisegundos de empezar,
  // matando transferencias que iban bien, y no se pudo explicar por que. Queda
  // afuera hasta entenderlo: una consola colgada en una barra se destraba
  // apretando reset, una actualizacion que nunca puede completarse no.

  // El total incluye el sobre del multipart, unos cientos de bytes sobre casi
  // un mega: alcanza y sobra para una barra.
  uint8_t pct = (otaTotal > 0) ? (uint8_t)((otaRecibido * 100) / otaTotal) : 0;
  if (pct > 100) pct = 100;

  // Se repinta solo cuando cambia el tramo de 5%, o sea unas veinte veces en
  // toda la subida. Antes era cada 100 ms, y mientras la otra tarea escribe
  // flash, cada FastLED.show() y cada linea al LCD --que va por I2C a 100 kHz
  // con esperas bloqueantes-- es tiempo que el servidor no tiene para mandar
  // los ACK. Si no llega a tiempo, AsyncTCP cierra la conexion: la subida se
  // corta justo como se corto.
  static uint8_t ultimoTramo = 255;
  uint8_t tramo = otaError ? 200 : (pct / 5);
  if (tramo == ultimoTramo) return;
  ultimoTramo = tramo;

  if (!otaError && (pct % 10) == 0) {
    Serial.printf("[OTA] %u%%  (%u de %u bytes)\n",
                  (unsigned)pct, (unsigned)otaRecibido, (unsigned)otaTotal);
  }

  // Con error se prende la tira ENTERA de rojo y no la barra: si el archivo se
  // rechazo en el primer byte, la barra estaria en cero y no se veria nada.
  FastLED.clear();
  int16_t hasta = otaError ? (int16_t)LARGO_TIRA
                           : (int16_t)(((int32_t)LARGO_TIRA * pct) / 100);
  CRGB c = otaError ? CRGB(255, 0, 0) : CRGB(0, 120, 255);
  for (int16_t i = 0; i < hasta; i++) setLed(i, c);
  FastLED.show();

  if (otaError) {
    lcdLinea(0, "Fallo la carga");
    lcdLinea(1, "Firmware intacto");   // el que corre es el de siempre
  } else {
    lcdLinea(0, "Firmware " + String(pct) + "%");   // "Actualizando 100%" son 17 columnas
    lcdLinea(1, barraLCD(pct, 100, 16));
  }
}
