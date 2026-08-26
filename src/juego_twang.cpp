// ---------- Twang: dungeon 1D, solo o de a dos ----------
// El jugador es un punto verde que se mueve con el joystick: la deflexion del
// eje Y es una VELOCIDAD (mas inclinado, mas rapido), no un paso por pulsacion,
// que es lo que le da el tacto del TWANG original. Por eso la posicion se
// guarda en float y el LED que se pinta es esa posicion redondeada.
// Todos los enemigos entran por el extremo lejano (el de la salida) y bajan a
// paso fijo, pero solo los CAMINANTES bajan hasta el fondo: los CENTINELAS se
// plantan en un punto y las PATRULLAS se quedan yendo y viniendo dentro de su
// banda. Es el reparto del TWANG original (ahi el enemigo con "wobble" oscila
// alrededor de donde nacio) y es lo que obliga a subir a buscarlos: si todos
// bajaran, esperarlos abajo y matarlos de a uno seria siempre la mejor jugada.
// La salida es el ultimo LED y solo parpadea cuando la tanda esta limpia: asi
// se ve de un vistazo que primero hay que matar a todos y despues cruzar.
//
// Se juega solo o de a dos en cooperativo (verde el control 1, cian el 2), lo
// que se elige en una pantalla previa. Mismo mapa para los dos, sin fuego
// amigo y con tres vidas cada uno. Para pasar de nivel tienen que llegar a la
// salida TODOS los que sigan en pie -- el que llega primero espera ahi, a salvo
// -- y la partida recien termina cuando se quedan sin vidas los dos.

#include "juego_twang.h"
#include "juego_twang32.h"   // la variante canonica, que es otro juego entero

// ---------- Parametros ----------
uint16_t       TWANG_VEL_JUGADOR   = 45;   // LEDs por segundo con el joystick a fondo
uint16_t       TWANG_ATAQUE_RADIO  = 5;    // LEDs a cada lado que alcanza el pulso ya expandido
uint16_t       TWANG_ATAQUE_ESPERA = 200;  // cooldown entre ataques: agil, pero no spam infinito

const uint16_t TWANG_VEL_LENTA    = 300;  // enemigos mas lentos, pote al minimo (ms por LED)
const uint16_t TWANG_VEL_RAPIDA   = 120;  // enemigos mas rapidos, pote al maximo (ms por LED)
const uint16_t TWANG_VEL_MINIMA   = 55;   // piso de dificultad: por mas niveles que pasen no aceleran mas
const uint8_t  TWANG_VIDAS        = 3;    // por jugador
const uint8_t  TWANG_MAX_JUG      = 2;    // cooperativo de a dos
// Twang es de los juegos ABSOLUTOS: los bichos, la lava y las cintas miden lo
// mismo en las dos tiras, asi que la larga es el doble de mazmorra. Para que no
// quede el doble de vacia, cada nivel pone el doble de todo (y por eso su
// record --el nivel alcanzado-- se guarda por largo de tira).
//
// Los tres MAX dimensionan los arrays para la tira larga (el de enemigos con
// lugar de sobra para el generador); los topes reales de cada tanda salen de
// las tope*(), que con la corta dan 12 / 4 / 3.
const uint8_t  TWANG_MAX_ENEMIGOS = 32;   // tamano de los arrays paralelos
const uint8_t  TWANG_ENEMIGOS_N1  = 3;    // enemigos del nivel 1; sube de a uno por nivel
const uint16_t TWANG_APARICION_MS = 900;  // separacion entre apariciones para que no salgan todos pegados
const uint16_t TWANG_ATAQUE_MS    = 200;  // ventana en la que el pulso mata (y en la que se ve creciendo)
const uint16_t TWANG_INVUL_MS     = 700;  // invulnerabilidad tras un golpe (evita perder varias vidas de un tiron)
const uint16_t TWANG_SALIDA_MS    = 1200; // festejo al completar un nivel antes de la tanda siguiente
const uint16_t TWANG_FIN_MS       = 3000; // duracion de la animacion de derrota antes de volver al menu

// Generador: desde TWANG_GEN_NIVEL la salida suelta un caminante cada tanto
// mientras quede tanda por limpiar. Es el equivalente de los spawn pools del
// TWANG original, y es lo que hace que demorarse cueste caro.
const uint8_t  TWANG_GEN_NIVEL    = 4;    // primer nivel con generador
const uint16_t TWANG_GEN_MS       = 9000; // cada cuanto suelta uno en ese primer nivel
const uint16_t TWANG_GEN_PASO     = 600;  // se acelera esto por cada nivel que pasa
const uint16_t TWANG_GEN_MS_MIN   = 4000; // piso: por mas niveles que pasen no aprieta mas
const int16_t  TWANG_GEN_ZONA_SEGURA = 5; // LEDs antes de la salida donde el generador no suelta nada
const uint16_t TWANG_GEN_AVISO_1  = 500;  // dos destellos violetas en la puerta antes de soltar uno: el
const uint16_t TWANG_GEN_AVISO_2  = 250;  // primero a 500 ms y el segundo a 250 ms, para poder reaccionar
const uint16_t TWANG_GEN_AVISO_MS = 150;  // cuanto dura cada destello

