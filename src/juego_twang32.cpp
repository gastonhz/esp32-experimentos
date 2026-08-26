// ---------- Twang32: el TWANG canonico de bdring, portado ----------
// Codigo de https://github.com/bdring/TWANG32 (commit a848394, MIT, ver
// src/twang32/LICENSE). La idea de este archivo NO es reescribir TWANG con
// nuestro estilo: es correr el original. Los niveles, las constantes, los
// ticks y las animaciones son los de bdring, con sus comentarios en ingles
// donde el codigo quedo igual. Lo que sigue es la lista COMPLETA de lo que
// hubo que adaptar, que es lo unico que no es suyo:
//
//   ENTRADA. getInput() leia un MPU6050: el angulo daba joystickTilt (-90..90)
//     y el sacudon daba joystickWobble, que arriba de un umbral disparaba el
//     ataque. Aca el angulo sale del joystick analogico (que ya es "inclinacion
//     = velocidad", igual que el original) y el ataque es el boton arcade.
//     El propio autor deja escrito ahi que esa funcion es el punto de cambio.
//
//   SONIDO. El original tiene un DAC con muestras y sound(freq, vol) sostiene
//     un tono que cambia frame a frame. Nuestro buzzer es de beeps con
//     duracion, asi que sound() renueva un beep corto en cada frame (60 ms
//     contra frames de 16: no llega a cortarse nunca) e ignora el volumen,
//     que en un buzzer pasivo no existe. Las SFX* quedan iguales.
//
//   TIRA. leds[] y FastLED son los de la consola. user_settings.led_count pasa
//     a ser LARGO_TIRA y led_brightness a BRILLO. Se saco el FastLED.show() por
//     tarea en el otro nucleo: la consola dibuja como los demas juegos. El
//     funeral del boss se ponia en setBrightness(255) y se quedaba asi: ahora
//     usa BRILLO, que es el techo de corriente de la maquina. Y se corrigieron
//     cuatro "leds[LARGO_TIRA]" (uno pasado del final) que en la tira de 200,
//     que es justo el tamano del buffer, escribian afuera.
//
//   AJUSTES Y WIFI. settings.h y wifi_ap.h enteros: la consola ya tiene su
//     panel, su NVS y sus ajustes. Quedan los valores por defecto de fabrica.
//
//   SALIR. El original es una maquina que nunca termina: al perder vuelve al
//     nivel 0 y al matar al boss tambien. Aca las dos cosas cierran la partida,
//     guardan el puntaje como record y vuelven al menu, que es lo que hacen
//     todos los juegos de la consola.
//
//   SCREENSAVER. Se fue: la consola tiene su propio modo Ambiente y su menu.
//     Con el se fueron Fire2012/LED_march/sinelon/juggle, que solo usaba el.
//     Tambien se fue tickComplete() (no lo llama nadie en el original) y el
//     objeto iSin, que esta declarado y no se usa.
//
//   VIDAS. drawLives() dibuja las vidas en la tira ENTRE nivel y nivel con
//     delay(): son ~460 ms en los que la consola no atiende nada. Se dejo tal
//     cual porque es parte del ritmo del juego, y el LCD ademas las muestra.

#include "juego_twang32.h"

#include "twang32/settings.h"      // VIRTUAL_LED_COUNT, MAX_PLAYER_SPEED, LIVES_PER_LEVEL
#include "twang32/Enemy.h"
#include "twang32/Particle.h"
#include "twang32/Spawner.h"
#include "twang32/Lava.h"
#include "twang32/Boss.h"
#include "twang32/Conveyor.h"

// ---------- Constantes del original (config.h + settings.h + el .ino) ----------
#define DIRECTION            1
#define USE_GRAVITY          0     // 0/1 use gravity (LED strip going up wall)

#define TWANG32_ATAQUE       30000 // el DEFAULT_ATTACK_THRESHOLD del original, que aca dispara el boton
#define VOLUMEN              0     // donde el original pasaba user_settings.audio_volume: nuestro buzzer
                                   // es pasivo y no tiene volumen, asi que sound() lo ignora
#define MIN_REDRAW_INTERVAL  (1000 / 60)   // el juego avanza a 60 fps: los enemigos se mueven POR FRAME
#define CONVEYOR_BRIGHTNESS  40
#define LAVA_OFF_BRIGHTNESS  15

// WOBBLE ATTACK
#define DEFAULT_ATTACK_WIDTH 70    // Width of the wobble attack, world is 1000 wide
#define ATTACK_DURATION      500   // Duration of a wobble attack (ms)
#define BOSS_WIDTH           40

#define STARTUP_WIPEUP_DUR   200
#define STARTUP_SPARKLE_DUR  1300
#define STARTUP_FADE_DUR     1500

#define GAMEOVER_SPREAD_DURATION 1000
#define GAMEOVER_FADE_DURATION   1500

#define WIN_FILL_DURATION    500   // sound has a freq effect that might need to be adjusted
#define WIN_CLEAR_DURATION   1000
#define WIN_OFF_DURATION     1200

#define FIN_MS               2600  // nuestro: cuanto dura el cartel final antes de volver al menu

