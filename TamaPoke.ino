// TamaPoke - tamagotchi pixel art inspirado en la gen 1
// para Waveshare ESP32-S3-Touch-AMOLED-1.75 (por defecto)
// y, como soporte experimental, la ESP32-S3-Touch-AMOLED-1.75C (-DBOARD_AMOLED_175C=1)
//
// Librerias (Library Manager o repo de Waveshare):
//   - "GFX Library for Arduino" (moononournation), con soporte CO5300 QSPI
//   - "SensorLib" (Lewis He), driver tactil CST9217
//
// Original 1.75: ESP32S3 Dev Module | Flash 16MB | PSRAM OPI | CDC On Boot
//                Partition 16M Flash (3MB APP/9MB FATFS)
// 1.75C:         ESP32S3 Dev Module | Flash 32MB | PSRAM OPI | CDC On Boot
//                -DBOARD_AMOLED_175C=1 + boards/175c/partitions.csv
//                (tools/build_175c.sh). See PORTING.md and board.h.
//
// Los sprites y la tabla de especies se generan con tools/sprites.py (emit).

#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "pin_config.h"
#include "species.h"
#include "dex.h"
#include "pet.h"
#include "sdmon.h"
#include "rtcbat.h"
#include "i18n.h"
#include "audio.h"
#include "blit.h"
#include "power.h"
#include "panelpower.h"
#include "bootbutton.h"
#include "screenlock.h"
#include "touchlock.h"
#include "touchgesture.h"
#include "settingslayout.h"
#include "bedtime.h"
#include "clockprompt.h"
#include "attention.h"
#include "minigames.h"
static_assert(POWER_GAME_AWAKE_MS == GameFlow::IDLE_MS, "a game stays bright until its own idle end");
#include "trainingcapture.h"
#include "app.h"
#include "gameui.h"
#include "scene.h"
#include "screens.h"
#include "serialcmd.h"
#include "uitext.h"
#include <Preferences.h>

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RESET, 0 /*rotation*/, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
// Framebuffer completo en PSRAM: dibujamos todo y hacemos flush() (sin parpadeo)
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);

TouchDrvCST92xx touch;
#define TOUCH_ADDR 0x5A  // CST9217
Pet pet;

// sprite animado de la SD para la especie actual (si existe el archivo)
SdMon mon;          // sprite B/N (respaldo y minijuego si no hay PMD)
PmdMon pmd;         // sprite PMD multi-accion (pantalla principal)
PmdMon evoPmd;      // forma anterior, solo durante el parpadeo de evolucion
int16_t monFor = -2;
bool monShinyFor = false;

PetReact petReact;
PmdMon galleryPmd;  // sprite grande de la vista detalle de la galeria (PMD/TPK2, legal)

// galeria pokedex
bool galleryOpen = false;
bool galleryDirty = false;
int galleryPage = 0;        // 10 paginas de 16
int16_t galleryDetail = 0;  // dex en vista detalle, 0 = rejilla

bool screenOff = false;       // original 1.75: single PWR; 1.75C: triple-press lock
uint8_t userDim = 0;          // nivel de brillo de ajustes (power.h): 0 alto, 1 medio, 2 bajo
bool batAsPercent = false;    // ajustes: false = icono (el de siempre), true = "87%"
// The AXP2101 answered at boot (batBegin()). When it didn't, usbPresent() reports true
// and self-care (#24) has to treat that as unplugged.
bool pmuReady = false;
// Defaults when the NVS keys "off"/"tlk" are absent. The 1.75C turns the screen off at 60 s
// and locks touch 10 s later; the original 1.75 keeps its historical behaviour (never), and
// the user can pick either in settings.
#if BOARD_AMOLED_175C
#define DEFAULT_POWER_OFF_MS 60000UL
#define DEFAULT_TOUCH_LOCK_MS 10000UL
#else
#define DEFAULT_POWER_OFF_MS 0UL
#define DEFAULT_TOUCH_LOCK_MS TOUCH_LOCK_NEVER
#endif
uint32_t powerOffMs = DEFAULT_POWER_OFF_MS;  // 0 = nunca (horario de 90 s / 5 min)
uint32_t touchLockMs = DEFAULT_TOUCH_LOCK_MS;  // 0 = al apagarse; TOUCH_LOCK_NEVER = desactivado
TouchLock autoTouch;
static bool touchReady = false;
// touchBringUp() still has to run: the auto touch lock ended while the pocket lock holds the
// chip asleep, or the last touch.begin() failed and is retried every TOUCH_RETRY_MS (screen on).
// A failed bring-up blocks the loop ~0.5 s (3 tries, 150 ms apart), so a chip that stays dead
// gets TOUCH_RETRY_MAX attempts, then none until the next auto touch unlock.
static bool touchBringUpPending = false;
static bool touchRetrying = false;     // last touchBringUp() failed
static uint32_t touchRetryFrom = 0;    // millis() when it failed
static uint8_t touchFailures = 0;      // failed bring-ups in a row
constexpr uint32_t TOUCH_RETRY_MS = 2000;
constexpr uint8_t TOUCH_RETRY_MAX = 5;
static void touchBringUp();
bool cardOpen = false;        // ficha del bicho (deslizar vertical)
bool kbOpen = false;          // teclado para renombrar al bicho
char nameBuf[12] = "";
uint8_t nameLen = 0;
uint8_t cardPage = 0;         // 0 perfil, 1 stats, 2 medallas, 3 progreso, 4 cuidados (ritmo y hora de dormir)
BedEdge bedEdge;                 // horario de dormir: convierte la hora en eventos dormir/despertar
BedEvent bedPending = BED_NONE;  // evento a la espera de que no haya minijuego ni dialogo abiertos
// Attention calls (attention.h, #9): see attnLoop()
AttnArm attnArm;
uint32_t attnShowUntil = 0;   // a call lights (or brightens) the screen until this millis() (0 = none)
bool screenOffByPwr = false;  // the screen went dark with a PWR press, not the timeout: calls leave it dark
bool clockOpen = false;       // pantalla de ajuste de hora (deslizar abajo)
int clockH = 12, clockM = 0;  // hora en edicion
ClockPrompt clockPrompt;      // asks for the time after a reseed (clockprompt.h, #10)
// The stale flag of bedEdge, kept through soft resets: RTC memory survives a crash,
// watchdog or ESP.restart like the 1.75C soft clock does, and power loss clears both.
RTC_NOINIT_ATTR ClockStaleMark clockStaleMark;

uint32_t feedMenuUntil = 0;   // selector de comida abierto hasta este millis

// las 9 especies con sprite propio en flash (respaldo sin SD): dex -> indice
int flashIdxForDex(int16_t dex) {
  static const int8_t IDX[10] = { -1, 3, 4, 5, 0, 1, 2, 6, 7, 8 };
  return (dex >= 1 && dex <= 9) ? IDX[dex] : -1;
}

// settingslayout.h lays the settings pills out around its own copy of the centre
static_assert(CX == SET_PANEL_C && CY == SET_PANEL_C, "settingslayout.h assumes the panel centre is (CX, CY)");

Btn buttons[4] = {
  { 140, 390, SPR_ICON_FOOD },   // comer
  { 202, 404, SPR_ICON_PLAY },   // jugar
  { 264, 404, SPR_ICON_LIGHT },  // luz
  { 326, 390, SPR_ICON_CLEAN },  // bano
};

// grietas del huevo (pixeles 'k' sobre el sprite)
static const uint8_t CRACK1[][2] = { {15,8},{16,9},{15,10} };
static const uint8_t CRACK2[][2] = { {11,13},{12,14},{11,15},{20,12},{19,13},{20,14} };

bool wasPressed = false;
// eleccion de huevo (ajuste Care "Huevos"): tres huevos en fila. Un toque lo
// levanta y ensena su rareza; un segundo toque en el mismo lo elige.
#define EGGSEL_S 4      // escala: el huevo visible mide 68x92 px
#define EGGSEL_Y 150    // borde superior del sprite de 32x32 escalado
#define EGGSEL_LIFT 16  // cuanto sube el huevo tocado
#define EGGSEL_HIT 60   // medio ancho de la zona tactil de cada huevo
static const int16_t EGGSEL_CX[EGG_CHOICES] = { 93, 233, 373 };
int8_t eggSel = -1;     // huevo tocado una vez (-1 = ninguno); solo UI, no se guarda
// el CST9217 avisa por el pin INT cuando hay datos tactiles; lo usamos para no
// leer el bus I2C mientras el chip esta dormido (esa lectura se colgaba ~1s)
volatile bool gTouchIrq = false;
#if BOARD_AMOLED_175C
static portMUX_TYPE touchIrqMux = portMUX_INITIALIZER_UNLOCKED;
void IRAM_ATTR touchIsr() {
  portENTER_CRITICAL_ISR(&touchIrqMux);
  gTouchIrq = true;
  portEXIT_CRITICAL_ISR(&touchIrqMux);
}
static bool takeTouchIrq() {
  portENTER_CRITICAL(&touchIrqMux);
  bool pending = gTouchIrq;
  gTouchIrq = false;
  portEXIT_CRITICAL(&touchIrqMux);
  return pending;
}
TrainingCapture trainingCapture;
bool trainingCaptureReady = false;
static bool trainingCaptureStarted = false;  // worker task exists (begin() runs once)
static bool trainingNeedsRelease = false;
#else
void IRAM_ATTR touchIsr() { gTouchIrq = true; }
#endif
// The touch ISR goes on and off TP_INT only through these two. The core installs the GPIO ISR
// service on the first attachInterrupt(); a detachInterrupt() before that logs
// "E gpio: GPIO isr service is not installed", which touchBringUp() at boot used to print.
// So a detach runs only while the ISR is attached.
static bool touchIsrAttached = false;
static void attachTouchIsr() {
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), touchIsr, FALLING);
  touchIsrAttached = true;
}
static void detachTouchIsr() {
  if (!touchIsrAttached) return;
  detachInterrupt(digitalPinToInterrupt(TP_INT));
  touchIsrAttached = false;
}
uint32_t lastRender = 0;
uint32_t sdEpoch = 0;  // +1 cada vez que se recarga la SD (las miniaturas cambian)
// proteccion del AMOLED: atenuado por inactividad
uint32_t lastInteract = 0;
uint8_t dimStage = 0;        // 0 despierto, 1 atenuado (90s), 2 casi apagado (5min)
bool swallowGesture = false; // el toque que despierta no acciona nada
uint32_t holdStart = 0;     // pulsacion larga sobre el bicho
uint32_t confirmUntil = 0;  // dialogo "soltar?" activo hasta este millis
uint8_t choiceKind = 0;     // dialogo de decision: 0 ninguno, 1 evolucion, 2 despedida
uint32_t choiceUntil = 0;   // se cierra solo a este millis
TouchGesture touchG;        // gesto en curso y gestos sobre el bicho (touchgesture.h)
bool holdFired = false;

// BOOT controls and small overlay; all UI/pet changes stay on the loop task.
#if BOARD_AMOLED_175C
BootButton bootButton;
ScreenLock screenLock;
static uint8_t pwrEdges = 0;     // short PKEY presses still inside the triple window
static uint32_t pwrEdgeAt = 0;
static bool touchNeedsRelease = false;
#endif
// Both boards: syncPanelPower() sleeps the CO5300 on screen-off on either one.
bool panelAsleep = false;
static uint8_t panelBrightness = 255;  // ultimo brillo enviado al panel (255 = forzar el primero)
#if BOARD_AMOLED_175C
static char controlToast[48] = "";
static uint32_t controlToastUntil = 0;
static bool controlToastVisible = false;
#endif

bool screenIsLocked() {
#if BOARD_AMOLED_175C
  return screenLock.locked();
#else
  return false;
#endif
}

#if BOARD_AMOLED_175C
void showControlToast(const char *text, uint32_t duration) {
  if (screenIsLocked()) return;  // sync/pet/serial feedback cannot wake a pocketed device
  snprintf(controlToast, sizeof(controlToast), "%s", text);
  controlToastUntil = millis() + duration;
  controlToastVisible = true;
  galleryDirty = true;
  screenOff = false;
  wakeUI();
}

// Every renderer flushes through this so hints work on settings/gallery too.
void flushUI() {
#if BOARD_AMOLED_175C
  if (screenLock.blank(millis())) return;  // no QSPI frames after the lock hint
#endif
  bool lockHint = screenIsLocked();
  const char *hint = lockHint ? "Locked" : controlToast;
  if (lockHint || (controlToastVisible && (int32_t)(controlToastUntil - millis()) > 0)) {
    gfx->fillRoundRect(43, 272, 380, 56, 10, RGB565_BLACK);
    gfx->drawRoundRect(43, 272, 380, 56, 10, RGB565_WHITE);
    gfx->setTextColor(RGB565_WHITE);
    setSize(2);
    setCur(centerX(hint, 2), 284);
    printT(hint);
  }
  gfx->flush();
#if BOARD_AMOLED_175C
  screenLock.frameShown(millis());
#endif
}
#else
// Original board: no overlays, the flush is exactly the old one.
void flushUI() { gfx->flush(); }
#endif

#if BOARD_AMOLED_175C
// BOOT double / long press. Free in the board port; a build with time sync
// hooks these up to resync and Bluetooth pairing.
void bootDoublePress() { Serial.println("BOOT double: no action in this build"); }
void bootLongPress() { Serial.println("BOOT long: no action in this build"); }
#endif

void pollBootButton() {
#if BOARD_AMOLED_175C
  if (screenLock.locked()) return;
  BootButton::Action action = bootButton.update(digitalRead(BOOT_BUTTON_PIN) == LOW, millis());
  if (bootButton.active()) { screenOff = false; wakeUI(); }
  if (action == BootButton::SINGLE) {
    audioSetEnabled(!audioEnabled());  // existing NVS tamapoke/snd setting
    Serial.printf("BOOT single: sound %s (saved)\n", audioEnabled() ? "on" : "muted");
    showControlToast(audioEnabled() ? "Sound on" : "Muted");
    if (audioEnabled()) sfxPlay(SFX_TAP);
  } else if (action == BootButton::DOUBLE) {
    bootDoublePress();
  } else if (action == BootButton::LONG) {
    bootLongPress();
  }
#endif
}

#if BOARD_AMOLED_175C
static void pwrToggleLock() {
  bool locked = screenLock.toggle();
  // Discard both a partly-entered BOOT gesture and a finger already on the glass.
  bootButton.begin(digitalRead(BOOT_BUTTON_PIN) == LOW, millis());
  wasPressed = false;
  trainingCapture.reset(true);  // discard hits/exit holds across a lock transition
  trainingNeedsRelease = false;
  holdStart = 0;
  holdFired = false;
  swallowGesture = true;
  gTouchIrq = false;
  controlToastVisible = false;
  if (locked) {
    detachTouchIsr();
    screenOff = false;  // render only the 600 ms hint, then blank via powerCtx()
    galleryDirty = true;
    lastInteract = millis();
    Serial.println("PWR triple: Locked");
  } else {
    // Wake is syncPanelPower(): 0x11, 120 ms, 0x29, then 0x51.
    // GFX displayOn() sends 0x29 before 0x11 and leaves the panel dark.
    screenOff = false;
    if (touchBringUpPending) {  // the chip is still asleep from the auto touch lock
      touchBringUpPending = false;
      touchBringUp();
    } else {
      attachTouchIsr();
    }
    touchNeedsRelease = true;
    gTouchIrq = true;  // one initial sample: swallow any finger held through unlock
    showControlToast("Unlocked");
    Serial.println("PWR triple: Unlocked");
  }
}
#endif

