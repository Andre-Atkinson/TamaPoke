#include "pet.h"
#include "dex.h"
#include "audio.h"
#include "bedtime.h"

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
  if (nowEpoch) save();  // persiste ya: un corte de luz no pierde la referencia
}

void Pet::syncClock(uint32_t nowEpoch) {
  uint32_t seen = prefs.getUInt("seen", 0);
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
      energy = clamp100(energy + 6);
      if (ageMinutes % 2 == 0) {
        fullness = dropTo(fullness, 1, 30);
        joy = dropTo(joy, 1, 35);
      }
      if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
      continue;
    }
    fullness = dropTo(fullness, 2, 15);
    energy = dropTo(energy, 1, 15);
    hygiene = dropTo(hygiene, 1, 15);
    joy = dropTo(joy, 1, 15);
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

void Pet::update(uint32_t nowMs) {
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
    energy = clamp100(energy + 6);
    if (weight > 0 && ageMinutes % 3 == 0) weight--;
    if (ageMinutes % 2 == 0) {                 // ~4x mas lento que despierto
      fullness = dropTo(fullness, 1, 30);
      joy = dropTo(joy, 1, 35);
    }
    if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
    checkMedals();  // aun puede cruzar un nivel por edad mientras duerme
    if (++ticksSinceSave >= 5) pendingSave = true;
    return;
  }

  if (ageMinutes % MINUTES_PER_LEVEL == 0) sfxPlay(SFX_LEVEL);  // subio de nivel (despierto)

  // Goteo de necesidades. Ritmo normal: cada minuto. Ritmo relajado: solo los
  // minutos pares (la mitad de rapido). Todo lo demas de este tick (edad, peso,
  // racha de buen cuidado, descuidos, abandono, medallas, guardado) sigue siendo
  // por minuto en ambos ritmos, y la progresion offline (syncClock) no cambia.
  bool drain = pace != PACE_RELAXED || ageMinutes % 2 == 0;
  if (drain) drainBody();
  if (weight > 0 && ageMinutes % 3 == 0) weight--;

  // la disciplina forja la defensa: 12 h seguidas bien cuidado = +1 DEF
  // (va entre la caida de comida/higiene y la de felicidad, como siempre: lowestStat
  // incluye la felicidad, y moverlo cambiaria el ritmo normal)
  if (lowestStat() >= 40) {
    if (++goodTicks >= 720) {
      goodTicks = 0;
      if (trDef < 100) trDef++;
    }
  } else {
    goodTicks = 0;
  }

  if (drain) drainJoy();

  // Self-care while charging (#24). After this minute's drain, so a top-up shows as
  // 50, and before the slip-up and run-away checks, so a need it lifts this minute is
  // not a slip-up. Its position is the guard: the returns above keep it off during a
  // ceremony, the starter or egg choice, as an egg and asleep. syncClock() (the
  // powered-off catch-up) never runs tick(), so it never self-cares. Every minute in
  // both paces: relaxed only halves the drain.
  if (selfCareOn) selfCare();

  // Descuido: dejar una estadistica por los suelos cuenta como error de
  // cuidado (con enfriamiento para no contar el mismo descuido cada minuto)
  if (mistakeCooldown > 0) mistakeCooldown--;
  if (lowestStat() <= 10 && mistakeCooldown == 0) {
    careMistakes++;
    mistakeCooldown = 60;
    if (bond > 1) bond--;  // el descuido enfria el vinculo, pero sin arrasarlo:
                           // a -3 cada 30 min se perdia mucho mas de lo que se
                           // podia ganar en todo un dia y el vinculo se atascaba
  }

  checkMedals();  // la evolucion la dispara el usuario (canEvolveNow + tap), no el tick

  // abandono total: con TODO a cero durante una hora queda lista para escaparse;
  // NO se va sola, la dispara el usuario con el boton (final triste, lo presencia)
  if (fullness == 0 && joy == 0 && energy == 0 && hygiene == 0) {
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

void Pet::drainBody() {
  fullness = clamp100(fullness - 2);
  energy = clamp100(energy - 1);
  if (fullness > 40 && poops < 3 && random(100) < 15) poops++;

  hygiene = clamp100(hygiene - 1 - 4 * poops);
  // el sobrepeso da pereza: la energia cae el doble
  if (weight > 50) energy = clamp100(energy - 1);
}

void Pet::drainJoy() {
  int dJoy = -1;
  if (fullness < 30) dJoy -= 2;
  if (hygiene < 30) dJoy -= 2;
  joy = clamp100(joy + dJoy);
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
  return (eggTarget >= 1 && eggTarget <= 151) ? DEX_TBL[eggTarget].rarity : R_COMUN;
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

// los tres huevos a elegir: `special` es la tirada normal (pickEggSpecies) y
// los otros dos son comunes, de especie y color de tipo distintos (el color es
// la pista que se ve en la cascara). Como pickEggSpecies, prefieren lineas
// evolutivas incompletas; si no se puede todo a la vez, cede primero la linea
// nueva y despues el color. Hay 47 comunes, asi que siempre sale algo.
void Pet::rollEggChoices(int16_t special, int16_t out[EGG_CHOICES]) {
  // defensivo: las llamadas de newEgg() pasan una especie de huevo; load() puede
  // pasar un eggTarget danado que no lo sea (entonces se sortea uno)
  if (special < 1 || special > DEX_COUNT || DEX_TBL[special].rarity == R_EVO)
    special = pickEggSpecies();
  out[0] = special;
  for (int k = 1; k < EGG_CHOICES; k++) {
    out[k] = -1;
    for (int pass = 0; pass < 4 && out[k] < 0; pass++) {
      bool newColour = pass < 2;               // pasadas 0 y 1
      bool newLine = pass == 0 || pass == 2;   // pasadas 0 y 2
      int16_t cand[DEX_COUNT];
      int n = 0;
      for (int16_t d = 1; d <= DEX_COUNT; d++) {
        if (DEX_TBL[d].rarity != R_COMUN) continue;
        bool taken = false, sameColour = false;
        for (int j = 0; j < k; j++) {
          if (out[j] == d) taken = true;
          if (DEX_TBL[out[j]].accent == DEX_TBL[d].accent) sameColour = true;
        }
        // `taken` es redundante con `sameColour` en las pasadas 0 y 1 (defensivo):
        // solo cuenta en las pasadas 2 y 3, donde el color ya no se exige
        if (taken || (newColour && sameColour)) continue;
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
    milestoneUntil = millis() + 4500;
  }
  checkMedals();
  save();
}

void Pet::addBond(uint8_t amt) {
  if (bondToday >= 20) return;  // tope diario: el vinculo no se farmea
  bond = clamp100(bond + amt);
  bondToday += amt;
}

void Pet::checkMedals() {
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
  uint16_t gained = medals & ~before;
  if (gained) {
    for (uint16_t m = gained; m; m &= (m - 1)) totalMedals++;
    newMedal = gained;
    medalUntil = millis() + 4000;
    if (!sleeping) sfxPlay(SFX_MEDAL);
    save();
  }
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
bool Pet::canRunawayNow() const {
  return !isEgg() && !sleeping && ceremony == CER_NONE && neglectTicks >= RUNAWAY_TICKS;
}

void Pet::startFarewell() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_FAREWELL;
  ceremony = CER_FAREWELL;
  ceremonyUntil = millis() + CEREMONY_MS;
  heartUntil = ceremonyUntil;  // corazones durante toda la despedida
  sfxPlay(SFX_BYE);
  save();
}

void Pet::startRunaway() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_RUNAWAY;
  ceremony = CER_RUNAWAY;
  ceremonyUntil = millis() + CEREMONY_MS;
  sfxPlay(SFX_BYE);
  save();
}

void Pet::release() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_RELEASE;
  ceremony = CER_RELEASE;
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
  berryKnown = false;
  bond = 0;          // vinculo, medallas y nombre son del individuo
  bondToday = 0;
  medals = 0;
  newMedal = 0;
  nick[0] = 0;
  registerSpecies(speciesId);  // criado = registrado en la pokedex
  checkMedals();     // por si nace ya en forma final (legendario)
  sfxPlay(SFX_HATCH);
  save();
}

// ¿se dan ya las condiciones para evolucionar? Cada descuido retrasa la
// evolucion 1 nivel, y ademas tiene que estar bien cuidado en ese momento
// (ninguna estadistica por debajo de 40). NO evoluciona sola: la dispara el
// usuario tocando al bicho (evolve()), para que vea la transformacion.
bool Pet::canEvolveNow() const {
  if (isEgg() || sleeping || ceremony != CER_NONE) return false;
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
  if (lovesBerry(color)) {
    fullness = clamp100(fullness + 35);
    joy = clamp100(joy + 10);
    heartUntil = millis() + HEART_MS;  // "le encanta!"
    berryKnown = true;                 // descubierto: se muestra en la ficha
    addBond(2);
  } else {
    fullness = clamp100(fullness + 25);
  }
  eatUntil = millis() + EAT_ANIM_MS;
  registerCare();
  save();
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

// Fin de un minijuego de Play. Cada juego guarda su record con la puntuacion que
// ve el jugador; la recompensa sale de la normalizada (gamecore.h), asi que una
// buena partida paga lo mismo sea cual sea el juego que toco.
void Pet::playGame(uint8_t game, uint16_t raw) {
  if (ceremony != CER_NONE || isEgg() || game >= GAME_COUNT) return;
  if (raw > GAME_REC_MAX[game]) raw = GAME_REC_MAX[game];
  uint16_t *rec = game == GAME_CATCH ? &catchHi : game == GAME_PEEK ? &peekHi
                : game == GAME_SONG ? &songHi : &gameHi;
  if (raw > *rec) *rec = raw;  // nuevo record
  playReward(gameNormScore(game, raw));
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

void Pet::playReward(uint8_t n) {
  uint8_t v = trSpe + n / 5;  // jugar entrena la velocidad
  trSpe = v > 100 ? 100 : v;
  joy = clamp100(joy + 5 + (n > 15 ? 30 : n * 2));
  energy = dropTo(energy, 10 + n / 2, 5);
  fullness = dropTo(fullness, 5, 5);
  int burn = (int)weight - n * 2;  // el ejercicio quema peso
  weight = burn > 0 ? burn : 0;
  if (n >= 5) heartUntil = millis() + HEART_MS;
  addBond(2);
  registerCare();
  save();
}

// saco de entrenamiento: los golpes entrenan la fuerza. Devuelve la subida.
uint8_t Pet::trainStrength(uint16_t hits) {
  if (ceremony != CER_NONE || isEgg()) return 0;
  uint8_t gain = hits / 4;          // ~4 golpes = 1 punto de entrenamiento
  if (gain > 18) gain = 18;         // tope por sesion: la FUE se forja a fuego lento
  uint8_t antes = trAtk;
  uint16_t v = (uint16_t)trAtk + gain;
  trAtk = v > 100 ? 100 : (uint8_t)v;
  gain = trAtk - antes;             // lo que de verdad entro: al topar en 100 la
                                    // pantalla anunciaba +18 aunque cupieran menos
  energy = dropTo(energy, 12, 5);   // cansa
  fullness = dropTo(fullness, 5, 5);
  int burn = (int)weight - hits / 3;  // tambien quema peso
  weight = burn > 0 ? burn : 0;
  joy = clamp100(joy + 6);
  if (hits >= 20) heartUntil = millis() + HEART_MS;
  if (hits > strHi) strHi = hits;   // record de golpes
  addBond(2);
  registerCare();
  save();
  return gain;
}

void Pet::play() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  joy = clamp100(joy + 25);
  energy = clamp100(energy - 10);
  fullness = clamp100(fullness - 5);
  heartUntil = millis() + HEART_MS;
  addBond(2);
  registerCare();
  save();
}

void Pet::toggleLight() {
  if (ceremony != CER_NONE) return;
  if (isEgg()) return;
  sleeping = !sleeping;
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

void Pet::clean() {
  if (ceremony != CER_NONE) return;
  poops = 0;
  hygiene = 100;
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
  if (lowestStat() < 25) return MOOD_SAD;
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
  prefs.putUShort("shi", strHi);
  prefs.putString("nick", nick);
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
  medals = prefs.getUShort("medal", 0);
  totalMedals = prefs.getUShort("tmedal", 0);
  lastMilestone = prefs.getUShort("mstone", 0);
  // records de los minijuegos: un valor que el juego no puede dar (NVS danada o de
  // otro firmware) se lee como 0
  gameHi = gameRecordOk(GAME_KEEPY, prefs.getUShort("ghi", 0));
  catchHi = gameRecordOk(GAME_CATCH, prefs.getUShort("mgc", 0));
  peekHi = gameRecordOk(GAME_PEEK, prefs.getUShort("mgp", 0));
  songHi = gameRecordOk(GAME_SONG, prefs.getUShort("mgs", 0));
  strHi = prefs.getUShort("shi", 0);
  prefs.getString("nick", nick, sizeof(nick));
  pace = prefs.getUChar("pace", PACE_NORMAL);
  bedPreset = prefs.getUChar("bed", 0);
  eggPickMode = prefs.getUChar("eggch", EGGPICK_OFF);

  // Range-check what came out of NVS before anything indexes with it. Normally
  // these were written by this firmware, but a different build flashed earlier
  // (e.g. a fork with more than 151 species) or a damaged flash page can leave
  // a species number past DEX_TBL, which every screen reads directly.
  fullness = clamp100(fullness);
  joy = clamp100(joy);
  energy = clamp100(energy);
  hygiene = clamp100(hygiene);
  if (poops > 3) poops = 3;
  if (lastEnd > CER_RELEASE) lastEnd = CER_NONE;
  if (pace >= PACE_COUNT) pace = PACE_NORMAL;
  if (bedPreset >= BED_PRESETS) bedPreset = 0;
  if (eggPickMode >= EGGPICK_COUNT) eggPickMode = EGGPICK_OFF;
  if (eggTarget < 1 || eggTarget > DEX_COUNT) eggTarget = pickEggSpecies();
  if (speciesId == 0 || speciesId < -1 || speciesId > DEX_COUNT) {
    newEgg();  // that pet doesn't exist in this firmware: start a fresh egg
    return;
  }
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
