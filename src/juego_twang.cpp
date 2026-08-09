// ---------- Twang: dungeon 1D de un jugador ----------
// El jugador es un punto verde que se mueve con el joystick: la deflexion del
// eje Y es una VELOCIDAD (mas inclinado, mas rapido), no un paso por pulsacion,
// que es lo que le da el tacto del TWANG original. Por eso la posicion se
// guarda en float y el LED que se pinta es esa posicion redondeada.
// Los enemigos entran por el extremo lejano (el de la salida) y caminan hacia
// el jugador a paso fijo, igual que avanza el muro de Rompecolores.
// La salida es el ultimo LED y solo parpadea cuando la tanda esta limpia: asi
// se ve de un vistazo que primero hay que matar a todos y despues cruzar.

#include "juego_twang.h"

// ---------- Parametros ----------
uint16_t       TWANG_VEL_JUGADOR   = 45;   // LEDs por segundo con el joystick a fondo
uint16_t       TWANG_ATAQUE_RADIO  = 6;    // LEDs a cada lado que alcanza el pulso ya expandido
uint16_t       TWANG_ATAQUE_ESPERA = 380;  // cooldown entre ataques: agil, pero no spam infinito

const uint16_t TWANG_VEL_LENTA    = 300;  // enemigos mas lentos, pote al minimo (ms por LED)
const uint16_t TWANG_VEL_RAPIDA   = 120;  // enemigos mas rapidos, pote al maximo (ms por LED)
const uint16_t TWANG_VEL_MINIMA   = 55;   // piso de dificultad: por mas niveles que pasen no aceleran mas
const uint8_t  TWANG_VIDAS        = 3;
const uint8_t  TWANG_MAX_ENEMIGOS = 12;   // tope de la tanda (tamano de los arrays paralelos)
const uint8_t  TWANG_ENEMIGOS_N1  = 3;    // enemigos del nivel 1; sube de a uno por nivel
const uint16_t TWANG_APARICION_MS = 900;  // separacion entre apariciones para que no salgan todos pegados
const uint16_t TWANG_ATAQUE_MS    = 200;  // ventana en la que el pulso mata (y en la que se ve creciendo)
const uint16_t TWANG_INVUL_MS     = 700;  // invulnerabilidad tras un golpe (evita perder varias vidas de un tiron)
const uint16_t TWANG_SALIDA_MS    = 1200; // festejo al completar un nivel antes de la tanda siguiente
const uint16_t TWANG_FIN_MS       = 3000; // duracion de la animacion de derrota antes de volver al menu

// Terreno del nivel: lava (tramos que se prenden y apagan) y cintas
// transportadoras (tramos que empujan al que este parado encima).
const uint8_t  TWANG_MAX_LAVA     = 4;    // tope de tramos de lava por nivel
const uint8_t  TWANG_MAX_CINTAS   = 3;    // tope de cintas por nivel
const int16_t  TWANG_TERRENO_MARGEN = 12; // LEDs despejados en la base (donde se reaparece)
                                          // y en la salida: nunca es imposible arrancar ni terminar
const uint16_t TWANG_LAVA_ON_MS   = 1400; // cuanto queda encendida (peligrosa)
const uint16_t TWANG_LAVA_OFF_MS  = 1800; // apagada mas tiempo que encendida: siempre hay ventana para cruzar
const uint16_t TWANG_LAVA_AVISO_MS= 500;  // parpadeo de aviso antes de encenderse: la muerte nunca es sorpresa
const uint8_t  TWANG_CINTA_VEL    = 18;   // LEDs/s que suma la cinta; bastante menos que TWANG_VEL_JUGADOR
                                          // para poder caminar contra ella (mas lento) y no quedar atrapado

// Paleta: la del juego original (verde vos, rojo el peligro, azul la salida).
// La lava va naranja y la cinta cyan: colores que no usa ningun otro elemento,
// asi el terreno se lee de un vistazo sin confundirlo con un enemigo (rojo),
// con la salida (azul) ni con un record (dorado).
static const CRGB COL_JUGADOR = CRGB(  0, 255,   0);
static const CRGB COL_ENEMIGO = CRGB(255,   0,   0);
static const CRGB COL_SALIDA  = CRGB(  0,  60, 255);
static const CRGB COL_LAVA    = CRGB(255,  60,   0);
static const CRGB COL_CINTA   = CRGB(  0, 180, 160);

