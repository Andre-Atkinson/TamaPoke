#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "clockprompt.h"  // CLOCK_EPOCH_END
#include "gamecore.h"  // GameId, records and normalised scores of the Play minigames

// 1 tick = 1 minuto de juego. Baja este valor para probar mas rapido
// (p. ej. 5000UL = las estadisticas caen 12x mas rapido).
#define PET_TICK_MS 60000UL
// Minutos de juego por nivel. Con 60, CHARMANDER evoluciona a las ~16 h
// de juego con cuidado perfecto. Baja a 1 para ver evoluciones al momento.
#define MINUTES_PER_LEVEL 60
#define EAT_ANIM_MS 2500UL
#define HEART_MS 1500UL
#define EVOLVE_ANIM_MS 5200UL              // animacion de evolucion (mas larga = mas epica)
#define CEREMONY_MS 10000UL                // duracion de la despedida en pantalla
#define FAREWELL_AGE_MIN (3UL * 24 * 60)   // se despide a los 3 dias de juego (en forma final)
#define RUNAWAY_TICKS 60                   // se escapa tras 1 h con TODO a cero
// Self-care while charging (#24, idea from Mu-Muu/TamaPoke's autoCare): on USB and
// untouched for SELFCARE_IDLE_MS, once a minute the first need under SELFCARE_BELOW
// (food, then hygiene, then energy, then joy) goes back up to SELFCARE_LEVEL. Only
// up to half, so the player's own care still matters.
#define SELFCARE_IDLE_MS (10UL * 60 * 1000)
#define SELFCARE_BELOW 40
#define SELFCARE_LEVEL 50
// Over this weight the status line says "chubby" (statusMsg) and the idle planner (#22) has
// the pet rest more and play less. Weight is 0-100.
#define PET_CHUBBY_WEIGHT 60

// ritmo de cuidado (ajuste "Care", NVS "pace"): normal o relajado (las necesidades
// bajan a la mitad de velocidad mientras esta despierto)
enum : uint8_t { PACE_NORMAL = 0, PACE_RELAXED = 1, PACE_COUNT = 2 };
// eleccion de huevo (ajuste "Care", NVS "eggch"): tras un final, en vez de un
// huevo al azar salen tres a elegir. Apagado por defecto.
enum : uint8_t { EGGPICK_OFF = 0, EGGPICK_ON = 1, EGGPICK_COUNT = 2 };
#define EGG_CHOICES 3

// milisegundos que faltan hasta `deadline` (0 si ya paso, o si deadline==0:
// "temporizador inactivo"). Todos los temporizadores del juego (dialogos,
// animaciones) se guardan como un instante absoluto "deadline = millis() +
// duracion" y se comparaban con millis() < deadline / millis() > deadline;
// eso se rompe cuando millis() da la vuelta a los ~49,7 dias de uptime. La
// resta sin signo, reinterpretada como con signo, es segura frente a esa
// vuelta mientras el intervalo real sea muy inferior a ~24,8 dias (aqui el
// temporizador mas largo son ~12 s), sin cambiar como se guarda el deadline.
static inline uint32_t timeLeft(uint32_t deadline) {
  if (!deadline) return 0;
  int32_t left = (int32_t)(deadline - millis());
  return left > 0 ? (uint32_t)left : 0;
}

// ceremonias de fin de ciclo
enum : uint8_t { CER_NONE = 0, CER_FAREWELL, CER_RUNAWAY, CER_RELEASE };

enum PetMood : uint8_t { MOOD_HAPPY, MOOD_SAD, MOOD_EATING, MOOD_SLEEPING };

