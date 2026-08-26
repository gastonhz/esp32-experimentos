/*********
  CONSOLA — infraestructura compartida por todos los juegos.

  Aca vive todo lo que NO es un juego: pines, la tira, los botones, el buzzer,
  el LCD, el joystick, el potenciometro y la tabla de records. Cada juego vive
  en su propio juego_*.cpp y solo usa lo que esta declarado en este header.

  Regla para agregar un juego nuevo (ver main.cpp):
    1. juego_x.h / juego_x.cpp con nuevoX() / loopX() / lcdX() / webX()
    2. una fila en la tabla JUEGOS[] de main.cpp
    3. (si puntua) una fila en RECORDS[] de consola.cpp
    4. (si tiene parametros tuneables) filas en PARAMS[] de panel_web.cpp
*********/
#pragma once

#include <Arduino.h>
#include <FastLED.h>

// ---------- Hardware ----------
// (ver scripts-y-pruebas/setup-hardware-maquina-juegos-led.md)
// La tira puede ser la corta de 100 LEDs o la larga de 200, y con la larga
// puesta se puede usar igual solo el primer tramo (los primeros 100 desde el
// extremo de datos). Cual se usa se elige en Ajustes y queda guardado en NVS.
//
// LEDS_MAX dimensiona los buffers: siempre la tira mas larga posible, porque el
// array no se puede redimensionar en caliente. LARGO_TIRA es lo que la consola
// usa AHORA, y es una VARIABLE: ningun juego puede cachearla en una constante
// de archivo, tiene que leerla cada vez (o al empezar la partida).
#define LEDS_MAX    200         // tira fisica mas larga que se puede enchufar
extern uint16_t LARGO_TIRA;     // 100 o 200: lo que se usa ahora
#define DATA_PIN    16          // -> SN74AHCT125N -> 470ohm -> DIN de la tira
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

// ---------- Controles ----------
// Cuatro controles iguales, cada uno con un joystick HW-504 (dos ejes + boton)
// y un boton arcade, sobre un cable de 1,5 m y ficha DB9:
//
//   DB9  1=GND  3=3V3  5=SW del stick  7=VRy  8=arcade  9=VRx
//
// Los dos botones van a GND con pull-up interno, asi que apretado = LOW.
#define NUM_CONTROLES 4

#define BTN_C1 14               // boton arcade (DB9 pin 8)
#define BTN_C2 27
#define BTN_C3 32               // pin liberado al pasar los ejes al ADS1115
#define BTN_C4 33               // idem

#define SW_C1  4                // boton del propio joystick (DB9 pin 5)
#define SW_C2  13
#define SW_C3  17
#define SW_C4  19

#define BUZZER_PIN 25           // buzzer pasivo por LEDC
#define BUZZER_CH  0            // canal LEDC (core arduino-esp32 2.0.x)

#define RESET_PIN  18           // pulsador: vuelve al menu desde cualquier juego

#define POT_PIN    34           // potenciometro B10k (ADC1, input-only)

// Los ocho ejes entran por dos ADS1115 (ADC I2C de 16 bits) en un bus PROPIO,
// separado del LCD. Tener bus aparte no es capricho: el backpack del display
// corre a 5V y limita a 100 kHz, y asi los ejes van a 400 kHz y 3.3V, en spec
// de punta a punta y sin que un cuelgue del LCD congele los controles.
#define ADS_SDA    23
#define ADS_SCL    26
#define ADS_FREQ   400000UL
#define ADS_ADDR_A 0x48         // ADDR al aire (lo baja su pull-down de 10k)
#define ADS_ADDR_B 0x49         // ADDR puenteado al riel de 3.3V

#define LCD_ADDR 0x27
#define LCD_SDA  21
#define LCD_SCL  22

// ---------- Tira ----------
extern CRGB    leds[LEDS_MAX];
extern uint16_t BRILLO;         // techo de seguridad de corriente (tuneable por web)

// ---------- Ajustes de la consola ----------
// Lo que depende de como esta montada la tira y antes obligaba a recompilar. Se
// cambian en la entrada "Ajustes" del selector y desde el panel web. El largo y
// la orientacion se guardan en NVS --son hardware, no tiene sentido volver a
// configurarlos en cada encendido--; el silencio NO, vuelve a "no" al apagar.
enum Orientacion { TIRA_VERTICAL,      // parada: el stick de arriba a abajo corre la tira
                   TIRA_HORIZONTAL };  // acostada: la corre el de izquierda a derecha
