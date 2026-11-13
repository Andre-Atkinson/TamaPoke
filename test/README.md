# Tests

Bateria de tests que corre **en el PC, sin placa**: compila la logica pura del
firmware (`pet.cpp`, `i18n.cpp`, `dex.h`) contra unos shims de Arduino y la
ejecuta con un reloj y un `random()` falsos, asi que los resultados son
deterministas y tardan menos de un segundo.

```bash
./test/run_tests.sh           # todo
./test/run_tests.sh --asan    # ademas con AddressSanitizer + UBSan
```

O por partes:

```bash
cd test && make               # solo la logica del firmware
cd test && make FILTER=evolve # solo los tests con "evolve" en el nombre
cd test && make asan          # con sanitizers
python3 test/test_tools.py    # solo las herramientas de tools/
```

## Que se cubre

| Fichero | Que comprueba |
|---|---|
| `test_pet.cpp` | huevo y eleccion de inicial, comida, tick de estadisticas, sueno, descuidos, niveles, evolucion (incluida la rama de Eevee), stats de combate, entrenamiento y minijuego, racha y vinculo, medallas, ceremonias, progresion offline y guardado en NVS |
| `test_dex.cpp` | integridad de la Pokedex: nombres ASCII, cadenas evolutivas sin bucles, toda especie alcanzable, ninguna linea imposible de completar, limites de los buffers de la UI |
| `test_i18n.cpp` | los 6 idiomas completos, sin acentos (la fuente GFX es ASCII), y con los mismos `%u`/`%s` que le pasa el sketch en cada llamada |
| `test_screenlock.cpp` | PWR triple-press, lock hint timing, unlocked reboot, BOOT gesture reset, render/brightness/sleep policy and rollover; `screenlock.cpp` also checks 22 gesture/lock scenarios at compile time |
| `test_bootbutton.cpp` | 1.75C BOOT button: debounce, single/double/long press timing and edge cases |
| `test_trainingtouch.cpp` | 1.75C strength training: tap target, hold-to-exit button, CST9217 report decoding |
| `test_tools.py` | los scripts de `tools/` compilan, los datos de la Pokedex son coherentes y `dex.h` sigue coincidiendo con lo que genera `tools/gen_dex.py` |
| `test_sim.cpp` | simulador de escritorio (`sim/`): lectura de guiones (pasos, duraciones, hora, idiomas, errores con linea), avance del reloj falso (vuelta de `millis()`, tramos de mas de 25 dias, corte por apagado) y el escritor de PNG (CRC-32, Adler-32, zlib) |
| `test_tools.py` (sim) | `sim/gen_sketch.py` (prototipos del `.ino` como arduino-cli, con sus `#if`), `sim/extract_fonts.py`, y la imagen que escribe `test_sim.cpp` leida con el zlib de Python |

## Como esta montado

- `shim/Arduino.h` + `shim/Preferences.h` + `shim/shim.cpp`: lo minimo de Arduino
  que usan `pet.cpp` e `i18n.cpp`, mas lo que necesita el simulador (`String`,
  `Print`, pines e interrupciones, `delay()`, `ESP.*`, el audio mudo). El reloj (`millis()`) y `random()` son falsos
  y controlables; `Preferences` es un mapa en memoria que imita la NVS, asi que
  se puede simular un reinicio creando otro `Pet` y llamando a `begin()`.
- `framework.h` + `main.cpp`: micro framework propio, sin dependencias.
- Los tests marcados con `TEST_KNOWN_ISSUE` describen el comportamiento
  **correcto** de algo que hoy falla: no rompen la bateria, salen listados
  aparte, y avisan si algun dia empiezan a pasar (para poder quitar la marca).

## Que NO se cubre

Todo lo que necesita la placa: pantalla y tactil (`TamaPoke.ino`), audio
(`audio.cpp`), tarjeta SD (`sdmon.cpp`) y RTC/bateria (`rtcbat.cpp`). El
`.ino` tampoco se compila aqui: lo compila el simulador (`tools/sim.sh`, ver la
seccion "Simulator" del README) contra una placa simulada, y `tools/gate.sh` lo
corre en las dos placas. Los tests de este directorio se compilan con el `char`
del Mac (con signo); el simulador usa `-funsigned-char`, como el ESP32-S3.