// ---------- Estado ----------
// Los enemigos van en arrays paralelos (misma idea simple que el muro de
// Rompecolores): posicion, si sigue vivo, cuando dio su ultimo paso y a partir
// de cuando entra a la mazmorra. Un enemigo "vivo" pero con aparicion futura
// todavia no se dibuja ni se mueve, asi la tanda entra escalonada.
enum EstadoTwang { TWANG_JUGANDO, TWANG_NIVEL, TWANG_FIN };
static EstadoTwang estadoTwang;
static float    jugadorPos;                        // posicion continua: el movimiento es por velocidad, no por pasos
static int16_t  enemigoPos[TWANG_MAX_ENEMIGOS];
static bool     enemigoVivo[TWANG_MAX_ENEMIGOS];
static uint32_t enemigoPaso[TWANG_MAX_ENEMIGOS];   // millis() del ultimo paso de cada enemigo
static uint32_t enemigoAparece[TWANG_MAX_ENEMIGOS];// millis() en que ese enemigo entra a la mazmorra
static uint16_t velEnemigo;                        // intervalo de avance de la tanda actual (ms por LED)
static uint16_t velInicial;                        // lo fija el pote al arrancar la partida

// Terreno del nivel, tambien en arrays paralelos. Es fijo mientras dura el
// nivel y se regenera entero en cada tanda nueva (ver generarTerreno).
static int16_t  lavaIni[TWANG_MAX_LAVA];           // tramo de lava [ini, fin], ambos incluidos
static int16_t  lavaFin[TWANG_MAX_LAVA];
static bool     lavaOn[TWANG_MAX_LAVA];            // encendida ahora (quema)
static uint32_t lavaCambio[TWANG_MAX_LAVA];        // millis() del ultimo cambio on/off
static uint8_t  lavaN;                             // cuantos tramos de lava tiene el nivel actual
static int16_t  cintaIni[TWANG_MAX_CINTAS];        // tramo de cinta [ini, fin], ambos incluidos
static int16_t  cintaFin[TWANG_MAX_CINTAS];
static int8_t   cintaDir[TWANG_MAX_CINTAS];        // +1 empuja hacia la salida, -1 hacia la base
static uint8_t  cintasN;                           // cuantas cintas tiene el nivel actual

static uint8_t  nivel;
static uint8_t  vidas;
static uint32_t ultimoFrame;                       // para el paso continuo del jugador (dt del frame)
static uint32_t ataqueDesde;                       // millis() del ultimo ataque (sirve de ventana y de cooldown)
static bool     atacando;                          // true mientras el pulso todavia mata
static uint32_t invulDesde;                        // millis() del ultimo golpe recibido
static bool     invul;                             // true mientras el jugador parpadea sin recibir dano
static int16_t  caida;                             // LED donde cayo el jugador: centro de la animacion de derrota
static bool     esRecord;
static uint32_t faseDesde;                         // millis() en que empezo TWANG_NIVEL o TWANG_FIN

// ---------- Sonido ----------
// Nivel superado (ascendente y corto) y derrota (mas grave y arrastrada que la
// de Rompecolores, para distinguir de oido en que juego estas).
static const Nota JINGLE_NIVEL[] = { {784, 90}, {988, 90}, {1319, 220} };              // G-B-E(alto)
static const Nota JINGLE_FIN[]   = { {330, 170}, {294, 170}, {247, 170}, {165, 520} }; // E-D-B-E(bajo)

// El ataque suena seco, la muerte de un enemigo bien aguda y el golpe recibido
// grave y largo, para no confundir "mate" con "me pegaron".
static void sonarAtaque()   { beep(1500,  30); }
static void sonarMuerte()   { beep(2100,  45); }
static void sonarGolpe()    { beep( 140, 220); }
static void sonarNivel()    { tocarJingle(JINGLE_NIVEL, 3); }
static void sonarGameOver() { tocarJingle(JINGLE_FIN, 4); }

// ---------- Helpers ----------
static uint8_t enemigosVivos() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) if (enemigoVivo[i]) n++;
  return n;
}