extern uint8_t ORIENTACION;
extern bool    SILENCIO;               // mute global

// Lee lo guardado y deja LARGO_TIRA, ORIENTACION y BRILLO listos. Va ANTES de
// FastLED.addLeds(), que necesita el largo ya resuelto.
void iniciarAjustes();
void ponerLargoTira(uint16_t largo);   // 100 o 200: apaga la cola, avisa a FastLED y guarda
void ponerOrientacion(uint8_t orient);
void ponerSilencio(bool s);            // corta en seco lo que este sonando

// ---------- Escala de los juegos ----------
// Las constantes de los juegos estan tuneadas para una tira de 100 LEDs. Los
// juegos PROPORCIONALES (Pong, Paddle, Stacker, Alunizaje, Reaccion, Pelea y
// Tiros) las pasan por estas funciones: en la tira larga todo mide el doble y
// la partida se siente igual, solo mas grande.
//
// Los juegos donde el largo ES el juego NO las usan, a proposito: en Carrera,
// Tira y Afloja, Rompecolores, Salta Muros, Twang y Ambiente, 200 LEDs son el
// doble de pista, de muro, de mazmorra o de tira que empujar. Por eso mismo
// esos guardan un record por cada largo (ver RecordDef::porLargo).
int16_t  escalaLeds(int16_t leds100);      // tamanos y distancias, en LEDs
uint16_t escalaVel(uint16_t ledsPorSeg);   // velocidades en LEDs/s
uint16_t escalaMsPorLed(uint16_t ms);      // periodos en ms por LED: cruzar tarda lo mismo

// ---------- Colores compartidos ----------
#define COL_PELOTA  CRGB::White
#define COL_P1      CRGB(0, 255, 0)   // verde  (jugador 1, inicio de la tira)
#define COL_P2      CRGB(0, 60, 255)  // azul   (jugador 2, final de la tira)

// Dorado exclusivo de los records: no lo usa ningun juego, asi que cuando la
// tira chispea de este color se entiende solo que es un record nuevo.
extern const CRGB COL_RECORD;

// ---------- Los cuatro controles ----------
// Nombre, inicial y color de cada control, en una sola tabla. Los juegos de dos
// jugadores siguen usando COL_P1/COL_P2 y no se enteran de que esto existe; los
// de cuatro se dibujan y se rotulan desde aca.
//
// Las abreviaturas son de DOS letras porque Azul y Amarillo empiezan igual: con
// una sola, el marcador de cuatro ("Ve1 Az0 Ro2 Am0") quedaba ambiguo. Con dos
// entra igual en las 16 columnas del LCD. Si algun control fisico es de otro
// color, esta tabla es el unico lugar que hay que tocar.
struct ControlDef {
  const char* nombre;     // para los mensajes largos ("GANA Amarillo")
  const char* abrev;      // dos letras, para los marcadores de cuatro
  CRGB        color;      // con que se dibuja ese jugador en la tira
};
extern const ControlDef CONTROLES[NUM_CONTROLES];

// "** GANA Verde **" entra en 16 columnas, pero "Amarillo" no. Esto elige la
// forma mas vistosa que entre, para que ningun juego tenga que preocuparse.
String textoGana(uint8_t jugador);

const uint8_t ESTELA = 2;       // largo de la cola de un punto en movimiento

// ---------- Dibujo ----------
void setLed(int16_t i, const CRGB& c);
// Punto brillante con la cola desvaneciendose DETRAS (contra el sentido de la
// marcha). Lo usan la pelota de Pong, las balas de Rompecolores y el Paddle.
void dibujarPuntoConEstela(int16_t pos, int8_t dir, const CRGB& col, uint8_t largo = ESTELA);
// Chispas doradas al azar por encima de lo que ya haya dibujado: se pinta sobre
// la animacion de fin de cualquier juego cuando la partida fue record.
void dibujarChispasRecord();

// ---------- Botones ----------
// Todo se indexa por control: 0 = C1 ... 3 = C4. Los juegos de dos jugadores
// usan el 0 y el 1, y los de uno solo siempre el 0.
extern bool btnFlanco[NUM_CONTROLES];    // arcade: true un frame, al presionar
extern bool btnEstable[NUM_CONTROLES];   // arcade: true mientras esta apretado
// El boton del propio joystick, que antes no existia. Se expone aparte del
// arcade para que ningun juego lo confunda con el boton de accion de siempre.
extern bool btnStickFlanco[NUM_CONTROLES];
extern bool btnStickEstable[NUM_CONTROLES];
void actualizarBotones();