// ---------- Estado ----------
static long previousMillis = 0;    // Time of the last redraw
static int  levelNumber    = 0;
static int  joystickTilt   = 0;    // Stores the angle of the joystick
static int  joystickWobble = 0;    // Stores the max amount of acceleration (wobble)
static int  attack_width   = DEFAULT_ATTACK_WIDTH;
static long attackMillis   = 0;    // Time the attack started
static bool attacking      = 0;    // Is the attack in progress?

#define ENEMY_COUNT     10
#define PARTICLE_COUNT  100
#define SPAWN_COUNT     5
#define LAVA_COUNT      5
#define CONVEYOR_COUNT  2
static Enemy    enemyPool[ENEMY_COUNT];
static Particle particlePool[PARTICLE_COUNT];
static Spawner  spawnPool[SPAWN_COUNT];
static Lava     lavaPool[LAVA_COUNT];
static Conveyor conveyorPool[CONVEYOR_COUNT];
static Boss     boss = Boss();

enum stages { STARTUP, PLAY, WIN, DEAD, BOSS_KILLED, GAMEOVER, FIN };
static stages stage;

static long stageStartTime;        // Stores the time the stage changed for stages that are time based
static int  playerPosition;        // Stores the player position
static int  playerPositionModifier;// +/- adjustment to player position
static bool playerAlive;
static long killTime;
static int  lives = LIVES_PER_LEVEL;
static bool lastLevel = false;
static int  score = 0;
static bool esRecord = false;      // nuestro: si el puntaje final entro en la tabla

// ---------- Prototipos (el .ino de Arduino no los necesitaba) ----------
static void loadLevel();
static void nextLevel();
static void die();
static void moveBoss();
static void spawnEnemy(int pos, int dir, int speed, int wobble);
static int  getLED(int pos);
static bool tickParticles();
static void SFXcomplete();
static void SFXFreqSweepWarble(int duration, int elapsedTime, int freqStart, int freqEnd, int warble);
static void SFXFreqSweepNoise(int duration, int elapsedTime, int freqStart, int freqEnd, uint8_t noiseFactor);
static long map_constrain(long x, long in_min, long in_max, long out_min, long out_max);
static void updateLives();
static void cleanupLevel();
static void spawnLava(int left, int right, int ontime, int offtime, int offset, int state, float grow, float flow);
static void spawnConveyor(int startPoint, int endPoint, int dir);
static void spawnBoss();
static bool inLava(int pos);
static void SFXkill();
static void SFXwin();
static void SFXgameover();
static void SFXbosskilled();
static void terminar();

// ---------- Sonido: el puente al buzzer de la consola ----------
// TWANG32 llama a sound() en CADA frame para sostener un tono que va cambiando.
// Renovar un beep de 60 ms en frames de 16 ms da exactamente eso, y ademas se
// apaga solo si el juego deja de pedirlo.
static void sound(int freq, int vol) {
  (void)vol;                       // el buzzer pasivo no tiene volumen
  if (freq > 0) beep((uint16_t)freq, 60);
}
static void soundOff() { beep(0, 0); }

// ---------- Entrada: joystick + boton en lugar del acelerometro ----------
// El original avisa en este mismo lugar que se puede reemplazar por lo que sea
// que deje un valor -90..90 en joystickTilt y algo mayor al umbral en
// joystickWobble. Eso es literalmente lo que hacemos.
// El boton se engancha aparte: btnFlanco dura UN frame de la consola, y el
// juego solo mira la entrada cuando le toca su frame de 60 fps. Sin el enganche
// los ataques se pierden en el medio.
static bool botonPendiente = false;

