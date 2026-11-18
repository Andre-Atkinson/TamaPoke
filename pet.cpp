#include "pet.h"
#include "dex.h"
#include "exercise.h"
#include "audio.h"
#include "bedtime.h"
#include "attention.h"  // ATTN_LOW: sickness's "dirty" is the hygiene bar's red (#15)

static_assert(SICK_DIRTY == ATTN_LOW, "sickness counts the minutes the hygiene bar is red and calling");
static_assert(SICK_RUNAWAY_MIN % SICK_SLIP_MIN == 0, "the run-away comes on an hour's boundary");

void Pet::begin() {
  prefs.begin("tamapoke", false);
  if (!prefs.getBool("init", false)) {
    prefs.putBool("init", true);
    newEgg();
  } else {
    load();
  }
  lastTick = millis();
}

void Pet::newEgg() {
  ceremony = CER_NONE;
  neglectTicks = 0;
  weight = 0;
  speciesId = -1;
  prevSpeciesId = -1;
  eggTarget = pickEggSpecies();  // especie oculta segun rareza y pokedex
  starterPick = (registeredCount() == 0);  // primera partida: el jugador elige inicial
  // sorteo shiny: 1/48 base, mejor con despedida y con racha/vinculo altos
  int shinyBase = (lastEnd == CER_FAREWELL ? 24 : 48) - careBonus();
  if (shinyBase < 8) shinyBase = 8;
  eggShiny = (random(shinyBase) == 0);
  // eleccion de huevo: la tirada de arriba (rareza, bendicion, maldicion, bonus
  // de cuidado) es uno de los tres, asi que las probabilidades no cambian; los
  // otros dos son comunes. Va despues del shiny para que, con el ajuste
  // apagado, la secuencia de tiradas sea la de siempre. El shiny sorteado
  // arriba va con el huevo que elija, sea cual sea.
  eggPick = !starterPick && eggPickMode == EGGPICK_ON;
  if (eggPick) rollEggChoices(eggTarget, eggCand);
  eggTaps = 0;
  fullness = 80;
  joy = 80;
  energy = 80;
  hygiene = 100;
  poops = 0;
  ageMinutes = 0;
  careMistakes = 0;
  mistakeCooldown = 0;
  sleeping = false;
  milked = false;  // a new pet has had no milk (#13)
  clearSickness();  // a new pet is well (#15)
  medUntil = 0;
  save();
}

// progresion offline: el tiempo paso aunque estuviera apagado, pero con
// piedad — las barras bajan con suelo en 15 (vuelve hambriento, no muerto),
// sin descuidos ni escapadas en ausencia
static uint8_t dropTo(uint8_t v, uint8_t d, uint8_t fl) {
  if (v <= fl) return v;
  return (v - fl > d) ? v - d : fl;
}

void Pet::setClock(uint32_t nowEpoch) {
  lastSeenEpoch = nowEpoch;
  garden.at = 0;  // the garden starts afresh from the new time: a clock set never grows or dries it (#115)
  if (nowEpoch) save();  // persiste ya: un corte de luz no pierde la referencia
}

void Pet::syncClock(uint32_t nowEpoch) {
  uint32_t seen = prefs.getUInt("seen", 0);
  // With the clock on a real time, a saved time outside the clock's range is corrupt (the
  // old TIME bug, #45): no offline time from it, as on a first boot.
  if (clockEpochValid(nowEpoch) && !clockEpochValid(seen)) seen = 0;
  lastSeenEpoch = nowEpoch;
  if (nowEpoch == 0) return;
  uint32_t mins = (seen && nowEpoch > seen) ? (nowEpoch - seen) / 60 : 0;
  if (mins < 2 || ceremony != CER_NONE || starterPick || eggPick) {
    save();  // primera vez, sin tiempo que aplicar o aun eligiendo inicial/huevo
    return;
  }
  if (mins > 14UL * 24 * 60) mins = 14UL * 24 * 60;  // tope: 2 semanas

  for (uint32_t i = 0; i < mins; i++) {
    ageMinutes++;
    if (isEgg()) {
      if (ageMinutes >= 3) hatch();  // eclosiona en tu ausencia
      continue;
    }
    if (sleeping) {  // descanso: baja lento y con suelo, igual que en vivo
      energy = clamp100(energy + sleepEnergy());
      if (ageMinutes % 2 == 0) {
        fullness = dropTo(fullness, 1, 30);
        joy = dropTo(joy, 1, 35);
      }
      if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
      continue;
    }
    // the nature's extra point (#25) every NATURE_DRAIN_EVERY minutes, as awake at the
    // normal pace (this catch-up ignores the pace), down to the same floor of 15
    uint8_t ex = natureDrainMinute(ageMinutes, false) ? 1 : 0;
    fullness = dropTo(fullness, 2 + (nature == NAT_GREEDY ? ex : 0), 15);
    energy = dropTo(energy, 1 + (nature == NAT_LAZY ? ex : 0), 15);
    hygiene = dropTo(hygiene, 1 + (nature == NAT_TIDY ? ex : 0), 15);
    joy = dropTo(joy, 1 + (nature == NAT_PLAYFUL ? ex : 0), 15);
  }
  if (!isEgg()) {
    if (!sleeping) {  // durmiendo no ensucia
      uint8_t p = poops + mins / 240;
      poops = p > 3 ? 3 : p;
    }
    // la evolucion NO se aplica offline: queda lista y la dispara el usuario
    // tocando al bicho cuando vuelve (para que vea la transformacion)
  }
  Serial.printf("offline: %u min aplicados (nv.%u)\n", mins, level());
  save();
}

// A deadline that has passed reads 0 from then on (timeLeft(0) is 0)
static void endIfPast(uint32_t &deadline) {
  if (deadline && !timeLeft(deadline)) deadline = 0;
}

void Pet::update(uint32_t nowMs) {
  // What has ended is cleared on the next pass: a deadline left behind would read as ahead
  // again once millis() is 2^31 ms (~24.8 days of uptime) past it, and an evolution, a plush
  // play, warm milk or a meal from weeks before would then seem to run for weeks more, holding
  // back bedtime and the attention calls (careBusy() in the sketch); a medal's or a streak's
  // banner would come back (BannerQueue::update takes the ended ones off its queue).
  banners.update(millis());
  endIfPast(evolveUntil);
  endIfPast(toyUntil);
  endIfPast(milkUntil);
  endIfPast(medUntil);
  endIfPast(eatUntil);
  endIfPast(heartUntil);
  // fin de ceremonia: la criatura se va y queda un huevo nuevo
  if (ceremony != CER_NONE && !timeLeft(ceremonyUntil)) {
    newEgg();
    return;
  }
  while (nowMs - lastTick >= PET_TICK_MS) {
    lastTick += PET_TICK_MS;
    tick();
  }
}