// Poll every loop (not the old 250 ms PMU poll, which misses quick presses).
void pollPwrButton() {
#if BOARD_AMOLED_175C
  // PWR is the AXP2101 PKEY. GPIO3 does not change when the key is pressed.
  // The IRQ is one latched bit, so two releases between two polls merge into one.
  // Polled every 40 ms while the loop runs. Once a press sequence is pending
  // (pwrEdges), idleWait() keeps its pause to 10 ms and skips light sleep, so the
  // 2nd and 3rd press are seen at that rate even with the screen off or locked.
  // The first press can wait up to one idle pause (100 ms screen off, 200 ms light
  // sleep) before it is read; only two presses released inside that window merge.
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  bool event = false;
  if (now - lastPoll >= 40) {
    lastPoll = now;
    event = pwrShortPressed();
  }
  PwrEdgeAction act = pwrShortAction(pwrEdges, pwrEdgeAt, event, now, PwrButton::GAP_MS);
  if (act == PWR_EDGE_LOCK) pwrToggleLock();
  else if (act == PWR_EDGE_TOGGLE && !screenLock.locked()) {
    Serial.println("PWR short");
    screenOff = !screenOff;
    screenOffByPwr = screenOff;
    attnShowUntil = 0;  // PWR wins over a call's window
    if (!screenOff) wakeUI();
  }
#else
  // Keep the original board's PEK short-press behaviour unchanged.
  static uint32_t lastPwr = 0;
  uint32_t now = millis();
  if (now - lastPwr > 250) {
    lastPwr = now;
    if (pwrShortPressed()) {
      screenOff = !screenOff;
      screenOffByPwr = screenOff;
      attnShowUntil = 0;  // PWR wins over a call's window
      if (!screenOff) wakeUI();
    }
  }
#endif
}

static void runPanelProgram(const PanelStep *s, int n) {
  uint32_t since = millis() - lastRender;
  if (since < 30) delay(30 - since);
  for (int i = 0; i < n; i++) {
    if (s[i].kind == PANEL_WAIT) {
#if TAMAPOKE_CHG_DEBUG
      Serial.printf("panel wait %u\n", s[i].waitMs);
#endif
      delay(s[i].waitMs);
    } else if (s[i].kind == PANEL_CMD) {
#if TAMAPOKE_CHG_DEBUG
      Serial.printf("panel cmd 0x%02X\n", s[i].a);
#endif
      bus->sendCommand(s[i].a);
    } else {
#if TAMAPOKE_CHG_DEBUG
      Serial.printf("panel reg 0x%02X=%u\n", s[i].a, s[i].b);
#endif
      bus->beginWrite();
      bus->writeC8D8(s[i].a, s[i].b);
      bus->endWrite();
    }
  }
}

static void touchBringUp() {
  detachTouchIsr();
  touch.setPins(TP_RESET, TP_INT);
  touchReady = false;
  for (int i = 0; i < 3 && !touchReady; i++) {
    touchReady = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    if (!touchReady) delay(150);
  }
#if BOARD_AMOLED_175C
  trainingCaptureReady = touchReady && trainingCaptureStarted;
#endif
  if (!touchReady) {
    Serial.println("CST9217 no detectado");
    // SensorLib leaves TP_INT as plain INPUT: a floating pin that reads LOW would keep the
    // loop awake and make handleTouch probe a dead chip. Pull it up and try again later.
    pinMode(TP_INT, INPUT_PULLUP);
    if (touchFailures < TOUCH_RETRY_MAX) touchFailures++;
    touchBringUpPending = touchFailures < TOUCH_RETRY_MAX;
    touchRetrying = true;
    touchRetryFrom = millis();
    return;
  }
  touchRetrying = false;
  touchFailures = 0;
  touch.reset();
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
  touch.setMirrorXY(true, true);
  attachTouchIsr();
}

// DCDC1 is VCC3V3 for the ESP32 and the panel. Do not switch it off.
static void syncPanelPower(bool wantSleep, uint8_t wakeBrightness) {
  if (!panelSleepEdge(panelAsleep, wantSleep)) return;
  PanelStep steps[6];
  int n = wantSleep ? panelSleepProgram(steps, 6) : panelWakeProgram(steps, 6, wakeBrightness);
  runPanelProgram(steps, n);
  panelBrightness = wantSleep ? 0 : wakeBrightness;
}

void setup() {
  Serial.setRxBufferSize(8192);  // la transferencia a SD llega en bloques de 2 KB
  Serial.begin(115200);
  // CRITICO: sin esto, Serial.print BLOQUEA el juego cuando no hay un
  // monitor serie abierto en el host (el bufer TX del USB CDC se llena
  // y nadie lo vacia) -> con timeout 0 los mensajes se descartan
  Serial.setTxTimeoutMs(0);
  Serial.printf("TamaPoke fw v%s (%s)\n", FW_VERSION, BOARD_NAME);
  loadLang();  // idioma guardado (ES por defecto)
  Wire.begin(IIC_SDA, IIC_SCL);
#if BOARD_AMOLED_175C
  // QMI8658 is on VCC3V3 and is not used by this firmware. CTRL7=0 disables its sensors; the
  // bigger saving is stopping the internal high-speed oscillator via CTRL1. SensorLib's
  // powerDown() sets CTRL1 bit 1; the previous code here set bit 0. No QMI8658 datasheet is
  // available locally to say which is the disable bit, so both are set (CTRL1=0x03).
  // WHO_AM_I is 0x05.
  const uint8_t qmiAddrs[] = { 0x6B, 0x6A };
  for (uint8_t a : qmiAddrs) {
    Wire.beginTransmission(a);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) continue;
    if (Wire.requestFrom(a, (uint8_t)1) != 1 || Wire.read() != 0x05) continue;
    Wire.beginTransmission(a); Wire.write(0x08); Wire.write(0x00); Wire.endTransmission();
    Wire.beginTransmission(a); Wire.write(0x02); Wire.write(0x03); Wire.endTransmission();
    Serial.printf("QMI8658 0x%02X power-down CTRL7=0 CTRL1=0x03\n", a);
    break;
  }
#endif
  // CST9217 (tactil), AXP2101 (PMU) y PCF85063 (RTC) comparten este bus I2C.
  // Red de seguridad para PMU/RTC (SensorLib NO respeta este timeout en el
  // tactil; el cuelgue del tactil dormido se resuelve gateando por INT, ver
  // handleTouch).
  Wire.setTimeOut(50);

  // CRITICO: encender la alimentacion del panel (BLDO1=OLED VDD 3.3V) ANTES de
  // inicializar el display. Si el PMU se reseteo (drenaje total), este rail
  // queda OFF y la pantalla se ve negra aunque el resto de la placa funcione.
  pmuEnablePanel();

  // QSPI a 80MHz (por defecto 40): el flush del framebuffer es el cuello de
  // botella del fps (~56ms a 40MHz). Si el panel mostrara basura, bajar a 40M.
  if (!gfx->begin(80000000)) Serial.println("gfx->begin() fallo");
  {  // nivel de brillo guardado (ajustes)
    Preferences p;
    p.begin("tamapoke", true);
    userDim = p.getUChar("bri", 0) % BRIGHT_LEVELS;
    batAsPercent = p.getBool("bpct", false);
    if (p.isKey("off")) {  // absent key keeps the per-board default above
      uint8_t offSel = p.getUChar("off", 0);  // 0 = 60 s, 1 = 30 s, 2 = 15 s, 3 = nunca
      powerOffMs = powerOffFromSel(offSel);
    }
    if (p.isKey("tlk"))
      touchLockMs = touchLockFromSel(p.getUChar("tlk", 0));  // 0 = 10 s, 1 = 30 s, 2 = 60 s, 3 = 0 s, 4 = off
    p.end();
  }
  autoTouch.delayMs = touchLockMs;
  if (powerOffMs)
    Serial.printf("screen-off %lu s (USB does not disable it; dims at half)\n",
                  (unsigned long)(powerOffMs / 1000));
  else
    Serial.println("screen-off never (USB does not disable it)");
  if (touchLockMs == TOUCH_LOCK_NEVER) Serial.println("touch-lock off");
  else Serial.printf("touch-lock %lu s after screen off\n", (unsigned long)(touchLockMs / 1000));
  panel->setBrightness(180);
  applyLangFont();  // fuente del idioma guardado (clasica salvo CJK)

  // INT activo-bajo: salta cuando hay datos. Gatea las lecturas I2C (ver loop)
  touchBringUp();

  pet.begin();
  sdBegin();
  thumbs.load();

  // reloj real: aplica el tiempo que estuvo apagado
  rtcBegin();
  pmuReady = batBegin();
  pwrSetup();
  uint32_t e = rtcEpoch();
  bool reseeded = (e == 0);
  if (reseeded) {
    // Sin pila de respaldo, el PCF85063 pierde la hora al cortar la
    // alimentacion. Sembrar siempre la fecha fija haria RETROCEDER el tiempo de
    // juego respecto a lo ya guardado, y eso rompe la racha, la edad y la hora
    // de la escena. Se siembra con la ultima hora vista, si es posterior, para
    // que el tiempo no vaya nunca hacia atras. Los minutos apagado se pierden
    // igual (sin RTC no hay forma de saberlos), pero nada se descuadra.
    uint32_t seed = 1767225600UL;             // RTC virgen de verdad
    uint32_t seen = pet.savedEpoch();         // usa la NVS que pet ya tiene abierta
    if (seen > seed) seed = seen;
    rtcSetEpoch(seed);
    e = rtcEpoch();
    Serial.printf("RTC sin hora: sembrado en %u%s\n", seed,
                  seen > 1767225600UL ? " (desde la ultima hora guardada)" : "");
  }
  // A reseeded clock has the last saved time, not the real one: bedtime pauses until
  // somebody sets it (clock screen or TIME), or it would sleep and wake at odd hours,
  // and the clock screen asks for the time (clockprompt.h). A soft reset keeps a clock
  // that was never set since its reseed just as wrong, so the RTC-memory mark carries
  // the flag over.
  if (clockStaleAtBoot(reseeded, clockStaleMark)) {
    bedEdge.markStale();
    clockMarkStale(clockStaleMark, true);
    if (!reseeded) Serial.println("clock: not set since its reseed (kept through a reset)");
  }
  clockPrompt.boot(e != 0);  // e == 0 here: no PCF85063, the clock cannot be set
  pet.syncClock(e);
  // Sin RTC (la 1.75 original sin PCF85063) e == 0, y el minuto 0 cae dentro de toda ventana:
  // muestrear aqui dormiria al bicho en cada arranque y el bucle (que tampoco muestrea con
  // e == 0) nunca lo despertaria.
  if (e) bedSample(e);  // arrancar dentro de la ventana de dormir = dormir una vez

  audioBegin();  // ES8311 + I2S + amplificador (suena un jingle de arranque)

#if BOARD_AMOLED_175C
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  bootButton.begin(digitalRead(BOOT_BUTTON_PIN) == LOW, millis());
  Serial.println("PWR: AXP2101 PKEY short toggles the screen, triple within 350ms locks; hold 4s=hardware off");
  Serial.println("BOOT GPIO0 ready: debounce=25ms single=mute");
  trainingCaptureStarted = touchReady && trainingCapture.begin(readTrainingTouch);
  trainingCaptureReady = trainingCaptureStarted && touchReady;
  if (!trainingCaptureReady) Serial.println("Strength training: touch capture unavailable");
#endif
  lastInteract = millis();
}

// carga/descarga el sprite de SD cuando cambia la especie
void ensureMon() {
  if (pet.speciesId == monFor && monShinyFor == pet.shiny && !sdDirty) return;
  // los ficheros recien recibidos pueden incluir un thumbs.bin nuevo, que se
  // cargaba solo en setup(): sin esto no se veia en la galeria hasta reiniciar
  if (sdDirty) { thumbs.unload(); thumbs.load(); sdEpoch++; }
  sdDirty = false;
  monFor = pet.speciesId;
  monShinyFor = pet.shiny;
  mon.unload();
  pmd.unload();
  beh.x = beh.targetX = 233;
  beh.mode = 0;
  beh.until = 0;
  beh.fast = false;
  petReact.kind = REACT_NONE;
  if (pet.speciesId >= 1 && pet.speciesId <= DEX_COUNT) {
    pmd.load(pet.speciesId, pet.shiny);          // principal: PMD
    if (!pmd.loaded) mon.load(pet.speciesId, pet.shiny);  // respaldo: B/N
  }
}

// ---------- horario de dormir (bedtime.h) ----------
// Se muestrea la hora (minuto del dia, la RTC guarda hora local) cada 30 s desde el
// bucle, donde ya se lee la RTC: sin I2C ni despertares extra. Solo los BORDES de la
// ventana generan eventos, asi que despertar al bicho de madrugada se respeta hasta
// la proxima hora de dormir.
void bedSample(uint32_t epoch) {
  BedEvent ev = bedEdge.update(pet.bedPreset, bedMinuteOfDay(epoch));
  if (ev != BED_NONE) bedPending = ev;  // un evento nuevo sustituye a uno aun pendiente
}

// Alguien puso la hora (pantalla del reloj o TIME): si estaba obsoleta (sembrada tras
// quedarse sin corriente) el horario se reanuda como si fuera un arranque, y el reloj
// deja de pedir la hora (clockprompt.h lee bedEdge.stale()). La hora debe haber quedado
// puesta de verdad (TIME con un valor fuera de rango no cambia el reloj de software de
// la 1.75C, y sin PCF85063 la 1.75 original sigue sin hora).
void bedClockSet(uint32_t set) {
  uint32_t now = rtcEpoch();
  if (!clockSetTook(set, now)) return;
  clockMarkStale(clockStaleMark, false);  // a soft reset now keeps a trusted clock
  if (!bedEdge.stale()) return;
  bedEdge.clockSet();
  bedPending = BED_NONE;
  bedSample(now);
}

// Con un minijuego, el saco, un dialogo, el teclado, el reloj, el bano, comer o la
// animacion de evolucion abiertos NO se interrumpe: el evento queda pendiente y se
// aplica en cuanto se cierran. Si el bicho no puede (huevo, ceremonia, eleccion de
// inicial) pet.autoSleep/autoWake no hacen nada y el evento se descarta.
// El minijuego, el saco, el teclado y el reloj no tienen caducidad con la pantalla
// apagada (el juego y el saco solo avanzan y terminan al dibujarse): solo bloquean
// mientras la pantalla esta encendida y sin bloquear. Si se quedaron abiertos y el
// panel se apago (PWR o bloqueo de bolsillo) el horario se aplica igual: ni renombrar
// (solo toca el nick) ni ajustar la hora (el siguiente muestreo trata el salto como un
// borde mas) dependen de pet.sleeping, y al dormir el juego y el saco se abandonan sin
// premio (bedApply). El bano se compara con timeLeft, no en bruto: bathUntil solo se
// pone a 0 en drawBath(), que no corre con la pantalla apagada.
//
// careBusy() is the part that does not depend on the screen: bedBusy() and the attention
// calls (attnLoop) both wait while it holds, and the pet gestures are off (petGestureGate).
static bool careBusy() {
  return timeLeft(bathUntil) || pet.evolving() || pet.eating() || timeLeft(confirmUntil) || timeLeft(feedMenuUntil) ||
         (choiceKind && timeLeft(choiceUntil));
}

static bool bedBusy() {
  bool dark = screenOff || screenIsLocked();
  return (!dark && (gameOpen || sackOpen || kbOpen || clockOpen)) || careBusy();
}

