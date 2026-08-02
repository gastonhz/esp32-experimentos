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
#define NUM_LEDS    100
#define DATA_PIN    16          // -> SN74AHCT125N -> 470ohm -> DIN de la tira
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

// Botones arcade (microswitch) a GND, con pull-up interno. Apretado = LOW.
#define BTN_P1 14
#define BTN_P2 27

#define BUZZER_PIN 25           // buzzer pasivo por LEDC
#define BUZZER_CH  0            // canal LEDC (core arduino-esp32 2.0.x)

#define RESET_PIN  18           // pulsador: vuelve al menu desde cualquier juego

#define POT_PIN    34           // potenciometro B10k (ADC1, input-only)

// Joystick analogico PS2. La tira va montada vertical, asi que el eje Y mueve
// al jugador; el eje X, que casi ningun juego usa, navega los menus.
#define JOY_Y_PIN  33           // VRy -> ADC1_CH5
#define JOY_X_PIN  32           // VRx -> ADC1_CH4

#define LCD_ADDR 0x27
#define LCD_SDA  21
#define LCD_SCL  22

// ---------- Tira ----------
extern CRGB    leds[NUM_LEDS];
extern uint16_t BRILLO;         // techo de seguridad de corriente (tuneable por web)

// ---------- Colores compartidos ----------
#define COL_PELOTA  CRGB::White
#define COL_P1      CRGB(0, 255, 0)   // verde  (jugador 1, inicio de la tira)
#define COL_P2      CRGB(0, 60, 255)  // azul   (jugador 2, final de la tira)

// Dorado exclusivo de los records: no lo usa ningun juego, asi que cuando la
// tira chispea de este color se entiende solo que es un record nuevo.
extern const CRGB COL_RECORD;

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
extern bool btnFlanco[2];       // true un solo frame, al recien presionar
extern bool btnEstable[2];      // true mientras esta presionado (ya con debounce)
void actualizarBotones();

// ---------- Potenciometro ----------
// Devuelve la lectura cruda promediada (0..4095). Cada juego decide que mapea
// y cuando: los que fijan dificultad lo leen UNA vez, al arrancar la partida.
uint16_t leerPoteCrudo();

// ---------- Joystick ----------
// Eje X en pasos discretos (-1/0/+1) con auto-repeat, para navegar menus.
int8_t joystickPasoX();
void   calibrarJoyX();          // una sola vez en setup(): el stick esta suelto al encender

// Eje Y como deflexion continua, de -1.0 a +1.0 (+ hacia el final de la tira),
// con zona muerta y arranque suave en el borde. Cada juego lo escala a lo suyo:
// velocidad, empuje o posicion absoluta.
float leerJoyYNorm();
// El centro real de un joystick barato no cae exacto en 2048 y el desvio puede
// superar la zona muerta (se siente como que el personaje "cae" solo). Por eso
// todo juego que use el eje Y llama a esto al arrancar la partida, asumiendo
// que nadie esta tocando el stick en ese instante.
void calibrarJoyY();

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
enum Record { REC_PONG, REC_TUG, REC_RC, REC_TWANG,
              REC_PADDLE, REC_STACKER, REC_ESQUIVA, REC_LANDER, REC_DUELO,
              REC_CARRERA, NUM_RECORDS };

struct RecordDef {
  const char* clave;        // clave en NVS: NO cambiar, se perderia el record guardado
  const char* nombre;       // como se muestra en el LCD y en el panel web
  const char* prefijo;      // texto antes del numero  ("Nivel ")
  const char* unidad;       // texto despues del numero (" golpes"), con su espacio
  bool        menorEsMejor; // para records de tiempo: gana el valor mas chico
};

// Sin el tamano a proposito: asi el static_assert de consola.cpp es el que
// verifica que la tabla tenga exactamente NUM_RECORDS filas. Con el tamano
// puesto, una fila de menos se rellenaria con ceros en silencio y la clave NVS
// quedaria en nullptr.
extern const RecordDef RECORDS[];
extern uint32_t hsValor[NUM_RECORDS];

void   iniciarRecords();
bool   intentarRecord(uint8_t rec, uint32_t valor);   // true si quedo grabado
String textoRecord(uint8_t rec);                      // "12 golpes" / "---"

// ---------- Pantallas y juegos ----------
enum Pantalla { MENU, JUEGO };
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
};

enum Juego { JUEGO_PONG, JUEGO_TUG, JUEGO_ROMPECOLORES, JUEGO_TWANG,
             JUEGO_PADDLE, JUEGO_STACKER, JUEGO_ESQUIVA, JUEGO_LANDER, JUEGO_DUELO,
             JUEGO_CARRERA, JUEGO_AMBIENTE, JUEGO_HIGHSCORES, JUEGO_IP, NUM_JUEGOS };

// Idem RECORDS: el tamano lo chequea el static_assert de main.cpp. Una fila de
// menos aca serian punteros a funcion nulos y la consola se reiniciaria al
// entrar al ultimo juego de la lista.
extern const JuegoDef JUEGOS[];
extern uint8_t juegoSel;      // indice resaltado en el menu
extern uint8_t juegoActivo;   // juego que se esta jugando

void volverAlMenu();
void iniciarJuego(uint8_t j);