static void getInput() {
  // El bucle hace playerPosition -= tilt/6 con el signo dado vuelta por
  // DIRECTION, o sea que un tilt POSITIVO empuja hacia la salida. leerJoyNorm
  // ya viene con el sentido de la consola, asi que va derecho: a fondo son 90,
  // que es justo donde el original satura en MAX_PLAYER_SPEED.
  joystickTilt   = (int)(leerJoyNorm(0) * 90.0f);
  joystickWobble = botonPendiente ? TWANG32_ATAQUE : 0;
  botonPendiente = false;
}
static void loadLevel(){    	
	// leave these alone
	FastLED.setBrightness(BRILLO);
	updateLives();
	cleanupLevel();    
	playerAlive = 1;
	lastLevel = false; // this gets changed on the boss level
	
	/// Defaults...OK to change the following items in the levels below
	attack_width = DEFAULT_ATTACK_WIDTH; 
	playerPosition = 0; 
	
	/* ==== Level Editing Guide ===============
	Level creation is done by adding to or editing the switch statement below
	
	You can add as many levels as you want but you must have a "case"  
	for each level. The case numbers must start at 0 and go up without skipping any numbers.
	
	Don't edit case 0 or the last (boss) level. They are special cases and editing them could
	break the code.
	
	TWANG uses a virtual 1000 LED grid. It will then scale that number to your strip, so if you
	want something in the middle of your strip use the number 500. Consider the size of your strip
	when adding features. All time values are specified in milliseconds (1/1000 of a second)
	
	You can add any of the following features.
	
	Enemies: You add up to 10 enemies with the spawnEnemy(...) functions.
		spawnEnemy(position, direction, speed, wobble);
			position: Where the enemy starts 
			direction: If it moves, what direction does it go 0=down, 1=away
			speed: How fast does it move. Typically 1 to 4.
			wobble: 0=regular movement, 1=bouncing back and forth use the speed value
				to set the length of the wobble.
				
	Spawn Pools: This generates and endless source of new enemies. 2 pools max
		spawnPool[index].Spawn(position, rate, speed, direction, activate);
			index: You can have up to 2 pools, use an index of 0 for the first and 1 for the second.
			position: The location the enemies with be generated from. 
			rate: The time in milliseconds between each new enemy
			speed: How fast they move. Typically 1 to 4.
			direction: Directions they go 0=down, 1=away
			activate: The delay in milliseconds before the first enemy
			
	Lava: You can create 4 pools of lava.
		spawnLava(left, right, ontime, offtime, offset, state, grow, flow);
			left: the lower end of the lava pool
			right: the upper end of the lava pool
			ontime: How long the lave stays on.
			offset: the delay before the first switch
			state: does it start on or off
			grow: This specifies the rate of growth. Use 0 for no growth. Reasonable growth is 0.1 to 0.5
			flow: This specifies the rate/direction of flow. Reasonable numbers are 0.2 to 0.8 
	
	Conveyor: You can create 2 conveyors.
		spawnConveyor(startPoint, endPoint, direction)
			startPoint: The close end of the conveyor
			endPoint: The far end of the conveyor
			direction(speed): positive = away, negative = towards you (must be less than +/- player speed)
	
	===== Other things you can adjust per level ================ 
	
		Player Start position:
			playerPosition = xxx; 
				
			
		The size of the TWANG attack
			attack_width = xxx;
					
	
	*/
	switch(levelNumber){
	case 0: // basic introduction 
		playerPosition = 200;			
		spawnEnemy(1, 0, 0, 0);					
		break;
	case 1:
		// Slow moving enemy			
		spawnEnemy(900, 0, 1, 0);	
		break;
	case 2:	
		// Spawning enemies at exit every 2 seconds		
		spawnPool[0].Spawn(1000, 3000, 2, 0, 0);
		break;
	case 3:
		// Lava intro
		spawnLava(400, 490, 2000, 2000, 0, Lava::OFF, 0, 0);
		spawnEnemy(350, 0, 1, 0);
		spawnPool[0].Spawn(1000, 5500, 3, 0, 0);
		break;		
	case 4:
	    // intro to moving lava (down)
		spawnLava(400, 490, 2000, 2000, 0, Lava::OFF, 0, -0.5);
		spawnEnemy(350, 0, 1, 0);
		spawnPool[0].Spawn(1000, 5500, 3, 0, 0);
		break;
	case 5:		
		// lava spreading
		spawnLava(400, 450, 2000, 2000, 0, Lava::OFF, 0.25, 0);
		spawnEnemy(350, 0, 1, 0);
		spawnPool[0].Spawn(1000, 5500, 3, 0, 0);	
		break;
	case 6:	
		// Sin wave enemy				
		spawnEnemy(700, 1, 7, 275);
		spawnEnemy(500, 1, 5, 250);	
		break;
	case 7:
		// Sin enemy swarm
		spawnEnemy(700, 1, 7, 275);
		spawnEnemy(500, 1, 5, 250);
		
		spawnEnemy(600, 1, 7, 200);
		spawnEnemy(800, 1, 5, 350);
		
		spawnEnemy(400, 1, 7, 150);
		spawnEnemy(450, 1, 5, 400);		
		break;	
    case 8:
		// lava moving up
		playerPosition = 200;
		spawnLava(10, 180, 2000, 2000, 0, Lava::OFF, 0, 0.5);
		spawnEnemy(350, 0, 1, 0);
		spawnPool[0].Spawn(1000, 5500, 3, 0, 0);
		break;
	case 9:
		// Conveyor
		spawnConveyor(100, 600, -6);
		spawnEnemy(800, 0, 0, 0);
		break;
	case 10:
		// Conveyor of enemies
		spawnConveyor(50, 1000, 6);
		spawnEnemy(300, 0, 0, 0);
		spawnEnemy(400, 0, 0, 0);
		spawnEnemy(500, 0, 0, 0);
		spawnEnemy(600, 0, 0, 0);
		spawnEnemy(700, 0, 0, 0);
		spawnEnemy(800, 0, 0, 0);
		spawnEnemy(900, 0, 0, 0);
		break;
	case 11:
		// lava spread and fall
		spawnLava(400, 450, 2000, 2000, 0, Lava::OFF, 0.2, -0.5);
		spawnEnemy(350, 0, 1, 0);
		spawnPool[0].Spawn(1000, 5500, 3, 0, 0);
		break;
	case 12:   // spawn train;		
		spawnPool[0].Spawn(900, 1300, 2, 0, 0);					
		break;
	case 13:   // spawn train skinny attack width;
		attack_width = 32;
		spawnPool[0].Spawn(900, 1800, 2, 0, 0);
		break;
	case 14:  // evil fast split spawner
		spawnPool[0].Spawn(550, 1500, 2, 0, 0);
		spawnPool[1].Spawn(550, 1500, 2, 1, 0);
		break;
	case 15: // split spawner with exit blocking lava
		spawnPool[0].Spawn(500, 1200, 2, 0, 0);
		spawnPool[1].Spawn(500, 1200, 2, 1, 0);		
		spawnLava(900, 950, 2200, 800, 2000, Lava::OFF, 0, 0);
		break;
	case 16:
		// Lava run
		spawnLava(195, 300, 2000, 2000, 0, Lava::OFF, 0, 0);
		spawnLava(400, 500, 2000, 2000, 0, Lava::OFF, 0, 0);
		spawnLava(600, 700, 2000, 2000, 0, Lava::OFF, 0, 0);
		spawnPool[0].Spawn(1000, 3800, 4, 0, 0);
		break;
	case 17:
		// Sin enemy #2 practice (slow conveyor)
		spawnEnemy(700, 1, 7, 275);
		spawnEnemy(500, 1, 5, 250);
		spawnPool[0].Spawn(1000, 5500, 4, 0, 3000);
		spawnPool[1].Spawn(0, 5500, 5, 1, 10000);
		spawnConveyor(100, 900, -4);
		break;
	case 18:
		// Sin enemy #2 (fast conveyor)
		spawnEnemy(800, 1, 7, 275);
		spawnEnemy(700, 1, 7, 275);
		spawnEnemy(500, 1, 5, 250);			
		spawnPool[0].Spawn(1000, 3000, 4, 0, 3000);
		spawnPool[1].Spawn(0, 5500, 5, 1, 10000);
		spawnConveyor(100, 900, -6);
		break;
	case 19: // (don't edit last level)
		// Boss this should always be the last level			
		spawnBoss();
		break;
	}
	stageStartTime = millis();
	stage = PLAY;
}

