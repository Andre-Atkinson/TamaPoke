// The USB serial console (serialcmd.h). Moved out of TamaPoke.ino (#41).
#include <Arduino.h>
#include "app.h"
#include "audio.h"
#include "batdiary.h"
#include "bedtime.h"
#include "clockprompt.h"
#include "clockset.h"
#include "dex.h"
#include "holiday.h"
#include "owner.h"
#include "rtcbat.h"
#include "scene.h"
#include "screens.h"
#include "sdmon.h"
#include "serialcmd.h"
#include "steps.h"     // STEPS? (#117)

// ---------- consola serie (provision de SD + depuracion) ----------

// GARDEN? and GARDEN (#115): the pot as it is now (garden.h): its stage, the berry planted, the
// berries left on a ripe plant, whether it is watered, the minutes of water left, dry in a
// row and watered in all, wilting soon, and the trusted clock it was last moved on to (0:
// none yet, or the clock is stale).
static void printGarden() {
  const Garden &g = pet.garden;
  Serial.printf("garden stage=%s berry=%s left=%u wet=%d wetmin=%lu drymin=%lu grownmin=%lu wilting=%d at=%lu\n",
                GARDEN_STAGE_KEYS[g.stage()], g.growing() ? GARDEN_BERRY_KEYS[g.berry % GARDEN_BERRY_COLORS] : "none",
                (unsigned)g.left, g.watered() ? 1 : 0, (unsigned long)(g.wet / 60), (unsigned long)(g.dry / 60),
                (unsigned long)(g.grown / 60), g.wiltingSoon() ? 1 : 0, (unsigned long)g.at);
}

// BDAY and BDAY? (#26): the stored birthday ("none" or month and day), today's special day
// on the scene's clock (holiday.h; "none" on an ordinary day, while the clock is not trusted
// and during a farewell or a run-away, holidayNow()) and whether it is the birthday.
static void printBday() {
  Birthday b = birthday();
  HolidayToday t = holidayNow();
  if (b.month) Serial.printf("bday=%u %u", (unsigned)b.month, (unsigned)b.day);
  else Serial.print("bday=none");
  Serial.printf(" today=%s birthday=%d\n", HOLIDAY_KEYS[t.day < HOLIDAYS ? t.day : 0], t.birthday ? 1 : 0);
}

// SCENE and SCENE? (#60): the place on screen, today's, the home place, the weather and the
// season, with "*" after a value the SCENE command forced.
static void printScene() {
  WxNow w = sceneWeather();
  Serial.printf("scene place=%s%s today=%s home=%s wx=%s%s prev=%s mix=%u season=%s%s clock=%d particles=%d standby=%d\n",
                PLACE_KEYS[scenePlace()], sceneForcePlace < PLACES ? "*" : "", PLACE_KEYS[sceneToday()],
                PLACE_KEYS[sceneHome()], WX_KEYS[w.kind], sceneForceWx < WX_KINDS ? "*" : "", WX_KEYS[w.prev],
                w.mix, SEASON_KEYS[sceneSeason()], sceneForceSeason < SEASONS ? "*" : "",
                pet.lastSeenEpoch && !bedEdge.stale() ? 1 : 0,
                wxParticlesLive(frameIntervalMs(powerCtx(millis()))) ? 1 : 0, standby ? 1 : 0);
}

// ACC and ACC? (#116, accessory.h): what the pet wears (`worn`), what shows on it now (`shown`:
// asleep or dozing it shows nothing, which this doesn't say), today's special hat (`day`), the
// earned ones it has (`have`) and those with a red dot (`new`); lists comma-separated, "none"
// when empty.
static void printAccList(const char *key, uint16_t bits) {
  Serial.printf(" %s=", key);
  bool any = false;
  for (uint8_t id = ACC_BOW; id < ACC_COUNT; id++)
    if (bits & (1u << id)) {
      Serial.printf("%s%s", any ? "," : "", ACC_KEYS[id]);
      any = true;
    }
  if (!any) Serial.print("none");
}
static void printAcc() {
  uint8_t day = accDayHat();
  Serial.printf("ACC worn=%s shown=%s day=%s", ACC_KEYS[pet.acc < ACC_COUNT ? pet.acc : 0],
                ACC_KEYS[pet.accNow(day, accToday())], ACC_KEYS[day]);
  printAccList("have", pet.accUnlocked());
  printAccList("new", pet.accUnlocked() & ~pet.accSeen);
  Serial.println();
}

