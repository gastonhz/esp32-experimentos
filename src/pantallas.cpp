// ---------- Pantallas que no son juegos: Highscores, IP y el nombre del record ----------

#include "pantallas.h"
#include "panel_web.h"
#include "juego_carrera.h"

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
  fill_solid(leds, LARGO_TIRA, COL_RECORD);
  nscale8(leds, LARGO_TIRA, b);
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
  fill_solid(leds, LARGO_TIRA, CONTROLES[nomJug].color);
  nscale8(leds, LARGO_TIRA, b);
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

// ---------- Ajustes: como esta montada la consola ----------
// Cuatro items. La cruz del Verde pasa de uno al siguiente y su boton arcade
// cambia el valor del que este marcado. El ultimo, Red, no cambia nada: es la
// ficha del panel web, que hasta ahora era una entrada propia del selector
// ("IP") y se mudo aca adentro.
//
// La tira dibuja el largo elegido: se prende hasta LARGO_TIRA y el resto queda
// negro, asi el ajuste se ve en la tira misma y no solo en el LCD. Es la unica
// forma de darse cuenta de una si la consola quedo configurada para la tira
// corta con la larga enchufada.
enum ItemAjuste { AJ_LARGO, AJ_ORIENTACION, AJ_SILENCIO, AJ_PISTA, AJ_RED, AJ_ITEMS };

static uint8_t ajItem;
static bool    ajRegla = true;    // la regla encima de la pista, en el item Pista

// Regla para contar LEDs sobre la tira ya colocada: una marca cada 10 y una mas
// fuerte cada 50. Es lo que hace falta para anotar "de tal a tal LED" caminando
// al lado de la tira antes de cargar la pista en el panel web.
static void dibujarRegla() {
  for (int16_t i = 0; i < LARGO_TIRA; i += 10) {
    setLed(i, (i % 50 == 0) ? CRGB(255, 255, 255) : CRGB(90, 90, 90));
  }
}

void nuevoAjustes() {
  ajItem = 0;
  lcdForzarRefresh();
}

void loopAjustes() {
  int8_t paso = joystickPaso(0);
  if (paso) {
    ajItem = (ajItem + AJ_ITEMS + paso) % AJ_ITEMS;
    beep(1200, 25);
  }

  if (btnFlanco[0]) {
    switch (ajItem) {
      case AJ_LARGO:
        ponerLargoTira((LARGO_TIRA >= 200) ? 100 : 200);
        break;
      case AJ_ORIENTACION:
        ponerOrientacion((ORIENTACION == TIRA_HORIZONTAL) ? TIRA_VERTICAL : TIRA_HORIZONTAL);
        break;
      case AJ_SILENCIO:
        // Al silenciar, el beep de confirmacion no suena --que es justamente la
        // confirmacion--; al devolver el sonido, si.
        ponerSilencio(!SILENCIO);
        break;
      case AJ_PISTA:
        // La regla tapa un LED de cada diez: se apaga para ver el borde exacto
        // de cada tramo, y se prende para contar.
        ajRegla = !ajRegla;
        break;
      default:
        break;                    // Red no tiene nada que cambiar
    }
    if (ajItem != AJ_RED) beep(1600, 40);
  }

  // En Pista la tira muestra los tramos cargados, con la regla encima si esta
  // prendida. En el resto de los items muestra el largo elegido.
  FastLED.clear();
  if (ajItem == AJ_PISTA) {
    dibujarPistaCargada();
    if (ajRegla) dibujarRegla();
  } else {
    fill_solid(leds, LARGO_TIRA, CRGB(0, 60, 255));
    nscale8(leds, LARGO_TIRA, 30);
  }
  FastLED.show();
}

// Los items editables van con el nombre y las flechas arriba y el valor abajo.
// El de la red usa las DOS filas para el SSID y la IP juntos: es el unico sin
// valor que cambiar, y lo que se quiere ahi es leer las dos cosas de una para
// tipearlas en el celular. La clave del WiFi no se muestra: la sabe el que armo
// la consola. Pista no muestra un valor sino el estado de la regla, porque lo
// que hay para mirar ahi esta en la tira y no en el display.
void lcdAjustes() {
  if (ajItem == AJ_RED) {
    lcdLinea(0, AP_SSID);
    lcdLinea(1, apIP);
    return;
  }

  static const char* TITULO[] = { "< Largo tira >", "< Orientacion >", "< Silenciar >", "< Pista >" };
  String valor;
  switch (ajItem) {
    case AJ_LARGO:       valor = " " + String(LARGO_TIRA) + " LEDs"; break;
    case AJ_ORIENTACION: valor = (ORIENTACION == TIRA_HORIZONTAL) ? "Horizontal" : " Vertical"; break;
    case AJ_PISTA:       valor = ajRegla ? " con regla" : " sin regla"; break;
    default:             valor = SILENCIO ? " si" : " no"; break;
  }

  // En Pista el titulo lleva cuantos tramos hay cargados: con cero, cada carrera
  // sortea sus cuestas, y eso se tiene que ver sin ir hasta el panel web.
  String titulo = TITULO[ajItem];
  if (ajItem == AJ_PISTA) {
    uint8_t n = tramosCargados();
    titulo = n ? ("< Pista: " + String(n) + " >") : "< Pista: azar >";
  }
  lcdLinea(0, titulo);
  lcdLinea(1, "pulsa:" + valor);
}

String webAjustes() {
  return "(ajustes de la consola)";
}