// medallas del individuo (bitmask). Bits 8-10 are the training ribbons (#76): one per
// trained stat, for reaching RIBBON_STR / RIBBON_STA / RIBBON_SPD training points. A save
// from before them loads with those bits clear (Pet::load() keeps only MED_ALL), and a pet
// already past a threshold earns its ribbon at the next medal check.
enum : uint16_t {
  MED_LV10 = 1 << 0, MED_LV25 = 1 << 1, MED_LV50 = 1 << 2,
  MED_BERRY = 1 << 3, MED_STREAK7 = 1 << 4, MED_BOND = 1 << 5,
  MED_FINAL = 1 << 6, MED_FIT = 1 << 7,
  MED_STR = 1 << 8, MED_STA = 1 << 9, MED_SPD = 1 << 10,
};
#define MED_COUNT 11
#define MED_ALL ((uint16_t)((1u << MED_COUNT) - 1))
#define MED_CARE_COUNT 8  // the medals page's grid: bits 0-7; the ribbons are bits 8 to MED_COUNT - 1
// Training points (0-100) that earn each ribbon (#76), reachable in a normal pet's life:
//   strength 30: the bag pays 1 point per 4 hits, at most 18 a session, so about three
//     sessions of 40 hits;
//   stamina 10: steady care pays 1 point per STAMINA_CARE_MIN awake minutes with every need
//     at 40 or more, so about 5 good hours, two or three days of normal care;
//   speed 30: a Play minigame at par pays 3, so about ten games.
#define RIBBON_STR 30
#define RIBBON_STA 10
#define RIBBON_SPD 30
// Steady care builds stamina (#76): every awake minute with all four needs at 40 or more
// counts, in total (a minute under 40 pauses the count, it never resets it), and each
// STAMINA_CARE_MIN of them is +1 trDef. A minute self-care holds up on the charger (#24) is
// not the player's care and doesn't count. The count (goodTicks) is GOOD_STEP a good minute,
// so a per-minute bonus (#25's tidy nature: 5 instead of 4) stays exact without changing what
// the saved count means; it is saved as is (NVS "tdefm", 0 to STAMINA_CARE_MIN * GOOD_STEP - 1)
// so a restart keeps it. Until #76 it was 720 minutes in a row, kept in RAM only.
#define STAMINA_CARE_MIN 30
#define GOOD_STEP 4
// The medals the minute tick can gain by itself, with nobody there: the age levels, "fit"
// (the weight burns off by the minute, at level 5 or more), the stamina ribbon (steady care
// counts by the minute) and a strength or speed ribbon that a save from before #76 already
// qualifies for (the first tick after the update). Their sound keeps out of quiet hours
// (Pet::quietHours, #44) only when BOTH hold: the minute tick gains them with no exercise
// since the tick before (checkMedals(true)), and every medal gained in that call is one of
// these. Exercise burns weight and trains, so the tick right after a minigame or the bag,
// and the end of its screen (exerciseOver), check with checkMedals(false): an IN SHAPE or a
// ribbon the exercise completed sounds. So do the day's first care (registerCare) and
// hatching (checkMedals(false); the egg that hatches by itself is level 1, which earns no
// medal of time). The other medals need the player's care (the favourite berry, the streak,
// the bond, a final form reached by evolving), so they sound as before, also when the tick
// is what notices them a minute later.
#define MED_BY_TIME (MED_LV10 | MED_LV25 | MED_LV50 | MED_FIT | MED_STR | MED_STA | MED_SPD)
#define MEDAL_BANNER_MS 4000UL      // a new medal's banner (drawCelebration)
#define MILESTONE_BANNER_MS 4500UL  // a streak milestone's banner

