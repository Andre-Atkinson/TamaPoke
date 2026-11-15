#include "i18n.h"
#include "pet.h"        // MED_COUNT
#include <Preferences.h>

Lang gLang = LANG_DEFAULT;

// String table [language][id], in StrId order (i18n.h). Accented letters are single
// bytes in octal, never UTF-8 (see i18n.h).
static const char *const STRINGS[LANG_COUNT][STR_COUNT] = {
  // ---------------- EN ----------------
  {
    "Evolving!", "Yum yum!", "It likes it!", "It's hungry!", "Needs a bath!",
    "Worn out...", "Feeling sad...", "A bit chubby...", "It's SHINY!!", "It's happy",
    "THANKS! Farewell", "It ran away...", "Bye! Waving goodbye...",
    "EGG", "Legendary egg!?", "Rare egg!", "Tap the egg...", "It moves!", "Almost there!",
    "POKEDEX %u/151",
    "%s%s Lv.%u",
    "Release %s?", "YES", "NO",
    "%u HITS", "STR +%u", "NEW RECORD!", "BEST: %u", "HIT FAST!",
    "SCORE: %u", "So much fun!", "+happiness",
    "SET TIME", "HOUR", "MIN", "swipe up: cancel", "WHAT TIME?",
    "MEDAL!", "AWESOME!", "%u DAY STREAK!",
    "STREAK %u  best %u", "BOND", "BERRY ???", "RED BERRY", "BLUE BERRY", "GREEN BERRY",
    "%s   AGE %lud", "tap name: rename",
    "BATTLE", "ATK", "DEF", "SPD", "WGT", "TRAIN STRENGTH",
    "MEDALS %d/%d", "tap: back",
    "NAME:", "tap to go back",
    "FOOD", "JOY", "ENE", "HYG",
    "BEST %u",
    "PROGRESS", "Lv.%u", "%u min to Lv.%u", "EVOLUTION", "Final form",
    "Ready to evolve!", "All needs >=40 to evolve",
    "Evolves in %u lv.", "Slip-ups: %u",
    "SND ON", "SND OFF",
    "EVOLVE!", "%s wants to tell you...", "%s feels abandoned...",
    "Evolve?", "Keep form", "Say goodbye?", "Goodbye", "Stay together",
    "Choose your starter",
    "No sprites", "Load them onto the SD",
    "Hold to exit",
    "off",
    "CARE", "PACE", "Normal", "Relaxed", "BEDTIME", "Off",
    "Choose an egg!", "Tap again to choose", "Common egg", "EGGS", "Choose", "Surprise",
    "Keepy-uppy", "Berry Catch", "Peekaboo", "Berry Song",
    "Tap the ball!", "Slide to catch!", "Tap when I pop up!", "Watch, then repeat!",
    "Listen...", "Your turn!",
    // speech bubbles (#23): what the pet says, in BubbleLine order (bubble.h)
    "I'm hungry!", "I need a bath!", "I'm tired...", "I'm bored...",
    "Good morning!", "Look at the stars!", "Hi there!", "I'm happy!", "Pat me!",
    "I love you, %s!", "I love you!",
    // 1.75C toasts (#61)
    "Sound on", "Muted", "Locked", "Unlocked",
    // the standby clock's date (#60)
    "%s %u %s",
  },
  // ---------------- PT (European Portuguese, #61) ----------------
  // One form of address throughout: "tu" (toca, desliza, escolhe, ouve, mantem),
  // with enclisis (faz-me, adoro-te) and "a" + infinitive (a evoluir). The masculine
  // is the generic for the pet, agreeing with "o Pokemon". Accents: CP437 bytes where
  // the font has the letter, Latin-1 bytes for the 12 it lacks (ptglyphs.h):
  //   \240 a'  \205 a`  \203 a^  \343 a~  \207 c,  \202 e'  \210 e^  \241 i'
  //   \242 o'  \223 o^  \365 o~  \243 u'  \200 C,  \220 E'
  //   \301 A'  \300 A`  \302 A^  \303 A~  \312 E^  \315 I'  \323 O'  \324 O^  \325 O~  \332 U'
  {
    "A evoluir!", "Nham nham!", "Gosta disto!", "Tem fome!", "Precisa de banho!",
    "Exausto...", "Triste...", "Um pouco gordinho...", "\220 SHINY!!", "Est\240 feliz",
    "OBRIGADO! Adeus", "Fugiu...", "Adeus! A acenar...",
    "OVO", "Ovo lend\240rio!?", "Ovo raro!", "Toca no ovo...", "Mexe-se!", "Quase l\240!",
    "POKEDEX %u/151",
    "%s%s N\241v.%u",
    "Libertar %s?", "SIM", "N\303O",
    "%u GOLPES", "FOR\200A +%u", "NOVO RECORDE!", "RECORDE: %u", "BATE R\301PIDO!",
    "PONTOS: %u", "Que alegria!", "+alegria",
    "ACERTAR HORA", "HORA", "MIN", "para cima: cancelar", "QUE HORAS S\303O?",
    "MEDALHA!", "\323TIMO!", "%u DIAS SEGUIDOS!",
    "SEQ %u  rec %u", "LA\200O", "BAGA ???", "BAGA VERMELHA", "BAGA AZUL", "BAGA VERDE",
    "%s   IDADE %lud", "toca no nome: renomear",
    "COMBATE", "ATQ", "DEF", "VEL", "PES", "TREINAR FOR\200A",
    "MEDALHAS %d/%d", "toca: voltar",
    "NOME:", "toca para voltar",
    "COM", "ALE", "ENE", "HIG",
    "REC %u",
    "PROGRESSO", "N\241v.%u", "%u min at\202 N\241v.%u", "EVOLU\200\303O", "Forma final",
    "Pronto a evoluir!", "Tudo a 40 para evoluir",
    "Evolui em %u n\241v.", "Descuidos: %u",
    "SOM ON", "SOM OFF",
    "EVOLUIR!", "%s quer dizer-te algo...", "%s sente-se sozinho...",
    "Evoluir?", "Manter forma", "Dizer adeus?", "Adeus", "Ficar juntos",
    "Escolhe o teu inicial",
    "Sem sprites", "Carrega-os no SD",
    "Mant\202m: sair",
    "off",
    "CUIDADOS", "RITMO", "Normal", "Calmo", "HORA DE DORMIR", "Desligado",
    "Escolhe um ovo!", "Toca de novo para escolher", "Ovo comum", "OVOS", "Escolher", "Surpresa",
    "Toques de bola", "Apanha-bagas", "Cucu!", "M\243sica das bagas",
    "Toca na bola!", "Desliza e apanha!", "Toca quando eu aparecer!", "Olha e repete!",
    "Ouve...", "A tua vez!",
    // speech bubbles (#23): what the pet says, in BubbleLine order (bubble.h)
    "Tenho fome!", "Preciso de um banho!", "Estou com sono...", "Que t\202dio...",
    "Bom dia!", "Olha as estrelas!", "Ol\240!", "Estou feliz!", "Faz-me festinhas!",
    "Adoro-te, %s!", "Adoro-te!",
    // 1.75C toasts (#61)
    "Som ligado", "Sem som", "Bloqueado", "Desbloqueado",
    // the standby clock's date (#60)
    "%s, %u %s",
  },
};