void Pet::tick() {
  if (ceremony != CER_NONE) return;  // el tiempo se detiene en la despedida
  if (starterPick || eggPick) return;  // la partida no empieza hasta elegir
                            // inicial o huevo: si el tiempo corriera aqui, el
                            // huevo eclosionaria solo a los 3 min con la especie
                            // sorteada y se perderia la eleccion del jugador
  bool afterExercise = exercised;  // a minigame or an exercise since the last tick (#44)
  exercised = false;
  ageMinutes++;

  if (isEgg()) {
    if (ageMinutes >= 3) hatch();  // si no lo tocas, eclosiona solo a los 3 min
    return;
  }

  // el sueño es descanso: la energia se recupera y las necesidades bajan MUCHO
  // mas lento que despierto y con suelo (amanece pidiendo algo de mimo, no a
  // cero, sin descuidos ni escapadas). despierto: comida -2/min, hig/joy -1/min.
  // El peso aun se quema; la racha de buen cuidado (goodTicks) queda en pausa.
  if (sleeping) {
    energy = clamp100(energy + sleepEnergy());  // 6; lazy (#25) 8; after warm milk (#13) 2 more
    if (weight > 0 && ageMinutes % 3 == 0) weight--;
    if (ageMinutes % 2 == 0) {                 // ~4x mas lento que despierto
      fullness = dropTo(fullness, 1, 30);
      joy = dropTo(joy, 1, 35);
    }
    if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
    sickTick(false);    // asleep it can only get better (#15)
    checkMedals(true);  // aun puede cruzar un nivel por edad mientras duerme
    if (++ticksSinceSave >= 5) pendingSave = true;
    return;
  }

  // subio de nivel (despierto); in quiet hours silently (#44): nobody asked for it
  if (ageMinutes % MINUTES_PER_LEVEL == 0) {
    if (!quietHours) sfxPlay(SFX_LEVEL);
    else Serial.printf("alert level %u: silent (quiet)\n", level());
  }

  // Goteo de necesidades. Ritmo normal: cada minuto. Ritmo relajado: solo los
  // minutos pares (la mitad de rapido). Todo lo demas de este tick (edad, peso,
  // racha de buen cuidado, descuidos, abandono, medallas, guardado) sigue siendo
  // por minuto en ambos ritmos, y la progresion offline (syncClock) no cambia.
  bool drain = pace != PACE_RELAXED || ageMinutes % 2 == 0;
  if (drain) drainBody();
  if (weight > 0 && ageMinutes % 3 == 0) weight--;

  // Steady care builds stamina (#76, STAMINA_CARE_MIN in pet.h): a minute with every need
  // at 40 or more counts, in total; a minute under 40 only doesn't count. Self-care's minutes
  // on the charger are not the player's care. (Between the food/hygiene drain and the joy
  // drain, as always: lowestStat() includes joy, and moving it would change the rhythm.)
  if (lowestStat() >= 40 && !selfCareOn) {
    goodTicks += natureTrainGain(nature, TRAIN_STAMINA, GOOD_STEP);  // tidy (#25): 5
    if (goodTicks >= STAMINA_CARE_MIN * GOOD_STEP) {
      goodTicks = 0;
      if (trDef < 100) trDef++;
    }
  }

  if (drain) drainJoy();

  // Self-care while charging (#24). After this minute's drain, so a top-up shows as
  // 50, and before the slip-up and run-away checks, so a need it lifts this minute is
  // not a slip-up. Its position is the guard: the returns above keep it off during a
  // ceremony, the starter or egg choice, as an egg and asleep. syncClock() (the
  // powered-off catch-up) never runs tick(), so it never self-cares. Every minute in
  // both paces: relaxed only halves the drain.
  if (selfCareOn) selfCare();

  // Sickness (#15): after self-care, so a bath it gives this minute counts as clean, and before
  // the needs' slip-up below, which it neither waits for nor shares a cooldown with.
  sickTick(true);

  // Descuido: dejar una estadistica por los suelos cuenta como error de
  // cuidado (con enfriamiento para no contar el mismo descuido cada minuto)
  if (mistakeCooldown > 0) mistakeCooldown--;
  if (lowestStat() <= 10 && mistakeCooldown == 0) {
    addSlipUp();  // el descuido enfria el vinculo, pero sin arrasarlo (-1)
    mistakeCooldown = 60;
  }

  // la evolucion la dispara el usuario (canEvolveNow + tap), no el tick.
  // Exercise burns weight too: right after a minigame or an exercise this tick's medals count as
  // the player's (#44), so an IN SHAPE that the exercise completed sounds in quiet hours. It
  // is gained here, a minute at most after the game, as before #44: gained inside the game,
  // its banner would run out under the result screen.
  checkMedals(!afterExercise);

  // abandono total: con TODO a cero durante una hora queda lista para escaparse;
  // NO se va sola, la dispara el usuario con el boton (final triste, lo presencia)
  if (allNeedsEmpty()) {
    if (neglectTicks < RUNAWAY_TICKS) neglectTicks++;
  } else {
    neglectTicks = 0;  // un solo cuidado la salva
  }

  // ciclo completo (forma final + 7 dias): la despedida NO salta sola; queda
  // lista (canFarewellNow) y la dispara el usuario con el boton, para que la vea

  // autoguardado periodico: NO escribir a flash aqui (corre dentro del loop,
  // mientras se anima); solo marcar y dejar que el loop lo vuelque al atenuar
  if (++ticksSinceSave >= 5) pendingSave = true;
}

// The nature's extra point (#25, natureDrain) comes after each need's own drain, so the
// poop roll sees what it always saw. drainJoy() runs after this, so it sees the extra point:
// a greedy pet's food penalty (fullness < 30) and a tidy pet's hygiene penalty (hygiene < 30)
// start a few minutes sooner. The overweight drain reads only weight, so order means nothing to it.
void Pet::drainBody() {
  fullness = clamp100(fullness - 2);
  energy = clamp100(energy - 1);
  if (fullness > 40 && poops < 3 && random(100) < 15) poops++;

  hygiene = clamp100(hygiene - 1 - 4 * poops);
  fullness = clamp100(fullness - natureDrain(NAT_GREEDY));
  energy = clamp100(energy - natureDrain(NAT_LAZY));
  hygiene = clamp100(hygiene - natureDrain(NAT_TIDY));
  // el sobrepeso da pereza: la energia cae el doble
  if (weight > 50) energy = clamp100(energy - 1);
}

void Pet::drainJoy() {
  int dJoy = -1 - natureDrain(NAT_PLAYFUL);
  if (fullness < 30) dJoy -= 2;
  if (hygiene < 30) dJoy -= 2;
  joy = clamp100(joy + dJoy);
  if (sick) joy = dropTo(joy, 1, SICK_JOY_FLOOR);  // #15: one more, never under the floor
}