// Arma el terreno del nivel: la lava aparece desde el nivel 2 y las cintas
// desde el 3, sumando una mas de cada tipo cada dos niveles hasta el tope. El
// nivel 1 queda limpio a proposito, como introduccion.
// La colocacion recorre la tira UNA sola vez con un cursor que avanza un hueco
// al azar y despues ocupa un tramo al azar: asi por construccion los tramos no
// se pisan y siempre queda camino libre entre ellos. Se alternan los tipos para
// que no queden todas las lavas de un lado y todas las cintas del otro.
static void generarTerreno() {
  lavaN   = nivel / 2;              // 0 en el nivel 1, 1 en el 2 y 3, 2 en el 4 y 5...
  cintasN = (nivel - 1) / 2;        // lo mismo corrido un nivel: arranca en el 3
  if (lavaN   > TWANG_MAX_LAVA)   lavaN   = TWANG_MAX_LAVA;
  if (cintasN > TWANG_MAX_CINTAS) cintasN = TWANG_MAX_CINTAS;

  uint32_t ahora  = millis();
  int16_t  cursor = TWANG_TERRENO_MARGEN;                    // despues de la zona de reaparicion
  int16_t  limite = NUM_LEDS - 1 - TWANG_TERRENO_MARGEN;     // antes de la salida
  uint8_t  lavas = 0, cintas = 0;
  bool     tocaLava = true;

  while (lavas < lavaN || cintas < cintasN) {
    if ( tocaLava && lavas  >= lavaN)   tocaLava = false;    // ya no quedan de ese tipo
    if (!tocaLava && cintas >= cintasN) tocaLava = true;

    cursor += random(3, 9);                   // hueco de piso firme antes del tramo
    int16_t largo = random(4, 8);             // tramos de menos de 4 LEDs casi no se notan
    if (cursor + largo - 1 > limite) break;   // no entra: nos quedamos con lo ya colocado

    if (tocaLava) {
      lavaIni[lavas] = cursor;
      lavaFin[lavas] = cursor + largo - 1;
      lavaOn[lavas]  = false;                 // todas arrancan apagadas
      // Desfase por indice hacia ATRAS en el tiempo: cada tramo entra al ciclo
      // en un momento distinto. Si todas prendieran al unisono el nivel seria
      // un unico semaforo y cruzar seria trivial (o imposible) de una.
      lavaCambio[lavas] = ahora - (uint32_t)lavas * 450;
      lavas++;
    } else {
      cintaIni[cintas] = cursor;
      cintaFin[cintas] = cursor + largo - 1;
      cintaDir[cintas] = (random(2) == 0) ? -1 : +1;
      cintas++;
    }
    cursor += largo;
    tocaLava = !tocaLava;
  }

  lavaN   = lavas;    // puede haber entrado menos de lo pedido si se acabo la tira
  cintasN = cintas;
}

// Arma la tanda del nivel actual: mas enemigos y mas rapidos a cada nivel, con
// piso de velocidad para que no se vuelva imposible. Las apariciones se
// escalonan en el tiempo (y un poco en la posicion) para que no salgan pegados.
static void iniciarTanda() {
  uint8_t n = TWANG_ENEMIGOS_N1 + (nivel - 1);
  if (n > TWANG_MAX_ENEMIGOS) n = TWANG_MAX_ENEMIGOS;

  if (nivel == 1) velEnemigo = velInicial;    // el pote fija el nivel 1
  else velEnemigo = max<int>(TWANG_VEL_MINIMA, (velEnemigo * 88) / 100);

  uint32_t ahora = millis();
  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) {
    enemigoVivo[i]    = (i < n);
    enemigoPos[i]     = NUM_LEDS - 1 - (int16_t)random(0, 5);
    enemigoAparece[i] = ahora + (uint32_t)i * TWANG_APARICION_MS;
    enemigoPaso[i]    = enemigoAparece[i];
  }

  generarTerreno();             // lava y cintas nuevas en cada nivel

  jugadorPos  = 0;              // cada nivel se arranca de nuevo en la base
  ultimoFrame = ahora;
  atacando    = false;
  invul       = false;
}

void nuevoTwang() {
  calibrarJoy(0);               // asume el stick soltado en el instante de arrancar
  velInicial  = map(leerPoteCrudo(), 0, 4095, TWANG_VEL_LENTA, TWANG_VEL_RAPIDA);
  nivel       = 1;
  vidas       = TWANG_VIDAS;
  ataqueDesde = 0;              // 0 = ataque disponible desde el primer frame
  invulDesde  = 0;
  caida       = 0;
  esRecord    = false;
  estadoTwang = TWANG_JUGANDO;
  iniciarTanda();
}

static void perder() {
  estadoTwang = TWANG_FIN;
  faseDesde   = millis();
  esRecord    = intentarRecord(REC_TWANG, (uint32_t)nivel);
  esRecord ? sonarRecord() : sonarGameOver();
}