// ---------- Potenciometro ----------
// Devuelve la lectura cruda promediada (0..4095). Cada juego decide que mapea
// y cuando: los que fijan dificultad lo leen UNA vez, al arrancar la partida.
uint16_t leerPoteCrudo();

// ---------- Joysticks ----------
// Dos ejes por control, indexados igual que los botones. Se nombran por lo que
// HACEN y no por la serigrafia del modulo: el HW-504 va montado rotado adentro
// del control, asi que su "VRx" es el eje vertical del stick.
enum Eje { EJE_TIRA,            // VRx (A0/A2): + hacia el final de la tira
           EJE_CRUZ,            // VRy (A1/A3): + hacia la derecha
           NUM_EJES };

// Abre el bus y detecta que modulos hay. Un modulo ausente no rompe nada: sus
// controles quedan quietos en el centro.
void iniciarJoysticks();

// Una ronda del barrido de los ocho ejes. Va en cada pasada del loop(): no
// bloquea, arranca las conversiones de los dos chips a la vez y recoge el
// resultado en una pasada posterior, cuando ya paso el tiempo de conversion.
void actualizarJoysticks();

// Deflexion continua, de -1.0 a +1.0, con zona muerta y arranque suave en el
// borde. Cada juego la escala a lo suyo: velocidad, empuje o posicion absoluta.
float leerJoyNorm(uint8_t jugador);   // eje de la tira (el de toda la vida)
float leerJoyCruz(uint8_t jugador);   // eje transversal, todavia sin usar

// Un eje en pasos discretos (-1/0/+1) con auto-repeat, para navegar menus. Usa
// una zona muerta mas ancha que leerJoyNorm: elegir en una lista no necesita
// precision, y asi el menu no se mueve solo. El auto-repeat lleva estado propio
// por control Y por eje, asi que los dos ejes se pueden usar a la vez (la
// pantalla de las tres letras del record mueve el cursor con uno y cambia la
// letra con el otro).
int8_t joystickPasoEje(uint8_t jugador, uint8_t eje);
int8_t joystickPaso(uint8_t jugador);   // el transversal, que es el de los menus

// El centro real de un joystick barato no cae exacto en 2048 y el desvio puede
// superar la zona muerta (se siente como que el personaje "cae" solo). Por eso
// todo juego que use el eje llama a calibrarJoy() al arrancar la partida,
// asumiendo que nadie esta tocando el stick en ese instante.
//
// Con fichas DB9 hay un modo de falla nuevo: si un control esta desenchufado,
// su entrada del ADS flota en ~765 y calibrarla ahi dejaria ese eje clavado a
// fondo para siempre. Por eso el centro medido se acepta solo si cae en una
// banda plausible; si no, ese eje se marca ausente y devuelve 0 hasta la
// proxima calibracion (o sea, hasta que empiece la proxima partida).
void calibrarJoy(uint8_t jugador);
void calibrarJoys();            // los cuatro, una sola vez en setup()

// Que controles hay enchufados, segun la ultima calibracion: un DB9 desconectado
// deja su entrada del ADS flotando fuera de la banda plausible y se detecta ahi.
// Los juegos de cuatro jugadores lo usan para repartir la partida entre los que
// esten, en vez de exigir los cuatro siempre.
//
// Se decide con que UNO de los dos ejes haya calibrado bien: si alguien tenia el
// stick agarrado justo al empezar, hace falta una diagonal bastante decidida
// para sacar a los dos ejes de la banda a la vez.
bool    controlPresente(uint8_t jugador);
uint8_t numControles();         // cuantos hay enchufados (0..NUM_CONTROLES)

// ---------- Selector de cantidad de jugadores ----------
// Pantalla previa compartida por los juegos que admiten de dos a cuatro. Existe
// porque tener los cuatro controles enchufados no quiere decir que siempre haya
// cuatro personas: sin esto, jugar de a dos obligaba a ir desenchufando fichas.
//
// Los que juegan son SIEMPRE los primeros n controles (C1..Cn), que es como
// estan puestos sobre la mesa. La deteccion de presencia queda solo para
// sugerir el numero de arranque.
uint8_t jugadoresSugeridos();                  // controles enchufados, acotado a 2..4
bool    loopSelectorJugadores(uint8_t& n);     // un frame; true cuando se confirmo
void    lcdSelectorJugadores(const char* titulo, uint8_t n);