static void spawnBoss(){
	lastLevel = true;
	boss.Spawn();	
	moveBoss();
}

static void moveBoss(){
	int spawnSpeed = 1800;
	if(boss._lives == 2) spawnSpeed = 1600;
	if(boss._lives == 1) spawnSpeed = 1000;
	spawnPool[0].Spawn(boss._pos, spawnSpeed, 3, 0, 0);
	spawnPool[1].Spawn(boss._pos, spawnSpeed, 3, 1, 0);
}

static void spawnEnemy(int pos, int dir, int speed, int wobble){
    for(int e = 0; e<ENEMY_COUNT; e++){  // look for one that is not alive for a place to add one
        if(!enemyPool[e].Alive()){
            enemyPool[e].Spawn(pos, dir, speed, wobble);
            enemyPool[e].playerSide = pos > playerPosition?1:-1;
            return;
        }
    }
}

static void spawnLava(int left, int right, int ontime, int offtime, int offset, int state, float grow, float flow){
    for(int i = 0; i<LAVA_COUNT; i++){
        if(!lavaPool[i].Alive()){
            lavaPool[i].Spawn(left, right, ontime, offtime, offset, state, grow, flow);
            return;
        }
    }
}

static void spawnConveyor(int startPoint, int endPoint, int dir){
    for(int i = 0; i<CONVEYOR_COUNT; i++){
        if(!conveyorPool[i]._alive){
            conveyorPool[i].Spawn(startPoint, endPoint, dir);
            return;
        }
    }
}

static void cleanupLevel(){
    for(int i = 0; i<ENEMY_COUNT; i++){
        enemyPool[i].Kill();
    }
    for(int i = 0; i<PARTICLE_COUNT; i++){
        particlePool[i].Kill();
    }
    for(int i = 0; i<SPAWN_COUNT; i++){
        spawnPool[i].Kill();
    }
    for(int i = 0; i<LAVA_COUNT; i++){
        lavaPool[i].Kill();
    }
    for(int i = 0; i<CONVEYOR_COUNT; i++){
        conveyorPool[i].Kill();
    }
    boss.Kill();
}

static void levelComplete(){
    stageStartTime = millis();
    stage = WIN;
	
	if (lastLevel) {
		stage = BOSS_KILLED;
	}
	if (levelNumber != 0)  // no points for the first level
	{			
		score = score + (lives * 10);  // 
	}    
}

static void nextLevel(){		
	levelNumber ++;
	
	if(lastLevel)	{
		terminar();     // nuestro: el original volvia al nivel 0 y seguia para siempre
	}
	else {
		lives = LIVES_PER_LEVEL;
		loadLevel();
	}
}

static void die(){
    playerAlive = 0;
    if(levelNumber > 0) 
		lives --; 
	
    if(lives == 0){
       stage = GAMEOVER;
       stageStartTime = millis();
    }
    else
    {
      for(int p = 0; p < PARTICLE_COUNT; p++){
          particlePool[p].Spawn(playerPosition);
      }
      stageStartTime = millis();
      stage = DEAD;
    }
    killTime = millis();
}

