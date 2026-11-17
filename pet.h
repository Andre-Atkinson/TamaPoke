#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "clockprompt.h"  // CLOCK_EPOCH_END
#include "gamecore.h"  // GameId, records and normalised scores of the Play minigames
#include "nature.h"    // NAT_*, the drains, care boosts and training bonus of each nature (#25)

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
// Treats and toys (#13; the numbers are the owner's, 2026-11-16). Both are always there, with
// nothing to unlock and no daily limit, and both count as the day's care and +1 bond.
//  - The plush toy (the Play strip): a little happiness without a minigame, so no speed
//    training and no cost in energy, food or weight. TOY_JOY a play (a playful pet gets
//    NAT_PLAY_JOY_EXTRA more, as from every play) and a heart, for TOY_PER_HOUR plays an
//    hour; past them, or with joy already full, the pet still plays with it, for no joy and no
//    heart (and a play with joy full spends none of the hour's plays), so it doesn't
//    replace the games (a game at par gives about +35 joy). The hour starts at the first
//    play that gives joy, and lives in RAM like the idle perks' (idleplan.h): a restart gives
//    a fresh hour, and the save is untouched.
//  - Warm milk (the moon strip, "milk, then lights out"): the pet settles happily (MILK_JOY
//    and a heart), and it adds a little weight (MILK_WEIGHT; candy adds 12). It drinks for
//    EAT_ANIM_MS, then the sketch puts it to bed. Asleep after the milk, energy comes back
//    MILK_SLEEP_ENERGY a minute more than the nature's rate (6, lazy 8) until it next wakes,
//    by hand or by bedtime. RAM only: a restart in the night drops it, which shows nowhere,
//    since any sleep fills energy within a quarter of an hour.
#define TOY_JOY 10
#define TOY_PER_HOUR 2
#define TOY_WINDOW_MS 3600000UL
#define TOY_ANIM_MS 2800UL
#define MILK_JOY 5
#define MILK_WEIGHT 4
#define MILK_SLEEP_ENERGY 2