// A slip-up: a need left at 10 or under (tick) or sickness (#15). Each one puts the evolution
// a level later (canEvolveNow) and cools the bond by 1, never under 1: at -3 every 30 min far
// more was lost than a whole day's care could win back, and the bond got stuck. careMistakes
// is a byte, so it stops at 255 rather than wrap back to a spotless 0.
void Pet::addSlipUp() {
  if (careMistakes < 255) careMistakes++;
  if (bond > 1) bond--;
}

// Sickness's minute (#15, SICK_* in pet.h). `awake`: from tick()'s awake minute; asleep only
// the cure counts. A counted minute is awake and outside quiet hours (the sketch sets
// quietHours, which holds for a stale or unknown clock too). The 1.75C's pocket lock, which
// silences the calls too, still counts (pet.h, SICK_*).
void Pet::sickTick(bool awake) {
  if (sickMode != SICK_ON) return;
  bool dirty = hygiene < SICK_DIRTY;
  bool counted = awake && !quietHours;
  if (!sick) {
    if (!dirty) germMin = 0;  // any clean starts the count again
    else if (counted && ++germMin >= SICK_AFTER_MIN) {
      sick = true;
      germMin = healMin = sickMin = 0;
      addSlipUp();  // falling sick is a slip-up
      Serial.println("sick: fell ill");
      pendingSave = true;
    }
    return;
  }
  if (!dirty && ++healMin >= SICK_HEAL_MIN) {  // a red minute only pauses it
    clearSickness();
    Serial.println("sick: better by itself");
    pendingSave = true;
    return;
  }
  if (counted && sickMin < SICK_RUNAWAY_MIN) {
    sickMin++;
    // one more slip-up each hour sick; the hour it runs away has none
    if (sickMin % SICK_SLIP_MIN == 0 && sickMin < SICK_RUNAWAY_MIN) addSlipUp();
    if (sickMin == SICK_RUNAWAY_MIN) Serial.println("sick: runs away");
  }
}

bool Pet::dbgSick(bool on, uint16_t minutes) {
  if (sickMode != SICK_ON || isEgg() || ceremony != CER_NONE) return false;
  clearSickness();
  if (on) {
    sick = true;
    sickMin = minutes > SICK_RUNAWAY_MIN ? (uint16_t)SICK_RUNAWAY_MIN : minutes;
  }
  save();
  return true;
}

void Pet::clearSickness() {
  sick = false;
  germMin = healMin = sickMin = 0;
}

void Pet::setCharger(bool pmuOk, bool usb, uint32_t idleMs) {
  selfCareOn = pmuOk && usb && idleMs >= SELFCARE_IDLE_MS;
}

// One need per minute, in a fixed order: food and hygiene first, because either one
// under 30 makes joy fall three times as fast. Hygiene also clears the poops: with
// three on screen it drops 13 a minute and would be back under 40 the next minute.
// This is not the player's care: no registerCare() (streak), no addBond(), no medals,
// and no heart or eating state either, because a visible event wakes the screen and
// restarts the idle time that keeps self-care on. No save() of its own: tick() marks
// the periodic one.
void Pet::selfCare() {
  const char *what;
  uint8_t before;
  if (fullness < SELFCARE_BELOW) {
    what = "food";
    before = fullness;
    fullness = SELFCARE_LEVEL;
  } else if (hygiene < SELFCARE_BELOW) {
    what = "hygiene";
    before = hygiene;
    hygiene = SELFCARE_LEVEL;
    poops = 0;
  } else if (energy < SELFCARE_BELOW) {
    what = "energy";
    before = energy;
    energy = SELFCARE_LEVEL;
  } else if (joy < SELFCARE_BELOW) {
    what = "joy";
    before = joy;
    joy = SELFCARE_LEVEL;
  } else {
    return;
  }
  Serial.printf("self-care: %s %u->%u\n", what, (unsigned)before, (unsigned)SELFCARE_LEVEL);
}

void Pet::idlePerk(uint8_t energyUp, uint8_t joyUp) {
  if (ceremony != CER_NONE || isEgg() || sleeping) return;  // a starter or egg choice is an egg too
  energy = clamp100(energy + energyUp);
  joy = clamp100(joy + joyUp);
}

// vuelca el guardado periodico pendiente (lo llama el loop en un momento sin
// animacion para que el paron de la escritura a flash no se vea)
void Pet::flushSave() {
  if (pendingSave) save();
}

// quedan miembros sin registrar en la linea evolutiva de esta base?
bool Pet::lineHasUnregistered(int16_t base) const {
  int16_t cur = base;
  for (int guard = 0; cur >= 1 && cur <= 151 && guard < 6; guard++) {
    if (!isRegistered(cur)) return true;
    if (cur == DEX_EEVEE) {
      for (int16_t b = 134; b <= 136; b++)
        if (!isRegistered(b)) return true;
      return false;
    }
    cur = DEX_TBL[cur].evolvesTo;
  }
  return false;
}

uint8_t Pet::eggRarity() const {
  return (eggTarget >= 1 && eggTarget <= 151) ? DEX_TBL[eggTarget].rarity : (uint8_t)R_COMUN;
}

uint16_t Pet::eggAccent() const {
  return (eggTarget >= 1 && eggTarget <= DEX_COUNT) ? DEX_TBL[eggTarget].accent : (uint16_t)0x5DCD;  // 'g'
}

// elige la especie del huevo: tirada de rareza (mejorada por una despedida
// completa, castigada por una escapada) y sesgo hacia lineas incompletas
int16_t Pet::pickEggSpecies() {
  // primera partida: inicial clasico
  if (registeredCount() == 0) {
    return CLASSIC_DEX[random(NUM_CLASSIC_DEX)];
  }

  uint8_t tier = R_COMUN;
  if (lastEnd != CER_RUNAWAY) {
    bool blessed = (lastEnd == CER_FAREWELL);
    int rare = (blessed ? 45 : 27) + careBonus();
    int leg = (registeredCount() >= 25) ? (blessed ? 10 : 3) + careBonus() / 3 : 0;
    int r = random(100);
    if (r < leg) tier = R_LEGENDARIO;
    else if (r < leg + rare) tier = R_RARO;
  }

  // candidatos del tier con linea incompleta; si no hay, baja de tier;
  // si la pokedex del tier esta completa, vale cualquiera del tier
  for (int pass = 0; pass < 2; pass++) {
    for (int t = tier; t >= R_COMUN; t--) {
      int16_t cand[80];
      int n = 0;
      for (int16_t d = 1; d <= 151 && n < 80; d++) {
        if (DEX_TBL[d].rarity != t) continue;
        if (pass == 0 && !lineHasUnregistered(d)) continue;
        cand[n++] = d;
      }
      if (n > 0) return cand[random(n)];
    }
  }
  return CLASSIC_DEX[random(NUM_CLASSIC_DEX)];  // inalcanzable, por si acaso
}