static void tickStartup(long mm)
{
  FastLED.clear();
  if(stageStartTime+STARTUP_WIPEUP_DUR > mm) // fill to the top with green
  {
    int n = _min(map(((mm-stageStartTime)), 0, STARTUP_WIPEUP_DUR, 0, LARGO_TIRA), LARGO_TIRA - 1);  // fill from top to bottom (el -1 es nuestro: no pisar el final del buffer)
    for(int i = 0; i<= n; i++){     
      leds[i] = CRGB(0, 255, 0);
    }   
  }
  else if(stageStartTime+STARTUP_SPARKLE_DUR > mm) // sparkle the full green bar    
  {
    for(int i = 0; i< LARGO_TIRA; i++){    
      if(random8(30) < 28)
        leds[i] = CRGB(0, 255, 0);  // most are green
      else {
        int flicker = random8(250);
        leds[i] = CRGB(flicker, 150, flicker); // some flicker brighter
      }
    }
    
  }
  else if (stageStartTime+STARTUP_FADE_DUR > mm) // fade it out to bottom
  {
    int n = _max(map(((mm-stageStartTime)), STARTUP_SPARKLE_DUR, STARTUP_FADE_DUR, 0, LARGO_TIRA), 0);  // fill from top to bottom
    int brightness = _max(map(((mm-stageStartTime)), STARTUP_SPARKLE_DUR, STARTUP_FADE_DUR, 255, 0), 0);
    // for(int i = 0; i<= n; i++){    
       
      // leds[i] = CRGB(0, brightness, 0);
    // }
    for(int i = n; i< LARGO_TIRA; i++){   
       
      leds[i] = CRGB(0, brightness, 0);
    }   
  } 
  SFXFreqSweepWarble(STARTUP_FADE_DUR, millis()-stageStartTime, 40, 400, 20);
  
}

static void tickEnemies(){
    for(int i = 0; i<ENEMY_COUNT; i++){
        if(enemyPool[i].Alive()){
            enemyPool[i].Tick();
            // Hit attack?
            if(attacking){
                if(enemyPool[i]._pos > playerPosition-(attack_width/2) && enemyPool[i]._pos < playerPosition+(attack_width/2)){
                   enemyPool[i].Kill();
                   SFXkill();
                }
            }
            if(inLava(enemyPool[i]._pos)){
                enemyPool[i].Kill();
                SFXkill();
            }
            // Draw (if still alive)
            if(enemyPool[i].Alive()) {
                leds[getLED(enemyPool[i]._pos)] = CRGB(255, 0, 0);
            }
            // Hit player?
            if(
                (enemyPool[i].playerSide == 1 && enemyPool[i]._pos <= playerPosition) ||
                (enemyPool[i].playerSide == -1 && enemyPool[i]._pos >= playerPosition)
            ){
                die();
                return;
            }
        }
    }
}

static void tickBoss(){
	// DRAW
	if(boss.Alive()){
		boss._ticks ++;
		for(int i = getLED(boss._pos-BOSS_WIDTH/2); i<=getLED(boss._pos+BOSS_WIDTH/2); i++){
			leds[i] = CRGB::DarkRed;
			leds[i] %= 100;
		}
		// CHECK COLLISION
		if(getLED(playerPosition) > getLED(boss._pos - BOSS_WIDTH/2) && getLED(playerPosition) < getLED(boss._pos + BOSS_WIDTH)){
			die();
			return;
		}
		// CHECK FOR ATTACK
		if(attacking){
			if(
					(getLED(playerPosition+(attack_width/2)) >= getLED(boss._pos - BOSS_WIDTH/2) && getLED(playerPosition+(attack_width/2)) <= getLED(boss._pos + BOSS_WIDTH/2)) ||
					(getLED(playerPosition-(attack_width/2)) <= getLED(boss._pos + BOSS_WIDTH/2) && getLED(playerPosition-(attack_width/2)) >= getLED(boss._pos - BOSS_WIDTH/2))
					){
				boss.Hit();
				if(boss.Alive()){
					moveBoss();
				}else{
					spawnPool[0].Kill();
					spawnPool[1].Kill();
				}
			}
		}
	}
}

static void drawPlayer(){
    leds[getLED(playerPosition)] = CRGB(0, 255, 0);
}

static void drawExit(){
    if(!boss.Alive()){
        leds[LARGO_TIRA-1] = CRGB(0, 0, 255);
    }
}

static void tickSpawners(){
    long mm = millis();
    for(int s = 0; s<SPAWN_COUNT; s++){
        if(spawnPool[s].Alive() && spawnPool[s]._activate < mm){
            if(spawnPool[s]._lastSpawned + spawnPool[s]._rate < mm || spawnPool[s]._lastSpawned == 0){
                spawnEnemy(spawnPool[s]._pos, spawnPool[s]._dir, spawnPool[s]._sp, 0);
                spawnPool[s]._lastSpawned = mm;
            }
        }
    }
}

static void tickLava(){
	int A, B, p, i, brightness, flicker;
	long mm = millis();
	
	Lava LP;
	for(i = 0; i<LAVA_COUNT; i++){        
		LP = lavaPool[i];		
		if(LP.Alive()){
			LP.Update(); // for grow and flow
			A = getLED(LP._left);
			B = getLED(LP._right);
			if(LP._state == Lava::OFF){
				if(LP._lastOn + LP._offtime < mm){
					LP._state = Lava::ON;
					LP._lastOn = mm;
				}
				for(p = A; p<= B; p++){
					flicker = random8(LAVA_OFF_BRIGHTNESS);
					leds[p] = CRGB(LAVA_OFF_BRIGHTNESS+flicker, (LAVA_OFF_BRIGHTNESS+flicker)/1.5, 0);
				}
			}else if(LP._state == Lava::ON){
				if(LP._lastOn + LP._ontime < mm){
					LP._state = Lava::OFF;
					LP._lastOn = mm;
				}
				for(p = A; p<= B; p++){
					if(random8(30) < 29)
					leds[p] = CRGB(150, 0, 0);
					else
					leds[p] = CRGB(180, 100, 0);
				}				
			}
		}
		lavaPool[i] = LP;
	}
}