// What holds back the clock screen that opens by itself after a reseed (clockprompt.h):
// the starter or egg choice, a ceremony, evolution, a dialog or another screen. The
// timed dialogs are compared with timeLeft(), like bedBusy(), because render() only
// zeroes them when it draws.
static ClockPromptHold clockPromptHold() {
  ClockPromptHold h;
  h.choosing = pet.awaitingStarter() || pet.awaitingEgg();
  h.ceremony = pet.ceremony != CER_NONE;
  h.evolving = pet.evolving();
  h.dialog = timeLeft(confirmUntil) || timeLeft(feedMenuUntil) || (choiceKind && timeLeft(choiceUntil));
  h.otherScreen = galleryOpen || gameOpen || sackOpen || kbOpen || clockOpen || cardOpen;
  return h;
}

void bedApply() {
  if (bedPending == BED_NONE || bedBusy()) return;
  BedEvent ev = bedPending;
  bedPending = BED_NONE;
  if (ev == BED_SLEEP) {
    // Un juego o el saco abiertos aqui solo pueden estar a oscuras (bedBusy): se abandonan
    // sin premio, como al mantener su boton de salir. playGame() y trainStrength() se pagan al
    // terminar la partida (que ya no llegara) y no miran pet.sleeping.
    gameOpen = false;
    if (sackOpen) {
#if BOARD_AMOLED_175C
      leaveSack();
#else
      sackOpen = false;
#endif
    }
    pet.autoSleep();  // sin sonido: es de noche
  } else if (pet.autoWake()) {
    audioSetSleeping(false);  // el amplificador se enciende antes de encolar el sonido
    sfxPlay(SFX_HEART);       // un solo sonido suave (sfxPlay calla si el sonido esta apagado)
  }
}

// ---------- attention calls (attention.h, #9) ----------
// The needs are sampled on every loop pass, also with the screen off: the loop never stops
// (it light-sleeps at most POWER_SLEEP_MS between passes), so a need that drops in
// Pet::tick() is seen on the same pass, with no extra wakeup.
//
// A call opens a "showing" window of ATTN_PEEK_MS (attnShowUntil) and does NOT count as a
// touch: lastInteract is left alone (self-care while charging, #24, reads idleSince(now,
// lastInteract) at the top of loop() and waits for 10 untouched minutes).
// powerCtx() reads the window as "just touched" (idle 0, panel on), so the screen is lit
// at full brightness, drawn at 10 fps, with no auto-off, no dimming and no light sleep for
// its length; when it ends the real idle applies again (dim, or off, or dark).
//  - A screen that timed out (a "peek"): screenOff stays true, so the auto touch lock, if
//    on, keeps holding (TouchLock only sees screenOff). PWR and BOOT turn the screen on for
//    real (the existing path, which unlocks touch); with the touch lock off, or not engaged
//    yet, a touch does, and that first touch is swallowed.
//  - A lit screen (dimmed, or almost off): it brightens for the window.
// PWR off, a touch, BOOT or anything else that calls wakeUI() ends the window.

static bool attnShowing(uint32_t now) {
  return attnShowUntil && (int32_t)(attnShowUntil - now) > 0;
}

// back to the pet screen, as if each had been left by hand without finishing (no rename,
// no new time): they were left open when the screen timed out
static void attnCloseViews() {
  if (galleryOpen) {
    galleryOpen = false;
    galleryDetail = 0;
    galleryPmd.unload();
  }
  kbOpen = false;
  clockOpen = false;
  cardOpen = false;
}

void attnLoop(uint32_t now) {
  // the window ended, or the screen came on for real (PWR, BOOT, a touch): forget the
  // deadline, so an old one can't read as "showing" once millis() wraps
  if (attnShowUntil && !attnShowing(now)) attnShowUntil = 0;
  bool away = pet.sleeping || pet.isEgg() || pet.ceremony || pet.awaitingStarter() || pet.awaitingEgg();
  uint8_t calls = attnArm.update(attnLevels(pet.fullness, pet.joy, pet.energy, pet.hygiene), away);
  if (!calls) return;

  AttnScene s;
  s.busy = careBusy();
  s.playing = gameOpen || sackOpen;
  s.browsing = kbOpen || clockOpen || galleryOpen || cardOpen;
  s.screen = attnScreen(screenIsLocked(), screenOff, screenOffByPwr, dimStageNow(powerCtx(now)) == DIM_OFF);
  // the loop refreshes lastSeenEpoch from the clock every 30 s: no I2C read here
  uint32_t e = pet.lastSeenEpoch;
  s.quiet = attnQuiet(pet.bedPreset, e && !bedEdge.stale(), bedMinuteOfDay(e));
  AttnAction a = attnDecide(s);
  if (a.wait) return;  // asked again next pass; dropped if the need recovers or the pet goes away

  attnArm.done();
  if (a.closeViews) attnCloseViews();
  if (a.peek || a.brighten) {
    attnShowUntil = now + ATTN_PEEK_MS;
    if (!attnShowUntil) attnShowUntil = 1;  // 0 means "no window"
    // no wakeUI(): not a touch. The window makes powerCtx() full brightness and 10 fps, so
    // the first lit frame is drawn within 100 ms (lastRender is old: frozen while the screen
    // was off, at most 1 s ago on a dimmed one).
    // The amplifier on now, as the window will keep it (amplifierWanted): otherwise sfxPlay()
    // would take its 4 s idle hold, audioBusy() would stay true for those 4 s and the loop
    // would keep the CPU at 80/160 MHz while drawing the lit frames.
    audioSetIdle(false);
  }
  // a dark PWR-off call still uses sfxPlay()'s 4 s hold, which is what it is for
  if (a.chirp) sfxPlay(SFX_CALL);  // silent anyway if the sound is off

  static const char *const NAMES[ATTN_NEEDS] = { "food", "hygiene", "energy", "joy" };
  char what[32] = "";  // "food+hygiene+energy+joy" at most
  int n = 0;
  for (uint8_t i = 0; i < ATTN_NEEDS; i++)
    if (calls & (1u << i)) n += snprintf(what + n, sizeof what - n, "%s%s", n ? "+" : "", NAMES[i]);
  Serial.printf("call %s: %s%s\n", what, a.peek ? "peek" : a.brighten ? "brighten" : a.chirp ? "dark" : "silent",
                a.chirp ? (audioEnabled() ? " chirp" : " (muted)") : s.quiet ? " (quiet)" : " (locked)");
}

void loop() {
  uint32_t now = millis();
  // self-care while charging (#24): the cached USB reading, no extra I2C
  pet.setCharger(pmuReady, usbPresent(), idleSince(now, lastInteract));
  pet.update(now);

  // el amplificador sigue al estado de sueno (la llamada sale sola si no cambia).
  // Aqui cubre todas las vias: el boton de luz, el sueno nocturno y el que llega
  // aplicado desde la progresion offline.
  audioSetSleeping(pet.sleeping);

  // avisa con un sonido cuando el bicho pasa a estar listo para evolucionar
  // (incluye el caso de cumplir al despertar). canEvolveNow es false durmiendo.
  // At most once per level for each form (EvoChime, pet.h): self-care on the
  // charger keeps bringing the button back.
  static EvoChime evoChime;
  if (evoChime.update(pet.wantEvolveButton(), pet.speciesId, pet.level())) sfxPlay(SFX_MEDAL);
  // aviso sombrio cuando el bicho esta a punto de escaparse por abandono
  static bool wasRunReady = false;
  bool runReady = pet.canRunawayNow();
  if (runReady && !wasRunReady) sfxPlay(SFX_DENY);
  wasRunReady = runReady;

#if BOARD_AMOLED_175C
  pollPwrButton();  // lock input before handling touch/BOOT in this pass
#endif
  handleTouch();
  handleSerial();
#if BOARD_AMOLED_175C
  pollBootButton();
#endif
  ensureMon();
#if !BOARD_AMOLED_175C
  pollPwrButton();  // original board: same PEK short press as before
#endif

  // Apagado por inactividad en cualquier pantalla, tambien el reloj y la galeria
  // (son estaticas: fps 0 no significa que el temporizador este parado). El USB
  // no lo desactiva: solo impide el light sleep.
  if (!screenOff && autoScreenOff(powerCtx(now)) && !pet.evolving() && !pet.ceremony) {
    screenOff = true;
    screenOffByPwr = false;
    Serial.printf("screen off: idle %lu ms, timeout %lu ms, usb=%d\n",
                  (unsigned long)idleSince(now, lastInteract),
                  (unsigned long)powerOffMs, usbPresent() ? 1 : 0);
  }
  // after the auto-off above (the pass where idle reaches the delay sees a timed-out
  // screen, not an almost-off lit one) and before the panel decisions below, so a call's
  // window lights the panel in this same pass
  attnLoop(now);
  int tlockEdge = autoTouch.update(screenOff, now);
  if (tlockEdge > 0) {
    Serial.println("touch lock");
    if (touchReady) {
      touch.sleep();  // lowest-power mode; PWR wake re-inits the chip
      // sleep() puts TP_INT in open drain: a stray falling edge would only wake the loop.
      detachTouchIsr();  // touchBringUp() re-attaches it
    }
  } else if (tlockEdge < 0) {
    Serial.println("touch unlock");
    touchBringUpPending = true;
    touchRetrying = false;  // first attempt right away
    touchFailures = 0;      // and a fresh set of retries
  }
  // Only the auto touch lock puts the CST9217 to sleep; the pocket lock just detaches the ISR.
  // If the auto lock ended while the pocket lock was on, the pocket unlock brings the chip up.
  // A failed attempt is retried at most every TOUCH_RETRY_MS and only with the screen on.
  if (touchBringUpPending && !screenIsLocked() &&
      (!touchRetrying || (!screenOff && (uint32_t)(millis() - touchRetryFrom) >= TOUCH_RETRY_MS))) {
    touchBringUpPending = false;
    touchBringUp();
  }
  bool attnShow = attnShowing(now);  // once per pass: the panel sleep and the autosave must agree
  bool wantSleep = screenOff && !attnShow;
#if BOARD_AMOLED_175C
  wantSleep = wantSleep || screenLock.blank(millis());
#endif
  uint8_t wakeBri = wantSleep ? 0 : brightnessFor(powerCtx(now));
  syncPanelPower(wantSleep, wakeBri);
  now = millis();  // the CO5300 sequence waits 120 ms per command
  if (!panelAsleep) updateBrightness(now);

  // bateria al limite seguro (sin USB): se guarda la partida y se apaga el PMU antes de que la
  // celda baje mas; el RTC conserva la hora
  if (batShouldShutdown() && !usbPresent()) {
    pet.flushSave();
    panel->setBrightness(0);
    batPowerOff();
  }

  // vuelca el autoguardado periodico SOLO con la pantalla atenuada/apagada o
  // durmiendo: la escritura a NVS congela ~1s ambos cores (caché de flash off),
  // y aqui no hay animacion que se corte ni dedo esperando respuesta. Con 90s
  // de inactividad la pantalla ya atenua, asi que se vuelca enseguida; el uso
  // activo persiste igual por los guardados de cada accion (comer/jugar/...).
  bool lockBlank = false;
#if BOARD_AMOLED_175C
  lockBlank = screenLock.blank(millis());
#endif
  // (not while a call's window lights the panel: the freeze would show)
  if (pet.savePending() && ((screenOff && !attnShow) || lockBlank || dimStage >= 1 || pet.sleeping)) {
    pet.flushSave();
  }

  // anota la hora real cada 30 s (se persiste en cada save del juego)
  static uint32_t lastClock = 0;
  if (now - lastClock > 30000) {
    lastClock = now;
    uint32_t e = rtcEpoch();
    if (e) { pet.lastSeenEpoch = e; bedSample(e); }
  }
  bedApply();

  // Clock reseeded after a power-off: the clock screen opens by itself once, as soon as
  // nothing more important is on screen (clockprompt.h). A dark screen does not hold it:
  // it is there when the screen wakes.
  if (clockPrompt.due(bedEdge.stale(), clockPromptHold())) {
    openClock();
    Serial.println("clock: asking for the time (reseeded, not set yet)");
  }

  // latido de salud cada 5 min (para el soak test; se descarta si no hay monitor)
  static uint32_t lastHealth = 0;
  uint32_t healthEvery = TAMAPOKE_CHG_DEBUG ? 60000UL : 300000UL;
  if (now - lastHealth > healthEvery) {
    lastHealth = now;
#if TAMAPOKE_CHG_DEBUG
    char chg[220];
    batFormatHealth(chg, sizeof chg);
#endif
#if BOARD_AMOLED_175C
    Serial.printf("HEALTH up=%lus heap=%u min=%u bat=%d%% mv=%d chg=%d usb=%d dim=%u off=%d locked=%d tlock=%d"
#if TAMAPOKE_CHG_DEBUG
                  " cpu=%lu %s"
#endif
                  "\n",
                  (unsigned long)(now / 1000), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  batPercent(), batMillivolts(), batCharging() ? 1 : 0,
                  usbPresent() ? 1 : 0, dimStage, screenOff ? 1 : 0, screenIsLocked() ? 1 : 0,
                  autoTouch.active ? 1 : 0
#if TAMAPOKE_CHG_DEBUG
                  , (unsigned long)getCpuFrequencyMhz(), chg
#endif
                  );
#else
    Serial.printf("HEALTH up=%lus heap=%u min=%u bat=%d%% mv=%d chg=%d usb=%d dim=%u off=%d tlock=%d"
#if TAMAPOKE_CHG_DEBUG
                  " cpu=%lu %s"
#endif
                  "\n",
                  (unsigned long)(now / 1000), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  batPercent(), batMillivolts(), batCharging() ? 1 : 0,
                  usbPresent() ? 1 : 0, dimStage, screenOff ? 1 : 0, autoTouch.active ? 1 : 0
#if TAMAPOKE_CHG_DEBUG
                  , (unsigned long)getCpuFrequencyMhz(), chg
#endif
                  );
#endif
#if TAMAPOKE_CHG_DEBUG
    // vTaskGetRunTimeStats() writes ~40 bytes per task with no bound: size for the task count.
    static char tasks[1024];
    if (uxTaskGetNumberOfTasks() * 64 < sizeof tasks) vTaskGetRunTimeStats(tasks);
    else strcpy(tasks, "(task table too large)");
    Serial.println(tasks);
#endif
  }

  // Ritmo de dibujo (power.h): 10 fps con el bicho en uso, menos cuanto mas rato sin tocar,
  // 85 ms en juego/saco (margen seguro para que el redibujado no pise el envio DMA del frame
  // anterior: a 40-65 ms solapaba y causaba flashes negros).
  // Con la pantalla apagada no se dibuja: a brillo 0 no se ve nada, pero el redibujado y el
  // flush DMA del framebuffer del lienzo seguian corriendo a 10 fps, y medido son 70 ms de
  // trabajo por cada 100 (46 de ellos solo el volcado). Cada render repinta la escena
  // entera, asi que no queda nada a medias; y al quedarse lastRender congelado, el primer
  // frame tras despertar sale en el acto.
  PowerCtx pc = powerCtx(millis());
  audioSetIdle(!amplifierWanted(pc));  // el amplificador consume en reposo
  batSetPollMs(powerCacheMs(pc));
  uint32_t interval = frameIntervalMs(pc);
  bool drewFrame = false;
  if (interval && now - lastRender >= interval) {
    lastRender = now;
    drewFrame = render();
#if BOARD_AMOLED_175C
    // renderSack's capture worker has joined: only the loop changes hits/UI.
    if (sackOpen && !screenIsLocked()) drainTrainingTouch();
#endif
  }
  // cede la CPU en los pases que no dibujan: sin esto el loop gira a pleno rendimiento
  // entre frames; 1 ms no se nota en el tactil (se lee cada 20 ms) y el frame no se retrasa
  // bajar la CPU en reposo. No mientras suena, por prudencia: el I2S del S3 usa PLL_F160M por
  // defecto (clk_tree_defs.h, ESP_I2S), no la CPU.
  if (!audioBusy()) {
    uint32_t want = cpuMhzFor(pc);
    static uint32_t cpuNow = 240;
    if (want != cpuNow) {
      setCpuFrequencyMhz(want);
      cpuNow = want;
    }
  }
  if (!drewFrame) idleWait(pc);
}