// The three eggs to choose from: `special` is the usual roll (pickEggSpecies) and
// the other two are Common eggs of other species. An egg's spots show its type
// colour, the hint the shell gives, so neither extra may have spots that look like
// those of an egg already chosen, the special one included (`lookAlike`, by default
// eggSpotsLookAlike() in dex.h, #34). Like pickEggSpecies they prefer incomplete
// evolution lines; when both can't be had, the new line gives way before the
// colour. With today's colours the colour rule always leaves a candidate (a host
// test checks every case); passes 2 and 3 drop it anyway, so a roll always fills
// all three slots.
void Pet::rollEggChoices(int16_t special, int16_t out[EGG_CHOICES]) {
  rollEggChoices(special, out, eggSpotsLookAlike);
}

void Pet::rollEggChoices(int16_t special, int16_t out[EGG_CHOICES],
                         bool (*lookAlike)(uint16_t, uint16_t)) {
  // defensivo: las llamadas de newEgg() pasan una especie de huevo; load() puede
  // pasar un eggTarget danado que no lo sea (entonces se sortea uno)
  if (special < 1 || special > DEX_COUNT || DEX_TBL[special].rarity == R_EVO)
    special = pickEggSpecies();
  out[0] = special;
  for (int k = 1; k < EGG_CHOICES; k++) {
    out[k] = -1;
    for (int pass = 0; pass < 4 && out[k] < 0; pass++) {
      bool farColour = pass < 2;               // passes 0 and 1
      bool newLine = pass == 0 || pass == 2;   // passes 0 and 2
      int16_t cand[DEX_COUNT];
      int n = 0;
      for (int16_t d = 1; d <= DEX_COUNT; d++) {
        if (DEX_TBL[d].rarity != R_COMUN) continue;
        bool taken = false, alike = false;
        for (int j = 0; j < k; j++) {
          if (out[j] == d) taken = true;
          if (lookAlike(DEX_TBL[out[j]].accent, DEX_TBL[d].accent)) alike = true;
        }
        // `taken` keeps the three species apart in every pass, whatever `lookAlike`
        // says; in passes 0 and 1 eggSpotsLookAlike's `alike` covers it anyway (an
        // accent looks like itself)
        if (taken || (farColour && alike)) continue;
        if (newLine && !lineHasUnregistered(d)) continue;
        cand[n++] = d;
      }
      if (n > 0) out[k] = cand[random(n)];
    }
  }
  // barajar: el huevo de la tirada normal no siempre en el mismo sitio
  for (int i = EGG_CHOICES - 1; i > 0; i--) {
    int j = random(i + 1);
    int16_t t = out[i];
    out[i] = out[j];
    out[j] = t;
  }
}

// tres especies de huevo validas y distintas (lo que se lee de NVS se comprueba)
bool Pet::validEggChoices(const int16_t c[EGG_CHOICES]) {
  for (int i = 0; i < EGG_CHOICES; i++) {
    if (c[i] < 1 || c[i] > DEX_COUNT || DEX_TBL[c[i]].rarity == R_EVO) return false;
    for (int j = 0; j < i; j++)
      if (c[j] == c[i]) return false;
  }
  return true;
}

void Pet::chooseEgg(uint8_t i) {
  if (!eggPick || i >= EGG_CHOICES) return;
  eggTarget = eggCand[i];
  eggPick = false;
  save();
}

void Pet::cancelEggChoice() {
  if (!eggPick) return;
  eggPick = false;
  save();
}

void Pet::registerSpecies(int16_t dex) {
  if (dex < 1 || dex > 151) return;
  dexReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
  if (shiny) dexShinyReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
}

// la racha y el vinculo mejoran el sorteo del huevo (0..~14)
int Pet::careBonus() const {
  int s = streak > 30 ? 30 : streak;
  return s / 3 + bond / 25;
}

// primer cuidado del dia: avanza la racha y afianza el vinculo
void Pet::registerCare() {
  if (isEgg() || ceremony != CER_NONE) return;
  uint32_t d = today();
  if (d == 0 || d == lastCareDay) return;  // sin reloj, o ya conto hoy
  // Clock a little behind (e.g. tools/board.py clock after drift): keep the
  // streak. A bigger step back means lastCareDay itself is wrong (a care
  // registered with a far-future clock), so fall through and start again.
  if (d < lastCareDay && lastCareDay - d <= 2) return;
  if (lastCareDay == 0 || d == lastCareDay + 1) {
    streak++;
  } else {
    streak = 1;        // hubo un hueco de dias
    lastMilestone = 0;
  }
  lastCareDay = d;
  bondToday = 0;
  if (streak > bestStreak) bestStreak = streak;
  bond = clamp100(bond + 4);
  uint16_t ms = (streak >= 100) ? 100 : (streak >= 30) ? 30
              : (streak >= 7)   ? 7   : (streak >= 3)  ? 3 : 0;
  if (ms > lastMilestone) {
    lastMilestone = ms;
    banners.add(0, true, millis());  // first: the medals below wait behind it (#83)
  }
  checkMedals(false);  // de una accion del jugador, no del tick
  save();
}

void Pet::addBond(uint8_t amt) {
  if (bondToday >= 20) return;  // tope diario: el vinculo no se farmea
  bond = clamp100(bond + amt);
  bondToday += amt;
}

// byTick: the minute tick is the caller and no exercise came before it (see MED_BY_TIME).
void Pet::checkMedals(bool byTick) {
  if (isEgg()) return;
  uint16_t before = medals;
  if (level() >= 10) medals |= MED_LV10;
  if (level() >= 25) medals |= MED_LV25;
  if (level() >= 50) medals |= MED_LV50;
  if (berryKnown) medals |= MED_BERRY;
  if (streak >= 7) medals |= MED_STREAK7;
  if (bond >= 100) medals |= MED_BOND;
  if (DEX_TBL[speciesId].evolvesTo == 0) medals |= MED_FINAL;
  if (weight == 0 && level() >= 5 && careMistakes == 0) medals |= MED_FIT;
  if (trAtk >= RIBBON_STR) medals |= MED_STR;  // the training ribbons (#76)
  if (trDef >= RIBBON_STA) medals |= MED_STA;
  if (trSpe >= RIBBON_SPD) medals |= MED_SPD;
  uint16_t gained = medals & ~before;
  if (gained) {
    for (uint16_t m = gained; m; m &= (m - 1)) totalMedals++;
    banners.add(gained, false, millis());  // a banner each, one after another (#82)
    // a medal that only time earned, noticed by the tick, is silent in quiet hours (#44);
    // the banner shows anyway. One the player's action gains (exercise burns weight too) sounds.
    bool byTimeOnly = !(gained & ~MED_BY_TIME);
    if (!sleeping && byTick && quietHours && byTimeOnly) Serial.println("alert medal: silent (quiet)");
    else if (!sleeping) sfxPlay(SFX_MEDAL);
    save();
  }
}