static bool tickParticles(){
	bool stillActive = false;
	uint8_t brightness;
	for(int p = 0; p < PARTICLE_COUNT; p++){
		if(particlePool[p].Alive()){
			particlePool[p].Tick(USE_GRAVITY);
			
			if (particlePool[p]._power < 5)
			{
				brightness = (5 - particlePool[p]._power) * 10;
				leds[getLED(particlePool[p]._pos)] += CRGB(brightness, brightness/2, brightness/2);\
			}
			else      
			leds[getLED(particlePool[p]._pos)] += CRGB(particlePool[p]._power, 0, 0);
			
			stillActive = true;
		}
	}
	return stillActive;
}

static void tickConveyors(){    
	
	//TODO should the visual speed be proportional to the conveyor speed?
    
	int b, speed, n, i, ss, ee, led;
    long m = 10000+millis();
    playerPositionModifier = 0;
	
	int levels = 5; // brightness levels in conveyor 	
	

    for(i = 0; i<CONVEYOR_COUNT; i++){
        if(conveyorPool[i]._alive){
            speed = constrain(conveyorPool[i]._speed, -MAX_PLAYER_SPEED+1, MAX_PLAYER_SPEED-1);
            ss = getLED(conveyorPool[i]._startPoint);
            ee = getLED(conveyorPool[i]._endPoint);
            for(led = ss; led<ee; led++){
                
                n = (-led + (m/100)) % levels;
                if(speed < 0) 
					n = (led + (m/100)) % levels;
				
				b = map(n, 5, 0, 0, CONVEYOR_BRIGHTNESS);
                if(b > 0) 
					leds[led] = CRGB(0, 0, b);
            }

            if(playerPosition > conveyorPool[i]._startPoint && playerPosition < conveyorPool[i]._endPoint){
                playerPositionModifier = speed;
            }
        }
    }
}

static void tickBossKilled(long mm) // boss funeral
{
	static uint8_t gHue = 0; 
	
	FastLED.setBrightness(BRILLO); // super bright! (BRILLO es nuestro techo de corriente)
	
	int brightness = 0;
	FastLED.clear();	
	
	if(stageStartTime+6500 > mm){
		gHue++;
		fill_rainbow( leds, LARGO_TIRA, gHue, 7); // FastLED's built in rainbow
		if( random8() < 200) {  // add glitter
			leds[ random16(LARGO_TIRA) ] += CRGB::White;
		}
		SFXbosskilled();
	}else if(stageStartTime+7000 > mm){
		int n = _max(map(((mm-stageStartTime)), 5000, 5500, LARGO_TIRA, 0), 0);
		for(int i = 0; i< n; i++){
			brightness = (sin(((i*10)+mm)/500.0)+1)*255;
			leds[i].setHSV(brightness, 255, 50);
		}
		SFXcomplete();
	}else{		
		nextLevel();
	}
}

static void tickDie(long mm) { // a short bright explosion...particles persist after it.
	const int duration = 200; // milliseconds
	const int width = 20;     // half width of the explosion

	if(stageStartTime+duration > mm) {// Spread red from player position up and down the width
	
		int brightness = map((mm-stageStartTime), 0, duration, 255, 150); // this allows a fade from white to red
		
		// fill up
		int n = _min(_max(map(((mm-stageStartTime)), 0, duration, getLED(playerPosition), getLED(playerPosition)+width), 0), LARGO_TIRA - 1);   // el _min es nuestro
		for(int i = getLED(playerPosition); i<= n; i++){
			leds[i] = CRGB(255, brightness, brightness);
		}
		
		// fill to down
		n = _max(map(((mm-stageStartTime)), 0, duration, getLED(playerPosition), getLED(playerPosition)-width), 0);
		for(int i = getLED(playerPosition); i>= n; i--){
			leds[i] = CRGB(255, brightness, brightness);
		}
	}
}

static void tickGameover(long mm) {
  
    int brightness = 0;

  if(stageStartTime+GAMEOVER_SPREAD_DURATION > mm) // Spread red from player position to top and bottom
  {
    // fill to top
    int n = _max(map(((mm-stageStartTime)), 0, GAMEOVER_SPREAD_DURATION, getLED(playerPosition), LARGO_TIRA - 1), 0);   // el -1 es nuestro
    for(int i = getLED(playerPosition); i<= n; i++){
      leds[i] = CRGB(255, 0, 0);
    }
    // fill to bottom
    n = _max(map(((mm-stageStartTime)), 0, GAMEOVER_SPREAD_DURATION, getLED(playerPosition), 0), 0);
    for(int i = getLED(playerPosition); i>= n; i--){
      leds[i] = CRGB(255, 0, 0);
    }
    SFXgameover();
  }
  else if(stageStartTime+GAMEOVER_FADE_DURATION > mm)  // fade down to bottom and fade brightness
  {
    int n = _max(map(((mm-stageStartTime)), GAMEOVER_FADE_DURATION, GAMEOVER_SPREAD_DURATION, LARGO_TIRA, 0), 0);
    brightness =  map(((mm-stageStartTime)), GAMEOVER_SPREAD_DURATION, GAMEOVER_FADE_DURATION, 200, 0);

    for(int i = 0; i<= n; i++){
      leds[i] = CRGB(brightness, 0, 0);
    }
    SFXcomplete();
  }

}