// estado de consumo actual (ver power.h)
PowerCtx powerCtx(uint32_t now) {
  PowerCtx c;
  // a call's window (attnShowUntil) reads as "just touched" without touching lastInteract
  bool show = attnShowing(now);
  c.idleMs = show ? 0 : idleSince(now, lastInteract);
  c.screenOff = screenOff && !show;
#if BOARD_AMOLED_175C
  c.screenOff = c.screenOff || screenLock.blank(now);
#endif
  c.sleeping = pet.sleeping;
  c.gameOpen = gameOpen || sackOpen;
  c.usb = usbPresent();
  c.charging = batCharging();
  c.batPct = batPercent();
  c.userDim = userDim;
  c.offMs = powerOffMs;
  return c;
}

// el usuario ha interactuado: vuelve el ritmo normal de dibujo (sin esperar al proximo
// frame de un ritmo lento) y el amplificador se enciende antes de que suene nada
void wakeUI() {
  if (screenIsLocked()) return;
  uint32_t now = millis();
  uint32_t iv = frameIntervalMs(powerCtx(now));
  bool slow = !iv || iv > 100;  // pantalla apagada o ritmo lento: se dibuja ya, sin esperar al frame
  lastInteract = now;
  attnShowUntil = 0;  // a real touch ends a call's window (idle is 0 anyway)
  // el frame forzado respeta los 85 ms entre redibujados (si no, solapa el DMA del anterior)
  if (slow) lastRender = (now - lastRender >= 85) ? 0 : lastRender - 15;
  audioSetIdle(false);
}

// brillo segun sueno + inactividad (proteccion del AMOLED) y bateria
void updateBrightness(uint32_t now) {
#if BOARD_AMOLED_175C
  if (panelAsleep) return;  // 0x51 during SLPIN does not wake the CO5300
#endif
  // los eventos visibles despiertan la pantalla solos
  if (!screenOff && (pet.evolving() || pet.ceremony || pet.eating() || pet.showHeart())) {
    if (dimStageNow(powerCtx(now)) != DIM_AWAKE) wakeUI();
    else lastInteract = now;
  }
  PowerCtx c = powerCtx(now);
  dimStage = dimStageNow(c);
  uint8_t target = brightnessFor(c);
  if (target != panelBrightness) {
    panelBrightness = target;
    panel->setBrightness(target);
  }
}

// Pausa de un pase del bucle que no ha dibujado. Con la pantalla apagada, o encendida pero casi
// apagada (5 min sin tocar), y sin USB se duerme la CPU (light sleep) hasta el proximo frame o
// 200 ms: despierta por el temporizador o por un toque (el INT del tactil baja). Con USB no, para
// no perder el puerto serie de la tarjeta SD: se mira sin cache, por si se acaba de enchufar.
void idleWait(PowerCtx c) {
  // no sleep during a gesture/radio operation or while QSPI DMA is in flight
  bool locked = screenIsLocked();
  // touch.sleep() leaves TP_INT open-drain (input buffer off, reads LOW), so neither
  // the pocket lock nor the auto touch lock may poll or arm it (touchlock.h).
  bool touchWake = touchWakeEnabled(locked, autoTouch.active, touchReady);
  bool controlsBusy = false;
#if BOARD_AMOLED_175C
  // Stay awake while a short press is still waiting to become a single or a triple.
  // The PKEY IRQ latches across light sleep; GPIO3 is not the key.
  controlsBusy = pwrEdges || (!locked && bootButton.active());
#endif
  if (!controlsBusy && lightSleepAllowed(c) && millis() - lastRender >= 100 && !audioBusy()) {
    c.usb = usbPresentNow();
    if (!lightSleepAllowed(c)) { delay(idleDelayMs(c)); return; }
    if (touchWake && digitalRead(TP_INT) == LOW) { gTouchIrq = true; delay(1); return; }
#if BOARD_AMOLED_175C
    if (!locked && digitalRead(BOOT_BUTTON_PIN) == LOW) { delay(1); return; }
#endif
    uint32_t ms = POWER_SLEEP_MS;
    uint32_t iv = frameIntervalMs(c);
    if (iv) {
      uint32_t due = lastRender + iv - millis();
      if ((int32_t)due < (int32_t)ms) ms = (int32_t)due > 1 ? due : 1;
    }
    esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000);
    if (touchWake) gpio_wakeup_enable((gpio_num_t)TP_INT, GPIO_INTR_LOW_LEVEL);
#if BOARD_AMOLED_175C
    if (!locked) gpio_wakeup_enable((gpio_num_t)BOOT_BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
#endif
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
#if BOARD_AMOLED_175C
    if (!locked) gpio_wakeup_disable((gpio_num_t)BOOT_BUTTON_PIN);
#endif
    if (touchWake) {
      gpio_wakeup_disable((gpio_num_t)TP_INT);
      gpio_set_intr_type((gpio_num_t)TP_INT, GPIO_INTR_NEGEDGE);
      if (digitalRead(TP_INT) == LOW) gTouchIrq = true;
    }
    return;
  }
  // A press sequence is pending: keep the loop at ~40 ms so pollPwrButton() sees each press.
  if (controlsBusy) { delay(10); return; }
  delay(idleDelayMs(c));
}

// ---------- entrada tactil ----------

// test/test_clockprompt.cpp (ClockBadge) mirrors these bounds (x > 110): update it too.
bool inPetZone(int16_t x, int16_t y) {
  return x > 110 && x < 356 && y > 95 && y < 310;
}

#if BOARD_AMOLED_175C
// Retain the legacy getPoint() EMPTY -> no-contact behaviour and 20 ms
// cadence. The 5 ms/strict-release experiment merged taps on real hardware.
// Wire errors are still errors; no I2C or serial printing inside the ISR.
TrainingReport readTrainingTouch() {
  bool irq = takeTouchIrq();
  if (!irq && !trainingCapture.down() && digitalRead(TP_INT) != LOW) return {};
  TrainingReport failed;
  failed.attempted = true; failed.irq = irq;
  Wire.beginTransmission(TOUCH_ADDR);
  if (Wire.endTransmission() != 0) return failed;
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0xD0); Wire.write(0x00);
  if (Wire.endTransmission() != 0) return failed;
  uint8_t b[15] = {};
  size_t n = Wire.requestFrom((uint8_t)TOUCH_ADDR, (size_t)sizeof(b));
  size_t got = Wire.readBytes(b, n > sizeof(b) ? sizeof(b) : n);
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0xD0); Wire.write(0x00); Wire.write(0xAB);
  bool ackOk = Wire.endTransmission() == 0;
  TrainingReport report = decodeTrainingReport(b, got);
  report.irq = irq;
  if (!ackOk || got != sizeof(b)) { report.valid = false; report.kind = TrainingReport::ERROR; }
  return report;
}

// Optional bounded raw trace. Default OFF. Only the loop prints, after the
// worker has joined. Serial TX already has timeout zero; logging cannot block.
void drainTrainingTrace() {
  TrainingTrace trace;
  while (trainingCapture.nextTrace(trace)) {
    const auto &r = trace.report;
    Serial.printf("TRAINRAW ms=%lu irq=%u len=%u kind=%u press=%u xy=%d,%d action=%u raw=",
                  (unsigned long)trace.at, r.irq, r.bytes, (unsigned)r.kind,
                  r.pressed, r.x, r.y, (unsigned)trace.action);
    for (uint8_t byte : r.raw) Serial.printf("%02X", byte);
    Serial.println();
  }
}

void leaveSack() {
  bool held = trainingCapture.down();
  sackOpen = false;
  trainingCapture.reset(held);
  wasPressed = false;
  trainingNeedsRelease = held;
  touchNeedsRelease = held;  // never leak an exit hold into the pet/menus
  gTouchIrq = true;
}

void drainTrainingTouch() {
  drainTrainingTrace();
  TrainingEvent event;
  while (trainingCapture.next(event)) {
    wakeUI();
    if (event.action == TrainingTouch::EXIT && !sackOverUntil &&
        trainingBeforeDeadline(sackUntil, event.at)) { leaveSack(); return; }
    // Use capture time, not render/drain time: don't lose a last-moment tap,
    // or credit a tap that happened after the ten-second deadline.
    if (event.action == TrainingTouch::HIT && !sackOverUntil &&
        trainingBeforeDeadline(sackUntil, event.at)) {
      ++sackHits;
      sackShake = 16;
    }
  }
  wasPressed = trainingCapture.down();
}
#endif

// ---------- gestos sobre el bicho (#21, touchgesture.h) ----------

// When the gestures are live (touchgesture.h): the main screen with the pet awake and
// nothing over it, and not the touch that only wakes the screen.
static PetGestureGate petGestureGate() {
  PetGestureGate g;
  g.dark = swallowGesture;
  g.egg = pet.isEgg();
  g.ceremony = pet.ceremony != CER_NONE;
  g.sleeping = pet.sleeping;
  g.evolving = pet.evolving();
  g.eating = pet.eating();
  // the timed dialogs and the bath are compared with timeLeft(), like careBusy(), because
  // render() only zeroes them when it draws (it does not while the screen is off)
  g.bathing = timeLeft(bathUntil);
  g.choosing = pet.awaitingStarter() || pet.awaitingEgg();
  g.otherScreen = galleryOpen || cardOpen || kbOpen || clockOpen || gameOpen || sackOpen;
  // careBusy() covers the timed dialogs (and the bath, evolution and a meal again), so a
  // dialog it learns later turns the gestures off too
  g.dialog = careBusy();
  return g;
}

// A touch onTap() would give to the pet: in the pet zone and not on the evolve, runaway or
// farewell button drawn over it (the same hit tests, in the same order, as onTap).
static bool onPetBody(int16_t x, int16_t y) {
  if (!inPetZone(x, y)) return false;
  if (pet.wantEvolveButton() && x >= EVO_BTN_X && x <= EVO_BTN_X + EVO_BTN_W &&
      y >= EVO_BTN_Y && y <= EVO_BTN_Y + EVO_BTN_H) return false;
  if ((pet.canRunawayNow() || pet.wantFarewellButton()) && x >= FAR_BTN_X &&
      x <= FAR_BTN_X + FAR_BTN_W && y >= FAR_BTN_Y && y <= FAR_BTN_Y + FAR_BTN_H) return false;
  return true;
}

// The PMD action a reaction plays, from what this sprite has. Every sprite has IDLE, the
// walks and HOP; only about one species in five has POSE, NOD, BREATH or SIT, so the IDLE
// fallbacks get their movement from reactOffset(). The hop never uses PMD_HOP: its canvas
// is tall enough for the jump, so drawPmdActM() would draw most pets smaller mid-hop.
uint8_t reactAct(uint8_t kind) {
  switch (kind) {
    case REACT_GIGGLE: return pmd.has(PMD_POSE) ? PMD_POSE : pmd.has(PMD_NOD) ? PMD_NOD : PMD_IDLE;
    case REACT_HUFF: {  // turns its back, or else its side (one still frame); never PMD_HURT
      if (pmd.has(PMD_SIT)) return PMD_SIT;
      uint8_t side = petReact.away < 0 ? PMD_WALKL : PMD_WALKR;
      if (pmd.has(side)) return side;
      return PMD_IDLE;
    }
    case REACT_STROKE: return pmd.has(PMD_NOD) ? PMD_NOD : pmd.has(PMD_BREATH) ? PMD_BREATH : PMD_IDLE;
  }
  return PMD_IDLE;
}

bool reacting(uint32_t now) {
  return petReact.kind != REACT_NONE && (int32_t)(petReact.until - now) > 0;
}

// fingerX: where the finger touched, so a huff can turn away from it
static void startReact(uint8_t kind, int fingerX) {
  uint32_t now = millis();
  uint32_t ms = kind == REACT_HOP ? 700 : kind == REACT_GIGGLE ? 1400 : 1600;
  // the same reaction again (stroking on, tickling on, huffing on) keeps running; a hop starts over
  if (kind == REACT_HOP || petReact.kind != kind || !reacting(now)) {
    petReact.t0 = now;
    int petX = pmd.loaded ? (int)beh.x : CX;
    petReact.away = fingerX > petX ? -1 : 1;
  }
  petReact.kind = kind;
  petReact.until = now + ms;
}

// A tap on the pet's body with the gestures live: a pat at once, upgraded by the run of
// quick taps it belongs to. The huff is gentle: no pat, no sound, no stat change.
static void petTapGesture(PetTap t) {
  static const char *const NAME[] = { "", " double", " tickle", " huff" };
  Serial.printf("PET%s\n", NAME[t]);
  if (!petTapEarnsPat(t)) {
    startReact(REACT_HUFF, touchG.x0);
    return;
  }
  pet.caress();
  if (t == PET_TAP_DOUBLE) {
    startReact(REACT_HOP, touchG.x0);
    sfxPlay(SFX_HEART);
  } else if (t == PET_TAP_TICKLE) {
    startReact(REACT_GIGGLE, touchG.x0);
    sfxPlay(SFX_PLAY);
  } else {
    if (petReact.kind == REACT_HUFF) petReact.kind = REACT_NONE;  // a fresh pat: over the huff
    sfxPlay(SFX_HEART);
  }
}

// A stroke or a drag while the finger is down (touchgesture.h already checked it began on
// the pet with the gestures live; this re-checks in case the pet fell asleep meanwhile).
static void petHeldGesture(TouchAct a) {
  if (!petGesturesLive(petGestureGate())) return;
  if (a == TOUCH_DRAG_MOVE) {
    // Only keep the screen from dimming: wakeUI() would force a frame per move at a slow
    // frame rate (low battery), where a drag is meant to draw once per interval.
    lastInteract = millis();
    return;
  }
  wakeUI();  // a stroke or the start of a drag wakes the screen like any touch
  if (a == TOUCH_STROKE) {
    Serial.println("PET stroke");
    pet.caress();
    sfxPlay(SFX_HEART);
    startReact(REACT_STROKE, touchG.xl);
  } else if (a == TOUCH_DRAG_START) {
    Serial.println("PET drag");
    pet.caress();
    sfxPlay(SFX_HEART);
    petReact.kind = REACT_NONE;  // walking after the finger (drawPet*) replaces a reaction
  }
}

// How a reaction moves the sprite, in px. animated: the sprite plays its own action for it
// (reactAct); the IDLE fallbacks, the B/W SdMon and the built-in sprite pass false.
void reactOffset(uint32_t now, bool animated, int &dx, int &dy) {
  if (!reacting(now)) return;
  uint32_t e = now - petReact.t0;
  if (petReact.kind == REACT_HOP) {
    dy -= (int)(24.0f * sinf((float)(e % 350) * (float)PI / 350.0f));  // two little hops
  } else if (petReact.kind == REACT_GIGGLE) {
    dx += ((e / 60) % 2) ? 3 : -3;              // shaking with giggles
    if (!animated) dy -= ((e / 120) % 2) ? 4 : 0;
  } else if (petReact.kind == REACT_STROKE && !animated) {
    dy -= ((e / 300) % 2) ? 3 : 0;              // a contented bob
  }
}

// A hold-then-drag is being followed: the finger is dragging and the gestures are still
// live (a pet that fell asleep mid-drag stops following it).
bool petFollowing(uint32_t now) {
  return touchG.dragging(now) && petGesturesLive(petGestureGate());
}