void Pet::holdBanners() { banners.hold(millis()); }

void Pet::exerciseOver() {
  if (exercised) checkMedals(false);  // the next tick still sees the exercise, as before
}

void Pet::rename(const char *name) {
  strncpy(nick, name, sizeof(nick) - 1);
  nick[sizeof(nick) - 1] = 0;
  save();
}

static uint16_t calcStat(uint8_t base, uint8_t gene, uint16_t lvl, uint8_t tr) {
  return (uint16_t)base * gene / 100 + lvl + tr;
}

uint16_t Pet::atkStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bAtk, geneAtk, level(), trAtk);
}
uint16_t Pet::defStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bDef, geneDef, level(), trDef);
}
uint16_t Pet::speStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bSpe, geneSpe, level(), trSpe);
}

uint16_t Pet::registeredCount() const {
  uint16_t n = 0;
  for (int i = 1; i <= 151; i++)
    if (isRegistered(i)) n++;
  return n;
}

// forma final que ya cumplio su ciclo (7 dias): lista para despedirse. La
// despedida la dispara el usuario con el boton (no salta sola, para que la vea)
bool Pet::canFarewellNow() const {
  return !isEgg() && !sleeping && ceremony == CER_NONE &&
         DEX_TBL[speciesId].evolvesTo == 0 && ageMinutes >= FAREWELL_AGE_MIN;
}

// abandono total durante 1h: lista para escaparse. La dispara el usuario con el
// boton (final triste); cuidarla un solo tick la salva (neglectTicks se resetea)
// The four needs must still be at 0 too: tick() resets neglectTicks only on its next
// minute, so a care, or a night's sleep (asleep, tick() stops before the count and the
// energy comes back), left the button and its warning for up to a minute after the pet
// was no longer neglected, and the button could still be pressed then.
bool Pet::canRunawayNow() const {
  return !isEgg() && !sleeping && ceremony == CER_NONE && neglectTicks >= RUNAWAY_TICKS && allNeedsEmpty();
}

void Pet::startFarewell() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_FAREWELL;
  ceremony = CER_FAREWELL;
  banners.clear();  // the ending replaces any banner, and none waits for the egg after it
  ceremonyUntil = millis() + CEREMONY_MS;
  heartUntil = ceremonyUntil;  // corazones durante toda la despedida
  sfxPlay(SFX_BYE);
  save();
}

void Pet::startRunaway() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_RUNAWAY;
  ceremony = CER_RUNAWAY;
  banners.clear();  // the ending replaces any banner, and none waits for the egg after it
  ceremonyUntil = millis() + CEREMONY_MS;
  sfxPlay(SFX_BYE);
  save();
}

void Pet::release() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_RELEASE;
  ceremony = CER_RELEASE;
  banners.clear();  // the ending replaces any banner, and none waits for the egg after it
  ceremonyUntil = millis() + CEREMONY_MS;
  heartUntil = ceremonyUntil;
  sfxPlay(SFX_BYE);
  save();
}

void Pet::hatch() {
  speciesId = eggTarget;
  shiny = eggShiny;
  // genes del individuo: 90-110% por stat (cada crianza es unica)
  geneAtk = 90 + random(21);
  geneDef = 90 + random(21);
  geneSpe = 90 + random(21);
  trAtk = trDef = trSpe = 0;
  nature = (uint8_t)random(NAT_COUNT);  // #25: one in five each
  berryKnown = false;
  bond = 0;          // vinculo, medallas y nombre son del individuo
  bondToday = 0;
  goodTicks = 0;     // stamina's steady-care minutes are this pet's too (#76)
  medals = 0;
  nick[0] = 0;
  registerSpecies(speciesId);  // criado = registrado en la pokedex
  checkMedals(false);  // por si nace ya en forma final (legendario)
  sfxPlay(SFX_HATCH);
  save();
}

// ¿se dan ya las condiciones para evolucionar? Cada descuido retrasa la
// evolucion 1 nivel, y ademas tiene que estar bien cuidado en ese momento
// (ninguna estadistica por debajo de 40). NO evoluciona sola: la dispara el
// usuario tocando al bicho (evolve()), para que vea la transformacion.
bool Pet::canEvolveNow() const {
  if (isEgg() || sleeping || ceremony != CER_NONE) return false;
  if (sick) return false;  // it evolves once it is better (#15)
  const DexEntry &d = DEX_TBL[speciesId];
  if (d.evolvesTo == 0) return false;
  return level() >= (uint16_t)d.evolveLevel + careMistakes && lowestStat() >= 40;
}

void Pet::evolve() {
  if (!canEvolveNow()) return;
  const DexEntry &d = DEX_TBL[speciesId];
  prevSpeciesId = speciesId;
  int16_t next = d.evolvesTo;
  if (speciesId == DEX_EEVEE) {
    // rama de Eevee: prefiere la evolucion que falte en la pokedex
    int16_t opts[3];
    int n = 0;
    for (int16_t b = 134; b <= 136; b++)
      if (!isRegistered(b)) opts[n++] = b;
    next = n > 0 ? opts[random(n)] : (int16_t)(134 + random(3));
  }
  speciesId = next;
  registerSpecies(speciesId);
  sfxPlay(SFX_EVOLVE);
  evolveUntil = millis() + EVOLVE_ANIM_MS;
  save();
}

void Pet::feed() {
  feedBerry(0);
}

void Pet::feedBerry(uint8_t color) {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  eatBerry(color);
  registerCare();
  save();
}

void Pet::eatBerry(uint8_t color) {
  uint8_t extra = nature == NAT_GREEDY ? NAT_BERRY_EXTRA : 0;  // #25
  if (lovesBerry(color)) {
    fullness = clamp100(fullness + 35 + extra);
    joy = clamp100(joy + 10);
    heartUntil = millis() + HEART_MS;  // "le encanta!"
    berryKnown = true;                 // descubierto: se muestra en la ficha
    addBond(2);
  } else {
    fullness = clamp100(fullness + 25 + extra);
  }
  eatUntil = millis() + EAT_ANIM_MS;
}

// The berry garden (#115, garden.h)
GardenTap Pet::tapGarden(uint32_t roll, uint8_t &picked) {
  GardenTap t = garden.tap(roll, canPickBerry(), picked);
  if (t == GTAP_PICKED) pickBerry(picked);
  else if (t != GTAP_WAIT) pendingSave = true;  // the next quiet moment writes it (flushSave)
  return t;
}

bool Pet::pickBerry(uint8_t color) {
  if (!canPickBerry()) return false;
  eatBerry(color);
  fullness = clamp100(fullness + GARDEN_PICK_FULL);
  joy = clamp100(joy + GARDEN_PICK_JOY);
  heartUntil = millis() + HEART_MS;
  addBond(GARDEN_PICK_BOND);
  registerCare();
  save();
  return true;
}