// TIME or RTCSET with text that is not a time the clock can take (clockEpochParse, #65): an
// ERR line naming it (its first 24 characters), and nothing set. `petNote`: TIME's line also
// says the pet's saved time is unchanged, as for a time the clock refuses.
static void printEpochRefused(const char *arg, bool petNote) {
  Serial.printf("ERR clock not set: '%.24s' is not a time from 2025 to 2099 in seconds since 1970%s\n", arg,
                petNote ? "; the pet's saved time is unchanged" : "");
}

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
#if BOARD_AMOLED_175C
  if (line == "LOCK?") {
    Serial.printf("LOCK locked=%d panel_sleep=%d\n", screenLock.locked() ? 1 : 0,
                  panelAsleep ? 1 : 0);
    return;
  }
#endif
  if (line == "STEPS?") {  // read-only: the step counter (#117, steps.h)
    stepsPrintStatus();
    return;
  }
  if (line.startsWith("STEPRATE ")) {  // test builds (#117): the step engine's rate, in RAM only
    String args = line.substring(9);
    args.trim();
    int sp = args.indexOf(' ');
    String rate = sp < 0 ? args : args.substring(0, sp);
    String prof = sp < 0 ? String("") : args.substring(sp + 1);
    prof.trim();
    if (stepsSetRate(rate.c_str(), prof.c_str())) stepsPrintStatus();
    else Serial.println("ERR STEPRATE <lp21|lp128|28|56|112> [lib|scaled|easy] (or no step counter)");
    return;
  }
  if (sdSerialCommand(line)) return;

  char ownerArg[OWNER_CMD_NAME_BYTES];  // the name of an OWNER <name> line (owner.h)
  OwnerCmd ownerCmd;
  Birthday bdayArg;  // the date of a BDAY <month> <day> line (holiday.h)
  BdayCmd bdayCmd;
  uint16_t sickArg;  // the minutes of a SICK ON <minutes> line (#15, sickCmdParse in pet.h)
  SickCmd sickCmd;
  GardenCmdArgs gardenArgs;  // a GARDEN line's stage, water and berry (#115, gardenCmdParse in garden.h)
  GardenCmd gardenCmd;
  if (line == "HATCH") {
    pet.eggTap(); pet.eggTap(); pet.eggTap();
    Serial.println("DONE");
  } else if (line.startsWith("SPEC ")) {
    int n = line.substring(5).toInt();
    if (n >= 1 && n <= DEX_COUNT) {
      pet.cancelEggChoice();  // con un bicho en pantalla ya no hay huevo que elegir
      pet.prevSpeciesId = pet.speciesId;
      pet.speciesId = n;
      Serial.printf("especie #%d %s\n", n, DEX_TBL[n].name);
    }
    Serial.println("DONE");
  } else if (line.startsWith("LVL ")) {
    pet.ageMinutes = (uint32_t)line.substring(4).toInt() * MINUTES_PER_LEVEL;
    Serial.println("DONE");
  } else if (line.startsWith("TIME ")) {
    uint32_t e = 0;
    // the pet's saved time and the bedtime clock follow only if the clock took it (clockset.h)
    if (!clockEpochParse(line.c_str() + 5, e)) {  // not a time the clock can take (#65)
      printEpochRefused(line.c_str() + 5, true);
    } else if (setWallClock(pet, e, bedClockSet)) {
      if (clockOpen) loadClockFields(rtcEpoch());  // the open screen shows the time just set
    } else {  // nothing changes, not even what the player is editing on the clock screen
      Serial.printf("ERR clock not set: it did not take %lu; the pet's saved time is unchanged\n",
                    (unsigned long)e);
    }
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line.startsWith("RTCSET ")) {  // solo RTC (simular apagados en pruebas)
    uint32_t e = 0;
    if (clockEpochParse(line.c_str() + 7, e)) rtcSetEpoch(e);
    else printEpochRefused(line.c_str() + 7, false);  // #65
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "TIME") {
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "GAL") {
    // never opened over an ending (#33): everything over the scene closed as it started
    // (ending.h), and nothing opens over it until it is over
    if (!galleryOpen && pet.ceremony != CER_NONE) {
      Serial.println("ERR gallery not opened: an ending is on screen");
    } else {
      galleryOpen = !galleryOpen;
      galleryDetail = 0;
      galleryDirty = true;
      if (!galleryOpen) galleryPmd.unload();
    }
    Serial.println("DONE");
  } else if (line == "EGGS") {
    // simula 20 tiradas de huevo (no cambia el estado del juego)
    for (int i = 0; i < 20; i++) {
      int16_t d = pet.pickEggSpecies();
      Serial.printf("%d:%s(r%u) ", d, DEX_TBL[d].name, DEX_TBL[d].rarity);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line == "SHINY") {  // alterna shiny del actual (pruebas)
    pet.shiny = !pet.shiny;
    Serial.printf("shiny=%d\n", pet.shiny);
    Serial.println("DONE");
  } else if (line.startsWith("NICK ")) {
    pet.rename(line.substring(5).c_str());
    Serial.printf("nick=%s\n", pet.nick);
    Serial.println("DONE");
  } else if ((ownerCmd = ownerParse(line.c_str(), ownerArg, sizeof ownerArg)) != OWNER_CMD_NONE) {
    // the player's name in the speech bubbles (owner.h): "OWNER?" only reads it, "OWNER <name>"
    // sets it (empty clears it)
    if (ownerCmd == OWNER_CMD_SET) {
      uint32_t bad = 0;
      OwnerCheck c = ownerSet(ownerArg, &bad);
      if (c == OWNER_TOO_LONG) Serial.printf("owner not set: more than %d characters\n", OWNER_NAME_MAX);
      else if (c == OWNER_BAD_CHAR) Serial.printf("owner not set: U+%04lX cannot be drawn\n", (unsigned long)bad);
      else if (c == OWNER_BAD_UTF8) Serial.println("owner not set: not UTF-8 text");
      else if (c == OWNER_NO_LETTER) Serial.println("owner not set: a name needs a letter");
      else if (c == OWNER_NOT_SAVED) Serial.println("owner not set: the flash write failed");
      else if (c != OWNER_OK) Serial.println("owner not set");
    }
    Serial.printf("owner=%s\n", ownerName());  // what is stored now
    Serial.println("DONE");
  } else if ((bdayCmd = bdayParse(line.c_str(), bdayArg)) != BDAY_CMD_NONE) {
    // the player's birthday for the special days (holiday.h, #26): "BDAY?" only reads it,
    // "BDAY <month> <day>" sets it ("BDAY 0 0" clears it)
    if (bdayCmd == BDAY_CMD_BAD)
      Serial.println("ERR usage: BDAY <month 1-12> <day of that month> (BDAY 0 0 clears it)");
    else if (bdayCmd == BDAY_CMD_SET && !birthdaySet(bdayArg))
      Serial.println("bday not set: the flash write failed");
    printBday();  // what is stored now
    Serial.println("DONE");
  } else if (line == "CAREDAY") {  // simula un dia nuevo cuidado (pruebas)
    pet.setClock(pet.lastSeenEpoch + 86400);
    pet.caress();
    Serial.printf("streak=%u bond=%u medals=0x%X\n", pet.streak, pet.bond, pet.medals);
    Serial.println("DONE");
  } else if (line == "BYE") {  // closes what is open first, as every ending does (#33)
    startPetEnding(CER_FAREWELL);
    Serial.println("DONE");
  } else if (line == "RUN") {
    startPetEnding(CER_RUNAWAY);
    Serial.println("DONE");
  } else if (line == "ACC?") {  // read-only (#116)
    printAcc();
    Serial.println("DONE");
  } else if (line.startsWith("ACC ")) {  // wears one, as a tap on the Wardrobe page does (#116)
    int id = accFromKey(line.substring(4).c_str());
    if (id < 0)
      Serial.println("ERR usage: ACC <none|bow|flowers|bobble|cap|tophat|crown|laurel> or today's special hat");
    else if (!pet.wearAcc((uint8_t)id, accDayHat(), accToday()))
      Serial.println("ERR not worn: not earned yet, not today's special hat, or no pet (an egg or an ending)");
    else
      printAcc();
    Serial.println("DONE");
  } else if (line == "CARE?") {  // read-only: relaxed pace / bedtime settings and whether it is bedtime now
    uint32_t e = rtcEpoch();
    Serial.printf("pace=%u bed=%u inbed=%d stale=%d egg=%u picking=%d", pet.pace, pet.bedPreset,
                  e && isBedtime(pet.bedPreset, bedMinuteOfDay(e)) ? 1 : 0, bedEdge.stale() ? 1 : 0,
                  pet.eggPickMode, pet.awaitingEgg() ? 1 : 0);
    // sickness (#15): the setting, sick or not, and the counts (towards sickness while well;
    // the cure's and the minutes sick while sick)
    Serial.printf(" sickness=%u sick=%d germ=%u heal=%u sickmin=%u\n", pet.sickMode, pet.isSick() ? 1 : 0,
                  pet.germMinutes(), pet.healMinutes(), pet.sickMinutes());
    Serial.println("DONE");
  } else if ((gardenCmd = gardenCmdParse(line.c_str(), gardenArgs)) != GCMD_NONE) {
    // the berry garden (#115): "GARDEN?" reads it; "GARDEN <stage> ..." forces it (a test hook
    // for the simulator and screenshots) and saves
    if (gardenCmd == GCMD_BAD) {
      Serial.println("ERR usage: GARDEN <empty|seed|sprout|leaves|flower|ripe|withered> [wet|dry|wilting] [red|blue|green]");
    } else {
      if (gardenCmd == GCMD_SET) {
        pet.garden.force(gardenArgs.stage, gardenArgs.moist, gardenArgs.color);
        pet.saveNow();
      }
      printGarden();
    }
    Serial.println("DONE");
  } else if ((sickCmd = sickCmdParse(line.c_str(), sickArg)) != SICKCMD_NONE) {
    // a test hook (#15): sick now (with sickArg counted minutes) or well, with no slip-up
    if (sickCmd == SICKCMD_BAD)
      Serial.printf("ERR usage: SICK ON [minutes 0-%u] | SICK OFF\n", (unsigned)SICK_RUNAWAY_MIN);
    else if (!pet.dbgSick(sickCmd == SICKCMD_ON, sickArg))
      Serial.println("ERR sickness is off (Care page), or no pet (an egg or an ending)");
    else
      Serial.printf("sick=%d sickmin=%u\n", pet.isSick() ? 1 : 0, pet.sickMinutes());
    Serial.println("DONE");
  } else if (line == "BATLOG?") {  // read-only: the battery diary as CSV (batdiary.h, #14)
    batLogPrint();  // ends with DONE, sent under the dump's own transmit wait
  } else if (line == "BATLOG CLEAR") {  // erases only the diary's own namespace, not the pet
    if (!batLogClear(millis())) Serial.println("ERR battery diary not cleared: the flash write failed");
    batLogPrint();  // what is kept now, ending with DONE
  } else if (line == "SCENE?") {  // read-only: the place and the weather (weather.h, #60)
    printScene();
    Serial.println("DONE");
  } else if (line == "SCENE" || line.startsWith("SCENE ")) {  // forces them, in RAM only
    uint8_t place, wx, season;
    if (sceneCmdParse(line.c_str() + 5, place, wx, season)) {
      sceneForcePlace = place;
      sceneForceWx = wx;
      sceneForceSeason = season;
    } else {
      Serial.println("ERR usage: SCENE <meadow|beach|forest|volcano|mountain|snow|auto> "
                     "[clear|cloudy|rain|storm|snow|fog|auto] [spring|summer|autumn|winter|none|auto]");
    }
    printScene();
    Serial.println("DONE");
  } else if (line == "BEEP") {
    sfxPlay(SFX_HATCH);  // prueba de audio
    Serial.println("DONE");
  } else if (line == "ABANDON") {
    pet.dbgRunawayReady();  // fuerza el estado "lista para escaparse" (test del boton)
    Serial.println("DONE");
  } else if (line == "WIPE") {
    pet.factoryReset();     // borra NVS y reinicia -> partida nueva (eleccion de inicial)
    Serial.println("DONE");
    delay(100);
    ESP.restart();
  } else if (line == "REG") {
    Serial.printf("pokedex %u/151:", pet.registeredCount());
    for (int i = 1; i <= 151; i++)
      if (pet.isRegistered(i)) Serial.printf(" %d", i);
    Serial.println();
    Serial.println("DONE");
#if BOARD_AMOLED_175C
  } else if (line == "TRAINTRACE ON" || line == "TRAINTRACE OFF" || line == "TRAINTRACE?") {
    if (line != "TRAINTRACE?") trainingCapture.setTracing(line == "TRAINTRACE ON");
    Serial.printf("TRAINTRACE %s poll=20ms trace_drops=%lu kind=0:error,1:contact,2:release,3:empty,4:cover\n",
                  trainingCapture.tracing() ? "ON" : "OFF", (unsigned long)trainingCapture.traceDropped());
    Serial.println("DONE");
#endif
  } else if (line == "SAVE") {  // write pending pet changes now (tools/pet_save.sh runs it before a backup)
    pet.saveNow();  // always: lastSeenEpoch and the 5th-tick changes may be RAM-only
    Serial.println("saved");
    Serial.println("DONE");
  } else if (line == "HEALTH") {
    Serial.printf("up=%lus heap=%u min=%u sd=%d mon=%d\n",
                  (unsigned long)(millis() / 1000), ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), sdReady, pmd.loaded || mon.loaded);
    Serial.println("DONE");
  } else if (line == "STATS") {
    Serial.printf("spec=%d nv=%u com=%u fel=%u ene=%u lim=%u desc=%u sd=%d mon=%d bat=%d usb=%d rtc=%u\n",
                  pet.speciesId, pet.level(), pet.fullness, pet.joy, pet.energy,
                  pet.hygiene, pet.careMistakes, sdReady, mon.loaded,
                  batPercent(), usbPresent(), rtcEpoch());
    Serial.printf("peso=%u fue=%u def=%u vel=%u genes=%u/%u/%u tr=%u/%u/%u baya=%d nat=%s\n",
                  pet.weight, pet.atkStat(), pet.defStat(), pet.speStat(),
                  pet.geneAtk, pet.geneDef, pet.geneSpe,
                  pet.trAtk, pet.trDef, pet.trSpe, pet.berryKnown,
                  NATURE_KEYS[pet.nature < NAT_COUNT ? pet.nature : (uint8_t)NAT_EASYGOING]);
    Serial.printf("shiny=%d streak=%u/%u bond=%u medals=0x%X(%u) nick=%s\n",
                  pet.shiny, pet.streak, pet.bestStreak, pet.bond, pet.medals,
                  pet.totalMedals, pet.nick);
    Serial.println("DONE");
  }
}