class Pet {
public:
  // Estadisticas 0..100
  uint8_t fullness = 80;  // comida
  uint8_t joy = 80;       // felicidad
  uint8_t energy = 80;    // energia
  uint8_t hygiene = 100;  // limpieza
  uint8_t poops = 0;      // cacas en pantalla (max 3)
  uint8_t weight = 0;     // 0-100: las chuches engordan, el minijuego quema
  // genes (90-110%, se tiran al eclosionar) y entrenamiento (0-100)
  uint8_t geneAtk = 100, geneDef = 100, geneSpe = 100;
  uint8_t trAtk = 0, trDef = 0, trSpe = 0;
  bool berryKnown = false;  // ya descubrio su baya favorita
  bool shiny = false;       // variante de color rara (se sortea en el huevo)
  uint32_t ageMinutes = 0;
  int16_t speciesId = -1;      // numero de Pokedex (1-151), -1 = huevo
  int16_t prevSpeciesId = -1;  // para la animacion de evolucion
  uint8_t careMistakes = 0;   // descuidos: cada uno retrasa la evolucion 1 nivel
  bool sleeping = false;
  uint32_t lastSeenEpoch = 0;   // ultima hora RTC vista (para progresion offline)
  uint8_t ceremony = CER_NONE;  // despedida/escapada/liberacion en curso
  uint8_t lastEnd = CER_NONE;   // como acabo la anterior (afecta al huevo)
  uint8_t dexReg[19] = { 0 };       // pokedex de criados (bitmap 151 bits)
  uint8_t dexShinyReg[19] = { 0 };  // criados en version shiny
  // racha de cuidado diario (del jugador: persiste entre crianzas)
  uint16_t streak = 0, bestStreak = 0;
  uint32_t lastCareDay = 0;
  // vinculo (del bicho: sube lento con cuidado, se resetea al nacer otro)
  uint8_t bond = 0;
  char nick[12] = "";    // apodo (vacio = nombre de especie)
  // medallas: del individuo + contador acumulado entre todas las crianzas
  uint16_t medals = 0, totalMedals = 0;
  uint16_t newMedal = 0;   // recien conseguida(s), para celebrar
  uint16_t lastMilestone = 0;  // hito de racha ya celebrado
  uint16_t gameHi = 0;     // record de keepy-uppy, el minijuego original (del jugador)
  uint16_t catchHi = 0, peekHi = 0, songHi = 0;  // records de Berry Catch, Peekaboo y Berry Song
  uint16_t strHi = 0;      // record de golpes al saco

  void begin();                 // carga estado de NVS (o crea el primer huevo)
  void update(uint32_t nowMs);  // llamar en cada loop()

  // Acciones (botones tactiles)
  void feed();              // baya roja (compatibilidad)
  void feedBerry(uint8_t color);  // 0 roja, 1 azul, 2 verde
  void feedCandy();
  bool lovesBerry(uint8_t color) const {
    return !isEgg() && (speciesId % 3) == color;  // gusto oculto por especie
  }
  // fin de un minijuego de Play (gamecore.h): guarda el record de ese juego con la
  // puntuacion tal cual y paga la recompensa de la puntuacion normalizada (entrena VEL).
  // Returns the speed it really gained (0 at 100, or for a game that pays nothing), for the
  // result screen (#76).
  uint8_t playGame(uint8_t game, uint16_t raw);
  uint16_t gameBest(uint8_t game) const;  // record de un minijuego (0 si no existe)
  uint8_t trainStrength(uint16_t hits);  // saco de entrenamiento (entrena FUE)

  // stats de combate: base real de gen 1 x genes + nivel + entrenamiento
  uint16_t atkStat() const;
  uint16_t defStat() const;
  uint16_t speStat() const;
  void play();
  void toggleLight();  // dormir / despertar
  // horario de dormir: solo actuan si se puede (no huevo, ni ceremonia, ni eleccion
  // de inicial, ni ya en ese estado). Devuelven true si cambiaron algo. Dormir es
  // silencioso; el sonido al despertar lo decide el sketch.
  bool autoSleep();
  bool autoWake();
  // ajustes "Care" persistentes (NVS "pace" y "bed"); valores fuera de rango = 0
  uint8_t pace = PACE_NORMAL;  // 0 normal, 1 relajado
  uint8_t bedPreset = 0;       // 0 apagado, 1..3 ver bedtime.h
  uint8_t eggPickMode = EGGPICK_OFF;  // 0 un huevo sorpresa, 1 elegir entre tres
  void setPace(uint8_t v);
  void setBedPreset(uint8_t v);
  void setEggPickMode(uint8_t v);
  void clean();
  void caress();  // tocar al bicho
  void eggTap();  // tocar el huevo: 3 toques y eclosiona
  void newEgg();   // empezar de cero con un inicial aleatorio
  void release();  // soltar (pulsacion larga + confirmar)
  void syncClock(uint32_t nowEpoch);  // aplica el tiempo transcurrido apagado
  void setClock(uint32_t nowEpoch);   // fija la hora sin aplicar progresion
  // Self-care while charging (#24). The sketch calls this on every loop pass, before
  // update(): whether the PMU answered at boot, whether USB is in, and the time since
  // the last touch. A PMU that never answered counts as unplugged, because the
  // firmware's usbPresent() reports true in that case. RAM only, never saved.
  void setCharger(bool pmuOk, bool usb, uint32_t idleMs);
  // Quiet hours (attnQuietAt, attention.h; #44): the sketch sets it on every loop pass,
  // before update(). While it holds, tick() keeps the sounds nobody asked for to itself:
  // the hourly level-up and a medal only time earned (MED_BY_TIME). RAM only, never saved.
  bool quietHours = false;
  // A finished doze or ball game on the main screen (#22, idleplan.h): a little energy or
  // joy, already capped per hour by the caller. Never lowers anything, and is not the
  // player's care: no streak, bond, heart or save of its own (the periodic one keeps it).
  // Nothing as an egg (a starter or egg choice included), asleep or during a ceremony.
  void idlePerk(uint8_t energyUp, uint8_t joyUp);
  void startFarewell();  // tambien usable desde la consola serie (BYE)
  void startRunaway();   // tambien usable desde la consola serie (RUN)