// Sickness and medicine (#15; the owner's choices, 2026-11-17). Off unless the Care page's
// setting is on (NVS "sickopt", sickMode). It is meant to cost something: the owner chose to
// let it punish neglect harder than the rest of the game does, but never to end the pet any
// way but the run-away the game already has. All of it is Pet::sickTick(), once a minute.
//  - Falling sick: the hygiene bar in the red (under SICK_DIRTY) for SICK_AFTER_MIN counted
//    minutes in a row. A counted minute is a live one, awake, outside quiet hours (which
//    include a stale or unknown clock): a minute the pet could have called the player. Asleep
//    or in quiet hours the count waits; any clean (hygiene back to SICK_DIRTY or more) starts
//    it again. Both paces count alike: relaxed only makes the bar go red later. The
//    powered-off catch-up (syncClock) never counts.
//  - Sick: a slip-up (careMistakes, -1 bond, as a need's slip-up) when it falls sick, and
//    another every SICK_SLIP_MIN counted minutes it stays sick, on top of the needs' own. Joy
//    falls one point more a drain minute, but sickness never takes it under SICK_JOY_FLOOR.
//    It doesn't evolve, and the farewell button waits, until it is better.
//  - Better: the medicine (giveMedicine) at once; or by itself after SICK_HEAL_MIN minutes
//    with the hygiene out of the red, in all (a red minute only pauses it), awake or asleep,
//    at any hour.
//  - Left sick SICK_RUNAWAY_MIN counted minutes, it runs away (sickRunawayDue: the sketch
//    starts the run-away, the same ending as total neglect). The medicine saves it up to then.
enum : uint8_t { SICK_OFF = 0, SICK_ON = 1, SICK_MODES = 2 };
#define SICK_DIRTY 25           // hygiene under this is dirty: the bar's red (ATTN_LOW, attention.h)
#define SICK_AFTER_MIN 360      // 6 h of counted minutes in the red: sick
#define SICK_SLIP_MIN 60        // each hour sick is one more slip-up
#define SICK_WARN_MIN 720       // 12 h sick: the sketch's second call, a warning
#define SICK_RUNAWAY_MIN 1440   // 24 h sick: it runs away
#define SICK_HEAL_MIN 720       // 12 h out of the red, in all: better by itself
#define SICK_JOY_FLOOR 30       // sickness's own joy drain stops here
#define SICK_MEDICINE_JOY 10

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
//   strength 30: the rhythm punch (#78) pays 1.5 points a punch, at most 18 a session, so
//     two sessions;
//   stamina 10: steady care pays 1 point per STAMINA_CARE_MIN awake minutes with every need
//     at 40 or more, so 5 good hours. Energy counts too, and awake it only falls (about an
//     hour from full to 40; only sleep brings it back), so the good minutes come from a pet
//     that is fed, clean, happy AND rested: put to bed a while when it's tired, as its
//     "Worn out..." status asks. Looked after hourly with such naps, about a day and a half
//     (the test ribbons.the_thresholds_are_reachable plays it); never napped, about a week on
//     the relaxed pace, longer on the normal one (the stamina hold, #78, adds a faster way: up to 18 a session).
//     Owner's call, 2026-11-16: rest is part of steady care;
//   speed 30: a Play minigame at par pays 3, so about ten games.
#define RIBBON_STR 30
#define RIBBON_STA 10
#define RIBBON_SPD 30
// Steady care builds stamina (#76): every awake minute with all four needs at 40 or more
// counts, in total (a minute under 40 pauses the count, it never resets it), and each
// STAMINA_CARE_MIN of them is +1 trDef. A minute self-care holds up on the charger (#24) is
// not the player's care and doesn't count. The count (goodTicks) is GOOD_STEP a good minute,
// so a per-minute bonus (#25's tidy nature, natureTrainGain(): 5 instead of 4, the point in
// 24 minutes instead of 30) stays exact without changing what
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
// these. Exercise burns weight and trains, so the tick right after a minigame or an exercise,
// and the end of its screen (exerciseOver), check with checkMedals(false): an IN SHAPE or a
// ribbon the exercise completed sounds. So do the day's first care (registerCare) and
// hatching (checkMedals(false); the egg that hatches by itself is level 1, which earns no
// medal of time). The other medals need the player's care (the favourite berry, the streak,
// the bond, a final form reached by evolving), so they sound as before, also when the tick
// is what notices them a minute later.
#define MED_BY_TIME (MED_LV10 | MED_LV25 | MED_LV50 | MED_FIT | MED_STR | MED_STA | MED_SPD)
#define MEDAL_BANNER_MS 4000UL      // a new medal's banner (drawCelebration)
#define MILESTONE_BANNER_MS 4500UL  // a streak milestone's banner

// The celebration banners (#82, #83): a streak milestone's and each new medal's, one at a
// time, each for its whole time. One that comes while another shows waits its turn: a streak
// milestone first (it answers the day's first care), then the medals, lowest bit first. So
// several medals gained on one tick each get their own banner, and a milestone that comes
// with a medal no longer hides under it. Bounded by its shape: MED_COUNT bits of waiting
// medals and one milestone flag (a pet gains each medal once, a milestone comes once a day),
// so at most MED_COUNT + 1 banners in a row. Each one starts as the one before it ends, so the
// queue keeps time with no loop pass in between: with the screen off it runs out unseen, as a
// single banner always did. Their sound is the gain's (Pet::checkMedals), not the banner's.
// RAM only: a restart starts with none. An ending clears it (Pet::startFarewell, startRunaway,
// release), so the egg after it and the pet that hatches start with none either.
enum : int8_t { BANNER_NONE = -1, BANNER_STREAK = MED_COUNT };  // 0..MED_COUNT - 1: a medal's bit
struct BannerQueue {
  int8_t on = BANNER_NONE;  // on screen: a medal's bit, BANNER_STREAK or BANNER_NONE
  uint32_t until = 0;       // ...until this millis()
  uint16_t medals = 0;      // the medals waiting (bits), not the one on screen
  bool streak = false;      // a streak milestone waiting