// Zonas muertas, en cuentas de 0..4095. Tuneables desde el panel web porque el
// punto justo se encuentra con el control en la mano, no compilando: el ADS1115
// deja el reposo temblando +-2 cuentas, asi que hay MUCHO margen para bajarlas
// respecto de lo que necesitaba el ADC interno del ESP32.
extern uint16_t JOY_MUERTA_JUEGO;
extern uint16_t JOY_MUERTA_MENU;

// ---------- Buzzer (LEDC, no bloqueante) ----------
struct Nota { uint16_t freq; uint16_t dur; };   // freq 0 = silencio (pausa)

void beep(uint16_t freq, uint16_t dur);
void tocarJingle(const Nota* notas, uint8_t len);
void actualizarBuzzer();

void sonarVictoria();           // fin de partida ganada (Pong, Tira-Afloja)
void sonarRecord();             // reemplaza al sonido de fin cuando hubo record

// ---------- LCD 1602 ----------
// Centra el texto en 16 columnas y escribe la fila SOLO si cambio respecto de
// la cache: el I2C es lento y repintar cada frame haria stutter en el juego.
void lcdLinea(uint8_t fila, const String& txt);
void lcdForzarRefresh();        // al cambiar de pantalla: repinta las dos filas
// Splash de arranque. Si se mantiene apretado el pulsador de reset durante el
// splash, en vez de pasar de largo muestra por que se reinicio la placa la vez
// anterior (ver textoCausaReset).
void iniciarLCD();

// ---------- Diagnostico ----------
// Causa del ultimo reinicio, en texto corto para las 16 columnas del LCD. En
// una consola que vive en protoboard el dato que importa es si fue "Encendido"
// (arranque normal) o "BROWNOUT": lo segundo es la alimentacion cayendose, y
// manda a revisar cables y masas en vez de codigo.
String textoCausaReset();
// Barra de progreso de `ancho` columnas: "#####-----". Para combustible, tiempo
// restante y cualquier magnitud que se lea mejor de un vistazo que como numero.
String barraLCD(uint16_t valor, uint16_t maximo, uint8_t ancho);

// ---------- Records persistentes (NVS) ----------
// Un record por juego puntuable. Se escribe solo cuando se supera el valor
// viejo: la NVS tiene ciclos de escritura contados.
enum Record { REC_PONG, REC_TUG, REC_RC, REC_TWANG, REC_TWANG_COOP, REC_TWANG32,
              REC_PADDLE, REC_STACKER, REC_ESQUIVA, REC_LANDER, REC_DUELO,
              REC_CARRERA, REC_PELEA, REC_WESTERN, NUM_RECORDS };

struct RecordDef {
  const char* clave;        // clave en NVS: NO cambiar, se perderia el record guardado
  const char* nombre;       // como se muestra en el LCD y en el panel web
  const char* prefijo;      // texto antes del numero  ("Nivel ")
  const char* unidad;       // texto despues del numero (" golpes"), con su espacio
  bool        menorEsMejor; // para records de tiempo: gana el valor mas chico
  // Los juegos donde el largo de la tira ES el juego guardan un record por cada
  // tira: una vuelta de Carrera en 200 LEDs no se puede comparar con una de 100,
  // y en Salta Muros o Rompecolores el numero mide otra cosa. Los demas usan el
  // mismo record en las dos, porque escalan y se juegan igual.
  bool        porLargo;
};

// Sin el tamano a proposito: asi el static_assert de consola.cpp es el que
// verifica que la tabla tenga exactamente NUM_RECORDS filas. Con el tamano
// puesto, una fila de menos se rellenaria con ceros en silencio y la clave NVS
// quedaria en nullptr.
extern const RecordDef RECORDS[];
extern uint32_t hsValor[NUM_RECORDS];

void   iniciarRecords();
// Los records por largo hay que traerlos de nuevo de la NVS al cambiar de tira.
// Lo llama ponerLargoTira(); ningun juego necesita saber que existe.
void   recargarRecords();
// El `jugador` es el control al que le pertenece el record: el que lo hizo, o el
// que gano la partida cuando el numero es de todos (los golpes de un peloteo de
// Pong los ponen los dos). Es quien despues firma con su propio control, asi que
// los juegos de un solo jugador lo dejan en el 0, que es el Verde de siempre.
bool   intentarRecord(uint8_t rec, uint32_t valor, uint8_t jugador = 0);   // true si quedo grabado
String textoRecord(uint8_t rec);                      // "12 golpes" / "---"