  bool isEgg() const { return speciesId < 0; }
  uint8_t eggCracks() const { return eggTaps; }
  bool eating() const { return timeLeft(eatUntil) > 0; }
  bool showHeart() const { return timeLeft(heartUntil) > 0; }
  bool evolving() const { return timeLeft(evolveUntil) > 0; }
  float evolveT() const {     // progreso de la animacion de evolucion 0..1
    return 1.0f - (float)timeLeft(evolveUntil) / (float)EVOLVE_ANIM_MS;
  }
  bool canEvolveNow() const;  // condiciones de evolucion cumplidas (lista)
  void evolve();              // dispara la transformacion (la llama un toque del usuario)
  bool canFarewellNow() const;  // forma final + 7 dias: lista para despedirse (boton)
  bool canRunawayNow() const;   // abandono total 1h: lista para escaparse (boton triste)
  // el usuario decide en un dialogo; "mantener/quedaros" pospone y re-ofrece luego
  bool wantEvolveButton() const { return canEvolveNow() && level() > evoDeclinedLv; }
  bool wantFarewellButton() const { return canFarewellNow() && ageMinutes >= farDeclinedAge; }
  void declineEvolve() { evoDeclinedLv = level(); }              // re-ofrece al subir de nivel
  void declineFarewell() { farDeclinedAge = ageMinutes + 1440; } // re-ofrece dentro de 1 dia
  // primera partida: el jugador elige inicial (Bulbasaur/Charmander/Squirtle)
  bool awaitingStarter() const { return starterPick; }
  void chooseStarter(int16_t dex) { eggTarget = dex; starterPick = false; save(); }
  // eleccion de huevo (ajuste eggPickMode): tres candidatos guardados en NVS; el
  // tiempo se para hasta que elige, como con el inicial
  bool awaitingEgg() const { return eggPick; }
  int16_t eggChoice(uint8_t i) const { return i < EGG_CHOICES ? eggCand[i] : (int16_t)-1; }
  void chooseEgg(uint8_t i);         // i fuera de rango o sin eleccion: no hace nada
  void cancelEggChoice();            // consola SPEC: sale de la eleccion sin elegir
  // rellena out[] con tres especies de huevo distintas: `special` (la tirada de
  // siempre) y dos comunes de otros colores de tipo, en orden aleatorio
  void rollEggChoices(int16_t special, int16_t out[EGG_CHOICES]);
  static bool validEggChoices(const int16_t c[EGG_CHOICES]);
  void factoryReset() { prefs.clear(); }  // borra la NVS (test: comando serie WIPE)
  void dbgRunawayReady() { fullness = joy = energy = hygiene = 0; neglectTicks = RUNAWAY_TICKS; }  // test
  // uint16_t, no uint8_t: con MINUTES_PER_LEVEL=60 el nivel son las horas, asi
  // que un uint8_t da la vuelta a 0 en el nivel 256, a los ~10,6 dias. Y eso se
  // alcanza jugando normal, porque "quedaros juntos" permite posponer la
  // despedida indefinidamente. Se topa en 999 en vez de desbordar.
  uint16_t level() const {
    uint32_t lv = 1 + ageMinutes / MINUTES_PER_LEVEL;
    return lv > 999 ? 999 : (uint16_t)lv;
  }
  bool isRegistered(int16_t dex) const {
    return dex >= 1 && dex <= 151 && (dexReg[(dex - 1) >> 3] & (1 << ((dex - 1) & 7)));
  }
  bool isShinyRegistered(int16_t dex) const {
    return dex >= 1 && dex <= 151 && (dexShinyReg[(dex - 1) >> 3] & (1 << ((dex - 1) & 7)));
  }
  uint16_t registeredCount() const;
  bool lineHasUnregistered(int16_t base) const;
  uint8_t eggRarity() const;       // rareza del huevo actual (sin revelar especie)
  uint16_t eggAccent() const;      // color de tipo del huevo actual (pista: motas de la cascara)
  int16_t pickEggSpecies();        // publica para poder simular tiradas (EGGS)
  uint8_t lowestStat() const { return min(min(fullness, joy), min(energy, hygiene)); }
  PetMood mood() const;
  // progreso de la ceremonia de despedida/escapada, 0..1 (para animarla)
  float ceremonyT() const {
    if (ceremony == CER_NONE) return 0.0f;
    return 1.0f - (float)timeLeft(ceremonyUntil) / (float)CEREMONY_MS;
  }