void Pet::feedCandy() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  fullness = clamp100(fullness + 10);
  joy = clamp100(joy + 12);
  weight = clamp100(weight + 12);  // las chuches pasan factura
  eatUntil = millis() + EAT_ANIM_MS;
  registerCare();
  save();
}

// The plush toy (#13, TOY_* in pet.h). No save of its own, like a pat: the periodic one keeps
// the joy (registerCare() saves the day's first care).
uint8_t Pet::playToy() {
  if (ceremony != CER_NONE || isEgg() || sleeping) return 0;
  uint32_t now = millis();
  toyUntil = now + TOY_ANIM_MS;
  if (toyJoys && now - toyFrom >= TOY_WINDOW_MS) toyJoys = 0;  // that hour is over
  uint8_t before = joy;
  if (joy < 100 && toyJoys < TOY_PER_HOUR) {  // joy already full gives nothing: no slot, no heart
    if (!toyJoys) toyFrom = now;  // the hour starts at its first play that gives joy
    toyJoys++;
    joy = clamp100(joy + TOY_JOY + playJoyExtra());
    heartUntil = now + HEART_MS;
  }
  addBond(1);
  registerCare();
  return joy - before;
}

// Warm milk (#13, MILK_* in pet.h): the drink shows as eating (the mug while drinkingMilk()),
// and the sketch puts the pet to bed once it is over. `milked` lasts until the pet wakes
// (toggleLight, autoWake) or a new egg comes.
bool Pet::giveMilk() {
  if (ceremony != CER_NONE || isEgg() || sleeping) return false;
  uint32_t now = millis();
  joy = clamp100(joy + MILK_JOY);
  weight = clamp100(weight + MILK_WEIGHT);  // a little, less than candy
  heartUntil = now + HEART_MS;
  eatUntil = milkUntil = now + EAT_ANIM_MS;
  milked = true;
  addBond(1);
  registerCare();
  save();
  return true;
}

// The medicine (#15): taken like the warm milk is drunk (eating() for EAT_ANIM_MS, the pill
// while takingMedicine()). It is the player's care, so it saves at once.
bool Pet::giveMedicine() {
  if (!sick || ceremony != CER_NONE || isEgg() || sleeping) return false;
  uint32_t now = millis();
  clearSickness();
  joy = clamp100(joy + SICK_MEDICINE_JOY);
  heartUntil = now + HEART_MS;
  eatUntil = medUntil = now + EAT_ANIM_MS;
  addBond(1);
  registerCare();
  save();
  return true;
}

// Fin de un minijuego de Play. Cada juego guarda su record con la puntuacion que
// ve el jugador; la recompensa sale de la normalizada (gamecore.h), asi que una
// buena partida paga lo mismo sea cual sea el juego que toco.
uint8_t Pet::playGame(uint8_t game, uint16_t raw) {
  if (ceremony != CER_NONE || isEgg() || game >= GAME_COUNT) return 0;
  if (raw > GAME_REC_MAX[game]) raw = GAME_REC_MAX[game];
  uint16_t *rec = game == GAME_CATCH ? &catchHi : game == GAME_PEEK ? &peekHi
                : game == GAME_SONG ? &songHi : &gameHi;
  if (raw > *rec) *rec = raw;  // nuevo record
  return playReward(gameNormScore(game, raw));
}

uint16_t Pet::gameBest(uint8_t game) const {
  switch (game) {
    case GAME_KEEPY: return gameHi;
    case GAME_CATCH: return catchHi;
    case GAME_PEEK: return peekHi;
    case GAME_SONG: return songHi;
    default: return 0;
  }
}

uint8_t Pet::playReward(uint8_t n) {
  uint8_t before = trSpe;
  // jugar entrena la velocidad (playful: +25%, #25); n <= GAME_SCORE_CAP and load() caps
  // trSpe at 100, so v <= 108; 16 bits so a value past that can't wrap anyway
  uint16_t v = (uint16_t)trSpe + natureTrainGain(nature, TRAIN_SPEED, n / 5);
  trSpe = v > 100 ? 100 : (uint8_t)v;
  joy = clamp100(joy + 5 + (n > 15 ? 30 : n * 2) + playJoyExtra());
  energy = dropTo(energy, 10 + n / 2, 5);
  fullness = dropTo(fullness, 5, 5);
  int burn = (int)weight - n * 2;  // el ejercicio quema peso
  weight = burn > 0 ? burn : 0;
  if (n >= 5) heartUntil = millis() + HEART_MS;
  addBond(2);
  registerCare();
  exercised = true;  // el ejercicio quema peso: el proximo tick ve un IN SHAPE como del jugador (#44)
  save();
  return trSpe - before;  // what really went in: at 100 nothing does (#76)
}

// The calm exercises (exercise.h, #78). Each punch or rep is worth 1.5 points
// (exerciseGain), the nature's bonus goes on top (#25: greedy for strength, tidy for
// stamina, +25% rounded up, so a full session is 23), and the points that fit under 100 go
// in. The session's work is what it burns, so a pet already at 100 still gets the exercise.
uint8_t Pet::trainStrength(uint8_t punches) {
  if (ceremony != CER_NONE || isEgg() || !punches) return 0;
  if (punches > EX_COUNT) punches = EX_COUNT;
  uint8_t work = exerciseGain(punches);
  uint8_t before = trAtk;
  uint16_t v = (uint16_t)trAtk + natureTrainGain(nature, TRAIN_STRENGTH, work);
  trAtk = v > 100 ? 100 : (uint8_t)v;
  if (punches > punchHi) punchHi = punches;  // record de golpes
  exerciseDone(work);
  return trAtk - before;  // what really went in: at 100 the screen showed the full gain
}

uint8_t Pet::trainStamina(uint8_t reps) {
  if (ceremony != CER_NONE || isEgg() || !reps) return 0;
  if (reps > EX_COUNT) reps = EX_COUNT;
  uint8_t work = exerciseGain(reps);
  uint8_t before = trDef;
  uint16_t v = (uint16_t)trDef + natureTrainGain(nature, TRAIN_STAMINA, work);
  trDef = v > 100 ? 100 : (uint8_t)v;
  exerciseDone(work);
  return trDef - before;
}

// What a session of either exercise costs and gives, the old strength bag's: it tires
// (energy -12, not under 5) and makes a little hungry, burns weight (a full session 24,
// as the bag's 72 taps did), cheers up, shows the heart for a good session (5 points or
// more, the bag's 20 taps), and counts as the day's care and as exercise (#44).
void Pet::exerciseDone(uint8_t work) {
  energy = dropTo(energy, 12, 5);
  fullness = dropTo(fullness, 5, 5);
  int burn = (int)weight - work * 4 / 3;
  weight = burn > 0 ? burn : 0;
  joy = clamp100(joy + 6);
  if (work >= 5) heartUntil = millis() + HEART_MS;
  addBond(2);
  registerCare();
  exercised = true;  // el ejercicio quema peso: el proximo tick ve un IN SHAPE como del jugador (#44)
  save();
}