// Terreno del nivel: lava (tramos que se prenden y apagan) y cintas
// transportadoras (tramos que empujan al que este parado encima).
const uint8_t  TWANG_MAX_LAVA     = 8;    // idem para la lava
const uint8_t  TWANG_MAX_CINTAS   = 6;    // idem para las cintas
static uint8_t topeEnemigos() { return (uint8_t)escalaLeds(12); }
static uint8_t topeLava()     { return (uint8_t)escalaLeds(4);  }
static uint8_t topeCintas()   { return (uint8_t)escalaLeds(3);  }
const int16_t  TWANG_TERRENO_MARGEN = 12; // LEDs despejados en la base (donde se reaparece)
                                          // y en la salida: nunca es imposible arrancar ni terminar
const uint16_t TWANG_LAVA_ON_MS   = 1400; // cuanto queda encendida (peligrosa)
const uint16_t TWANG_LAVA_OFF_MS  = 1800; // apagada mas tiempo que encendida: siempre hay ventana para cruzar
const uint16_t TWANG_LAVA_AVISO_MS= 500;  // parpadeo de aviso antes de encenderse: la muerte nunca es sorpresa
const uint16_t TWANG_LAVA_CRECE_MS = 1500;// cada cuanto el tramo se estira un LED (alternando puntas). Lento a
                                          // proposito: la lava no persigue a nadie, solo hace que el camino que
                                          // memorizaste al entrar al nivel se vaya angostando si te quedas
const int16_t  TWANG_LAVA_LARGO_MAX = 12; // por mas corredor que tenga no pasa de aca: un tramo mas largo que
                                          // esto ya no se cruza en la ventana en que esta apagada
const int16_t  TWANG_LAVA_SEPARACION = 2; // LEDs de piso firme que siempre quedan entre dos tramos de terreno
const uint8_t  TWANG_CINTA_VEL    = 18;   // LEDs/s que suma la cinta; bastante menos que TWANG_VEL_JUGADOR
                                          // para poder caminar contra ella (mas lento) y no quedar atrapado

// Paleta: la del juego original (verde vos, rojo el peligro, azul la salida).
// La lava va naranja y la cinta cyan: colores que no usa ningun otro elemento,
// asi el terreno se lee de un vistazo sin confundirlo con un enemigo (rojo),
// con la salida (azul) ni con un record (dorado).
static const CRGB COL_JUGADOR = CRGB(  0, 255,   0);   // P1
static const CRGB COL_JUG2    = CRGB(  0, 255, 255);   // P2 en cooperativo: cian puro. El azul de COL_P2
                                                       // no se puede usar aca, es el mismo de la salida
static const CRGB COL_ENEMIGO = CRGB(255,   0,   0);
static const CRGB COL_SALIDA  = CRGB(  0,  60, 255);
static const CRGB COL_AVISO   = CRGB(160,   0, 255);   // violeta: la puerta esta por escupir un enemigo
static const CRGB COL_LAVA    = CRGB(255,  60,   0);
static const CRGB COL_CINTA   = CRGB(  0, 180, 160);

// ---------- Estado ----------
// Los enemigos van en arrays paralelos (misma idea simple que el muro de
// Rompecolores): tipo, posicion, si sigue vivo, cuando dio su ultimo paso y a
// partir de cuando entra a la mazmorra. Un enemigo "vivo" pero con aparicion
// futura todavia no se dibuja ni se mueve, asi la tanda entra escalonada.
//
// Los tres tipos dan el mismo paso (velEnemigo): lo unico que cambia es hasta
// donde. El caminante baja hasta salirse por la base, el centinela baja hasta
// su punto y se planta, y la patrulla baja hasta su banda y ahi rebota entre
// los dos bordes. Se pueden pisar entre ellos sin problema: no hay colision
// enemigo-enemigo, solo enemigo-jugador.
enum EstadoTwang { TWANG_ELIGIENDO, TWANG_CANONICO, TWANG_JUGANDO, TWANG_NIVEL, TWANG_FIN };

// Las tres variantes de la pantalla previa. Las dos primeras son esta version
// (uno o dos jugadores); la tercera es el TWANG32 canonico de bdring, que no
// comparte nada con esta salvo la consola y vive en juego_twang32.cpp.
enum VarTwang { VAR_1JUG, VAR_2JUG, VAR_CANONICO, VAR_N };
enum TipoEnemigo { ENE_CAMINANTE, ENE_CENTINELA, ENE_PATRULLA };
static EstadoTwang estadoTwang;

// Los jugadores tambien van en arrays paralelos. Con un solo jugador se usa
// nada mas que el indice 0 y el codigo es exactamente el mismo: asi no hay dos
// caminos que se desincronicen cada vez que se toca algo.
static uint8_t  variante;                          // que se eligio en la pantalla previa
static uint8_t  jugadoresN;                        // 1 o 2, lo elige la pantalla previa
static float    jugPos[TWANG_MAX_JUG];             // posicion continua: el movimiento es por velocidad, no por pasos
static int16_t  jugLed[TWANG_MAX_JUG];             // esa posicion redondeada, que es el LED que se pinta
static uint8_t  jugVidas[TWANG_MAX_JUG];           // 0 = eliminado: no se dibuja, no choca y no juega mas
static bool     jugLlego[TWANG_MAX_JUG];           // ya cruzo la salida y espera al companero
static uint32_t jugAtaque[TWANG_MAX_JUG];          // millis() del ultimo ataque (ventana y cooldown)
static bool     jugAtacando[TWANG_MAX_JUG];        // true mientras su pulso todavia mata
static int16_t  jugRadio[TWANG_MAX_JUG];           // alcance del pulso en este frame (0 si no esta atacando)
static uint32_t jugInvulDesde[TWANG_MAX_JUG];      // millis() del ultimo golpe recibido
static bool     jugInvul[TWANG_MAX_JUG];           // parpadea sin poder recibir dano
static uint8_t  enemigoTipo[TWANG_MAX_ENEMIGOS];
static int16_t  enemigoPos[TWANG_MAX_ENEMIGOS];
static int16_t  enemigoIni[TWANG_MAX_ENEMIGOS];    // borde bajo de su banda (o el punto donde se planta)
static int16_t  enemigoFin[TWANG_MAX_ENEMIGOS];    // borde alto de su banda: hasta ahi baja al entrar
static int8_t   enemigoDir[TWANG_MAX_ENEMIGOS];    // -1 bajando, +1 subiendo, 0 plantado
static bool     enemigoEnBanda[TWANG_MAX_ENEMIGOS];// ya llego a su zona y dejo de bajar
static bool     enemigoCuenta[TWANG_MAX_ENEMIGOS]; // si hay que matarlo para abrir la salida
static int8_t   enemigoLado[TWANG_MAX_ENEMIGOS][TWANG_MAX_JUG];  // de que lado de cada jugador quedo en el
                                                   // frame anterior: con eso se detecta el cruce
