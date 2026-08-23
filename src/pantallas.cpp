// ---------- Pantallas que no son juegos: Highscores, IP y el nombre del record ----------

#include "pantallas.h"
#include "panel_web.h"

// ---------- Highscores: vitrina de records ----------
// Un record por pantalla. Se pasa de juego con el joystick, igual que en el
// menu, y el pulsador de reset vuelve al selector (eso lo maneja main.cpp).
static uint8_t verIndice = 0;   // que record se esta mirando (0..NUM_RECORDS-1)

void nuevoHighscores() {
  verIndice = 0;
  lcdForzarRefresh();
}

void loopHighscores() {
  int8_t paso = joystickPaso(0);
  if (paso) {
    verIndice = (verIndice + NUM_RECORDS + paso) % NUM_RECORDS;
    beep(1200, 25);
  }

  uint8_t b = beatsin8(20, 10, 60);      // dorado respirando, sin prisa
  fill_solid(leds, NUM_LEDS, COL_RECORD);
  nscale8(leds, NUM_LEDS, b);
  FastLED.show();
}

// "Rec:" en vez de "Record:" porque con la unidad al lado no entra en 16 columnas.
// Cuando el record tiene duenio, las iniciales ocupan ese lugar: "GAS 12 golpes"
// ya se entiende sin rotulo y entra en las 16 columnas hasta con la unidad mas
// larga. Los records viejos, de antes de que se pudiera firmar, siguen con "Rec:".
void lcdHighscores() {
  lcdLinea(0, RECORDS[verIndice].nombre);
  String firma = String(hsNombre[verIndice]);
  lcdLinea(1, firma.length() ? (firma + " " + textoRecord(verIndice))
                             : ("Rec: " + textoRecord(verIndice)));
}

String webHighscores() {
  return "(vitrina de records)";
}