void Pet::toggleLight() {
  if (ceremony != CER_NONE) return;
  if (isEgg()) return;
  sleeping = !sleeping;
  if (!sleeping) milked = false;  // awake: the night after the milk is over (#13)
  save();
}

// dormir/despertar por horario: mismas condiciones que el boton (no huevo, no
// ceremonia) mas no estar eligiendo inicial; sin sonido (es de noche)
bool Pet::autoSleep() {
  if (ceremony != CER_NONE || starterPick || isEgg() || sleeping) return false;
  sleeping = true;
  save();
  return true;
}

bool Pet::autoWake() {
  if (ceremony != CER_NONE || starterPick || isEgg() || !sleeping) return false;
  sleeping = false;
  milked = false;  // the night after the milk is over (#13)
  save();
  return true;
}

// los ajustes "Care" se guardan solos en su clave (no pasan por save())
void Pet::setPace(uint8_t v) {
  pace = v < PACE_COUNT ? v : (uint8_t)PACE_NORMAL;
  prefs.putUChar("pace", pace);
}

void Pet::setBedPreset(uint8_t v) {
  bedPreset = v < BED_PRESETS ? v : 0;
  prefs.putUChar("bed", bedPreset);
}

void Pet::setEggPickMode(uint8_t v) {
  eggPickMode = v < EGGPICK_COUNT ? v : (uint8_t)EGGPICK_OFF;
  prefs.putUChar("eggch", eggPickMode);
}

// Off cures the pet and clears the counts (#15), and saves them; on starts counting from 0
void Pet::setSickMode(uint8_t v) {
  sickMode = v < SICK_MODES ? v : (uint8_t)SICK_OFF;
  prefs.putUChar("sickopt", sickMode);
  if (sickMode == SICK_OFF && (sick || germMin || healMin || sickMin)) {
    clearSickness();
    save();
  }
}

void Pet::clean() {
  if (ceremony != CER_NONE) return;
  poops = 0;
  hygiene = 100;
  if (nature == NAT_TIDY && !isEgg()) joy = clamp100(joy + NAT_BATH_JOY);  // #25
  addBond(1);
  registerCare();
  save();
}

void Pet::caress() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  joy = clamp100(joy + 5);
  heartUntil = millis() + HEART_MS;
  addBond(1);
  registerCare();
}

void Pet::eggTap() {
  if (!isEgg() || eggPick) return;  // eligiendo huevo: aun no hay huevo que tocar
  if (++eggTaps >= 3) hatch();
  else save();
}

PetMood Pet::mood() const {
  if (sleeping) return MOOD_SLEEPING;
  if (eating()) return MOOD_EATING;
  if (lowestStat() < 25 || sick) return MOOD_SAD;  // sick (#15): the HURT pose, the calm repertoire
  return MOOD_HAPPY;
}

void Pet::save() {
  ticksSinceSave = 0;
  pendingSave = false;
  prefs.putUChar("full", fullness);
  prefs.putUChar("joy", joy);
  prefs.putUChar("ene", energy);
  prefs.putUChar("hyg", hygiene);
  prefs.putUChar("poop", poops);
  prefs.putUChar("wgt", weight);
  prefs.putUChar("gatk", geneAtk);
  prefs.putUChar("gdef", geneDef);
  prefs.putUChar("gspe", geneSpe);
  prefs.putUChar("tatk", trAtk);
  prefs.putUChar("tdef", trDef);
  prefs.putUChar("tspe", trSpe);
  prefs.putUChar("nature", nature);
  prefs.putBool("bk", berryKnown);
  prefs.putBool("shy", shiny);
  prefs.putBool("eshy", eggShiny);
  prefs.putBool("stpk", starterPick);
  prefs.putBool("ecpk", eggPick);
  prefs.putShort("ec0", eggCand[0]);
  prefs.putShort("ec1", eggCand[1]);
  prefs.putShort("ec2", eggCand[2]);
  prefs.putBytes("dexsh", dexShinyReg, sizeof(dexShinyReg));
  prefs.putUInt("age", ageMinutes);
  prefs.putShort("dexn", speciesId);
  prefs.putShort("eggT2", eggTarget);
  prefs.putUChar("crack", eggTaps);
  prefs.putUChar("mist", careMistakes);
  prefs.putBool("sleep", sleeping);
  prefs.putUChar("lend", lastEnd);
  if (lastSeenEpoch) prefs.putUInt("seen", lastSeenEpoch);
  prefs.putBytes("dexreg", dexReg, sizeof(dexReg));
  prefs.putUShort("strk", streak);
  prefs.putUShort("bstrk", bestStreak);
  prefs.putUInt("cday", lastCareDay);
  prefs.putUChar("bond", bond);
  prefs.putUShort("medal", medals);
  prefs.putUShort("tmedal", totalMedals);
  prefs.putUShort("mstone", lastMilestone);
  prefs.putUShort("ghi", gameHi);
  prefs.putUShort("mgc", catchHi);
  prefs.putUShort("mgp", peekHi);
  prefs.putUShort("mgs", songHi);
  prefs.putUChar("phi", punchHi);
  prefs.putUShort("tdefm", goodTicks);
  prefs.putBool("sick", sick);  // #15
  prefs.putUShort("sickg", germMin);
  prefs.putUShort("sickh", healMin);
  prefs.putUShort("sickt", sickMin);
  prefs.putString("nick", nick);
  prefs.putUChar("potb", garden.packed());  // the berry garden (#115)
  prefs.putUInt("potg", garden.grown);
  prefs.putUInt("potw", garden.wet);
  prefs.putUInt("potd", garden.dry);
  prefs.putUInt("pota", garden.at);
}

