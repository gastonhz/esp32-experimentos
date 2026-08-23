// ---------- Duelo de reaccion ----------
// El unico juego de la consola que no usa ni el joystick ni la tira como espacio:
// la tira es puro semaforo. Lo que lo hace jugable no es la senal, son las dos
// reglas que castigan adivinar:
//
//   - apretar antes de la senal deja AFUERA de la ronda (salida en falso)
//   - de vez en cuando destella ROJO, que no vale: el que reacciona al color
//     equivocado tambien se queda afuera
//
// Sin eso, la estrategia optima seria martillar el boton y el juego no existiria.
//
// Juegan todos los controles que esten enchufados, de dos a cuatro. Con cuatro,
// una salida en falso NO termina la ronda: elimina al que se adelanto y los que
// quedan siguen esperando la senal. Recien cuando queda uno solo en pie la ronda
// se le adjudica. Con dos jugadores esa regla se reduce exactamente al duelo de
// siempre -- el que se adelanta pierde y gana el otro --, asi que no hay dos
// juegos distintos conviviendo, hay uno solo que escala.

#include "juego_duelo.h"

// ---------- Parametros ----------
uint16_t DUE_ESPERA_MIN = 2000;   // retardo minimo antes de la senal (ms)
uint16_t DUE_ESPERA_MAX = 8000;   // retardo maximo antes de la senal (ms)

const uint8_t  DUE_RONDAS_GANAR = 3;    // al mejor de cinco
const uint16_t DUE_FALSA_MS     = 160;  // cuanto dura un destello falso
const uint8_t  DUE_FALSA_PROB   = 45;   // de 255: probabilidad por chequeo
const uint16_t DUE_FALSA_CADA   = 700;  // cada cuanto se tira el dado de senal falsa
const uint16_t DUE_SALTO_MS     = 400;  // cuanto se marca al que se adelanto
const uint16_t DUE_RESULTADO_MS = 2600; // cuanto se muestra el resultado de la ronda
const uint16_t DUE_FIN_MS       = 3800;
const uint16_t DUE_TIEMPO_TOPE  = 2000; // reacciones mas lentas que esto no cuentan para el record

const uint8_t DUE_NADIE = 255;

// ---------- Estado ----------
enum EstadoDue { DUE_ESPERA, DUE_SENAL, DUE_RESULTADO, DUE_FIN };
static EstadoDue estadoDue;
static uint32_t  faseDesde;

static uint32_t senalEn;          // millis() en que aparece la senal de esta ronda
static uint32_t falsaHasta;       // millis() hasta el que dura el destello rojo falso
static uint32_t falsaProximoDado; // millis() del proximo sorteo de senal falsa

static bool     jugando[NUM_CONTROLES];   // controles que entraron a esta partida
static uint8_t  numJugadores;
static uint8_t  puntos[NUM_CONTROLES];
static bool     fuera[NUM_CONTROLES];     // eliminado de la RONDA en curso

static uint32_t saltoHasta;       // marca visual del que se adelanto
static uint8_t  saltoQuien;

static uint8_t  ganadorRonda;     // indice de control, o DUE_NADIE
static uint16_t tiempoRonda;      // ms de reaccion del que gano (0 si fue por salida en falso)
static bool     porFalso;         // la ronda se decidio porque el resto se adelanto
static uint8_t  ganador;          // indice de control, valido en DUE_FIN
static uint16_t mejorTiempo;      // mejor reaccion de la partida (para el record)
static uint8_t  mejorTiempoDe;    // quien la hizo: el record es suyo aunque gane otro
static bool     esRecord;

// ---------- Sonido ----------
static const Nota JINGLE_FALSO[] = { {220, 200}, {165, 320} };

static void sonarSenal()  { beep(2000, 60); }
static void sonarGana()   { beep(1500, 90); }
static void sonarFalso()  { tocarJingle(JINGLE_FALSO, 2); }

// ---------- Rondas ----------
static void nuevaRonda() {
  estadoDue        = DUE_ESPERA;
  faseDesde        = millis();
  senalEn          = faseDesde + random(DUE_ESPERA_MIN, DUE_ESPERA_MAX + 1);
  falsaHasta       = 0;
  falsaProximoDado = faseDesde + DUE_FALSA_CADA;
  saltoHasta       = 0;
  saltoQuien       = DUE_NADIE;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) fuera[j] = false;
}

