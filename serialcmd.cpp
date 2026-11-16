// The USB serial console (serialcmd.h). Moved out of TamaPoke.ino (#41).
#include <Arduino.h>
#include "app.h"
#include "audio.h"
#include "bedtime.h"
#include "clockprompt.h"
#include "clockset.h"
#include "dex.h"
#include "owner.h"
#include "rtcbat.h"
#include "scene.h"
#include "screens.h"
#include "sdmon.h"
#include "serialcmd.h"

// ---------- consola serie (provision de SD + depuracion) ----------

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
  if (sdSerialCommand(line)) return;

  char ownerArg[OWNER_CMD_NAME_BYTES];  // the name of an OWNER <name> line (owner.h)
  OwnerCmd ownerCmd;
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
    uint32_t e = (uint32_t)line.substring(5).toInt();
    // the pet's saved time and the bedtime clock follow only if the clock took it (clockset.h)
    if (setWallClock(pet, e, bedClockSet)) {
      if (clockOpen) loadClockFields(rtcEpoch());  // the open screen shows the time just set
    } else {  // nothing changes, not even what the player is editing on the clock screen
      Serial.printf("ERR clock not set: it did not take %lu; the pet's saved time is unchanged\n",
                    (unsigned long)e);
    }
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line.startsWith("RTCSET ")) {  // solo RTC (simular apagados en pruebas)
    rtcSetEpoch((uint32_t)line.substring(7).toInt());
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "TIME") {
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "GAL") {
    galleryOpen = !galleryOpen;
    galleryDetail = 0;
    galleryDirty = true;
    if (!galleryOpen) galleryPmd.unload();
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
  } else if (line == "CAREDAY") {  // simula un dia nuevo cuidado (pruebas)
    pet.setClock(pet.lastSeenEpoch + 86400);
    pet.caress();
    Serial.printf("streak=%u bond=%u medals=0x%X\n", pet.streak, pet.bond, pet.medals);
    Serial.println("DONE");
  } else if (line == "BYE") {
    pet.startFarewell();
    Serial.println("DONE");
  } else if (line == "RUN") {
    pet.startRunaway();
    Serial.println("DONE");
  } else if (line == "CARE?") {  // read-only: relaxed pace / bedtime settings and whether it is bedtime now
    uint32_t e = rtcEpoch();
    Serial.printf("pace=%u bed=%u inbed=%d stale=%d egg=%u picking=%d\n", pet.pace, pet.bedPreset,
                  e && isBedtime(pet.bedPreset, bedMinuteOfDay(e)) ? 1 : 0, bedEdge.stale() ? 1 : 0,
                  pet.eggPickMode, pet.awaitingEgg() ? 1 : 0);
    Serial.println("DONE");
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