static bool     enemigoVivo[TWANG_MAX_ENEMIGOS];
static uint32_t enemigoPaso[TWANG_MAX_ENEMIGOS];   // millis() del ultimo paso de cada enemigo
static uint32_t enemigoAparece[TWANG_MAX_ENEMIGOS];// millis() en que ese enemigo entra a la mazmorra
static uint16_t velEnemigo;                        // intervalo de avance de la tanda actual (ms por LED)
static uint16_t velInicial;                        // lo fija el pote al arrancar la partida
static uint32_t genProximo;                        // millis() del proximo del generador (0 = nivel sin generador)
static uint16_t genCada;                           // cada cuanto suelta uno en este nivel

// Terreno del nivel, tambien en arrays paralelos. Es fijo mientras dura el
// nivel y se regenera entero en cada tanda nueva (ver generarTerreno).
static int16_t  lavaIni[TWANG_MAX_LAVA];           // tramo de lava [ini, fin], ambos incluidos
static int16_t  lavaFin[TWANG_MAX_LAVA];
static bool     lavaOn[TWANG_MAX_LAVA];            // encendida ahora (quema)
static uint32_t lavaCambio[TWANG_MAX_LAVA];        // millis() del ultimo cambio on/off
static int8_t   lavaPunta[TWANG_MAX_LAVA];         // por que punta le toca estirarse la proxima vez
static int16_t  lavaMin[TWANG_MAX_LAVA];           // LED mas bajo y mas alto que puede llegar a ocupar: el
static int16_t  lavaMax[TWANG_MAX_LAVA];           // corredor que le quedo libre entre sus dos vecinos
static uint32_t lavaCrece[TWANG_MAX_LAVA];         // millis() del ultimo estiron
static uint8_t  lavaN;                             // cuantos tramos de lava tiene el nivel actual
static int16_t  cintaIni[TWANG_MAX_CINTAS];        // tramo de cinta [ini, fin], ambos incluidos
static int16_t  cintaFin[TWANG_MAX_CINTAS];
static int8_t   cintaDir[TWANG_MAX_CINTAS];        // +1 empuja hacia la salida, -1 hacia la base
static uint8_t  cintasN;                           // cuantas cintas tiene el nivel actual

static uint8_t  nivel;
static uint32_t ultimoFrame;                       // para el paso continuo del jugador (dt del frame)
static int16_t  caida;                             // LED donde cayo el ultimo jugador: centro de la derrota
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
static void sonarLlegada()  { beep(1046, 70); }        // llego uno, pero todavia falta el otro
static void sonarEliminado(){ beep(  90, 420); }       // se quedo sin vidas: mas grave y largo que un golpe
static void sonarNivel()    { tocarJingle(JINGLE_NIVEL, 3); }
static void sonarGameOver() { tocarJingle(JINGLE_FIN, 4); }

// ---------- Helpers ----------
// En juego = le quedan vidas. El que se queda sin ninguna desaparece de la tira
// y el companero sigue solo; la partida termina cuando no queda ninguno.
static bool    enJuego(uint8_t j) { return jugVidas[j] > 0; }
static uint8_t jugadoresEnJuego() {
  uint8_t n = 0;
  for (uint8_t j = 0; j < jugadoresN; j++) if (enJuego(j)) n++;
  return n;
}

// Alguien parado en los ultimos LEDs antes de la salida. Con eso el generador
// se calla: un enemigo naciendo en la puerta justo cuando la vas a cruzar no se
// puede esquivar. Alcanza con que este UNO de los dos, porque el que espera en
// la salida esta tan indefenso como el que llega corriendo.
static bool alguienEnLaPuerta() {
  for (uint8_t j = 0; j < jugadoresN; j++)
    if (enJuego(j) && jugLed[j] >= LARGO_TIRA - TWANG_GEN_ZONA_SEGURA) return true;
  return false;
}