void nuevoDuelo() {
  // Recalibrar sirve para dos cosas: refresca que controles estan enchufados
  // (que es como se arma la lista de jugadores) y permite sumar un control a
  // mitad de la noche sin reiniciar la consola.
  calibrarJoys();

  numJugadores = 0;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    jugando[j] = controlPresente(j);
    if (jugando[j]) numJugadores++;
    puntos[j] = 0;
  }
  // Este juego es solo de botones: si un joystick esta roto o alguien lo tenia
  // agarrado durante la calibracion, el control igual sirve para jugar. Antes
  // que dejar la partida sin rivales, se cae a los dos de siempre.
  if (numJugadores < 2) {
    jugando[0] = jugando[1] = true;
    numJugadores = 2;
  }

  ganador       = DUE_NADIE;
  ganadorRonda  = DUE_NADIE;
  tiempoRonda   = 0;
  porFalso      = false;
  mejorTiempo   = 0;
  mejorTiempoDe = 0;
  esRecord      = false;
  nuevaRonda();
}

// Cierra la ronda y, si alguien llego a DUE_RONDAS_GANAR, la partida.
static void terminarRonda(uint8_t quien, uint16_t ms, bool falso) {
  ganadorRonda = quien;
  tiempoRonda  = ms;
  porFalso     = falso;
  puntos[quien]++;

  // Solo las reacciones de verdad entran al record; las rondas ganadas porque
  // los otros se adelantaron no miden nada.
  if (!falso && ms > 0 && ms < DUE_TIEMPO_TOPE) {
    if (mejorTiempo == 0 || ms < mejorTiempo) { mejorTiempo = ms; mejorTiempoDe = quien; }
  }

  falso ? sonarFalso() : sonarGana();

  if (puntos[quien] >= DUE_RONDAS_GANAR) {
    ganador   = quien;
    estadoDue = DUE_FIN;
    faseDesde = millis();
    esRecord  = intentarRecord(REC_DUELO, mejorTiempo, mejorTiempoDe);
    if (esRecord) sonarRecord();
    else          sonarVictoria();
    return;
  }
  estadoDue = DUE_RESULTADO;
  faseDesde = millis();
}

// Saca de la ronda al que se adelanto. Si queda uno solo en pie, la ronda es
// suya sin necesidad de que llegue la senal. Devuelve true si la ronda dejo de
// estar en curso (se adjudico o se reinicio), para que el llamador corte.
static bool salidaEnFalso(uint8_t j, uint32_t ahora) {
  fuera[j]   = true;
  saltoQuien = j;
  saltoHasta = ahora + DUE_SALTO_MS;
  sonarFalso();

  uint8_t vivos = 0, ultimo = 0;
  for (uint8_t k = 0; k < NUM_CONTROLES; k++) {
    if (jugando[k] && !fuera[k]) { vivos++; ultimo = k; }
  }
  if (vivos == 1)      { terminarRonda(ultimo, 0, true); return true; }
  else if (vivos == 0) { nuevaRonda();                    return true; }  // se adelantaron todos
  return false;
}