  static uint32_t length(int8_t b) { return b == BANNER_STREAK ? MILESTONE_BANNER_MS : MEDAL_BANNER_MS; }
  // What shows at `now`, the ones waiting started as each before them ended; changes nothing
  int8_t shown(uint32_t now) const {
    BannerQueue q = *this;
    q.update(now);
    return q.on;
  }
  // Takes the banners that ended by `now` off the screen, the next ones on in their turn
  void update(uint32_t now) {
    while (on != BANNER_NONE && (int32_t)(until - now) <= 0) {
      uint32_t end = until;
      next();
      until = end + length(on);
    }
  }
  // New medals (bits) and a streak milestone: on screen at once if nothing shows, else waiting
  void add(uint16_t medalBits, bool milestone, uint32_t now) {
    update(now);
    medals |= medalBits & MED_ALL;
    streak = streak || milestone;
    if (on == BANNER_NONE) {
      next();
      until = now + length(on);
    }
  }
  // Under a minigame's or an exercise's screen (Pet::holdBanners): the banner on screen keeps
  // its whole time, and the ones behind it wait
  void hold(uint32_t now) {
    update(now);
    if (on != BANNER_NONE) until = now + length(on);
  }
  void clear() { *this = BannerQueue(); }

private:
  void next() {  // the next one waiting to the screen; BANNER_NONE when none waits
    if (streak) {
      streak = false;
      on = BANNER_STREAK;
      return;
    }
    on = BANNER_NONE;
    for (int8_t i = 0; i < MED_COUNT; i++)
      if (medals & (1u << i)) {
        medals &= (uint16_t)~(1u << i);
        on = i;
        return;
      }
  }
};

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
  uint8_t nature = NAT_EASYGOING;  // rolled at hatch (#25, nature.h); NVS "nature"
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
  uint16_t lastMilestone = 0;  // hito de racha ya celebrado
  uint16_t gameHi = 0;     // record de keepy-uppy, el minijuego original (del jugador)
  uint16_t catchHi = 0, peekHi = 0, songHi = 0;  // records de Berry Catch, Peekaboo y Berry Song
  // Most punches landed in one rhythm-punch session (#78), 0 to EX_COUNT: NVS `phi`, the
  // player's like the minigames' records. The old tap-as-fast-as-you-can bag's record
  // (`shi`) is no longer read, written or shown: a rhythm session lands far fewer punches,
  // so it would be out of reach. Its key stays in NVS as it was, so an older firmware
  // flashed back still finds it.
  uint8_t punchHi = 0;

  void begin();                 // carga estado de NVS (o crea el primer huevo)
  void update(uint32_t nowMs);  // llamar en cada loop()

  // Acciones (botones tactiles)
  void feed();              // baya roja (compatibilidad)
  void feedBerry(uint8_t color);  // 0 roja, 1 azul, 2 verde
  void feedCandy();
  // The plush toy (#13, TOY_* above): the pet plays with it for TOY_ANIM_MS (playingToy()).
  // Returns the joy it gave: 0 once this hour's plays have given theirs, or with joy already
  // full, and then no heart and no play of the hour spent either (0 = nothing given, nothing
  // spent). As an egg, asleep or in a ceremony nothing happens at all (no play either). Put
  // to bed mid-play, it stops.
  uint8_t playToy();
  bool playingToy() const { return !sleeping && timeLeft(toyUntil) > 0; }
  uint32_t toyElapsed() const {  // ms into the play (0 when none), for its animation
    uint32_t left = timeLeft(toyUntil);
    return left ? TOY_ANIM_MS - left : 0;
  }
  // Warm milk (#13, MILK_* above): it drinks (eating() and drinkingMilk(), EAT_ANIM_MS), and
  // its next sleep, until it wakes, restores more energy (milkNight()). The sketch puts it to
  // bed once the drink is over. False, and nothing given, as an egg, asleep or in a ceremony.
  bool giveMilk();
  bool drinkingMilk() const { return !sleeping && timeLeft(milkUntil) > 0; }  // put to bed mid-drink, its mug goes
  bool milkNight() const { return milked; }
  bool lovesBerry(uint8_t color) const {
    return !isEgg() && (speciesId % 3) == color;  // gusto oculto por especie
  }
  // fin de un minijuego de Play (gamecore.h): guarda el record de ese juego con la
  // puntuacion tal cual y paga la recompensa de la puntuacion normalizada (entrena VEL).
  // Returns the speed it really gained (0 at 100, or for a game that pays nothing), for the
  // result screen (#76).
  uint8_t playGame(uint8_t game, uint16_t raw);
  uint16_t gameBest(uint8_t game) const;  // record de un minijuego (0 si no existe)
  // The calm exercises (exercise.h, #78): a rhythm-punch session with `punches` landed
  // trains strength, a stamina-hold session with `reps` done trains stamina, 1.5 points
  // each up to EX_GAIN_CAP. Both cost and reward like any exercise (exerciseDone); a
  // session with nothing done does neither. They return the points that went in.
  uint8_t trainStrength(uint8_t punches);
  uint8_t trainStamina(uint8_t reps);

  // stats de combate: base real de gen 1 x genes + nivel + entrenamiento
  uint16_t atkStat() const;
  uint16_t defStat() const;
  uint16_t speStat() const;
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
  // Sickness (#15, SICK_* above). The setting is saved at once in its own key, like the pace;
  // switching it off cures the pet and clears the counts, and nothing counts while it is off.
  uint8_t sickMode = SICK_OFF;  // SICK_OFF or SICK_ON
  void setSickMode(uint8_t v);
  bool isSick() const { return sick; }
  // counted minutes sick so far (0 when well), up to SICK_RUNAWAY_MIN
  uint16_t sickMinutes() const { return sick ? sickMin : 0; }
  // The medicine: cures it at once, +SICK_MEDICINE_JOY, a heart, the day's care and +1 bond;
  // it takes the pill for EAT_ANIM_MS (eating() and takingMedicine()). False, and nothing
  // given, unless it is sick, awake, hatched and not in a ceremony.
  bool giveMedicine();
  bool takingMedicine() const { return !sleeping && timeLeft(medUntil) > 0; }
  // left sick SICK_RUNAWAY_MIN counted minutes: the sketch starts the run-away. Awake only,
  // as canRunawayNow(): a pet put to bed (or loaded asleep) waits until it wakes.
  bool sickRunawayDue() const { return sick && sickMin >= SICK_RUNAWAY_MIN && ceremony == CER_NONE && !isEgg() && !sleeping; }
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
  // a sick pet (#15) says goodbye once it is better: the button waits
  bool wantFarewellButton() const { return canFarewellNow() && !sick && ageMinutes >= farDeclinedAge; }
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
  // the celebration banner on screen now (BannerQueue): a medal's bit, BANNER_STREAK or
  // BANNER_NONE (drawCelebration names it)
  int8_t bannerShown() const { return banners.shown(millis()); }
  bool showMedal() const {
    int8_t b = bannerShown();
    return b >= 0 && b < MED_COUNT;
  }
  bool showMilestone() const { return bannerShown() == BANNER_STREAK; }
  // While a Play minigame or an exercise is on screen (the sketch, every loop pass,
  // #69): a medal or streak banner that came up under it (the exercise was the day's first
  // care) waits, and shows its whole time once the creature's screen is back, then the
  // ones queued behind it (#82, #83). Its sound played when it was gained, as before.
  void holdBanners();
  // A minigame or an exercise just closed (the sketch, once, as its screen goes; #76): a medal
  // its exercise completed (IN SHAPE, a training ribbon) is gained now, on the creature's
  // screen, as the player's (it sounds, quiet hours or not), instead of on the next tick.
  // Nothing new if the screen paid nothing (a game left by its exit hold, an exercise with
  // nothing done).
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
  uint32_t milkUntil = 0;   // drinking warm milk (#13): its mug shows until this millis()
  uint32_t toyUntil = 0;    // playing with the plush (#13) until this millis()
  uint8_t toyJoys = 0;      // plays that gave joy in this hour (TOY_PER_HOUR; RAM only)
  uint32_t toyFrom = 0;     // when this hour started (the first play that gave joy)
  bool milked = false;      // had warm milk: its sleep restores more until it wakes (#13; RAM only)
  uint32_t medUntil = 0;    // taking the medicine (#15): the pill shows until this millis()
  // Sickness (#15), saved (NVS "sick", "sickg", "sickh", "sickt"); all four are false/0 while
  // sickMode is off and for an egg.
  bool sick = false;
  uint16_t germMin = 0;  // well: counted minutes in the red in a row, under SICK_AFTER_MIN
  uint16_t healMin = 0;  // sick: minutes out of the red in all, under SICK_HEAL_MIN
  uint16_t sickMin = 0;  // sick: counted minutes sick, up to SICK_RUNAWAY_MIN
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
  BannerQueue banners;         // the medal and streak banners (#82, #83); RAM only
  bool selfCareOn = false;     // setCharger(): on USB and untouched SELFCARE_IDLE_MS

  uint32_t today() const { return lastSeenEpoch ? lastSeenEpoch / 86400 : 0; }
  void registerCare();   // primer cuidado del dia: racha + vinculo
  uint8_t playReward(uint8_t n);  // recompensa de un minijuego, n = 0..GAME_SCORE_CAP; the speed gained
  void exerciseDone(uint8_t work);  // an exercise session's cost and reward (#78)
  void addBond(uint8_t amt);
  void checkMedals(bool byTick);  // byTick: from the minute tick, no exercise since the last one
  bool exercised = false;  // a minigame or an exercise since the last tick (#44): RAM only
  bool allNeedsEmpty() const { return fullness == 0 && joy == 0 && energy == 0 && hygiene == 0; }
  void tick();
  void drainBody();  // parte del goteo de necesidades (comida, energia, caca, higiene)
  void drainJoy();   // ...y la felicidad (depende de comida e higiene ya bajadas)
  // this drain carries the nature's extra point for `nat` (#25): 1 or 0
  uint8_t natureDrain(uint8_t nat) const {
    return nature == nat && natureDrainMinute(ageMinutes, pace == PACE_RELAXED) ? 1 : 0;
  }
  uint8_t playJoyExtra() const { return nature == NAT_PLAYFUL ? NAT_PLAY_JOY_EXTRA : 0; }
  // per minute asleep: the nature's rate, and more after warm milk (#13)
  uint8_t sleepEnergy() const { return (nature == NAT_LAZY ? NAT_SLEEP_ENERGY : 6) + (milked ? MILK_SLEEP_ENERGY : 0); }
  void selfCare();   // one need back up to half while charging (#24), from tick()
  void sickTick(bool awake);  // sickness's minute (#15): falls sick, slips up, gets better
  void clearSickness();       // well, every count at 0
  void addSlipUp();           // one more slip-up (careMistakes, capped at 255) and -1 bond
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

// The Play minigames' and the exercises' screens (the sketch, every loop pass, fed
// gameOpen || trainOpen; #69, #76, #78). While one is up, a medal or streak banner that came up
// under it waits (Pet::holdBanners); on the pass it has closed, the medals its exercise
// completed are gained (Pet::exerciseOver), so a training ribbon's banner shows as the
// creature's screen comes back.
// `dark`: the screen is off or locked. A minigame or an exercise that the screen going dark
// ended (#88, #94) closes on a dark screen: its medals are gained at once (their sound comes
// with the player's press, as any medal the player's action earns), and the banners wait
// until the screen is back on, so the creature's screen still shows them.
struct ExerciseScreen {
  bool wasOpen = false;
  bool closedDark = false;  // closed while dark, and the screen hasn't been back on since
  void update(bool open, Pet &p, bool dark = false) {
    if (open) p.holdBanners();
    else if (wasOpen) {
      p.exerciseOver();
      closedDark = dark;
    }
    if (!dark) closedDark = false;
    if (closedDark) p.holdBanners();
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
