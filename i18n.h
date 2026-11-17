#pragma once
#include <Arduino.h>

// The UI languages (#61): English and European Portuguese.
//
// Text is drawn with Arduino_GFX's classic 5x7 font, a full CP437 table, so the
// accented letters go in as single bytes in octal (\240 a-acute, \207 c-cedilla,
// \202 e-acute...), never as UTF-8: the UI centres with strlen(t)*6, and a two-byte
// character would throw the labels off. The 12 Portuguese letters CP437 lacks (a~ o~
// and the accented capitals but C-cedilla and E-acute) are extra glyphs on their
// Latin-1 bytes (\303 A~, \343 a~...): see ptglyphs.h.
enum Lang : uint8_t { LANG_EN = 0, LANG_PT, LANG_COUNT };
#define LANG_DEFAULT LANG_EN  // the default language: English

// The `lang` key in NVS namespace `tamapoke` keeps the codes of the old eight-language
// enum (ES 0, EN 1, FR 2, DE 3, IT 4, PT 5, JA 6, KO 7), so the player's choice survives
// the update, and an older image flashed later still reads it. 1 is English and 5 is
// Portuguese; anything else (a removed language, a missing key, garbage) loads as
// English. loadLang() never rewrites the key: a board on a removed language keeps its
// byte until the player taps the language pill.
#define LANG_NVS_EN 1
#define LANG_NVS_PT 5
inline Lang langFromNvs(uint8_t v) { return v == LANG_NVS_PT ? LANG_PT : LANG_EN; }
inline uint8_t langToNvs(Lang l) { return l == LANG_PT ? LANG_NVS_PT : LANG_NVS_EN; }

extern Lang gLang;  // idioma activo (definido en i18n.cpp)