// ---------- Iniciales del que puso el record ----------
// Tres letras, como en los fichines. Se guardan al lado del valor, con la misma
// clave y una "N" al final ("hsPong" -> "hsPongN"), asi los records que ya
// estaban grabados en la flash siguen intactos y arrancan sin nombre.
#define LARGO_NOMBRE 3
extern char hsNombre[NUM_RECORDS][LARGO_NOMBRE + 1];   // "" si todavia no tiene

// Record recien batido al que le falta ponerle el nombre, o -1 si no hay
// ninguno. Lo deja armado intentarRecord() y lo consume la pantalla de las tres
// letras, que volverAlMenu() intercala antes del menu (ver pantallas.cpp).
int8_t  recordPendiente();
uint8_t jugadorPendiente();     // que control lo hizo, y por lo tanto lo firma
void   ponerNombreRecord(uint8_t rec, const char* nombre);  // graba y cierra el pendiente
void   cancelarNombreRecord();        // se va sin iniciales: el valor ya quedo grabado

// ---------- Guardado generico en NVS ----------
// Para lo que no es un record ni un ajuste de la consola: hoy, la pista fija de
// Carrera. La NVS la abre consola.cpp y el objeto Preferences no se expone, asi
// que todo lo que se guarde pasa por aca. Devuelven los bytes escritos/leidos
// (0 si la clave no existe todavia).
size_t guardarBlob(const char* clave, const void* datos, size_t n);
size_t leerBlob(const char* clave, void* datos, size_t n);

// ---------- Pantallas y juegos ----------
// NOMBRE es la pantallita de las tres letras: no es un juego ni una entrada del
// selector, se cuela sola entre el fin de una partida con record y el menu.
enum Pantalla { MENU, JUEGO, NOMBRE };
extern Pantalla pantalla;

// Cada entrada del selector es una fila de esta tabla (definida en main.cpp).
// Highscores e IP tambien entran aca aunque no sean juegos: tienen la misma
// forma (se entra, corren un frame, dibujan el LCD) y asi el menu es una sola
// lista sin casos especiales.
struct JuegoDef {
  const char* nombre;       // <=12 caracteres: en el menu se dibuja como "< nombre >"
  void   (*nuevo)();        // preparar una partida
  void   (*loop)();         // un frame
  void   (*lcd)();          // que mostrar en el 1602 mientras esta activo
  String (*web)();          // linea de estado para el panel web
  // Las entradas que no son partidas (Ambiente, Highscores, Ajustes) se salen
  // con un toque del boton del stick; de un juego hay que mantenerlo cinco
  // segundos. Se marca aca y no adivinando por el indice --los que no son
  // juegos estan al final de la tabla-- porque eso se rompe solo el dia que
  // alguien agregue un juego al final.
  bool        esPantalla;
};

enum Juego { JUEGO_PONG, JUEGO_TUG, JUEGO_ROMPECOLORES, JUEGO_TWANG,
             JUEGO_PADDLE, JUEGO_STACKER, JUEGO_ESQUIVA, JUEGO_LANDER, JUEGO_DUELO,
             JUEGO_CARRERA, JUEGO_PELEA, JUEGO_WESTERN,
             JUEGO_AMBIENTE, JUEGO_HIGHSCORES, JUEGO_AJUSTES, NUM_JUEGOS };

// Idem RECORDS: el tamano lo chequea el static_assert de main.cpp. Una fila de
// menos aca serian punteros a funcion nulos y la consola se reiniciaria al
// entrar al ultimo juego de la lista.
extern const JuegoDef JUEGOS[];
// Cuantas vueltas por segundo esta dando el loop de la consola. Lo mide y lo
// publica main.cpp (como JUEGOS[]), y lo mira el panel web: es el numero que
// dice si un juego se puso pesado, porque varios calculan el movimiento con el
// dt del frame y la tira larga tarda el doble en dibujarse.
extern uint16_t fpsConsola;

extern uint8_t juegoSel;      // indice resaltado en el menu
extern uint8_t juegoActivo;   // juego que se esta jugando

void volverAlMenu();
void iniciarJuego(uint8_t j);