// ---------- Nombre del record: tres letras, como en los fichines ----------
// No se entra desde el selector: la enciende volverAlMenu() cuando la partida
// que acaba de terminar dejo un record pendiente de firmar, asi que sale
// pegada al cartel de "NUEVO RECORD" de cada juego.
//
// Firma el control que hizo el record (intentarRecord() lo dejo anotado), no
// siempre el Verde: en Carrera es el que hizo la vuelta rapida, en Pelea el
// ultimo en pie. Para que se sepa de quien es el turno sin leer nada, la tira
// se pone del color de ese jugador con las chispas doradas del record encima.
//
// Controles, todos del que firma:
//   izquierda/derecha  cambia la letra marcada (el eje vertical no hace nada)
//   boton arcade       confirma la letra y pasa a la siguiente; con la tercera guarda
//   boton del stick    vuelve a la letra anterior, por si se paso
//   pulsador de reset  se va sin firmar (lo maneja main.cpp)
static const char    NOM_ALFABETO[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const uint8_t NOM_LETRAS     = sizeof(NOM_ALFABETO) - 1;   // sin el \0 del final

// Si el que jugo se fue, la consola no puede quedarse trabada aca para siempre:
// pasado este rato sin tocar nada se guarda lo que haya en pantalla.
static const uint32_t NOM_TIMEOUT_MS = 30000;

// El boton no cuenta durante el primer momento: en los juegos de machacar el
// boton (Carrera, Tira y Afloja) el jugador todavia lo esta apretando cuando
// aparece el cartel, y sin esto confirmaria la primera letra sin verla.
static const uint32_t NOM_GRACIA_MS = 700;

static uint8_t  nomIdx[LARGO_NOMBRE];   // que letra del alfabeto muestra cada posicion
static uint8_t  nomSlot;                // cual se esta editando (0..LARGO_NOMBRE-1)
static uint8_t  nomJug;                 // control que firma: el que hizo el record
static uint32_t nomDesde;               // millis() en que aparecio la pantalla
static uint32_t nomUltimoToque;         // millis() del ultimo movimiento, para el timeout

void nuevoNombre() {
  for (uint8_t i = 0; i < LARGO_NOMBRE; i++) nomIdx[i] = 0;   // "AAA", como en los fichines
  nomSlot        = 0;
  nomJug         = jugadorPendiente();
  nomDesde       = millis();
  nomUltimoToque = nomDesde;
  lcdForzarRefresh();
}

static void guardarYSalir() {
  int8_t rec = recordPendiente();
  if (rec >= 0) {
    char firma[LARGO_NOMBRE + 1];
    for (uint8_t i = 0; i < LARGO_NOMBRE; i++) firma[i] = NOM_ALFABETO[nomIdx[i]];
    firma[LARGO_NOMBRE] = '\0';
    ponerNombreRecord((uint8_t)rec, firma);
  }
  volverAlMenu();          // ya sin pendiente, asi que esta vez si cae en el menu
}

void loopNombre() {
  // Red de seguridad: sin record pendiente esta pantalla no tiene nada que
  // hacer (no deberia poder pasar, pero quedarse colgado aca seria peor).
  if (recordPendiente() < 0) { volverAlMenu(); return; }

  uint32_t ahora = millis();

  // La letra se elige a lo ancho, que es para donde apuntan las flechas.
  int8_t paso = joystickPasoEje(nomJug, EJE_CRUZ);
  if (paso) {
    nomIdx[nomSlot] = (nomIdx[nomSlot] + NOM_LETRAS + paso) % NOM_LETRAS;
    nomUltimoToque  = ahora;
    beep(1500, 20);
  }

  // Volver atras no esta anunciado en el display (no hay renglon libre), pero
  // sin esto una letra de mas obliga a cancelar la firma entera.
  if (btnStickFlanco[nomJug] && nomSlot > 0) {
    nomSlot--;
    nomUltimoToque = ahora;
    beep(700, 30);
    return;
  }

  bool confirma = btnFlanco[nomJug] && (ahora - nomDesde) > NOM_GRACIA_MS;
  if (confirma) {
    nomSlot++;
    nomUltimoToque = ahora;
    if (nomSlot >= LARGO_NOMBRE) {      // confirmo la tercera: se guarda
      beep(2000, 60);
      guardarYSalir();
      return;
    }
    beep(1800, 25);
  }

  // Nadie firma: se guarda igual lo que haya quedado en pantalla.
  if (ahora - nomUltimoToque > NOM_TIMEOUT_MS) { guardarYSalir(); return; }

  // Fondo del color del que firma, respirando, con las chispas del record: se
  // entiende de quien es el turno sin tener que decirlo en el LCD.
  uint8_t b = beatsin8(20, 10, 60);
  fill_solid(leds, NUM_LEDS, CONTROLES[nomJug].color);
  nscale8(leds, NUM_LEDS, b);
  dibujarChispasRecord();
  FastLED.show();
}

// Las tres letras en celdas de ancho fijo (cinco columnas cada una), asi solo
// se mueven los < >. Con el espacio de adelante son exactamente las 16 columnas
// del display y las letras caen siempre en la 3, la 8 y la 13:
//
//     " < A >  A    A  "
//
// El renglon de abajo dice lo que hace el boton AHORA: en las dos primeras
// letras confirma y pasa a la siguiente, y recien en la tercera guarda.
void lcdNombre() {
  String s = " ";
  for (uint8_t i = 0; i < LARGO_NOMBRE; i++) {
    s += (i == nomSlot) ? "< " : "  ";
    s += NOM_ALFABETO[nomIdx[i]];
    s += (i == nomSlot) ? " >" : "  ";
  }
  lcdLinea(0, s);
  lcdLinea(1, (nomSlot == LARGO_NOMBRE - 1) ? "pulsa p/ guardar" : "pulsa p/ elegir");
}

// ---------- IP: como entrar al panel web ----------
// El LCD muestra la red y la IP (que ya se calcularon al levantar el AP) y la
// tira queda en un azul tenue, sin animacion, para que se note que aca no hay
// nada que jugar.
void nuevoIP() {
  lcdForzarRefresh();
}

void loopIP() {
  fill_solid(leds, NUM_LEDS, CRGB(0, 60, 255));
  nscale8(leds, NUM_LEDS, 30);
  FastLED.show();
}

// El SSID va solo, sin prefijo: el nombre de la red ya se entiende por si mismo
// y asi queda lugar para la IP completa en la otra fila. La clave no se muestra:
// la sabe el que armo la consola.
void lcdIP() {
  lcdLinea(0, AP_SSID);
  lcdLinea(1, apIP);
}

String webIP() {
  return "(pantalla informativa)";
}