// el toque se resuelve al LEVANTAR el dedo para distinguir tap de deslizar
void handleTouch() {
  if (screenIsLocked() || autoTouch.active) return;  // no touch I2C while either lock is on
#if BOARD_AMOLED_175C
  if (trainingNeedsRelease && trainingCaptureReady) {
    // Swallow the exit contact until the legacy reader reports no point.
    trainingCapture.poll();
    wasPressed = trainingCapture.down();
    if (!trainingCapture.waitingRelease()) {
      trainingNeedsRelease = false; touchNeedsRelease = false; wasPressed = false;
    }
    return;
  }
  if (sackOpen) {
    trainingCapture.poll();  // legacy 20 ms; also serviced during QSPI rendering
    if (!trainingCapture.waitingRelease()) touchNeedsRelease = false;
    drainTrainingTouch();
    return;
  }
#endif
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 20) return;  // 50 Hz le sobra a un dedo
  lastPoll = millis();
  // solo tocamos el bus si el chip aviso por INT o si el dedo sigue abajo (hay
  // que detectar el levantamiento). Leer el CST9217 dormido se colgaba ~1s y
  // congelaba el loop entero; SensorLib no respeta el timeout de Wire.
#if BOARD_AMOLED_175C
  if (!takeTouchIrq() && !wasPressed) return;
#else
  if (!gTouchIrq && !wasPressed) return;
  gTouchIrq = false;
#endif
  // Un ciclo de solo direccion antes de leer. Sin esto, getPoint() se colgaba
  // exactamente 1000 ms (el timeout por defecto del driver I2C, que SensorLib no
  // acota) y congelaba el loop entero: es lo que se percibia como "el minijuego
  // se congela 2-3 segundos al tocar la bola" (issue #16), porque ahi los toques
  // son rapidos y seguidos.
  //
  // Medido en placa, jugando lo mismo (~470 lecturas, marcador ~50):
  //   sin esta linea: 5 parones de 1000 ms en 60 s
  //   con ella:       0
  // El contador de rechazos salio 0 en ambos casos, asi que NO funciona
  // saltandose lecturas cuando el chip no contesta: lo que hace es despertarlo,
  // para que la lectura siguiente no se encuentre el CST9217 dormido.
  Wire.beginTransmission(TOUCH_ADDR);
  if (Wire.endTransmission() != 0) return;  // no responde: se reintenta en 20 ms
  int16_t x, y;
  bool pressed = touch.getPoint(&x, &y, 1) > 0;
#if BOARD_AMOLED_175C
  if (touchNeedsRelease) {
    wasPressed = pressed;
    if (!pressed) touchNeedsRelease = false;
    return;
  }
#endif

#if !BOARD_AMOLED_175C
  // Original board: retain its existing touch/exit policy.
  if (sackOpen) {
    if (pressed && !wasPressed) {
      wakeUI();
      if (y < 72) sackOpen = false;  // tocar arriba = abandonar
      else sackTap();
    }
    wasPressed = pressed;
    return;
  }
#endif

  if (gameOpen) {  // el juego recibe el dedo tal cual: sin deslizar ni pulsacion larga
    gameTouch(pressed, x, y);
    wasPressed = pressed;
    return;
  }

  // Swipes, taps, the long-press and the gestures on the pet are resolved by touchG
  // (touchgesture.h, host-tested): a stroke or a hold-then-drag never ends as anything else.
  if (pressed && !wasPressed) {  // empieza el gesto
    holdFired = false;
    swallowGesture = (dimStage > 0) || screenOff;  // si estaba a oscuras, solo despierta
    screenOff = false;
    wakeUI();
    touchG.down(millis(), x, y, onPetBody(x, y) && petGesturesLive(petGestureGate()));
  } else if (pressed) {  // sigue apoyado
    // pulsacion larga sin moverse sobre el bicho -> dialogo de soltar
    bool longOk = !holdFired && !swallowGesture && !galleryOpen && !cardOpen && !kbOpen && !clockOpen &&
                  inPetZone(touchG.x0, touchG.y0) && !pet.isEgg() && !confirmUntil && !pet.ceremony;
    TouchAct a = touchG.held(millis(), x, y, longOk);
    if (a == TOUCH_LONG_PRESS) {
      confirmUntil = millis() + 10000;
      holdFired = true;
    } else if (a != TOUCH_NONE) {
      petHeldGesture(a);
    }
  } else if (wasPressed) {  // levanta el dedo: resolver gesto
    wakeUI();
    switch (touchG.lift(millis(), holdFired || swallowGesture)) {
      case TOUCH_SWIPE_RIGHT: onSwipe(1); break;
      case TOUCH_SWIPE_LEFT: onSwipe(-1); break;
      case TOUCH_SWIPE_DOWN: onSwipeV(1); break;
      case TOUCH_SWIPE_UP: onSwipeV(-1); break;
      case TOUCH_TAP: onTap(touchG.x0, touchG.y0); break;
      default: break;
    }
  }
  wasPressed = pressed;
}

// deslizar vertical: abre/cierra la ficha del bicho
void onSwipeV(int dir) {
  if (pet.awaitingStarter() || pet.awaitingEgg()) return;  // bloqueado mientras elige inicial o huevo
  if (gameOpen || galleryOpen || kbOpen || sackOpen || pet.ceremony) return;
  if (clockOpen) { clockOpen = false; return; }
  if (cardOpen) {
    if (dir < 0) cardOpen = false;  // arriba cierra la ficha
    return;
  }
  if (dir > 0) {                    // deslizar abajo: ajustar hora
    if (!confirmUntil && !feedMenuUntil) openClock();
  } else if (!pet.isEgg() && !confirmUntil && !feedMenuUntil) {
    cardOpen = true;                // deslizar arriba: ficha
    cardPage = 0;
  }
}

// deslizar: dir +1 = hacia la derecha
void onSwipe(int dir) {
  if (pet.awaitingStarter() || pet.awaitingEgg()) return;  // bloqueado mientras elige inicial o huevo
  if (gameOpen || kbOpen || clockOpen) return;
  if (cardOpen) {  // dentro de la ficha: cambiar entre las 5 paginas
    int p = (int)cardPage + (dir > 0 ? -1 : 1);  // izquierda avanza
    cardPage = p < 0 ? 0 : (p > CARD_LAST_PAGE ? CARD_LAST_PAGE : p);
    return;
  }
  if (!galleryOpen) {
    if (!pet.ceremony && !confirmUntil) {
      galleryOpen = true;
      galleryPage = 0;
      galleryDetail = 0;
      galleryDirty = true;
    }
    return;
  }
  if (galleryDetail) {  // en detalle: volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  int np = galleryPage - dir;  // deslizar a la izquierda avanza pagina
  if (np < 0) {                // retroceder desde la primera = salir
    galleryOpen = false;
    galleryPmd.unload();
    return;
  }
  if (np > 9) np = 9;
  if (np != galleryPage) {
    galleryPage = np;
    galleryDirty = true;
  }
}

void onTap(int16_t x, int16_t y) {
  // Serial.printf("TOUCH %d %d\n", x, y);  // diagnostico (silenciado: satura el log)
  if (pet.awaitingStarter()) {  // primera partida: elegir inicial
    for (int i = 0; i < 3; i++) {
      int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
      if (x >= 70 && x <= 396 && y >= ry && y <= ry + STARTER_ROW_H) {
        pet.chooseStarter(STARTER_DEX[i]);
        sfxPlay(SFX_TAP);
        break;
      }
    }
    return;
  }
  if (pet.awaitingEgg()) {  // tras un final: elegir uno de los tres huevos
    eggSelectTap(x, y);
    return;
  }
  if (galleryOpen) {
    galleryTap(x, y);
    return;
  }
  if (kbOpen) {
    keyboardTap(x, y);
    return;
  }
  if (clockOpen) {
    clockTap(x, y);
    return;
  }
  if (pet.ceremony) return;  // durante la despedida no hay botones
  if (cardOpen) {
    if (cardPage == 0 && y < 84) openKeyboard();  // tocar el nombre = renombrar
    else if (cardPage == 1 && y >= 300 && y <= 340 && x >= 96 && x <= 370) {
      cardOpen = false;            // boton ENTRENAR FUERZA
      startSack();
    } else if (cardPage == CARD_PAGE_CARE && careTap(x, y)) {
      // ritmo / hora de dormir: la ficha sigue abierta (careTap ya guardo y repinta)
    } else {
      cardOpen = false;
    }
    return;
  }
  if (gameOpen) return;  // los toques del juego van por gameTouch() (handleTouch)
  if (choiceKind) {          // dialogo de decision: boton accion (arriba) / mantener (abajo)
    bool b1 = (x >= 93 && x <= 373 && y >= 206 && y <= 258);  // accion
    bool b2 = (x >= 93 && x <= 373 && y >= 268 && y <= 320);  // mantener / quedaros
    if (choiceKind == 1) {                 // evolucion
      if (b1) { int16_t old = pet.speciesId; pet.evolve(); evoPmd.load(old, pet.shiny); }
      else if (b2) pet.declineEvolve();
    } else if (choiceKind == 2) {          // despedida
      if (b1) pet.startFarewell();
      else if (b2) pet.declineFarewell();
    }
    choiceKind = 0;
    return;
  }
  if (confirmUntil) {        // dialogo "soltar?": SI / NO
    if (timeLeft(confirmUntil) && x >= 118 && x <= 218 && y >= 252 && y <= 304) {
      pet.release();
    }
    confirmUntil = 0;
    return;
  }
  if (feedMenuUntil) {       // selector de comida
    if (timeLeft(feedMenuUntil) && y >= 288 && y <= 352 && x >= 101 && x <= 365) {
      int item = (x - 101) / 66;
      if (item == 3) pet.feedCandy();
      else pet.feedBerry(item);
      sfxPlay(SFX_EAT);
    }
    feedMenuUntil = 0;
    return;
  }
  if (clockPrompt.asking(bedEdge.stale()) && clockBadgeHit(x, y)) {  // reloj sin poner
    sfxPlay(SFX_TAP);
    openClock();
    return;
  }
  if (pet.isEgg()) {
    pet.eggTap();
    sfxPlay(SFX_TAP);
    return;
  }
  // boton de evolucion: abre el dialogo evolucionar/mantener
  if (pet.wantEvolveButton() && x >= EVO_BTN_X && x <= EVO_BTN_X + EVO_BTN_W &&
      y >= EVO_BTN_Y && y <= EVO_BTN_Y + EVO_BTN_H) {
    choiceKind = 1; choiceUntil = millis() + 12000;
    return;
  }
  // botones de final (mismo recuadro): escapada directa; despedida abre dialogo
  if (x >= FAR_BTN_X && x <= FAR_BTN_X + FAR_BTN_W &&
      y >= FAR_BTN_Y && y <= FAR_BTN_Y + FAR_BTN_H) {
    if (pet.canRunawayNow()) { pet.startRunaway(); return; }
    if (pet.wantFarewellButton()) { choiceKind = 2; choiceUntil = millis() + 12000; return; }
  }
  for (int i = 0; i < 4; i++) {
    int dx = x - buttons[i].cx, dy = y - buttons[i].cy;
    if (dx * dx + dy * dy <= BTN_HIT * BTN_HIT) {
      Serial.printf("BTN %d\n", i);
      sfxPlay(SFX_TAP);
      if (i == 0) {
        if (!pet.sleeping) feedMenuUntil = millis() + 6000;
      } else if (i == 1) {
        startGame();
      } else if (i == 2) {
        pet.toggleLight();
      } else {
        startBath();
      }
      return;
    }
  }
  // tocar al bicho = caricia; con los gestos vivos, toque doble / cosquillas / bufido (#21)
  if (inPetZone(x, y)) {
    if (petGesturesLive(petGestureGate())) {
      petTapGesture(touchG.petTap(millis()));
      return;
    }
    Serial.println("PET");
    pet.caress();
    if (!pet.sleeping) sfxPlay(SFX_HEART);
  }
}

// primera partida: elige inicial entre Bulbasaur / Charmander / Squirtle
void renderStarterSelect() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  const char *t = T(S_CHOOSE_STARTER);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(t, 2), 68);
  printT(t);
  for (int i = 0; i < 3; i++) {
    int16_t d = STARTER_DEX[i];
    const DexEntry &de = DEX_TBL[d];
    int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
    gfx->fillRoundRect(70, ry, 326, STARTER_ROW_H, 14, lerp565(de.accent, UI_WHITE, 6, 8));
    gfx->drawRoundRect(70, ry, 326, STARTER_ROW_H, 14, de.accent);
    const uint8_t *th = thumbs.get(d);     // miniatura del inicial (si la SD esta lista)
    if (th) drawThumb(th, 76, ry - 5, 3, false);
    gfx->setTextColor(UI_INK);
    setSize(3);
    setCur(178, ry + 24);
    printT(dexName(d));
  }
  flushUI();
}

// huevo con pista: cascara segun la rareza (comun crema, raro plata, legendario
// oro) y motas ('g' en SPR_EGG) del color del tipo. El contorno y las grietas
// siguen en tinta. La especie no se ve.
void drawEggStyled(uint8_t rarity, uint16_t spot, int x, int y, int s) {
  uint16_t shell, shade;
  if (rarity == R_LEGENDARIO) {
    shell = C565(0xff, 0xd9, 0x5e);
    shade = C565(0xd9, 0xa0, 0x2a);
  } else if (rarity == R_RARO) {
    shell = C565(0xdc, 0xe6, 0xf2);
    shade = C565(0x9c, 0xb0, 0xcc);
  } else {
    shell = C565(0xf8, 0xf1, 0xe0);
    shade = C565(0xd8, 0xcc, 0xae);
  }
  for (int r = 0; r < SPRITE_H; r++) {
    const char *row = SPR_EGG[r];
    forRuns(SPRITE_W, [&](int c) { return charKey(row[c], false); },
            [&](int c0, int len, int k) {
              char ch = (char)k;
              uint16_t col = ch == 'y' ? shell : ch == 'Y' ? shade : ch == 'g' ? spot : spriteColor(ch);
              gfx->fillRect(x + c0 * s, y + r * s, len * s, s, col);
            });
  }
}

const char *eggRarityText(uint8_t rarity) {
  return rarity == R_LEGENDARIO ? T(S_EGG_LEGEND) : rarity == R_RARO ? T(S_EGG_RARE) : T(S_EGG_COMMON);
}
uint16_t eggRarityColor(uint8_t rarity) {
  return rarity == R_LEGENDARIO ? UI_BAR_WARN : rarity == R_RARO ? 0x4C98 : inkColor();
}

// tras un final (con el ajuste Care "Huevos" en Elegir): tres huevos a elegir
void renderEggSelect() {
  int h = sceneHour();
  gNight = h < 6 || h >= 20;  // un huevo no duerme: solo la hora
  uint16_t bg = gNight ? UI_BG_NIGHT : UI_BG_DAY;
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, bg);
  const char *t = T(S_CHOOSE_EGG);
  gfx->setTextColor(inkColor());
  setSize(2);
  setCur(centerX(t, 2), 84);
  printT(t);
  for (int i = 0; i < EGG_CHOICES; i++) {
    int16_t d = pet.eggChoice(i);
    if (d < 1 || d > DEX_COUNT) continue;  // Pet ya lo garantiza; por si acaso
    const DexEntry &de = DEX_TBL[d];
    int lift = (i == eggSel) ? EGGSEL_LIFT : 0;
    int x = EGGSEL_CX[i] - 16 * EGGSEL_S, y = EGGSEL_Y - lift;
    // halo del tocado y despues la sombra en el suelo (se separa al levantarlo)
    if (i == eggSel) gfx->fillCircle(EGGSEL_CX[i], y + 17 * EGGSEL_S, 58, lerp565(bg, de.accent, 3, 8));
    gfx->fillEllipse(EGGSEL_CX[i], EGGSEL_Y + 29 * EGGSEL_S, 30 - lift / 2, 7, lerp565(bg, UI_INK, 2, 8));
    drawEggStyled(de.rarity, de.accent, x, y, EGGSEL_S);
  }
  if (eggSel >= 0 && eggSel < EGG_CHOICES) {
    int16_t d = pet.eggChoice(eggSel);
    uint8_t r = (d >= 1 && d <= DEX_COUNT) ? DEX_TBL[d].rarity : (uint8_t)R_COMUN;
    const char *rt = eggRarityText(r);
    gfx->setTextColor(eggRarityColor(r));
    setSize(2);
    setCur(centerX(rt, 2), 300);
    printT(rt);
    const char *hint = T(S_EGG_TAP_AGAIN);
    gfx->setTextColor(lerp565(inkColor(), bg, 3, 8));
    setCur(centerX(hint, 2), 336);
    printT(hint);
  }
  flushUI();
}

