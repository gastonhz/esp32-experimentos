# TWANG32 vendorizado

Las clases de este directorio son de [bdring/TWANG32](https://github.com/bdring/TWANG32)
(commit `a848394`, 2021-12-02), licencia MIT — ver `LICENSE`. Estan **sin tocar
una linea**: `Enemy.h`, `Spawner.h`, `Boss.h`, `Lava.h`, `Conveyor.h` y
`Particle.h` son byte por byte las del repo original.

`settings.h` es lo unico propio: un recorte con las dos constantes que esas
clases necesitan (`VIRTUAL_LED_COUNT` y `MAX_PLAYER_SPEED`), porque `Conveyor.h`
hace `#include "settings.h"` y el original arrastraba ajustes por NVS y WiFi que
en esta consola ya existen aparte.

La logica del juego --niveles, ticks, animaciones-- esta portada en
`../juego_twang32.cpp`, tambien lo mas parecida posible al original: ahi arriba
esta la lista completa de lo que hubo que adaptar y por que.