// Los que hay que limpiar para que se abra la salida. Los que suelta el
// generador estan vivos y pegan igual, pero no cuentan: si contaran, con el
// generador activo la salida no se abriria nunca.
static uint8_t enemigosDeTanda() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) if (enemigoVivo[i] && enemigoCuenta[i]) n++;
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
  lavaN   = (uint8_t)escalaLeds(nivel / 2);        // 0 en el nivel 1, 1 en el 2 y 3...
  cintasN = (uint8_t)escalaLeds((nivel - 1) / 2);  // lo mismo corrido un nivel: arranca en el 3
  if (lavaN   > topeLava())   lavaN   = topeLava();
  if (cintasN > topeCintas()) cintasN = topeCintas();

  uint32_t ahora  = millis();
  int16_t  cursor = TWANG_TERRENO_MARGEN;                    // despues de la zona de reaparicion
  int16_t  limite = LARGO_TIRA - 1 - TWANG_TERRENO_MARGEN;     // antes de la salida
  uint8_t  lavas = 0, cintas = 0;
  bool     tocaLava = true;

  // Los tramos en el orden en que quedaron sobre la tira. Lo unico que se hace
  // con esto es, al final, calcular hasta donde puede correrse cada lava sin
  // comerse al vecino (ver el flujo, mas abajo).
  int16_t ordIni[TWANG_MAX_LAVA + TWANG_MAX_CINTAS];
  int16_t ordFin[TWANG_MAX_LAVA + TWANG_MAX_CINTAS];
  int8_t  ordLava[TWANG_MAX_LAVA + TWANG_MAX_CINTAS];        // indice de lava, o -1 si el tramo es cinta
  uint8_t ordN = 0;

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
    ordIni[ordN]  = cursor;
    ordFin[ordN]  = cursor + largo - 1;
    ordLava[ordN] = tocaLava ? (int8_t)(lavas - 1) : (int8_t)-1;
    ordN++;
    cursor += largo;
    tocaLava = !tocaLava;
  }

  lavaN   = lavas;    // puede haber entrado menos de lo pedido si se acabo la tira
  cintasN = cintas;

  // Corredor de cada lava: hasta donde puede estirarse. En las puntas de la tira
  // el limite son los margenes; entre dos tramos el hueco se REPARTE, porque el
  // vecino tambien puede estar creciendo (si cada uno se midiera contra donde
  // nacio el otro, los dos se comerian el mismo hueco y terminarian fundidos).
  // De lo que sobra despues de descontar los TWANG_LAVA_SEPARACION LEDs de piso
  // firme se lleva la mitad cada uno, o todo el que crece si el vecino es cinta.
  for (uint8_t k = 0; k < ordN; k++) {
    int8_t l = ordLava[k];
    if (l < 0) continue;
    lavaMin[l]   = TWANG_TERRENO_MARGEN;    // los vecinos, si los hay, achican esto abajo
    lavaMax[l]   = limite;
    lavaPunta[l] = (random(2) == 0) ? -1 : +1;
    lavaCrece[l] = ahora;
  }
  for (uint8_t k = 0; k + 1 < ordN; k++) {
    int16_t libre = ordIni[k + 1] - ordFin[k] - 1 - TWANG_LAVA_SEPARACION;
    if (libre < 0) libre = 0;
    int16_t paraAbajo = (ordLava[k] < 0) ? 0 : ((ordLava[k + 1] < 0) ? libre : libre / 2);
    if (ordLava[k]     >= 0) lavaMax[ordLava[k]]     = ordFin[k]     + paraAbajo;
    if (ordLava[k + 1] >= 0) lavaMin[ordLava[k + 1]] = ordIni[k + 1] - (libre - paraAbajo);
  }
}

// Arma la tanda del nivel actual: mas enemigos y mas rapidos a cada nivel, con
// piso de velocidad para que no se vuelva imposible. Las apariciones se
// escalonan en el tiempo (y un poco en la posicion) para que no salgan pegados.
static void iniciarTanda() {
  uint8_t n = (uint8_t)escalaLeds(TWANG_ENEMIGOS_N1 + (nivel - 1));
  if (n > topeEnemigos()) n = topeEnemigos();

  if (nivel == 1) velEnemigo = velInicial;    // el pote fija el nivel 1
  else velEnemigo = max<int>(TWANG_VEL_MINIMA, (velEnemigo * 88) / 100);

  // Reparto de tipos. El nivel 1 es la presentacion: uno de cada uno, para que
  // se vea de entrada que no todos bajan. El 2 todavia es mitad caminantes, y
  // del 3 en adelante la mayoria patrulla y solo un cuarto baja a buscarte.
  uint8_t caminantes, centinelas;
  if      (nivel == 1) { caminantes = n / 3;       centinelas = n / 3; }
  else if (nivel == 2) { caminantes = (n + 1) / 2; centinelas = 1; }
  else                 { caminantes = (n >= 4) ? n / 4 : 1; centinelas = (n >= 6) ? 2 : 1; }
  if (caminantes + centinelas > n) centinelas = n - caminantes;   // red de seguridad si se tocan los topes
  uint8_t estacionarios = n - caminantes;                         // centinelas + patrullas

  // Bandas: la mazmorra util se parte en n tramos iguales y cada enemigo se
  // queda con el suyo, asi la tanda se reparte por toda la tira en vez de venir
  // en fila india. Los caminantes tambien ocupan banda aunque no la miren
  // nunca, y eso corre a los estacionarios hacia arriba: el fondo, que es donde
  // reaparecemos, queda para los que vienen bajando.
  int16_t base  = TWANG_TERRENO_MARGEN;
  int16_t tope  = LARGO_TIRA - 1 - TWANG_TERRENO_MARGEN;
  int16_t ancho = (tope - base + 1) / n;
  if (ancho < 1) ancho = 1;

  uint32_t ahora = millis();
  uint8_t  puestos = 0;                       // centinelas ya colocados
  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) {
    enemigoVivo[i]    = (i < n);
    enemigoCuenta[i]  = true;                 // los de la tanda son los que abren la salida
    enemigoPos[i]     = LARGO_TIRA - 1 - (int16_t)random(0, 5);
    enemigoDir[i]     = -1;                   // todos entran bajando desde la salida
    for (uint8_t j = 0; j < TWANG_MAX_JUG; j++) enemigoLado[i][j] = 0;   // se fija en su primer frame
    enemigoAparece[i] = ahora + (uint32_t)i * TWANG_APARICION_MS;
    enemigoPaso[i]    = enemigoAparece[i];

    int16_t bIni = base + (int16_t)i * ancho;
    if (i < caminantes) {
      enemigoTipo[i]    = ENE_CAMINANTE;
      enemigoEnBanda[i] = true;               // no tiene zona: baja hasta salirse por la base
      enemigoIni[i]     = 0;
      enemigoFin[i]     = 0;
    } else {
      // Los centinelas se reparten entre las patrullas por proporcion, para que
      // no queden los dos amontonados en las bandas de mas abajo.
      uint8_t j = i - caminantes;
      enemigoEnBanda[i] = false;
      if ((uint16_t)puestos * estacionarios <= (uint16_t)j * centinelas) {
        enemigoTipo[i] = ENE_CENTINELA;
        enemigoIni[i]  = bIni + ancho / 2;    // se planta en el centro de su banda
        enemigoFin[i]  = enemigoIni[i];
        puestos++;
      } else {
        enemigoTipo[i] = ENE_PATRULLA;
        enemigoIni[i]  = bIni;
        enemigoFin[i]  = bIni + ancho - 1;
      }
    }
  }

  // Generador: aparece en TWANG_GEN_NIVEL y aprieta un poco mas cada nivel.
  if (nivel >= TWANG_GEN_NIVEL) {
    uint16_t resta = (uint16_t)(nivel - TWANG_GEN_NIVEL) * TWANG_GEN_PASO;
    genCada    = (resta > TWANG_GEN_MS - TWANG_GEN_MS_MIN) ? TWANG_GEN_MS_MIN : TWANG_GEN_MS - resta;
    genProximo = ahora + genCada;
  } else {
    genProximo = 0;
  }

  generarTerreno();             // lava y cintas nuevas en cada nivel

  for (uint8_t j = 0; j < TWANG_MAX_JUG; j++) {
    jugPos[j]      = 0;         // cada nivel se arranca de nuevo en la base, los dos juntos
    jugLed[j]      = 0;
    jugLlego[j]    = false;
    jugAtacando[j] = false;
    jugRadio[j]    = 0;
    jugInvul[j]    = false;
  }
  ultimoFrame = ahora;
}