// toque en la eleccion de huevo: el primero levanta el huevo, el segundo lo elige
void eggSelectTap(int16_t x, int16_t y) {
  if (y < EGGSEL_Y - EGGSEL_LIFT || y > EGGSEL_Y + 30 * EGGSEL_S) return;
  for (int i = 0; i < EGG_CHOICES; i++) {
    if (abs(x - EGGSEL_CX[i]) > EGGSEL_HIT) continue;
    if (eggSel == i) {
      pet.chooseEgg((uint8_t)i);
      eggSel = -1;
      sfxPlay(SFX_HEART);  // no SFX_HATCH: ese suena al eclosionar, tres toques despues
    } else {
      eggSel = (int8_t)i;
      sfxPlay(SFX_TAP);
    }
    return;
  }
}

// Pantallas que solo cambian con un toque (o con un dato que cambia muy de vez en cuando):
// mientras su "clave" no cambie no se repintan ni se vuelcan por QSPI.
static bool staticScreen() {
  if (pet.awaitingStarter() || pet.awaitingEgg()) return true;
  if (galleryOpen || gameOpen || sackOpen) return false;  // la galeria gestiona su propio repintado
  if (kbOpen || clockOpen) return true;
  return cardOpen && cardPage != 0;  // la pagina 0 tiene el retrato animado
}
static uint32_t mixKey(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t screenKey() {
  uint32_t h = 2166136261u;
  h = mixKey(h, pet.awaitingStarter() ? 1 : pet.awaitingEgg() ? 100 : kbOpen ? 2 : clockOpen ? 3 : 4 + cardPage);
  h = mixKey(h, sdEpoch);
  if (pet.awaitingStarter()) {
    // nada mas: la pantalla del inicial no cambia hasta elegir
  } else if (pet.awaitingEgg()) {
    int hr = sceneHour();
    h = mixKey(h, (uint32_t)(eggSel + 1));
    h = mixKey(h, (hr < 6 || hr >= 20) ? 1 : 0);
    h = mixKey(h, gLang);
    for (uint8_t i = 0; i < EGG_CHOICES; i++) h = mixKey(h, (uint32_t)(uint16_t)pet.eggChoice(i));
  } else if (kbOpen) {
    h = mixKey(h, nameLen);
    for (int i = 0; i < nameLen; i++) h = mixKey(h, (uint8_t)nameBuf[i]);
  } else if (clockOpen) {
    h = mixKey(h, clockH * 60 + clockM);
    h = mixKey(h, audioEnabled());
    h = mixKey(h, audioVolume());
    h = mixKey(h, userDim);
    h = mixKey(h, batAsPercent);
    h = mixKey(h, (uint32_t)batPercent());  // the battery pill previews the live level
    h = mixKey(h, batCharging());
    h = mixKey(h, powerOffMs);
    h = mixKey(h, touchLockMs);
    h = mixKey(h, gLang);
    h = mixKey(h, clockPrompt.asking(bedEdge.stale()));  // the title (TIME can set it meanwhile)
  } else if (cardOpen) {
    for (uint32_t v : { (uint32_t)pet.speciesId, (uint32_t)pet.shiny, (uint32_t)pet.ageMinutes, (uint32_t)pet.streak,
                        (uint32_t)pet.bestStreak, (uint32_t)pet.bond, (uint32_t)pet.medals, (uint32_t)pet.weight,
                        (uint32_t)pet.trAtk, (uint32_t)pet.trDef, (uint32_t)pet.trSpe, (uint32_t)pet.geneAtk,
                        (uint32_t)pet.geneDef, (uint32_t)pet.geneSpe, (uint32_t)pet.careMistakes,
                        (uint32_t)pet.fullness, (uint32_t)pet.joy, (uint32_t)pet.energy, (uint32_t)pet.hygiene,
                        (uint32_t)pet.berryKnown, (uint32_t)pet.strHi, (uint32_t)gLang,
                        (uint32_t)pet.gameHi, (uint32_t)pet.catchHi, (uint32_t)pet.peekHi, (uint32_t)pet.songHi,
                        (uint32_t)pet.pace, (uint32_t)pet.bedPreset, (uint32_t)pet.eggPickMode })
      h = mixKey(h, v);
    for (const char *c = pet.nick; *c; c++) h = mixKey(h, (uint8_t)*c);
  }
  return h;
}
bool screenKeyValid = false;  // (el renderer del host lo invalida entre escenas)
uint32_t lastScreenKey = 0;

// Dibuja un frame y devuelve true si lo ha volcado al panel.
bool render() {
#if BOARD_AMOLED_175C
  if (controlToastVisible || screenIsLocked()) {
    screenKeyValid = false;
    galleryDirty = true;
    // Also invalidate on the first frame after expiry to erase the overlay.
    if ((int32_t)(controlToastUntil - millis()) <= 0) controlToastVisible = false;
  }
#endif
  // la ficha es de un bicho vivo: con huevo o ceremonia no hay nada que mostrar (un
  // BYE/RUN por serie la deja abierta; renderCardProfile leeria DEX_TBL[-1] y, ya
  // elegido el huevo nuevo, el boton Huevos se podria cambiar con el huevo a la vista)
  if (cardOpen && (pet.isEgg() || pet.ceremony)) cardOpen = false;
  if (staticScreen()) {
    uint32_t k = screenKey();
    if (screenKeyValid && k == lastScreenKey) return false;
    lastScreenKey = k;
    screenKeyValid = true;
  } else {
    screenKeyValid = false;
  }
  if (pet.awaitingStarter()) {  // primera partida: elegir inicial (prioridad total)
    renderStarterSelect();
    return true;
  }
  if (pet.awaitingEgg()) {  // tras un final: elegir huevo (prioridad total)
    renderEggSelect();
    return true;
  }
  eggSel = -1;  // ninguna eleccion abierta: que la siguiente empiece sin huevo tocado
  if (galleryOpen) {
    renderGallery();
    return true;
  }
  if (gameOpen) {
    renderGame();
    return true;
  }
  if (sackOpen) {
    renderSack();
    return true;
  }
  if (kbOpen) {
    renderKeyboard();
    return true;
  }
  if (clockOpen) {
    renderClock();
    return true;
  }
  if (cardOpen) {
    renderCard();
    return true;
  }
  int h = sceneHour();
  gNight = pet.sleeping || h < 6 || h >= 20;
  // drawScene cubre los 466x466 completos: sin fillScreen(NEGRO) previo para
  // que un flush DMA solapado nunca capture negro a medias (anti-parpadeo)
  drawScene(pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome, millis(), gNight);

  if (pet.ceremony) {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    const char *msg = (pet.ceremony == CER_FAREWELL) ? T(S_FAREWELL)
                      : (pet.ceremony == CER_RUNAWAY) ? T(S_RUNAWAY)
                                                      : T(S_GOODBYE);
    drawHeader(dexName(pet.speciesId), d.accent, msg);
    drawCeremony();
    flushUI();
    return true;
  }

  if (pet.isEgg()) {
    drawHeader(T(S_EGG_HDR), inkColor(), eggMsg());
    int s = 5, x = CX - 16 * s, y = PET_CY - 16 * s;
    // con el ajuste "Huevos" en Elegir este huevo salio de la eleccion (el
    // ajuste solo se cambia en la ficha, que render() cierra con un huevo en
    // pantalla): se ve igual que alli
    if (pet.eggPickMode == EGGPICK_ON) drawEggStyled(pet.eggRarity(), pet.eggAccent(), x, y, s);
    else drawMap(SPR_EGG, SPRITE_H, x, y, s, false);
    if (pet.eggCracks() >= 1)
      for (auto &c : CRACK1) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggCracks() >= 2)
      for (auto &c : CRACK2) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    drawClockBadge();
    char reg[24];
    snprintf(reg, sizeof(reg), T(S_POKEDEX_FMT), pet.registeredCount());
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    // la etiqueta de rareza va DESPUES del panel: antes se pintaba en y=316 y el
    // fillRect de arriba la tapaba, asi que "Huevo raro!" nunca se veia
    if (pet.eggRarity() >= R_RARO) {
      const char *rar = eggRarityText(pet.eggRarity());
      gfx->setTextColor(eggRarityColor(pet.eggRarity()));
      setSize(2);
      setCur(centerX(rar, 2), 326);
      printT(rar);
    }
    gfx->setTextColor(inkColor());
    setSize(2);
    setCur(centerX(reg, 2), 358);
    printT(reg);
  } else {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    char name[28];
    const char *base = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
    snprintf(name, sizeof(name), T(S_NAME_FMT), pet.shiny ? "*" : "", base, pet.level());
    drawHeader(name, gNight ? UI_INK_NIGHT : d.accent, statusMsg());
    drawStreakBadge();
    drawPet();
    drawBath();
    drawPoops();
    drawClockBadge();
    drawAttnBubble();
    // panel inferior: base limpia para barras y botones sobre el paisaje
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    drawBars();
    drawButtons();
    drawCelebration();
    if (pet.wantEvolveButton()) drawEvolveButton();        // CTA rojo: evolucionar
    else if (pet.canRunawayNow()) drawRunawayButton();     // CTA sombrio: escapada (abandono)
    else if (pet.wantFarewellButton()) drawFarewellButton();  // CTA dorado: despedida
  }

  if (pet.sleeping) {
    gfx->setTextColor(UI_INK_NIGHT);
    setSize(3);
    setCur(320, 130);
    printT("Zz");
  }

  // selector de comida
  if (feedMenuUntil) {
    if (!timeLeft(feedMenuUntil)) {
      feedMenuUntil = 0;
    } else {
      gfx->fillRoundRect(101, 288, 264, 64, 14, UI_WHITE);
      gfx->drawRoundRect(101, 288, 264, 64, 14, inkColor());
      drawMap(SPR_ICON_FOOD, 16, 110, 296, 3, false);
      drawMap(SPR_ICON_BERRY_B, 16, 176, 296, 3, false);
      drawMap(SPR_ICON_BERRY_G, 16, 242, 296, 3, false);
      drawMap(SPR_ICON_CANDY, 16, 308, 296, 3, false);
    }
  }

  // dialogo "soltar?" (pulsacion larga sobre el bicho)
  if (confirmUntil) {
    if (!timeLeft(confirmUntil)) {
      confirmUntil = 0;
    } else {
      gfx->fillRoundRect(94, 168, 278, 152, 16, UI_WHITE);
      gfx->drawRoundRect(94, 168, 278, 152, 16, UI_INK);
      // 48 y no 28: el buffer se dimensiono cuando toda cadena era de un byte
      // por caracter. Con una fila UTF-8, "%s 놓아줄까요?" mas el nombre pasa
      // de 28 y snprintf corta a mitad de una secuencia de 3 bytes, que la
      // fuente ya no sabe dibujar.
      char q[48];
      snprintf(q, sizeof(q), T(S_RELEASE_FMT), dexName(pet.speciesId));
      gfx->setTextColor(UI_INK);
      setSize(2);
      setCur(centerX(q, 2), 196);
      printT(q);
      gfx->fillRoundRect(118, 252, 100, 52, 12, UI_BAR_OK);
      gfx->setTextColor(UI_WHITE);
      setCur(118 + (100 - textW(T(S_YES), 2)) / 2, 270);
      printT(T(S_YES));
      gfx->fillRoundRect(248, 252, 100, 52, 12, UI_BAR_BAD);
      setCur(248 + (100 - textW(T(S_NO), 2)) / 2, 270);
      printT(T(S_NO));
    }
  }

  // dialogo de decision (evolucionar/mantener, despedirse/quedaros)
  if (choiceKind) {
    if (!timeLeft(choiceUntil)) choiceKind = 0;
    else drawChoiceDialog();
  }

  flushUI();
  return true;
}

// ---------- ficha del bicho (deslizar vertical) ----------

// x donde arrancan las barras de la ficha. Estaba fijo en 150, que daba de sobra
// para etiquetas latinas de 3 caracteres pero no para las japonesas, mas anchas:
// こうげき se metia dentro de la barra. Se calcula a partir de la etiqueta mas
// larga para que las cuatro barras sigan alineadas en cualquier idioma, con el
// valor original como suelo (asi en los idiomas latinos no cambia nada).
// Requiere tener ya puesto el tamano de texto 2, porque textW() lo necesita.
static int statBarX() {
  const StrId ids[] = { S_STAT_ATK, S_STAT_DEF, S_STAT_SPE, S_STAT_WGT, S_VIN };
  int ancho = 0;
  for (StrId id : ids) {
    int w = textW(T(id), 2);
    if (w > ancho) ancho = w;
  }
  int x = 96 + ancho + 12;   // 12 px de aire entre etiqueta y barra
  return x < 150 ? 150 : x;
}