// Medal names in two lengths [language][medal]: the banner and the description.
static const char *const MED_NAME[LANG_COUNT][MED_COUNT] = {
  { "Lv.10", "Lv.25", "Lv.50", "BERRY", "7 STREAK", "BOND", "TOP FORM", "IN SHAPE" },
  { "N\241v.10", "N\241v.25", "N\241v.50", "BAGA", "SEQ 7", "LA\200O", "FORMA M\301X.", "EM FORMA" },
};
static const char *const MED_DSC[LANG_COUNT][MED_COUNT] = {
  { "LEVEL 10", "LEVEL 25", "LEVEL 50", "BERRY FOUND",
    "7 DAY STREAK", "MAX BOND", "FINAL FORM", "IN SHAPE" },
  { "N\315VEL 10", "N\315VEL 25", "N\315VEL 50", "BAGA ACHADA",
    "7 DIAS A FIO", "LA\200O M\301X.", "FORMA FINAL", "EM FORMA" },
};

// The standby clock's date (#60), short [language][...]: English as in "Sat 15 Nov",
// European Portuguese in lower case, as it writes them ("s\240b, 15 nov").
static const char *const WEEKDAY[LANG_COUNT][7] = {
  { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
  { "dom", "seg", "ter", "qua", "qui", "sex", "s\240b" },
};
static const char *const MONTH[LANG_COUNT][12] = {
  { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" },
  { "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez" },
};

const char *T(StrId id) { return STRINGS[gLang][id]; }

void formatDate(char *buf, size_t n, uint8_t weekday, uint8_t day, uint8_t month) {
  if (!n) return;
  if (weekday > 6 || month < 1 || month > 12 || day < 1 || day > 31) {
    snprintf(buf, n, "?");
    return;
  }
  snprintf(buf, n, T(S_DATE_FMT), WEEKDAY[gLang][weekday], (unsigned)day, MONTH[gLang][month - 1]);
}
const char *medalName(int i)  { return MED_NAME[gLang][i]; }
const char *medalDesc(int i)  { return MED_DSC[gLang][i]; }

// The stored byte keeps the old enum's codes (langFromNvs/langToNvs, i18n.h). Read-only:
// a board on a removed language shows English and keeps its byte until the player
// picks a language.
void loadLang() {
  Preferences p;
  p.begin("tamapoke", true);  // read-only
  uint8_t v = p.getUChar("lang", LANG_NVS_EN);
  p.end();
  gLang = langFromNvs(v);
}

void setLang(Lang l) {
  if (l >= LANG_COUNT) return;
  gLang = l;
  Preferences p;
  p.begin("tamapoke", false);
  p.putUChar("lang", langToNvs(l));
  p.end();
}