static void tickWin(long mm) {
  int brightness = 0;
  FastLED.clear();
  if(stageStartTime+WIN_FILL_DURATION > mm){
    int n = _max(map(((mm-stageStartTime)), 0, WIN_FILL_DURATION, LARGO_TIRA, 0), 0);  // fill from top to bottom
    for(int i = LARGO_TIRA - 1; i>= n; i--){   // el -1 es nuestro: no pisar el final del buffer
      brightness = BRILLO;
      leds[i] = CRGB(0, brightness, 0);
    }
    SFXwin();
  }else if(stageStartTime+WIN_CLEAR_DURATION > mm){
    int n = _max(map(((mm-stageStartTime)), WIN_FILL_DURATION, WIN_CLEAR_DURATION, LARGO_TIRA, 0), 0);  // clear from top to bottom
    for(int i = 0; i< n; i++){
      brightness = BRILLO; 
      leds[i] = CRGB(0, brightness, 0);
    }
    SFXwin();
  }else if(stageStartTime+WIN_OFF_DURATION > mm){   // wait a while with leds off
    leds[0] = CRGB(0, BRILLO, 0);
  }else{        
    nextLevel();
  }
}

static void drawLives()
{
  // show how many lives are left by drawing a short line of green leds for each life
  SFXcomplete();  // stop any sounds
  FastLED.clear(); 
  
  int pos = 0;  
  for (int i = 0; i < lives; i++)
  { 
      for (int j=0; j<4; j++)
      {
        leds[pos++] = CRGB(0, 255, 0);
        FastLED.show();       
      }
      leds[pos++] = CRGB(0, 0, 0);      
      delay(20);
  }
  FastLED.show();
  delay(400);
  FastLED.clear();
}

static void drawAttack(){
    if(!attacking) return;
    int n = map(millis() - attackMillis, 0, ATTACK_DURATION, 100, 5);
    for(int i = getLED(playerPosition-(attack_width/2))+1; i<=getLED(playerPosition+(attack_width/2))-1; i++){
        leds[i] = CRGB(0, 0, n);
    }
    if(n > 90) {
        n = 255;
        leds[getLED(playerPosition)] = CRGB(255, 255, 255);
    }else{
        n = 0;
        leds[getLED(playerPosition)] = CRGB(0, 255, 0);
    }
    leds[getLED(playerPosition-(attack_width/2))] = CRGB(n, n, 255);
    leds[getLED(playerPosition+(attack_width/2))] = CRGB(n, n, 255);
}

static int getLED(int pos){
    // The world is 1000 pixels wide, this converts world units into an LED number
    return constrain((int)map(pos, 0, VIRTUAL_LED_COUNT, 0, LARGO_TIRA-1), 0, LARGO_TIRA-1);
}

static bool inLava(int pos){
    // Returns if the player is in active lava
    int i;
    Lava LP;
    for(i = 0; i<LAVA_COUNT; i++){
        LP = lavaPool[i];
        if(LP.Alive() && LP._state == Lava::ON){
            if(LP._left <= pos && LP._right >= pos) return true;
        }
    }
    return false;
}

static void updateLives(){  
	drawLives();
}

static void SFXFreqSweepWarble(int duration, int elapsedTime, int freqStart, int freqEnd, int warble)
{
	int freq = map_constrain(elapsedTime, 0, duration, freqStart, freqEnd);
	if (warble)
			warble = map(sin(millis()/20.0)*1000.0, -1000, 1000, 0, warble);
		
	sound(freq + warble, VOLUMEN);
}

static void SFXFreqSweepNoise(int duration, int elapsedTime, int freqStart, int freqEnd, uint8_t noiseFactor)
{
	int freq;
	
	if (elapsedTime > duration)
		freq = freqEnd;
	else
		freq = map(elapsedTime, 0, duration, freqStart, freqEnd);
	
	if (noiseFactor)
			noiseFactor = noiseFactor - random8(noiseFactor / 2);
		
	sound(freq + noiseFactor, VOLUMEN);
}

static void SFXtilt(int amount){
	if  (amount == 0){
		SFXcomplete();
		return;	
	}
	
    int f = map(abs(amount), 0, 90, 80, 900)+random8(100);
    if(playerPositionModifier < 0) f -= 500;
    if(playerPositionModifier > 0) f += 200;		
		int vol = map(abs(amount), 0, 90, VOLUMEN / 2, VOLUMEN * 3/4);
    sound(f,vol);
}

static void SFXattacking(){
    int freq = map(sin(millis()/2.0)*1000.0, -1000, 1000, 500, 600);
    if(random8(5)== 0){
      freq *= 3;
    }
    sound(freq, VOLUMEN);
}

static void SFXdead(){	
	SFXFreqSweepNoise(1000, millis()-killTime, 1000, 10, 200);
}

static void SFXgameover(){
	SFXFreqSweepWarble(GAMEOVER_SPREAD_DURATION, millis()-killTime, 440, 20, 60);
}

static void SFXkill(){
    sound(2000, VOLUMEN);
}

static void SFXwin(){
	SFXFreqSweepWarble(WIN_OFF_DURATION, millis()-stageStartTime, 40, 400, 20);
}