  // racha / vinculo / medallas / nombre
  void rename(const char *name);
  bool hasMedal(uint16_t m) const { return medals & m; }
  bool showMedal() const { return timeLeft(medalUntil) > 0; }
  bool showMilestone() const { return timeLeft(milestoneUntil) > 0; }
  // While a Play minigame or the strength bag is on screen (the sketch, every loop pass,
  // #69): a medal or streak banner that came up under it (the exercise was the day's first
  // care) waits, and shows its whole time once the creature's screen is back. Its sound
  // played when it was gained, as before.
  void holdBanners();
  // A minigame or the bag just closed (the sketch, once, as its screen goes; #76): a medal
  // its exercise completed (IN SHAPE, a training ribbon) is gained now, on the creature's
  // screen, as the player's (it sounds, quiet hours or not), instead of on the next tick.
  // Nothing new if the screen paid nothing (left by the hold before the end).
  void exerciseOver();
  int careBonus() const;  // mejora del huevo por racha + vinculo

  // guardado periodico diferido: tick() marca pendiente y el loop lo vuelca
  // cuando la pantalla esta atenuada/apagada (la escritura a flash congela
  // ~1s ambos cores: asi no se ve ni corta el tactil)
  bool savePending() const { return pendingSave; }
  // ultima hora real persistida; sirve para resembrar un RTC que perdio la hora
  // 0 for a value past the clock's range (CLOCK_EPOCH_END): the clock refuses it, and a
  // pet saved by the old TIME bug can hold one.
  uint32_t savedEpoch() {
    uint32_t seen = prefs.getUInt("seen", 0);
    return seen >= CLOCK_EPOCH_END ? 0 : seen;
  }
  void flushSave();
  void saveNow() { save(); }  // la consola SAVE: escribe ya, haya o no pendiente

private:
  Preferences prefs;
  uint32_t lastTick = 0;
  uint32_t eatUntil = 0;
  uint32_t heartUntil = 0;
  uint32_t evolveUntil = 0;
  int16_t eggTarget = 1;       // dex oculto que saldra del huevo
  bool eggShiny = false;       // sorpresa sorteada al crear el huevo
  uint8_t eggTaps = 0;
  uint8_t mistakeCooldown = 0;
  uint8_t ticksSinceSave = 0;
  bool pendingSave = false;     // guardado periodico pendiente de volcar
  uint16_t evoDeclinedLv = 0;   // "mantener forma": no ofrecer evolucion hasta subir de nivel
  uint32_t farDeclinedAge = 0;  // "quedaros juntos": no ofrecer despedida hasta esta edad
  bool starterPick = false;     // primera partida: esperando que el jugador elija inicial
  bool eggPick = false;         // esperando que elija uno de los tres huevos (NVS "ecpk")
  int16_t eggCand[EGG_CHOICES] = { 0, 0, 0 };  // candidatos (NVS "ec0".."ec2")
  uint8_t neglectTicks = 0;
  uint16_t goodTicks = 0;  // steady care towards the next stamina point, GOOD_STEP a minute (NVS "tdefm", #76)
  uint32_t ceremonyUntil = 0;
  uint8_t bondToday = 0;       // tope diario de subida de vinculo
  uint32_t medalUntil = 0;     // celebracion de medalla en pantalla
  uint32_t milestoneUntil = 0; // celebracion de hito de racha
  bool selfCareOn = false;     // setCharger(): on USB and untouched SELFCARE_IDLE_MS