void loopDuelo() {
  uint32_t ahora = millis();

  if (estadoDue == DUE_FIN) {
    uint32_t t = ahora - faseDesde;
    CRGB c = CONTROLES[ganador].color;
    FastLED.clear();
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      if ((i + t / 40) % 4 == 0) leds[i] = c;   // chase en el color del ganador
    }
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > DUE_FIN_MS) volverAlMenu();
    return;
  }

  if (estadoDue == DUE_RESULTADO) {
    // Toda la tira parpadeando en el color del que gano la ronda: con hasta
    // cuatro jugadores ya no hay "su mitad" de la tira que le corresponda, y el
    // color solo alcanza para saber quien se la llevo.
    uint32_t t = ahora - faseDesde;
    FastLED.clear();
    if ((t / 200) % 2 == 0) fill_solid(leds, NUM_LEDS, CONTROLES[ganadorRonda].color);
    FastLED.show();
    if (t > DUE_RESULTADO_MS) nuevaRonda();
    return;
  }

  if (estadoDue == DUE_ESPERA) {
    // Cualquier boton apretado antes de la senal es salida en falso.
    for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
      if (!jugando[j] || fuera[j]) continue;
      if (btnFlanco[j] && salidaEnFalso(j, ahora)) return;
    }

    if (ahora >= senalEn) {
      estadoDue = DUE_SENAL;
      faseDesde = ahora;
      sonarSenal();
      fill_solid(leds, NUM_LEDS, CRGB::White);
      FastLED.show();
      return;
    }

    // Senales falsas: destellos rojos cortos que NO valen.
    if (ahora >= falsaProximoDado) {
      falsaProximoDado = ahora + DUE_FALSA_CADA;
      // Nunca justo antes de la senal de verdad: seria imposible distinguirlas.
      if (random8() < DUE_FALSA_PROB && senalEn - ahora > DUE_FALSA_MS * 4) {
        falsaHasta = ahora + DUE_FALSA_MS;
      }
    }

    if (ahora < saltoHasta) {
      // Quien se adelanto, en su propio color: con cuatro jugadores la ronda
      // sigue, asi que hace falta que se vea a quien acaban de eliminar.
      fill_solid(leds, NUM_LEDS, CONTROLES[saltoQuien].color);
      nscale8(leds, NUM_LEDS, 90);
    } else if (ahora < falsaHasta) {
      fill_solid(leds, NUM_LEDS, CRGB(200, 0, 0));
    } else {
      // Un latido tenue en el centro para que se note que la consola espera y
      // no que se colgo.
      FastLED.clear();
      setLed(NUM_LEDS / 2, CRGB(0, 0, beatsin8(30, 4, 26)));
    }
    FastLED.show();
    return;
  }

  // --- DUE_SENAL: gana el primero que aprieta, de los que siguen en la ronda ---
  uint16_t ms = (uint16_t)(ahora - faseDesde);
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j] || fuera[j]) continue;
    if (btnFlanco[j]) { terminarRonda(j, ms, false); return; }
  }

  // Si nadie aprieta en 3 s la ronda se anula y se vuelve a empezar, para que
  // la consola no quede clavada en blanco.
  if (ahora - faseDesde > 3000) { nuevaRonda(); return; }

  fill_solid(leds, NUM_LEDS, CRGB::White);
  FastLED.show();
}

// ---------- LCD ----------
// Con dos jugadores el marcador de siempre ("Verde 2 - 1 Azul") es mas claro,
// pero solo entra en 16 columnas si los dos nombres son cortos. Cuando no entra,
// o cuando son mas de dos, se cae al compacto abreviado: "Ve2 Az1 Ro0 Am3".
static String marcador() {
  if (numJugadores == 2) {
    uint8_t a = DUE_NADIE, b = DUE_NADIE;
    for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
      if (!jugando[j]) continue;
      if (a == DUE_NADIE) a = j;
      else                b = j;
    }
    if (a != DUE_NADIE && b != DUE_NADIE) {
      String s = String(CONTROLES[a].nombre) + " " + String(puntos[a]) +
                 " - " + String(puntos[b]) + " " + String(CONTROLES[b].nombre);
      if (s.length() <= 16) return s;
    }
  }
  String s;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    if (s.length()) s += " ";
    s += String(CONTROLES[j].abrev) + String(puntos[j]);
  }
  return s;
}

void lcdDuelo() {
  lcdLinea(0, marcador());

  switch (estadoDue) {
    case DUE_ESPERA:
      if (saltoHasta && millis() < saltoHasta)
        lcdLinea(1, String("Falso: ") + CONTROLES[saltoQuien].nombre);
      else
        lcdLinea(1, "Preparados...");
      break;
    case DUE_SENAL:
      lcdLinea(1, "YA!");
      break;
    case DUE_RESULTADO:
      if (porFalso) lcdLinea(1, String("Gana ") + CONTROLES[ganadorRonda].nombre);
      else          lcdLinea(1, String(CONTROLES[ganadorRonda].abrev) + " " +
                                String(tiempoRonda) + " ms");
      break;
    case DUE_FIN:
      if (esRecord) lcdLinea(1, "*RECORD " + String(mejorTiempo) + " ms*");
      else          lcdLinea(1, textoGana(ganador));
      break;
  }
}

String webDuelo() {
  String s;
  for (uint8_t j = 0; j < NUM_CONTROLES; j++) {
    if (!jugando[j]) continue;
    if (s.length()) s += " - ";
    s += String(CONTROLES[j].nombre) + " " + String(puntos[j]);
  }
  if (mejorTiempo) s += " (mejor: " + String(mejorTiempo) + " ms)";
  return s;
}