static void arrancarPartida() {
  for (uint8_t j = 0; j < jugadoresN; j++) calibrarJoy(j);   // sticks soltados al confirmar
  velInicial  = map(leerPoteCrudo(), 0, 4095, TWANG_VEL_LENTA, TWANG_VEL_RAPIDA);
  nivel       = 1;
  for (uint8_t j = 0; j < TWANG_MAX_JUG; j++) {
    jugVidas[j]      = (j < jugadoresN) ? TWANG_VIDAS : 0;   // el que no juega arranca eliminado
    jugAtaque[j]     = 0;       // 0 = ataque disponible desde el primer frame
    jugInvulDesde[j] = 0;
  }
  caida       = 0;
  esRecord    = false;
  estadoTwang = TWANG_JUGANDO;
  iniciarTanda();
}

void nuevoTwang() {
  calibrarJoy(0);               // el del menu, para poder elegir
  variante    = VAR_1JUG;
  jugadoresN  = 1;
  esRecord    = false;
  estadoTwang = TWANG_ELIGIENDO;
}

// Pantalla previa: las tres variantes. Tiene la misma forma que el selector
// compartido de los juegos de dos a cuatro (loopSelectorJugadores), pero aca no
// se elige una cantidad sino un juego, y ademas hace falta poder elegir UN
// jugador, que aquel no contempla.
static void loopElegir() {
  int8_t paso = joystickPaso(0);
  if (paso) {
    int8_t v = (int8_t)variante + paso;
    if (v < 0)              v = VAR_N - 1;    // da la vuelta en los dos sentidos
    if (v >= (int8_t)VAR_N) v = 0;
    variante = (uint8_t)v;
    beep(1200, 25);
  }
  if (btnFlanco[0]) {
    if (variante == VAR_CANONICO) {
      estadoTwang = TWANG_CANONICO;
      nuevoTwang32();
    } else {
      jugadoresN = (variante == VAR_2JUG) ? 2 : 1;
      arrancarPartida();
    }
    return;
  }

  FastLED.clear();
  if (variante == VAR_CANONICO) {
    // La mazmorra apagada con la salida azul al fondo: la firma del canonico,
    // donde la salida esta SIEMPRE abierta y matar es opcional.
    for (int16_t i = 0; i < LARGO_TIRA; i++) setLed(i, CRGB(0, 22, 0));
    setLed(LARGO_TIRA - 1, CRGB(0, 60, 255));
  } else {
    // La tira partida en tantas franjas como jugadores, cada una del color con
    // el que va a jugar ese: se ve quien entra sin leer el LCD.
    uint8_t n = (variante == VAR_2JUG) ? 2 : 1;
    for (uint8_t j = 0; j < n; j++) {
      int16_t ini = (int16_t)(((int32_t)LARGO_TIRA * j) / n);
      int16_t fin = (int16_t)(((int32_t)LARGO_TIRA * (j + 1)) / n) - 1;
      for (int16_t i = ini; i <= fin; i++) setLed(i, (j == 0) ? COL_JUGADOR : COL_JUG2);
    }
    nscale8(leds, LARGO_TIRA, 60);
  }
  FastLED.show();
}