// Un golpe recibido, venga de un enemigo o de la lava: en los dos casos se
// pierde una vida, se arranca la invulnerabilidad y suena lo mismo. Devuelve
// true si fue el ultimo: el frame tiene que cortar porque ya se entro en
// TWANG_FIN y no hay nada mas que actualizar ni dibujar.
static bool recibirGolpe(int16_t jugadorLed) {
  vidas--;
  if (vidas == 0) {
    caida = jugadorLed;
    perder();
    return true;
  }
  invul      = true;
  invulDesde = millis();
  sonarGolpe();
  return false;
}

void loopTwang() {
  uint32_t ahora = millis();

  if (estadoTwang == TWANG_FIN) {
    // Derrota: onda roja que se expande desde donde cayo el jugador, y al menu.
    uint32_t t  = ahora - faseDesde;
    bool     on = (t / 120) % 2 == 0;
    int16_t  r  = t / 25;
    FastLED.clear();
    for (int16_t k = -r; k <= r; k++) setLed(caida + k, on ? COL_ENEMIGO : CRGB(40, 0, 0));
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (t > TWANG_FIN_MS) volverAlMenu();
    return;
  }

  if (estadoTwang == TWANG_NIVEL) {
    // Nivel superado: barrido azul desde la salida hacia la base y siguiente tanda.
    uint32_t t = ahora - faseDesde;
    int16_t  frente = NUM_LEDS - 1 - (int16_t)(t / 10);
    FastLED.clear();
    for (int16_t i = frente; i < NUM_LEDS; i++) setLed(i, COL_SALIDA);
    FastLED.show();
    if (t > TWANG_SALIDA_MS) {
      nivel++;
      iniciarTanda();
      estadoTwang = TWANG_JUGANDO;
    }
    return;
  }

  // --- Movimiento del jugador: velocidad * tiempo del frame ---
  // El dt se mide contra el frame anterior porque el loop no tiene periodo fijo.
  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;
  jugadorPos += leerJoyNorm(0) * TWANG_VEL_JUGADOR * dt;

  // Cintas: arrastran al que este parado encima, ademas de lo que haga el
  // joystick. Como TWANG_CINTA_VEL es bastante menor que TWANG_VEL_JUGADOR se
  // puede caminar en contra (mas lento), no es una trampa sin salida.
  int16_t pisando = (int16_t)(jugadorPos + 0.5f);
  for (uint8_t i = 0; i < cintasN; i++) {
    if (pisando >= cintaIni[i] && pisando <= cintaFin[i]) {
      jugadorPos += cintaDir[i] * (float)TWANG_CINTA_VEL * dt;
    }
  }

  if (jugadorPos < 0)            jugadorPos = 0;
  if (jugadorPos > NUM_LEDS - 1) jugadorPos = NUM_LEDS - 1;
  int16_t jugadorLed = (int16_t)(jugadorPos + 0.5f);

  if (invul && ahora - invulDesde > TWANG_INVUL_MS) invul = false;

  // --- Lava: cada tramo alterna encendida/apagada con su propio reloj ---
  // Estar parado en una encendida cuesta una vida, igual que un enemigo.
  for (uint8_t i = 0; i < lavaN; i++) {
    uint16_t dur = lavaOn[i] ? TWANG_LAVA_ON_MS : TWANG_LAVA_OFF_MS;
    if (ahora - lavaCambio[i] >= dur) {
      lavaCambio[i] += dur;
      lavaOn[i] = !lavaOn[i];
    }
    if (lavaOn[i] && !invul && jugadorLed >= lavaIni[i] && jugadorLed <= lavaFin[i]) {
      if (recibirGolpe(jugadorLed)) return;
    }
  }

  // --- Ataque: pulso que se expande durante una ventana corta, con cooldown ---
  if (btnFlanco[0] && ahora - ataqueDesde >= TWANG_ATAQUE_ESPERA) {
    ataqueDesde = ahora;
    atacando    = true;
    sonarAtaque();
  }
  uint32_t tAtaque = ahora - ataqueDesde;
  if (atacando && tAtaque > TWANG_ATAQUE_MS) atacando = false;
  int16_t radio = atacando ? (1 + ((int16_t)TWANG_ATAQUE_RADIO * (int16_t)tAtaque) / TWANG_ATAQUE_MS) : 0;

  // --- Enemigos: avanzan hacia la base y chocan (o mueren en el pulso) ---
  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) {
    if (!enemigoVivo[i]) continue;
    if (ahora < enemigoAparece[i]) continue;      // todavia no entro a la mazmorra

    if (ahora - enemigoPaso[i] >= velEnemigo) {
      enemigoPaso[i] += velEnemigo;
      enemigoPos[i]--;
      if (enemigoPos[i] < 0) { enemigoVivo[i] = false; continue; }  // se escapo por la base
    }

    int16_t d = enemigoPos[i] - jugadorLed;
    if (d < 0) d = -d;

    if (atacando && d <= radio) {                 // el pulso lo alcanzo
      enemigoVivo[i] = false;
      sonarMuerte();
      continue;
    }

    // Contacto: el enemigo llego (o paso) al jugador. Durante la invulnerabilidad
    // lo atraviesa sin hacer dano, para no comerse varias vidas del mismo choque.
    if (!invul && enemigoPos[i] <= jugadorLed) {
      enemigoVivo[i] = false;
      if (recibirGolpe(jugadorLed)) return;
    }
  }

  // --- Salida: cruzarla con la tanda limpia sube de nivel ---
  bool limpio = (enemigosVivos() == 0);
  if (limpio && jugadorLed >= NUM_LEDS - 1) {
    estadoTwang = TWANG_NIVEL;
    faseDesde   = ahora;
    sonarNivel();
    return;
  }

  // --- Dibujo: salida y terreno abajo, despues enemigos, pulso y jugador ---
  // (el jugador va ultimo para que se vea siempre, aunque este sobre la lava).
  FastLED.clear();
  // La salida parpadea SOLO con la tanda limpia; con enemigos vivos queda fija.
  bool salidaOn = limpio ? ((ahora / 200) % 2 == 0) : true;
  setLed(NUM_LEDS - 1, salidaOn ? COL_SALIDA : CRGB(0, 10, 40));

  // Lava: apagada se dibuja muy tenue (hay que ver donde esta para planear el
  // cruce) y en los ultimos TWANG_LAVA_AVISO_MS antes de prenderse parpadea
  // rapido, asi el jugador tiene tiempo de salir.
  for (uint8_t i = 0; i < lavaN; i++) {
    CRGB c = COL_LAVA;
    if (!lavaOn[i]) {
      uint32_t t = ahora - lavaCambio[i];
      bool aviso = (t + TWANG_LAVA_AVISO_MS >= TWANG_LAVA_OFF_MS);
      if (aviso) c.nscale8(((ahora / 60) % 2 == 0) ? 255 : 30);
      else       c.nscale8(25);
    }
    for (int16_t p = lavaIni[i]; p <= lavaFin[i]; p++) setLed(p, c);
  }

  // Cintas: fondo tenue con pixeles brillantes cada 4 LEDs que se desplazan en
  // el sentido del empuje, para que se vea de que lado tira antes de pisarla.
  for (uint8_t i = 0; i < cintasN; i++) {
    CRGB tenue = COL_CINTA;
    tenue.nscale8(30);
    int16_t fase = (int16_t)((ahora / 80) % 4);
    for (int16_t p = cintaIni[i]; p <= cintaFin[i]; p++) {
      int16_t k = (cintaDir[i] > 0) ? (p - fase) : (p + fase);
      setLed(p, (((k % 4) + 4) % 4 == 0) ? COL_CINTA : tenue);
    }
  }

  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) {
    if (enemigoVivo[i] && ahora >= enemigoAparece[i]) setLed(enemigoPos[i], COL_ENEMIGO);
  }

  if (atacando) {                            // flash blanco que crece y se apaga
    CRGB c = CRGB::White;
    c.nscale8(255 - (uint8_t)((255UL * tAtaque) / TWANG_ATAQUE_MS));
    for (int16_t k = -radio; k <= radio; k++) setLed(jugadorLed + k, c);
  }

  // Al recibir un golpe el jugador parpadea mientras dura la invulnerabilidad.
  if (!invul || (ahora / 100) % 2 == 0) setLed(jugadorLed, COL_JUGADOR);
  FastLED.show();
}

// ---------- LCD: nivel y vidas arriba, estado de la tanda abajo ----------
void lcdTwang() {
  if (estadoTwang == TWANG_FIN) {
    lcdLinea(0, "** GAME OVER **");
    if (esRecord) lcdLinea(1, "*NUEVO RECORD!*");
    else          lcdLinea(1, "Llegaste al Nv" + String(nivel));
    return;
  }
  lcdLinea(0, "Nivel " + String(nivel) + "  Vidas " + String(vidas));
  uint8_t vivos = enemigosVivos();
  lcdLinea(1, vivos ? ("Enemigos: " + String(vivos)) : "Salida abierta!");
}

String webTwang() {
  return "Nivel " + String(nivel) + ", vidas " + String(vidas);
}