void drawCardStat(int y, const char *label, uint16_t val, uint16_t maxBar, uint16_t color) {
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(96, y);
  printT(label);
  char num[8];
  snprintf(num, sizeof(num), "%u", val);
  setCur(330, y);
  printT(num);
  int bx = statBarX();
  int bw = 310 - bx;   // la barra siempre acaba en 310, dejando aire hasta el numero
  int fw = (int)val * bw / maxBar;
  if (fw > bw) fw = bw;
  gfx->fillRoundRect(bx, y + 2, bw, 11, 3, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(bx, y + 2, fw, 11, 3, color);
}

// ---------- ajuste de hora en pantalla (deslizar abajo) ----------
// El usuario pone su hora LOCAL a ojo; el firmware la usa tal cual, asi que
// no hay que gestionar zona horaria. Preserva el dia (no rompe racha/edad).

void loadClockFields(uint32_t e) {
  clockH = (e / 3600) % 24;
  clockM = (e / 60) % 60;
}

void openClock() {
  loadClockFields(pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch());
  clockOpen = true;
  clockPrompt.opened();  // however it opened, it does not open again by itself
}

void applyClock() {
  uint32_t base = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  uint32_t e = (base / 86400) * 86400 + (uint32_t)clockH * 3600 + (uint32_t)clockM * 60;
  rtcSetEpoch(e);
  pet.setClock(e);
  bedClockSet(e);
  clockOpen = false;
}

void drawClockBtn(int x, int y, const char *l) {
  gfx->fillRoundRect(x, y, 58, 58, 12, UI_WHITE);
  gfx->drawRoundRect(x, y, 58, 58, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  setSize(4);
  setCur(x + 17, y + 15);
  printT(l);
}

// the pills' and the OK button's rects are in settingslayout.h (shared with clockTap)
static const char *const LANG_CODES[LANG_COUNT] = { "ES", "EN", "FR", "DE", "IT", "PT", "JA", "KO" };

// tres barras crecientes (brillo, volumen); las `lit` primeras van rellenas
static void drawLevelBars(int x, int lit) {
  for (int i = 0; i < 3; i++) {
    int bh = 8 + i * 5, bx = x + i * 14, by = LANG_PILL_Y + LANG_PILL_H - 6 - bh;
    if (i < lit) gfx->fillRect(bx, by, 9, bh, UI_BAR_OK);
    gfx->drawRect(bx, by, 9, bh, UI_INK);
  }
}

#define PILL_BAT_W 34  // the battery pill's glyph body; its terminal adds (3 * 16 + 5) / 11 = 4 px
#define PILL_BAT_H 16

// Power pill icons, drawn in UI_INK on the white pill, about 20 px tall around `cy`.
// A padlock 16 px wide; open (shackle lifted, right leg out) when the touch lock is off.
#define PADLOCK_W 16
static void drawPadlock(int x, int cy, bool open) {
  int sy = open ? cy - 15 : cy - 11;  // shackle: two nested round rects, 2 px thick
  gfx->drawRoundRect(x + 3, sy, 10, 16, 5, UI_INK);
  gfx->drawRoundRect(x + 4, sy + 1, 8, 14, 4, UI_INK);
  if (open) gfx->fillRect(x + 10, cy - 6, 4, 4, UI_WHITE);  // the right leg stops short of the body
  gfx->fillRoundRect(x, cy - 2, PADLOCK_W, 12, 2, UI_INK);  // body
  gfx->fillCircle(x + 8, cy + 2, 2, UI_WHITE);              // keyhole
  gfx->fillRect(x + 7, cy + 2, 2, 5, UI_WHITE);
}

// A display on its stand, 22 px wide, with a dark face: the screen that goes off.
#define SCREEN_ICON_W 22
static void drawScreenIcon(int x, int cy) {
  gfx->fillRoundRect(x, cy - 10, SCREEN_ICON_W, 16, 3, UI_INK);
  gfx->drawRect(x + 2, cy - 8, SCREEN_ICON_W - 4, 12, UI_WHITE);  // bezel line
  gfx->fillRect(x + 9, cy + 6, 4, 3, UI_INK);                      // stand
  gfx->fillRect(x + 5, cy + 8, 12, 2, UI_INK);
}

// An icon `iconW` px wide and `text` side by side, centred in `r` (text size 2): prints
// the text and returns the icon's left x.
static int pillIconText(const SetRect &r, int iconW, const char *text) {
  const int gap = 8;
  int x = r.x + (r.w - (iconW + gap + textW(text, 2))) / 2;
  setCur(x + iconW + gap, r.y + (r.h - 14) / 2);
  printT(text);
  return x;
}

// The power pills (#11): an icon plus a number, like the sound and brightness pills
// above them. The rects are settingslayout.h's, the same ones clockTap() hit-tests.
// "off" (T(S_TLOCK_OFF)) is the only word, and so the only translated label.
static void drawPowerPills() {
  for (int i = 0; i < POWER_PILL_COUNT; i++) {
    SetRect r = powerPillRect(i);
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 10, UI_WHITE);
    gfx->drawRoundRect(r.x, r.y, r.w, r.h, 10, UI_INK);
  }
  gfx->setTextColor(UI_INK);
  setSize(2);
  char s[12];

  // touch lock: seconds after the screen goes off ("0s": as it goes off)
  SetRect r = powerPillRect(PILL_TOUCH_LOCK);
  bool lockOn = secondsLabel(s, sizeof(s), touchLockMs, TOUCH_LOCK_NEVER);
  int ix = pillIconText(r, PADLOCK_W, lockOn ? s : T(S_TLOCK_OFF));
  drawPadlock(ix, r.y + r.h / 2 + 1, !lockOn);

  // screen off: seconds without a touch; "off" = never (it still dims, power.h)
  r = powerPillRect(PILL_SCREEN_OFF);
  bool offOn = secondsLabel(s, sizeof(s), powerOffMs, 0);
  ix = pillIconText(r, SCREEN_ICON_W, offOn ? s : T(S_TLOCK_OFF));
  drawScreenIcon(ix, r.y + r.h / 2);

  // battery: a preview of the header, its glyph or its percent, with the live level
  r = powerPillRect(PILL_BATTERY);
  int pc = batPercent();
  bool charging = pc >= 0 && batCharging();
  // no reading: faint grey, so it reads as "nothing to show" rather than a flat (red) battery
  uint16_t col = pc < 0 ? UI_TRACK : batteryColor(pc, charging, UI_INK);
  if (batAsPercent) {
    batteryPctLabel(s, sizeof(s), pc, charging);
    gfx->setTextColor(col);
    setCur(r.x + (r.w - textW(s, 2)) / 2, r.y + (r.h - 14) / 2);
    printT(s);
  } else {  // with no reading (pc < 0) an empty grey outline
    drawBatteryGlyph(r.x + (r.w - PILL_BAT_W - 4) / 2, r.y + (r.h - PILL_BAT_H) / 2, PILL_BAT_W, PILL_BAT_H,
                     pc < 0 ? 0 : pc, charging, col, UI_INK);
  }
}

void renderClock() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  // reseeded after a power-off and not set yet (clockprompt.h): ask, and show the time
  // it has in red, since that is only the last time saved before it went off
  bool asking = clockPrompt.asking(bedEdge.stale());
  const char *title = asking ? T(S_CLOCK_ASK) : T(S_SET_TIME);
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(title, 3), 44);
  printT(title);

  char t[8];
  snprintf(t, sizeof(t), "%02d:%02d", clockH, clockM);
  gfx->setTextColor(asking ? UI_BAR_BAD : UI_INK);
  setSize(7);
  // CX - 105 in Latin; in CJK only about centred (centerX ignores the text x offset)
  setCur(centerX(t, 7), 108);
  printT(t);

  drawClockBtn(104, 190, "-");  // hora -
  drawClockBtn(170, 190, "+");  // hora +
  drawClockBtn(252, 190, "-");  // min -
  drawClockBtn(318, 190, "+");  // min +
  setSize(2);
  gfx->setTextColor(UI_TRACK);
  setCur(120, 256);
  printT(T(S_HOUR));
  setCur(276, 256);
  printT(T(S_MIN));

  // sonido (izquierda de la fila): un altavoz y tres barras con el volumen, o una X si esta apagado
  gfx->fillRoundRect(SND_PILL_X, LANG_PILL_Y, SND_PILL_W, LANG_PILL_H, 8, UI_WHITE);
  gfx->drawRoundRect(SND_PILL_X, LANG_PILL_Y, SND_PILL_W, LANG_PILL_H, 8, UI_INK);
  {
    int sx = SND_PILL_X + 18, sy = LANG_PILL_Y + LANG_PILL_H / 2;
    gfx->fillRect(sx, sy - 4, 5, 8, UI_INK);                           // cuerpo del altavoz
    gfx->fillTriangle(sx + 4, sy, sx + 12, sy - 9, sx + 12, sy + 9, UI_INK);  // bocina
    if (audioEnabled()) {
      drawLevelBars(SND_PILL_X + 46, VOL_LEVELS - audioVolume());
    } else {
      int xx = SND_PILL_X + 58;
      for (int d = -1; d <= 1; d++) {  // X de 3 px de grueso
        gfx->drawLine(xx - 7 + d, sy - 7, xx + 7 + d, sy + 7, UI_BAR_BAD);
        gfx->drawLine(xx - 7 + d, sy + 7, xx + 7 + d, sy - 7, UI_BAR_BAD);
      }
    }
  }

  // nivel de brillo (centro de la fila): un sol y tres barras, las encendidas son el nivel
  gfx->fillRoundRect(BRI_PILL_X, LANG_PILL_Y, BRI_PILL_W, LANG_PILL_H, 8, UI_WHITE);
  gfx->drawRoundRect(BRI_PILL_X, LANG_PILL_Y, BRI_PILL_W, LANG_PILL_H, 8, UI_INK);
  {
    int sx = BRI_PILL_X + 26, sy = LANG_PILL_Y + LANG_PILL_H / 2;
    gfx->fillCircle(sx, sy, 5, UI_INK);
    static const int8_t RAY[8][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1} };
    for (int i = 0; i < 8; i++) {
      int k = i < 4 ? 8 : 6, e = i < 4 ? 11 : 9;  // las diagonales algo mas cortas
      gfx->drawLine(sx + RAY[i][0] * k, sy + RAY[i][1] * k, sx + RAY[i][0] * e, sy + RAY[i][1] * e, UI_INK);
    }
    drawLevelBars(BRI_PILL_X + 52, BRIGHT_LEVELS - userDim);
  }

  // selector de idioma: una pildora que cicla los 6 idiomas al tocar
  gfx->fillRoundRect(LANG_PILL_X, LANG_PILL_Y, LANG_PILL_W, LANG_PILL_H, 8, UI_WHITE);
  gfx->drawRoundRect(LANG_PILL_X, LANG_PILL_Y, LANG_PILL_W, LANG_PILL_H, 8, UI_INK);
  char lp[10];
  snprintf(lp, sizeof(lp), "%s >", LANG_CODES[gLang]);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(LANG_PILL_X + (LANG_PILL_W - textW(lp, 2)) / 2, LANG_PILL_Y + 8);
  printT(lp);

  drawPowerPills();

  gfx->fillRoundRect(OK_BTN.x, OK_BTN.y, OK_BTN.w, OK_BTN.h, 14, UI_BAR_OK);
  gfx->setTextColor(UI_BG_DAY);
  setSize(3);
  setCur(centerX("OK", 3), OK_BTN.y + 8);  // CX - 18 with the Latin font
  printT("OK");

  gfx->setTextColor(UI_TRACK);
  setSize(2);
  setCur(centerX(T(S_CLOCK_CANCEL), 2), 410);
  printT(T(S_CLOCK_CANCEL));

  // version del firmware (discreta, abajo del todo)
  char ver[20];
  snprintf(ver, sizeof(ver), "TamaPoke v%s", FW_VERSION);
  setSize(1);
  setCur(centerX(ver, 1), 436);
  printT(ver);
  flushUI();
}

void clockTap(int16_t x, int16_t y) {
  if (y >= 190 && y <= 248) {  // fila de botones +/-
    if (x >= 104 && x < 162) clockH = (clockH + 23) % 24;
    else if (x >= 170 && x < 228) clockH = (clockH + 1) % 24;
    else if (x >= 252 && x < 310) clockM = (clockM + 59) % 60;
    else if (x >= 318 && x < 376) clockM = (clockM + 1) % 60;
    return;
  }
  if (SND_PILL.contains(x, y)) {  // cicla sonido: alto, medio, bajo, apagado
    if (!audioEnabled()) {
      audioSetVolume(0);
      audioSetEnabled(true);
    } else if (audioVolume() + 1 < VOL_LEVELS) {
      audioSetVolume(audioVolume() + 1);
    } else {
      audioSetEnabled(false);
    }
    if (audioEnabled()) sfxPlay(SFX_TAP);    // se oye el nivel nuevo
    return;
  }
  if (BRI_PILL.contains(x, y)) {  // cicla brillo: alto, medio, bajo
    userDim = (userDim + 1) % BRIGHT_LEVELS;
    Preferences p;
    p.begin("tamapoke", false);
    p.putUChar("bri", userDim);
    p.end();
    sfxPlay(SFX_TAP);
    return;
  }
  if (LANG_PILL.contains(x, y)) {  // cicla idioma
    setLang((Lang)((gLang + 1) % LANG_COUNT));
    applyLangFont();  // la fuente cambia con el idioma
    sfxPlay(SFX_TAP);
    return;
  }
  PowerPill pill = powerPillAt(x, y);
  if (pill == PILL_TOUCH_LOCK) {
    touchLockMs = touchLockNext(touchLockMs);
    autoTouch.delayMs = touchLockMs;
    Preferences p;
    p.begin("tamapoke", false);
    p.putUChar("tlk", touchLockSel(touchLockMs));
    p.end();
    return;
  }
  if (pill == PILL_SCREEN_OFF) {
    powerOffMs = powerOffNext(powerOffMs);
    Preferences p;
    p.begin("tamapoke", false);
    p.putUChar("off", powerOffSel(powerOffMs));
    p.end();
    return;
  }
  if (pill == PILL_BATTERY) {
    batAsPercent = !batAsPercent;
    Preferences p;
    p.begin("tamapoke", false);
    p.putBool("bpct", batAsPercent);
    p.end();
    return;
  }
  if (OK_BTN.contains(x, y)) { applyClock(); return; }
}

// medallas en la ficha: badge con etiqueta, color si conseguida
void drawMedalBadge(int x, int y, int i) {
  bool got = pet.hasMedal(1 << i);
  gfx->fillRoundRect(x, y, 100, 24, 6, got ? UI_BAR_OK : UI_TRACK);
  if (!got) gfx->drawRoundRect(x, y, 100, 24, 6, UI_TRACK);
  gfx->setTextColor(got ? UI_BG_DAY : 0x9492);
  setSize(2);
  setCur(x + (100 - textW(medalLabel(i), 2)) / 2, y + 5);
  printT(medalLabel(i));
}

// pagina 0: perfil (retrato grande, identidad, racha, vinculo, baya)
void renderCardProfile() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  char head[32];
  snprintf(head, sizeof(head), T(S_NAME_FMT), pet.shiny ? "*" : "", nm, pet.level());
  gfx->setTextColor(d.accent);
  // auto-encoge: a tamano 3 los nombres largos no caben en la franja estrecha de
  // arriba de la pantalla redonda, asi que se cortaban por el borde. El ancho se
  // MIDE con textW(): strlen() cuenta bytes, y en una fila UTF-8 no hay un byte
  // por caracter ni todos miden lo mismo. Con la fuente clasica textW() devuelve
  // strlen()*6*size, o sea exactamente la cuenta anterior: no mueve un pixel en
  // los seis idiomas latinos (11 caracteres a tamano 3 son esos 198 px).
  setSize(3);
  int hts = (textW(head, 3) <= 198) ? 3 : 2;
  setSize(hts);
  setCur(centerX(head, hts), hts == 3 ? 34 : 40);
  printT(head);
  if (pet.nick[0]) {  // especie real bajo el apodo
    const char *sp = dexName(pet.speciesId);
    char par[32];
    snprintf(par, sizeof(par), "(%s)", sp);
    gfx->setTextColor(UI_TRACK);
    setSize(2);
    setCur(centerX(par, 2), 64);   // medido, no contado en bytes
    printT(par);
  }

  // retrato grande animado
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, 206, millis(), true, false, 4);

  // racha con llama
  int sx = 138, sy = 224;
  gfx->fillTriangle(sx + 8, sy, sx + 1, sy + 18, sx + 15, sy + 18, UI_BAR_BAD);
  gfx->fillTriangle(sx + 8, sy + 7, sx + 4, sy + 18, sx + 12, sy + 18, UI_BAR_WARN);
  char rl[40];  // en japones pierde cifras con 30 a partir de 100 dias
  snprintf(rl, sizeof(rl), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(sx + 24, sy + 2);
  printT(rl);

  drawCardStat(258, T(S_VIN), pet.bond, 100, C565(0xd4, 0x52, 0x7e));

  const char *berry = !pet.berryKnown ? T(S_BERRY_UNK)
                      : pet.lovesBerry(0) ? T(S_BERRY_RED)
                      : pet.lovesBerry(1) ? T(S_BERRY_BLUE)
                                          : T(S_BERRY_GREEN);
  char info[48];
  snprintf(info, sizeof(info), T(S_INFO_FMT), berry,
           (unsigned long)(pet.ageMinutes / 1440));
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(info, 2), 296);
  printT(info);

  gfx->setTextColor(UI_TRACK);
  setCur(centerX(T(S_RENAME_HINT), 2), 332);
  printT(T(S_RENAME_HINT));
}