static void perder() {
  estadoTwang = TWANG_FIN;
  faseDesde   = millis();
  // El cooperativo tiene su propio record: de a dos se limpia mucho mas rapido
  // y meter los dos numeros en la misma lista seria comparar juegos distintos.
  esRecord    = intentarRecord(jugadoresN > 1 ? REC_TWANG_COOP : REC_TWANG, (uint32_t)nivel);
  esRecord ? sonarRecord() : sonarGameOver();
}

// Un golpe recibido por el jugador j, venga de un enemigo o de la lava: en los
// dos casos pierde una vida, arranca su invulnerabilidad y suena. Si era la
// ultima queda eliminado y el companero sigue solo. Devuelve true unicamente si
// con eso se acabo la partida: ahi el frame tiene que cortar, porque ya se
// entro en TWANG_FIN y no hay nada mas que actualizar ni dibujar.
static bool recibirGolpe(uint8_t j) {
  jugVidas[j]--;
  if (jugVidas[j] == 0) {
    caida = jugLed[j];
    sonarEliminado();
    if (jugadoresEnJuego() == 0) { perder(); return true; }
    return false;
  }
  jugInvul[j]      = true;
  jugInvulDesde[j] = millis();
  sonarGolpe();
  return false;
}

void loopTwang() {
  uint32_t ahora = millis();

  if (estadoTwang == TWANG_ELIGIENDO) { loopElegir(); return; }
  if (estadoTwang == TWANG_CANONICO)  { loopTwang32(); return; }

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
    int16_t  frente = LARGO_TIRA - 1 - (int16_t)(t / 10);
    FastLED.clear();
    for (int16_t i = frente; i < LARGO_TIRA; i++) setLed(i, COL_SALIDA);
    FastLED.show();
    if (t > TWANG_SALIDA_MS) {
      nivel++;
      iniciarTanda();
      estadoTwang = TWANG_JUGANDO;
    }
    return;
  }

  // --- Movimiento: velocidad * tiempo del frame, cada jugador con su stick ---
  // El dt se mide contra el frame anterior porque el loop no tiene periodo fijo.
  float dt = (ahora - ultimoFrame) / 1000.0f;
  ultimoFrame = ahora;
  for (uint8_t j = 0; j < jugadoresN; j++) {
    if (!enJuego(j)) continue;
    if (jugLlego[j]) { jugLed[j] = LARGO_TIRA - 1; continue; }   // esperando en la salida: ya no se mueve
    jugPos[j] += leerJoyNorm(j) * TWANG_VEL_JUGADOR * dt;

    // Cintas: arrastran al que este parado encima, ademas de lo que haga el
    // joystick. Como TWANG_CINTA_VEL es bastante menor que TWANG_VEL_JUGADOR se
    // puede caminar en contra (mas lento), no es una trampa sin salida.
    int16_t pisando = (int16_t)(jugPos[j] + 0.5f);
    for (uint8_t i = 0; i < cintasN; i++) {
      if (pisando >= cintaIni[i] && pisando <= cintaFin[i]) {
        jugPos[j] += cintaDir[i] * (float)TWANG_CINTA_VEL * dt;
      }
    }

    if (jugPos[j] < 0)               jugPos[j] = 0;
    if (jugPos[j] > LARGO_TIRA - 1)  jugPos[j] = LARGO_TIRA - 1;
    jugLed[j] = (int16_t)(jugPos[j] + 0.5f);

    if (jugInvul[j] && ahora - jugInvulDesde[j] > TWANG_INVUL_MS) jugInvul[j] = false;
  }

  // --- Lava: cada tramo alterna encendida/apagada con su propio reloj ---
  // Estar parado en una encendida cuesta una vida, igual que un enemigo.
  for (uint8_t i = 0; i < lavaN; i++) {
    uint16_t dur = lavaOn[i] ? TWANG_LAVA_ON_MS : TWANG_LAVA_OFF_MS;
    if (ahora - lavaCambio[i] >= dur) {
      lavaCambio[i] += dur;
      lavaOn[i] = !lavaOn[i];
    }
    // Crecimiento: el tramo se ESTIRA de a un LED, alternando puntas, hasta
    // llegar a su largo maximo o a los bordes del corredor que le calculo
    // generarTerreno. No se desplaza: una lava que se corriera entera por la
    // tira se leeria como un bicho moviendose, no como lava.
    if (ahora - lavaCrece[i] >= TWANG_LAVA_CRECE_MS) {
      lavaCrece[i] += TWANG_LAVA_CRECE_MS;
      if (lavaFin[i] - lavaIni[i] + 1 < TWANG_LAVA_LARGO_MAX) {
        for (uint8_t intento = 0; intento < 2; intento++) {   // si la punta que toca ya no da, prueba la otra
          bool crecio = false;
          if (lavaPunta[i] < 0) { if (lavaIni[i] > lavaMin[i]) { lavaIni[i]--; crecio = true; } }
          else                  { if (lavaFin[i] < lavaMax[i]) { lavaFin[i]++; crecio = true; } }
          lavaPunta[i] = -lavaPunta[i];                       // la proxima le toca a la otra
          if (crecio) break;
        }
      }
    }
    if (!lavaOn[i]) continue;
    for (uint8_t j = 0; j < jugadoresN; j++) {
      if (!enJuego(j) || jugInvul[j] || jugLlego[j]) continue;
      if (jugLed[j] >= lavaIni[i] && jugLed[j] <= lavaFin[i]) {
        if (recibirGolpe(j)) return;
      }
    }
  }

  // --- Ataque: pulso que se expande durante una ventana corta, con cooldown ---
  // Uno por jugador, cada uno con su boton y su cooldown. No hay fuego amigo:
  // el pulso solo mira enemigos, los jugadores se atraviesan entre ellos.
  for (uint8_t j = 0; j < jugadoresN; j++) {
    jugRadio[j] = 0;
    if (!enJuego(j) || jugLlego[j]) { jugAtacando[j] = false; continue; }
    if (btnFlanco[j] && ahora - jugAtaque[j] >= TWANG_ATAQUE_ESPERA) {
      jugAtaque[j]   = ahora;
      jugAtacando[j] = true;
      sonarAtaque();
    }
    uint32_t t = ahora - jugAtaque[j];
    if (jugAtacando[j] && t > TWANG_ATAQUE_MS) jugAtacando[j] = false;
    if (jugAtacando[j]) jugRadio[j] = 1 + ((int16_t)TWANG_ATAQUE_RADIO * (int16_t)t) / TWANG_ATAQUE_MS;
  }

  // --- Generador: la salida va soltando caminantes hasta que llegues ---
  // No cuentan para abrir la salida y no paran cuando la tanda queda limpia:
  // lo unico que los frena es que te acerques a la meta. Asi la corrida final
  // tambien se juega, en vez de ser un paseo por una mazmorra ya vacia.
  if (genProximo && ahora >= genProximo) {
    // Zona franca: con el jugador en los ultimos LEDs antes de la salida no
    // sale nada. Un enemigo naciendo en la salida justo cuando estas por
    // cruzarla te mata sin margen de reaccion, y eso no es jugable. Se
    // reprograma igual, asi al bajar de la zona tenes el intervalo entero por
    // delante en vez de un enemigo encima al primer paso.
    if (!alguienEnLaPuerta()) {
      int8_t libre = -1;
      for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS && libre < 0; i++) if (!enemigoVivo[i]) libre = (int8_t)i;
      if (libre >= 0) {
        enemigoVivo[libre]    = true;
        enemigoTipo[libre]    = ENE_CAMINANTE;
        enemigoCuenta[libre]  = false;
        enemigoEnBanda[libre] = true;
        enemigoPos[libre]     = LARGO_TIRA - 1;
        enemigoDir[libre]     = -1;
        for (uint8_t j = 0; j < TWANG_MAX_JUG; j++) enemigoLado[libre][j] = 0;
        enemigoAparece[libre] = ahora;
        enemigoPaso[libre]    = ahora;
      }
    }
    genProximo = ahora + genCada;
  }

  // --- Enemigos: cada tipo da su paso, y despues se mira el choque ---
  for (uint8_t i = 0; i < TWANG_MAX_ENEMIGOS; i++) {
    if (!enemigoVivo[i]) continue;
    if (ahora < enemigoAparece[i]) continue;      // todavia no entro a la mazmorra

    if (ahora - enemigoPaso[i] >= velEnemigo) {
      enemigoPaso[i] += velEnemigo;
      enemigoPos[i]  += enemigoDir[i];

      if (enemigoTipo[i] == ENE_CAMINANTE) {
        if (enemigoPos[i] < 0) { enemigoVivo[i] = false; continue; }   // se escapo por la base
      } else if (!enemigoEnBanda[i]) {
        // Sigue bajando a ocupar su zona: entro por la salida, como todos.
        if (enemigoPos[i] <= enemigoFin[i]) {
          enemigoPos[i]     = enemigoFin[i];
          enemigoEnBanda[i] = true;
          if (enemigoTipo[i] == ENE_CENTINELA) enemigoDir[i] = 0;      // llego a su punto y se planta
        }
      } else if (enemigoTipo[i] == ENE_PATRULLA) {
        // Ya esta en su banda: rebota entre los bordes y no baja de enemigoIni.
        if      (enemigoPos[i] <= enemigoIni[i]) { enemigoPos[i] = enemigoIni[i]; enemigoDir[i] = +1; }
        else if (enemigoPos[i] >= enemigoFin[i]) { enemigoPos[i] = enemigoFin[i]; enemigoDir[i] = -1; }
      }
    }

    // Lo mata el pulso de cualquiera de los dos.
    bool muerto = false;
    for (uint8_t j = 0; j < jugadoresN && !muerto; j++) {
      if (!jugAtacando[j]) continue;
      int16_t d = enemigoPos[i] - jugLed[j];
      if ((d < 0 ? -d : d) <= jugRadio[j]) muerto = true;
    }
    if (muerto) {
      enemigoVivo[i] = false;
      sonarMuerte();
      continue;
    }

    // Contacto: como hay enemigos que se quedan arriba se puede chocar de los
    // dos lados (subiendo contra uno, o dejando que uno te alcance por la
    // espalda), asi que no se mira "esta debajo mio" sino el CRUCE: que haya
    // cambiado de lado desde el frame anterior, o que caiga en el mismo LED.
    // Se lleva un lado por jugador, porque el mismo bicho puede estar arriba de
    // uno y abajo del otro. Durante la invulnerabilidad lo atraviesan sin hacer
    // dano, para no comerse varias vidas del mismo choque.
    for (uint8_t j = 0; j < jugadoresN; j++) {
      if (!enJuego(j)) continue;
      int16_t d      = enemigoPos[i] - jugLed[j];
      int8_t  lado   = (d > 0) ? +1 : (d < 0 ? -1 : 0);
      bool    choque = (lado == 0) || (enemigoLado[i][j] != 0 && lado != enemigoLado[i][j]);
      enemigoLado[i][j] = lado;
      if (choque && !jugInvul[j] && !jugLlego[j]) {
        enemigoVivo[i] = false;
        if (recibirGolpe(j)) return;
        break;                       // ya se lo llevo puesto uno: no puede chocar tambien al otro
      }
    }
  }

  // --- Salida: con la tanda limpia, el que llega se queda esperando ahi ---
  // Para pasar de nivel tienen que haber llegado TODOS los que sigan en pie. El
  // que espera queda a salvo (no lo tocan ni los enemigos ni la lava): si no,
  // ayudar al companero seria peor que dejarlo morir.
  bool limpio = (enemigosDeTanda() == 0);
  if (limpio) {
    bool llegoAlguno = false;
    for (uint8_t j = 0; j < jugadoresN; j++) {
      if (enJuego(j) && !jugLlego[j] && jugLed[j] >= LARGO_TIRA - 1) {
        jugLlego[j] = true;
        llegoAlguno = true;
      }
    }
    uint8_t enPie = 0, llegaron = 0;
    for (uint8_t j = 0; j < jugadoresN; j++) {
      if (!enJuego(j)) continue;
      enPie++;
      if (jugLlego[j]) llegaron++;
    }
    if (enPie && llegaron == enPie) {
      estadoTwang = TWANG_NIVEL;
      faseDesde   = ahora;
      sonarNivel();
      return;
    }
    if (llegoAlguno) sonarLlegada();     // falta el otro: solo se avisa que este ya esta
  }

  // --- Dibujo: salida y terreno abajo, despues enemigos, pulsos y jugadores ---
  FastLED.clear();
  // La salida parpadea SOLO con la tanda limpia; con enemigos vivos queda fija.
  // Y antes de cada enemigo del generador da dos destellos violetas: la puerta
  // es la boca por la que entran, asi que avisa ella. Solo los del generador,
  // que salen de a uno y con tiempo de sobra; los de la tanda nacen todos
  // juntos al arrancar el nivel y esto seria un parpadeo ilegible.
  uint32_t falta  = (genProximo > ahora) ? genProximo - ahora : 0;
  bool     avisar = falta && !alguienEnLaPuerta() &&
                    ((falta <= TWANG_GEN_AVISO_1 && falta > TWANG_GEN_AVISO_1 - TWANG_GEN_AVISO_MS) ||
                     (falta <= TWANG_GEN_AVISO_2 && falta > TWANG_GEN_AVISO_2 - TWANG_GEN_AVISO_MS));
  bool salidaOn = limpio ? ((ahora / 200) % 2 == 0) : true;
  if (avisar) setLed(LARGO_TIRA - 1, COL_AVISO);
  else        setLed(LARGO_TIRA - 1, salidaOn ? COL_SALIDA : CRGB(0, 10, 40));

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

  for (uint8_t j = 0; j < jugadoresN; j++) {   // flash blanco que crece y se apaga
    if (!enJuego(j) || !jugAtacando[j]) continue;
    uint32_t t = ahora - jugAtaque[j];
    CRGB c = CRGB::White;
    c.nscale8(255 - (uint8_t)((255UL * t) / TWANG_ATAQUE_MS));
    for (int16_t k = -jugRadio[j]; k <= jugRadio[j]; k++) setLed(jugLed[j] + k, c);
  }

  // Los jugadores van ultimos para que se vean siempre, aunque esten sobre la
  // lava o dentro del pulso del otro. Al recibir un golpe parpadea el que fue,
  // mientras le dure la invulnerabilidad.
  for (uint8_t j = 0; j < jugadoresN; j++) {
    if (!enJuego(j)) continue;
    if (jugInvul[j] && (ahora / 100) % 2 != 0) continue;
    setLed(jugLed[j], (j == 0) ? COL_JUGADOR : COL_JUG2);
  }
  FastLED.show();
}