  uint32_t today() const { return lastSeenEpoch ? lastSeenEpoch / 86400 : 0; }
  void registerCare();   // primer cuidado del dia: racha + vinculo
  uint8_t playReward(uint8_t n);  // recompensa de un minijuego, n = 0..GAME_SCORE_CAP; the speed gained
  void addBond(uint8_t amt);
  void checkMedals(bool byTick);  // byTick: from the minute tick, no exercise since the last one
  bool exercised = false;  // a minigame or the bag since the last tick (#44): RAM only
  bool allNeedsEmpty() const { return fullness == 0 && joy == 0 && energy == 0 && hygiene == 0; }
  void tick();
  void drainBody();  // parte del goteo de necesidades (comida, energia, caca, higiene)
  void drainJoy();   // ...y la felicidad (depende de comida e higiene ya bajadas)
  void selfCare();   // one need back up to half while charging (#24), from tick()
  void hatch();
  void registerSpecies(int16_t dex);
  void save();
  void load();
  static uint8_t clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
};

// "Ready to evolve" chime (the sketch, fed wantEvolveButton(), speciesId and level() on
// every loop pass): it sounds when the button appears, at most once per level for each
// form. Self-care on the charger (#24) keeps lifting the needs back over 40, so the
// button comes and goes several times an hour while nobody is there, and each return
// used to chime. A new form (evolve()) chimes again at the same level; that also covers
// a new pet: it starts as an egg (speciesId -1), which the loop always sees in between,
// so the species change clears chimedLevel and the level check needs no reset of its own.
//
// `quiet` is quiet hours (attnQuietAt, attention.h; #44). A chime that comes due then is
// spent silently, as if it had sounded: it never sounds later, not when quiet hours end
// and not when the button comes back at the same level. The button shows anyway. The
// sketch's EvoChime lives in RAM, so a reboot with the button showing chimes once more,
// outside quiet hours only (after a full power-off the clock is stale, which is quiet).
// `quieted` says the last update() was such a silent one (for the serial log).
struct EvoChime {
  bool wasReady = false;
  int16_t species = -1;      // the form last seen
  uint16_t chimedLevel = 0;  // 0 = none yet (levels start at 1)
  bool quieted = false;
  bool update(bool ready, int16_t speciesNow, uint16_t level, bool quiet) {
    if (speciesNow != species) {
      species = speciesNow;
      chimedLevel = 0;
    }
    bool due = ready && !wasReady && level != chimedLevel;
    if (due) chimedLevel = level;
    wasReady = ready;
    quieted = due && quiet;
    return due && !quiet;
  }
};

// The Play minigames' and the strength bag's screen (the sketch, every loop pass, fed
// gameOpen || sackOpen; #69, #76). While one is up, a medal or streak banner that came up
// under it waits (Pet::holdBanners); on the pass it has closed, the medals its exercise
// completed are gained (Pet::exerciseOver), so a training ribbon's banner shows as the
// creature's screen comes back.
struct ExerciseScreen {
  bool wasOpen = false;
  void update(bool open, Pet &p) {
    if (open) p.holdBanners();
    else if (wasOpen) p.exerciseOver();
    wasOpen = open;
  }
};

// "About to run away" warning (the sketch, fed canRunawayNow() on every loop pass): one
// SFX_DENY when the run-away button appears. It comes due again only after the button
// went away (a single care saves the pet) and came back. In quiet hours (`quiet`, as for
// EvoChime) it is spent silently and does not sound later; the button shows anyway, and
// the pet never runs away by itself (the player has to press the button).
struct RunawayWarn {
  bool wasReady = false;
  bool quieted = false;  // the last update() came due in quiet hours (for the serial log)
  bool update(bool ready, bool quiet) {
    bool due = ready && !wasReady;
    wasReady = ready;
    quieted = due && quiet;
    return due && !quiet;
  }
};