// pagina 1: combate (4 barras + boton de entrenar)
void renderCardStats() {
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(T(S_BATTLE), 3), 48);
  printT(T(S_BATTLE));

  drawCardStat(118, T(S_STAT_ATK), pet.atkStat(), 260, UI_BAR_BAD);
  drawCardStat(160, T(S_STAT_DEF), pet.defStat(), 260, 0x4C98);
  drawCardStat(202, T(S_STAT_SPE), pet.speStat(), 260, UI_BAR_WARN);
  drawCardStat(244, T(S_STAT_WGT), pet.weight, 100, 0xB3C8);

  // boton: saco de entrenamiento de fuerza
  gfx->fillRoundRect(96, 300, 274, 40, 12, UI_BAR_BAD);
  gfx->setTextColor(UI_BG_DAY);
  setSize(2);
  setCur(centerX(T(S_TRAIN_STR), 2), 311);
  printT(T(S_TRAIN_STR));
}

// pagina 2: medallas con etiqueta descriptiva
void renderCardMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[24];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(head, 3), 48);
  printT(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 28 + (i % 2) * 206, y = 104 + (i / 2) * 54;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 196, 44, 10, g ? UI_BAR_OK : UI_TRACK);
    if (g) {  // marca de conseguida
      gfx->fillCircle(x + 22, y + 22, 11, UI_BG_DAY);
      gfx->setTextColor(UI_BAR_OK);
      setSize(2);
      setCur(x + 16, y + 13);
      printT("v");
    }
    gfx->setTextColor(g ? UI_BG_DAY : 0x8410);
    setSize(2);
    setCur(x + 44, y + 14);
    printT(medalDesc(i));
  }

  // records de los minijuegos de Play: icono y mejor puntuacion de cada uno
  for (uint8_t g = 0; g < GAME_COUNT; g++) {
    int x = 61 + g * 88, y = 322;
    gfx->fillRoundRect(x, y, 80, 40, 10, UI_TRACK);
    drawMap(GAME_ICON[g], 16, x + 4, y + 4, 2, false);
    char n[8];
    snprintf(n, sizeof(n), "%u", pet.gameBest(g));
    gfx->setTextColor(UI_INK);
    setSize(2);
    setCur(x + 38 + (40 - textW(n, 2)) / 2, y + 12);
    printT(n);
  }
}

// pagina 3: progreso (nivel, evolucion, descuidos) — saca a la luz mecanicas
// que antes eran invisibles (cuanto falta para subir/evolucionar y por que)
void renderCardProgress() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(T(S_PROGRESS), 3), 44);
  printT(T(S_PROGRESS));

  // nivel grande
  char lv[10];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), pet.level());
  setSize(5);
  setCur(centerX(lv, 5), 86);
  printT(lv);

  // barra de progreso al siguiente nivel (1 nivel = 60 min de juego)
  uint8_t into = pet.ageMinutes % MINUTES_PER_LEVEL;
  int bx = 93, bw = 280, by = 158, bh = 22;
  gfx->fillRoundRect(bx, by, bw, bh, 6, UI_TRACK);
  int fw = (bw - 4) * into / MINUTES_PER_LEVEL;
  if (fw > 0) gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 5, UI_BAR_OK);
  char nx[32];
  snprintf(nx, sizeof(nx), T(S_NEXT_LVL_FMT), MINUTES_PER_LEVEL - into, pet.level() + 1);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(nx, 2), by + 32);
  printT(nx);

  // estado de evolucion
  gfx->setTextColor(UI_TRACK);
  setCur(centerX(T(S_EVO_LABEL), 2), 230);
  printT(T(S_EVO_LABEL));
  char evoBuf[32];
  const char *evo;
  uint16_t evoCol = UI_INK;
  if (d.evolvesTo == 0) {
    evo = T(S_FINAL_FORM);
  } else {
    int needed = d.evolveLevel + pet.careMistakes;
    if (pet.level() >= needed) {
      if (pet.lowestStat() >= 40) { evo = T(S_EVO_READY); evoCol = UI_BAR_OK; }
      else { evo = T(S_EVO_BLOCKED); evoCol = UI_BAR_BAD; }
    } else {
      snprintf(evoBuf, sizeof(evoBuf), T(S_EVO_IN_FMT), needed - pet.level());
      evo = evoBuf;
    }
  }
  gfx->setTextColor(evoCol);
  setCur(centerX(evo, 2), 256);
  printT(evo);

  // descuidos (retrasan la evolucion)
  char ms[24];
  snprintf(ms, sizeof(ms), T(S_MISTAKES_FMT), pet.careMistakes);
  gfx->setTextColor(pet.careMistakes > 0 ? UI_BAR_BAD : UI_INK);
  setCur(centerX(ms, 2), 312);
  printT(ms);
}

// pagina 4: cuidados. Tres botones que ciclan un ajuste cada uno (se guardan al
// tocar): ritmo normal <-> relajado, hora de dormir apagado -> 21-07 -> 22-07 ->
// 23-08, y huevos sorpresa <-> elegir. 72 px de alto para que el tercero acabe en
// y=324, lejos de los puntos de pagina (y=374); ahi el circulo mide 424 px.
#define CARE_BTN_X 73
#define CARE_BTN_W 320
#define CARE_BTN_H 72
#define CARE_PACE_Y 92
#define CARE_BED_Y 172
#define CARE_EGG_Y 252
static const char *const BED_TXT[BED_PRESETS] = { "", "21:00-07:00", "22:00-07:00", "23:00-08:00" };

static void drawCareBtn(int y, const char *label, const char *value) {
  gfx->fillRoundRect(CARE_BTN_X, y, CARE_BTN_W, CARE_BTN_H, 16, UI_WHITE);
  gfx->drawRoundRect(CARE_BTN_X, y, CARE_BTN_W, CARE_BTN_H, 16, UI_INK);
  gfx->setTextColor(UI_TRACK);
  setSize(2);
  setCur(centerX(label, 2), y + 8);
  printT(label);
  gfx->setTextColor(UI_INK);
  setSize(4);
  setCur(centerX(value, 4), y + 32);
  printT(value);
}

void renderCardCare() {
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(T(S_CARE), 3), 44);
  printT(T(S_CARE));
  uint8_t bed = pet.bedPreset < BED_PRESETS ? pet.bedPreset : 0;
  drawCareBtn(CARE_PACE_Y, T(S_PACE), T(pet.pace == PACE_RELAXED ? S_PACE_RELAXED : S_PACE_NORMAL));
  drawCareBtn(CARE_BED_Y, T(S_BEDTIME), bed ? BED_TXT[bed] : T(S_BED_OFF));
  drawCareBtn(CARE_EGG_Y, T(S_EGGS), T(pet.eggPickMode == EGGPICK_ON ? S_EGG_CHOOSE : S_EGG_SURPRISE));
}

// toque en la pagina de cuidados: true si cayo en un boton (cicla, guarda y suena)
bool careTap(int16_t x, int16_t y) {
  if (x < CARE_BTN_X || x > CARE_BTN_X + CARE_BTN_W) return false;
  if (y >= CARE_PACE_Y && y <= CARE_PACE_Y + CARE_BTN_H) {
    pet.setPace(pet.pace == PACE_RELAXED ? PACE_NORMAL : PACE_RELAXED);
  } else if (y >= CARE_BED_Y && y <= CARE_BED_Y + CARE_BTN_H) {
    pet.setBedPreset((pet.bedPreset + 1) % BED_PRESETS);
    uint32_t e = rtcEpoch();
    if (e) bedSample(e);  // vuelve a tomar la referencia: cambiar el horario no duerme ni despierta por si solo
  } else if (y >= CARE_EGG_Y && y <= CARE_EGG_Y + CARE_BTN_H) {
    pet.setEggPickMode(pet.eggPickMode == EGGPICK_ON ? EGGPICK_OFF : EGGPICK_ON);  // vale para el proximo final
  } else {
    return false;
  }
  sfxPlay(SFX_TAP);
  return true;
}

void renderCard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (cardPage == 0) renderCardProfile();
  else if (cardPage == 1) renderCardStats();
  else if (cardPage == 2) renderCardMedals();
  else if (cardPage == 3) renderCardProgress();
  else renderCardCare();

  // indicador de 5 paginas + ayuda
  for (int i = 0; i <= CARD_LAST_PAGE; i++) {
    if (i == cardPage) gfx->fillCircle(181 + i * 26, 374, 5, UI_INK);
    else gfx->drawCircle(181 + i * 26, 374, 4, UI_INK);
  }
  gfx->setTextColor(UI_TRACK);
  setSize(2);
  setCur(centerX(T(S_BACK), 2), 398);
  printT(T(S_BACK));
  flushUI();
}

// ---------- teclado para renombrar ----------

static const char KB_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-";  // 28 + DEL + OK = 30
#define KB_COLS 6
#define KB_X 40
#define KB_Y 150
#define KB_W 64
#define KB_H 52

void openKeyboard() {
  kbOpen = true;
  strncpy(nameBuf, pet.nick, sizeof(nameBuf) - 1);
  nameBuf[sizeof(nameBuf) - 1] = 0;
  nameLen = strlen(nameBuf);
}

void renderKeyboard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(T(S_NAME), 2), 56);
  printT(T(S_NAME));
  // buffer actual
  gfx->fillRoundRect(83, 84, 300, 40, 8, UI_WHITE);
  gfx->drawRoundRect(83, 84, 300, 40, 8, UI_INK);
  setSize(3);
  setCur(95, 94);
  printT(nameLen ? nameBuf : "_");

  for (int i = 0; i < 30; i++) {
    int x = KB_X + (i % KB_COLS) * KB_W, y = KB_Y + (i / KB_COLS) * KB_H;
    bool special = (i >= 28);
    gfx->fillRoundRect(x, y, KB_W - 6, KB_H - 6, 6, special ? UI_BAR_WARN : UI_WHITE);
    gfx->drawRoundRect(x, y, KB_W - 6, KB_H - 6, 6, UI_INK);
    gfx->setTextColor(UI_INK);
    setSize(2);
    if (i < 28) {
      setCur(x + KB_W / 2 - 9, y + KB_H / 2 - 10);
      printT(KB_KEYS[i]);
    } else {
      const char *lab = (i == 28) ? "<-" : "OK";
      setCur(x + KB_W / 2 - 15, y + KB_H / 2 - 10);
      printT(lab);
    }
  }
  flushUI();
}

void keyboardTap(int16_t x, int16_t y) {
  // filtra ANTES de dividir: con x/y menores que KB_X/KB_Y la resta da negativo
  // y la division entera trunca hacia cero (no hacia -inf), asi que col/row
  // saldria 0 en vez de negativo y el filtro de abajo no lo detectaria
  if (x < KB_X || y < KB_Y) return;
  int col = (x - KB_X) / KB_W, row = (y - KB_Y) / KB_H;
  if (col >= KB_COLS || row >= 5) return;
  int i = row * KB_COLS + col;
  if (i >= 30) return;
  if (i == 28) {  // borrar
    if (nameLen) nameBuf[--nameLen] = 0;
  } else if (i == 29) {  // OK
    pet.rename(nameBuf);
    kbOpen = false;
  } else if (nameLen < sizeof(nameBuf) - 1) {
    nameBuf[nameLen++] = KB_KEYS[i];
    nameBuf[nameLen] = 0;
  }
}

// ---------- galeria pokedex ----------

#define GAL_X 73
#define GAL_Y 84
#define GAL_CELL 80

// dibuja una miniatura centrada en su celda; sil=true la pinta en tinta
void drawThumb(const uint8_t *b, int x, int y, int s, bool sil) {
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *pal = b + 3;
  const uint8_t *d = pal + n * 2;
  int ox = x + (GAL_CELL - w * s) / 2;
  int oy = y + (GAL_CELL - h * s) / 2;
  for (int r = 0; r < h; r++) {
    const uint8_t *row = d + r * w;
    // thumbKey(): an index past the thumbnail's own palette (n entries) is transparent
    forRuns(w, [&](int c) { return thumbKey(row[c], n, sil); },
            [&](int c0, int len, int k) {
              uint16_t col = sil ? INK_K : (uint16_t)(pal[k * 2] | (pal[k * 2 + 1] << 8));
              gfx->fillRect(ox + c0 * s, oy + r * s, len * s, s, col);
            });
  }
}

void renderGallery() {
  if (galleryDetail) {  // vista detalle: se redibuja siempre (animada)
    gfx->fillScreen(RGB565_BLACK);
    gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
    const DexEntry &d = DEX_TBL[galleryDetail];
    bool reg = pet.isRegistered(galleryDetail);
    char head[32];
    snprintf(head, sizeof(head), "N.%03d %s%s", galleryDetail,
             pet.isShinyRegistered(galleryDetail) ? "*" : "", reg ? dexName(galleryDetail) : "???");
    gfx->setTextColor(reg ? d.accent : UI_INK);
    // auto-encoge nombres largos (no caben a t3), midiendo en vez de contar
    // bytes; 13 caracteres a tamano 3 son los 234 px de antes.
    setSize(3);
    int gts = (textW(head, 3) <= 234) ? 3 : 2;
    setSize(gts);
    setCur(centerX(head, gts), gts == 3 ? 56 : 60);
    printT(head);
    if (galleryPmd.loaded) {
      // animado y a color si esta registrado; silueta estatica si no (estilo "?")
      drawPmdActM(galleryPmd, PMD_IDLE, CX, 300, reg ? millis() : 0, true, !reg, 6);
    } else {
      const uint8_t *t = thumbs.get(galleryDetail);
      if (t) drawThumb(t, CX - GAL_CELL, 135, 4, !reg);
    }
    gfx->setTextColor(UI_INK);
    setSize(2);
    setCur(centerX(T(S_DETAIL_BACK), 2), 408);
    printT(T(S_DETAIL_BACK));
    flushUI();
    return;
  }

  if (!galleryDirty) return;  // la rejilla es estatica
  galleryDirty = false;

  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char head[24];
  snprintf(head, sizeof(head), T(S_POKEDEX_FMT), pet.registeredCount());
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(head, 3), 36);
  printT(head);

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int16_t dex = galleryPage * 16 + r * 4 + c + 1;
      if (dex > 151) break;
      int x = GAL_X + c * GAL_CELL, y = GAL_Y + r * GAL_CELL;
      const uint8_t *t = thumbs.get(dex);
      if (t) {
        drawThumb(t, x, y, 2, !pet.isRegistered(dex));
        if (pet.isShinyRegistered(dex)) {
          gfx->setTextColor(UI_BAR_WARN);
          setSize(2);
          setCur(x + 62, y + 4);
          printT("*");
        }
      } else {
        char num[6];
        snprintf(num, sizeof(num), "%d", dex);
        gfx->setTextColor(UI_TRACK);
        setSize(2);
        setCur(x + 24, y + 32);
        printT(num);
      }
    }
  }
  // puntos de pagina
  for (int i = 0; i < 10; i++) {
    if (i == galleryPage) gfx->fillCircle(170 + i * 14, 436, 4, UI_INK);
    else gfx->drawCircle(170 + i * 14, 436, 3, UI_INK);
  }
  flushUI();
}

void galleryTap(int16_t x, int16_t y) {
  if (galleryDetail) {  // volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  if (y < 72) {  // tocar la cabecera = salir
    galleryOpen = false;
    galleryPmd.unload();
    return;
  }
  // mismo motivo que en el teclado: filtrar antes de dividir
  if (x < GAL_X || y < GAL_Y) return;
  int c = (x - GAL_X) / GAL_CELL, r = (y - GAL_Y) / GAL_CELL;
  if (c > 3 || r > 3) return;
  int16_t dex = galleryPage * 16 + r * 4 + c + 1;
  if (dex > 151) return;
  galleryDetail = dex;
  galleryPmd.load(dex, pet.isShinyRegistered(dex));
}