static void SFXbosskilled()
{
	SFXFreqSweepWarble(7000, millis()-stageStartTime, 75, 1100, 60);
}

static void SFXcomplete(){
    soundOff();
}

static long map_constrain(long x, long in_min, long in_max, long out_min, long out_max)
{
  // constain the x value to be between in_min and in_max
  if (in_max > in_min){   // map allows min to be larger than max, but constrain does not
    x = constrain(x, in_min, in_max);
  }
  else {
    x = constrain(x, in_max, in_min);
  }  
  return map(x, in_min, in_max, out_min, out_max);
}

// ---------- Enganche con la consola ----------
// Nuestro: el original es setup() + loop() de un aparato que solo corre TWANG.
// Aca hay un menu, asi que la partida empieza, termina y devuelve el control.
static void terminar() {
  soundOff();
  esRecord = intentarRecord(REC_TWANG32, (uint32_t)score);
  if (esRecord) sonarRecord();
  stage          = FIN;
  stageStartTime = millis();
}

void nuevoTwang32() {
  calibrarJoy(0);                 // asume el stick soltado en el instante de arrancar
  cleanupLevel();
  levelNumber    = 0;
  lives          = LIVES_PER_LEVEL;
  score          = 0;
  esRecord       = false;
  attacking      = 0;
  botonPendiente = false;
  previousMillis = 0;
  playerPosition = 0;
  playerAlive    = 1;
  lastLevel      = false;
  stage          = STARTUP;
  stageStartTime = millis();
}

void loopTwang32() {
  long mm = millis();

  if (btnFlanco[0]) botonPendiente = true;   // ver getInput(): el flanco dura un frame de la consola

  if (stage == FIN) {                        // nuestro: el cartel final antes de volver al menu
    FastLED.clear();
    if (esRecord) dibujarChispasRecord();
    FastLED.show();
    if (mm - stageStartTime > FIN_MS) volverAlMenu();
    return;
  }

  // --- De aca abajo es el loop() del original, sin el screensaver ni el WiFi ---
  if(stage == PLAY){
      if(attacking){
          SFXattacking();
      }else{
          SFXtilt(joystickTilt);
      }
  }else if(stage == DEAD){
      SFXdead();
  }

  if (mm - previousMillis >= MIN_REDRAW_INTERVAL) {
      getInput();
      previousMillis = mm;

        if(stage == STARTUP){
			if (stageStartTime+STARTUP_FADE_DUR > mm) {
				tickStartup(mm);
			}
			else
			{
			SFXcomplete();
			levelNumber = 0;
			loadLevel();
			}
		}else if(stage == PLAY){
            // PLAYING
            if(attacking && attackMillis+ATTACK_DURATION < mm) attacking = 0;

            // If not attacking, check if they should be
            if(!attacking && joystickWobble >= TWANG32_ATAQUE){
                attackMillis = mm;
                attacking = 1;
            }

            // If still not attacking, move!
            playerPosition += playerPositionModifier;
            if(!attacking){
                SFXtilt(joystickTilt);
                int moveAmount = (joystickTilt/(6.0));  // 6.0 is ideal at 16ms interval
                if(DIRECTION) moveAmount = -moveAmount;
                moveAmount = constrain(moveAmount, -MAX_PLAYER_SPEED, MAX_PLAYER_SPEED);

                playerPosition -= moveAmount;
                if(playerPosition < 0)
					playerPosition = 0;

				// stop player from leaving if boss is alive
				if (boss.Alive() && playerPosition >= VIRTUAL_LED_COUNT) // move player back
					playerPosition = 999;

                if(playerPosition >= VIRTUAL_LED_COUNT && !boss.Alive()) {
                    // Reached exit!
                    levelComplete();
                    return;
                }
            }

            if(inLava(playerPosition)){
                die();
            }

            // Ticks and draw calls
            FastLED.clear();
            tickConveyors();
            tickSpawners();
            tickBoss();
            tickLava();
            tickEnemies();
            drawPlayer();
            drawAttack();
            drawExit();
        }else if(stage == DEAD){
            // DEAD
            FastLED.clear();
			tickDie(mm);
            if(!tickParticles()){
                loadLevel();
            }
        }else if(stage == WIN){
            // LEVEL COMPLETE
            tickWin(mm);
        }else if(stage == BOSS_KILLED){
					tickBossKilled(mm);
         } else if (stage == GAMEOVER) {
						if (stageStartTime+GAMEOVER_FADE_DURATION > mm)
						{
							tickGameover(mm);
						}
						else
						{
						terminar();     // nuestro: el original arrancaba de nuevo de cero
						}
         }

      FastLED.show();
  }
}

// ---------- LCD y panel web ----------
void lcdTwang32() {
  if (stage == FIN) {
    lcdLinea(0, esRecord ? "*NUEVO RECORD!*" : "** GAME OVER **");
    lcdLinea(1, "Puntaje: " + String(score));
    return;
  }
  lcdLinea(0, "Twang32  Nivel " + String(levelNumber + 1));
  lcdLinea(1, "Vidas " + String(lives) + "   Pts " + String(score));
}

String webTwang32() {
  return "Twang32 nivel " + String(levelNumber + 1) +
         ", vidas " + String(lives) + ", puntaje " + String(score);
}