// IDs de cadena. El orden debe coincidir con la tabla STRINGS de i18n.cpp.
enum StrId : uint8_t {
  // estado del bicho (statusMsg)
  S_EVOLVING, S_EATING, S_LIKES, S_HUNGRY, S_NEEDS_BATH,
  S_EXHAUSTED, S_SAD, S_CHUBBY, S_IS_SHINY, S_HAPPY,
  // ceremonias de despedida
  S_FAREWELL, S_RUNAWAY, S_GOODBYE,
  // huevo
  S_EGG_HDR, S_EGG_LEGEND, S_EGG_RARE, S_EGG_TOUCH, S_EGG_MOVES, S_EGG_ALMOST,
  // formatos compartidos
  S_POKEDEX_FMT,   // "POKEDEX %u/151"
  S_NAME_FMT,      // "%s%s Nv.%u"
  // dialogo soltar
  S_RELEASE_FMT, S_YES, S_NO,
  // minijuego y ejercicios (#78): the punch's result and hint
  S_PUNCHES_FMT, S_STR_GAIN_FMT, S_NEW_RECORD, S_RECORD_FMT, S_PUNCH_HINT,
  // the calm exercises (#78): the hold's result and hints, the stat card's TRAIN button
  // and the choice it opens (title, then each exercise's name and what it trains)
  S_REPS_FMT, S_STA_GAIN_FMT, S_HOLD_HINT, S_HOLD_KEEP,
  S_ONE_PUNCH, S_ONE_REP,  // the results' singular: "1 PUNCH", "1 REP"
  S_TRAIN, S_EX_TITLE, S_EX_PUNCH, S_EX_STR, S_EX_HOLD, S_EX_STA,
  S_TRAIN_ASLEEP,  // the toast when TRAIN is tapped while the pet sleeps (#89)
  S_SPD_GAIN_FMT,  // the Play minigame result's speed gain (#76), as the punch shows its strength
  S_SCORE_FMT, S_GREAT_JOY, S_PLUS_JOY,
  // reloj / ajustes
  S_SET_TIME, S_HOUR, S_MIN, S_CLOCK_CANCEL,
  S_CLOCK_ASK,  // clock screen title while the clock is stale (reseeded after a power-off)
  // celebracion
  S_MEDAL_BANNER, S_GREAT, S_STREAK_DAYS_FMT,
  // ficha: perfil
  S_STREAK_FMT, S_VIN, S_BERRY_UNK, S_BERRY_RED, S_BERRY_BLUE, S_BERRY_GREEN,
  S_INFO_FMT, S_RENAME_HINT,
  // ficha: perfil, the nature (#25): "NATURE %s" and the names in NAT_* order (nature.h)
  S_NATURE_FMT, S_NAT_EASYGOING, S_NAT_GREEDY, S_NAT_PLAYFUL, S_NAT_TIDY, S_NAT_LAZY,
  // ficha: fitness (#76; the page was "battle"): the title, the four bars' labels, and
  // under each trained bar what trains it
  S_FITNESS, S_STAT_STR, S_STAT_STA, S_STAT_SPD, S_STAT_WGT,
  S_TRAIN_HINT_STR, S_TRAIN_HINT_STA, S_TRAIN_HINT_SPD,
  // ficha: medallas
  S_MEDALS_FMT, S_BACK,
  // teclado y galeria
  S_NAME, S_DETAIL_BACK,
  // barras
  S_BAR_FOOD, S_BAR_JOY, S_BAR_ENE, S_BAR_HYG,
  // marcador en vivo del minijuego
  S_REC_FMT,
  // ficha: pagina de progreso
  S_PROGRESS, S_LVL_FMT, S_NEXT_LVL_FMT, S_EVO_LABEL, S_FINAL_FORM,
  S_EVO_READY, S_EVO_BLOCKED, S_EVO_IN_FMT, S_MISTAKES_FMT,
  // interruptor de sonido (ajustes)
  S_SND_ON, S_SND_OFF,
  S_EVO_TAP,        // texto del boton de evolucion
  S_FAREWELL_BTN,   // texto del boton de despedida (lleva el nombre: "%s ...")
  S_RUNAWAY_BTN,    // texto del boton de escapada por abandono (final triste)
  // dialogos de decision (evolucionar/mantener, despedirse/quedaros)
  S_EVO_Q, S_EVO_KEEP, S_FAR_Q, S_FAR_GO, S_FAR_STAY,
  S_CHOOSE_STARTER,  // titulo de la eleccion del inicial (primera vez)
  S_NO_SPRITES, S_LOAD_SPRITES,  // aviso cuando falta el sprite en la SD
  S_HOLD_EXIT,  // one-second exit hold: every Play minigame, and the exercises on the 1.75C
  S_TLOCK_OFF,  // "off" — automatic touch lock disabled
  // ficha: pagina Care (ritmo y hora de dormir; los horarios van en digitos)
  S_CARE, S_PACE, S_PACE_NORMAL, S_PACE_RELAXED, S_BEDTIME, S_BED_OFF,
  // eleccion de huevo: titulo, pista bajo el huevo tocado, rareza comun, y el
  // boton de la pagina Care (etiqueta + sus dos valores)
  S_CHOOSE_EGG, S_EGG_TAP_AGAIN, S_EGG_COMMON, S_EGGS, S_EGG_CHOOSE, S_EGG_SURPRISE,
  // minijuegos de Play: nombre y pista de la portada (orden de GameId), Berry Song
  S_GAME_KEEPY, S_GAME_CATCH, S_GAME_PEEK, S_GAME_SONG,
  S_HINT_KEEPY, S_HINT_CATCH, S_HINT_PEEK, S_HINT_SONG,
  S_SONG_LISTEN, S_SONG_TURN,
  // speech bubbles (#23): what the pet says, in BubbleLine order (bubble.h)
  S_SAY_HUNGRY, S_SAY_BATH, S_SAY_TIRED, S_SAY_BORED,
  S_SAY_SICK,  // sick (#15)
  S_SAY_MORNING, S_SAY_STARS,
  S_SAY_MILK,  // the warm-milk hint (#13)
  S_SAY_HI, S_SAY_HAPPY, S_SAY_PAT, S_SAY_LOVE_FMT, S_SAY_LOVE,
  // the natures' lines (#86), three for each nature in NAT_* order: the first names it with
  // the stat card's word (natureName()), and none asks for care
  S_SAY_EASYGOING_1, S_SAY_EASYGOING_2, S_SAY_EASYGOING_3, S_SAY_GREEDY_1, S_SAY_GREEDY_2, S_SAY_GREEDY_3,
  S_SAY_PLAYFUL_1, S_SAY_PLAYFUL_2, S_SAY_PLAYFUL_3, S_SAY_TIDY_1, S_SAY_TIDY_2, S_SAY_TIDY_3,
  S_SAY_LAZY_1, S_SAY_LAZY_2, S_SAY_LAZY_3,
  // the special days' greetings (#26, holiday.h), in BubbleLine order: each with the
  // player's name, then without
  S_SAY_CHRISTMAS_FMT, S_SAY_CHRISTMAS, S_SAY_NEW_YEAR_FMT, S_SAY_NEW_YEAR,
  S_SAY_VALENTINE_FMT, S_SAY_VALENTINE, S_SAY_EASTER_FMT, S_SAY_EASTER,
  S_SAY_HALLOWEEN_FMT, S_SAY_HALLOWEEN, S_SAY_BIRTHDAY_FMT, S_SAY_BIRTHDAY,
  // 1.75C toasts (#61): the BOOT button's sound toggle and the screen lock
  S_TOAST_SOUND_ON, S_TOAST_MUTED, S_TOAST_LOCKED, S_TOAST_UNLOCKED,
  // the standby clock's date (#60): weekday, day, month ("Sat 15 Nov"; see formatDate())
  S_DATE_FMT,
  // sickness and medicine (#15): the Care page's row (label and "On"; "Off" is S_BED_OFF), the
  // status line, the medicine button, the status while it takes it, and the Progress page
  S_SICKNESS, S_ON, S_SICK, S_GIVE_MEDICINE, S_BETTER, S_EVO_SICK,
  STR_COUNT
};

const char *T(StrId id);       // texto en el idioma activo
const char *natureName(uint8_t n);  // the nature's name (#25, NAT_*); easygoing's when out of range
const char *medalName(int i);  // banner de medalla (MED_COUNT)
const char *medalDesc(int i);  // descripcion larga de medalla
// The celebration banner's two lines (drawCelebration, scene.cpp; #82, #83) for the banner on
// screen (Pet::bannerShown(), BannerQueue in pet.h): a medal's bit gives "MEDAL!" and its name;
// BANNER_STREAK gives "AWESOME!" and "<streakDays> DAY STREAK!" (a buffer of its own, valid
// until the next call).
void bannerLines(int banner, uint16_t streakDays, const char **l1, const char **l2);
// The date as the standby clock shows it (#60), short: "Sat 15 Nov", "s\240b, 15 nov".
// `weekday` 0 (Sunday) to 6, `month` 1 to 12 (CivilDate, weather.h); out of range: "?".
void formatDate(char *buf, size_t n, uint8_t weekday, uint8_t day, uint8_t month);

void loadLang();             // reads the language from NVS (call in setup); never writes
void setLang(Lang l);        // switches and saves the language (an invalid one is ignored)