// ---------- LCD: nivel y vidas arriba, estado de la tanda abajo ----------
static String vidasDe(uint8_t j) { return enJuego(j) ? String(jugVidas[j]) : String("-"); }

void lcdTwang() {
  if (estadoTwang == TWANG_ELIGIENDO) {
    lcdLinea(0, variante == VAR_CANONICO ? "Twang canonico" : "Twang PixeLED");
    lcdLinea(1, variante == VAR_1JUG ? "< 1 jugador  >" :
                variante == VAR_2JUG ? "< 2 jugadores >" : "< Twang32 >");
    return;
  }
  if (estadoTwang == TWANG_CANONICO) { lcdTwang32(); return; }
  if (estadoTwang == TWANG_FIN) {
    lcdLinea(0, "** GAME OVER **");
    if (esRecord) lcdLinea(1, "*NUEVO RECORD!*");
    else          lcdLinea(1, "Llegaste al Nv" + String(nivel));
    return;
  }
  if (jugadoresN == 1) lcdLinea(0, "Nivel " + String(nivel) + "  Vidas " + vidasDe(0));
  else                 lcdLinea(0, "Nv " + String(nivel) + " P1:" + vidasDe(0) + " P2:" + vidasDe(1));
  uint8_t vivos = enemigosDeTanda();
  lcdLinea(1, vivos ? ("Enemigos: " + String(vivos)) : "Salida abierta!");
}

String webTwang() {
  if (estadoTwang == TWANG_ELIGIENDO) return "Eligiendo variante";
  if (estadoTwang == TWANG_CANONICO)  return webTwang32();
  String s = "Nivel " + String(nivel);
  if (jugadoresN == 1) return s + ", vidas " + vidasDe(0);
  return s + ", P1 " + vidasDe(0) + " vidas, P2 " + vidasDe(1) + " vidas";
}