void Pet::load() {
  fullness = prefs.getUChar("full", 80);
  joy = prefs.getUChar("joy", 80);
  energy = prefs.getUChar("ene", 80);
  hygiene = prefs.getUChar("hyg", 100);
  poops = prefs.getUChar("poop", 0);
  weight = prefs.getUChar("wgt", 0);
  geneAtk = prefs.getUChar("gatk", 0);
  geneDef = prefs.getUChar("gdef", 0);
  geneSpe = prefs.getUChar("gspe", 0);
  if (geneAtk == 0) {  // mascota anterior a los genes: tirada unica ahora
    geneAtk = 90 + random(21);
    geneDef = 90 + random(21);
    geneSpe = 90 + random(21);
  }
  trAtk = prefs.getUChar("tatk", 0);
  trDef = prefs.getUChar("tdef", 0);
  trSpe = prefs.getUChar("tspe", 0);
  nature = prefs.getUChar("nature", NAT_EASYGOING);  // a pet saved before natures: easygoing
  goodTicks = prefs.getUShort("tdefm", 0);  // saves from before #76: none, 0
  berryKnown = prefs.getBool("bk", false);
  shiny = prefs.getBool("shy", false);
  eggShiny = prefs.getBool("eshy", false);
  starterPick = prefs.getBool("stpk", false);
  eggPick = prefs.getBool("ecpk", false);  // guardados antiguos: sin la clave = sin eleccion
  eggCand[0] = prefs.getShort("ec0", 0);
  eggCand[1] = prefs.getShort("ec1", 0);
  eggCand[2] = prefs.getShort("ec2", 0);
  prefs.getBytes("dexsh", dexShinyReg, sizeof(dexShinyReg));
  ageMinutes = prefs.getUInt("age", 0);
  if (prefs.isKey("dexn")) {
    speciesId = prefs.getShort("dexn", -1);
    eggTarget = prefs.getShort("eggT2", 4);
  } else {
    // migracion desde la version con indices de flash (0-8)
    static const uint8_t OLD2DEX[9] = { 4, 5, 6, 1, 2, 3, 7, 8, 9 };
    int8_t old = prefs.getChar("spec", -1);
    speciesId = (old >= 0 && old < 9) ? OLD2DEX[old] : -1;
    int8_t oldT = prefs.getChar("eggT", 0);
    eggTarget = (oldT >= 0 && oldT < 9) ? OLD2DEX[oldT] : 4;
  }
  eggTaps = prefs.getUChar("crack", 0);
  careMistakes = prefs.getUChar("mist", 0);
  sleeping = prefs.getBool("sleep", false);
  lastEnd = prefs.getUChar("lend", CER_NONE);
  prefs.getBytes("dexreg", dexReg, sizeof(dexReg));
  streak = prefs.getUShort("strk", 0);
  bestStreak = prefs.getUShort("bstrk", 0);
  lastCareDay = prefs.getUInt("cday", 0);
  bond = prefs.getUChar("bond", 0);
  medals = prefs.getUShort("medal", 0) & MED_ALL;  // only the medals this firmware defines
  totalMedals = prefs.getUShort("tmedal", 0);
  lastMilestone = prefs.getUShort("mstone", 0);
  // records de los minijuegos: un valor que el juego no puede dar (NVS danada o de
  // otro firmware) se lee como 0
  gameHi = gameRecordOk(GAME_KEEPY, prefs.getUShort("ghi", 0));
  catchHi = gameRecordOk(GAME_CATCH, prefs.getUShort("mgc", 0));
  peekHi = gameRecordOk(GAME_PEEK, prefs.getUShort("mgp", 0));
  songHi = gameRecordOk(GAME_SONG, prefs.getUShort("mgs", 0));
  punchHi = prefs.getUChar("phi", 0);
  prefs.getString("nick", nick, sizeof(nick));
  pace = prefs.getUChar("pace", PACE_NORMAL);
  bedPreset = prefs.getUChar("bed", 0);
  eggPickMode = prefs.getUChar("eggch", EGGPICK_OFF);
  sickMode = prefs.getUChar("sickopt", SICK_OFF);  // saves from before #15: none, off and well
  sick = prefs.getBool("sick", false);
  germMin = prefs.getUShort("sickg", 0);
  healMin = prefs.getUShort("sickh", 0);
  sickMin = prefs.getUShort("sickt", 0);
  // the berry garden (#115): range-checked as it loads (Garden::load); saves from before it
  // have none of its keys, an empty pot
  garden.load(prefs.getUChar("potb", 0), prefs.getUInt("potg", 0), prefs.getUInt("potw", 0),
              prefs.getUInt("potd", 0), prefs.getUInt("pota", 0), CLOCK_EPOCH_MIN,
              CLOCK_EPOCH_END);

  // Range-check what came out of NVS before anything indexes with it. Normally
  // these were written by this firmware, but a different build flashed earlier
  // (e.g. a fork with more than 151 species) or a damaged flash page can leave
  // a species number past DEX_TBL, which every screen reads directly.
  fullness = clamp100(fullness);
  joy = clamp100(joy);
  energy = clamp100(energy);
  hygiene = clamp100(hygiene);
  if (poops > 3) poops = 3;
  if (punchHi > EX_COUNT) punchHi = 0;  // a record no session can land: damaged, read as none
  weight = clamp100(weight);
  trAtk = clamp100(trAtk);  // training points are 0-100: the bars, the ribbons and idle life read them
  trDef = clamp100(trDef);
  trSpe = clamp100(trSpe);
  if (goodTicks >= STAMINA_CARE_MIN * GOOD_STEP) goodTicks = 0;
  if (lastEnd > CER_RELEASE) lastEnd = CER_NONE;
  if (nature >= NAT_COUNT) nature = NAT_EASYGOING;
  if (trAtk > 100) trAtk = 100;
  if (trDef > 100) trDef = 100;
  if (trSpe > 100) trSpe = 100;
  if (pace >= PACE_COUNT) pace = PACE_NORMAL;
  if (bedPreset >= BED_PRESETS) bedPreset = 0;
  if (eggPickMode >= EGGPICK_COUNT) eggPickMode = EGGPICK_OFF;
  // Sickness (#15): a count no minute of the game can reach reads as 0, which never makes the
  // pet sick sooner. A damaged recovery count read as 0 makes it wait longer; it never fakes a
  // cure, a run-away or a sickness.
  if (sickMode >= SICK_MODES) sickMode = SICK_OFF;
  if (germMin >= SICK_AFTER_MIN) germMin = 0;
  if (healMin >= SICK_HEAL_MIN) healMin = 0;
  if (sickMin > SICK_RUNAWAY_MIN) sickMin = 0;
  if (eggTarget < 1 || eggTarget > DEX_COUNT) eggTarget = pickEggSpecies();
  if (speciesId == 0 || speciesId < -1 || speciesId > DEX_COUNT) {
    newEgg();  // that pet doesn't exist in this firmware: start a fresh egg
    return;
  }
  if (sickMode == SICK_OFF || isEgg()) clearSickness();  // nothing counts while it's off; an egg is well
  // A pending egg choice only makes sense on an egg with no starter choice open
  // (if a damaged save has both, the starter screen wins). Candidates that are
  // not three distinct egg species are rolled again around the saved roll (eggTarget,
  // already range-checked above), so a damaged candidate save keeps a Rare/Legendary.
  // A saved roll that is not an egg species is replaced first, so eggTarget stays
  // one of the three while the choice is open.
  if (eggPick) {
    if (starterPick || !isEgg()) {
      eggPick = false;
    } else if (!validEggChoices(eggCand)) {
      if (DEX_TBL[eggTarget].rarity == R_EVO) eggTarget = pickEggSpecies();
      rollEggChoices(eggTarget, eggCand);  // conserva la tirada guardada
    }
  }

  // siembra: la mascota actual cuenta como criada (guardados antiguos)
  if (speciesId >= 1) registerSpecies(speciesId);
}
