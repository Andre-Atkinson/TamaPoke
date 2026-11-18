// Contact sheets of an accessory on every species (#116), for checking by eye where it sits:
// tools/acc_sheet.sh builds and runs this. Each species' sprite is loaded as the firmware loads
// it (PmdMon::load, from the sim's sprite folder, so with its heads found and accfix.h's skip),
// and each frame is drawn with drawPmdFrame() and the accessory with drawAccOn(), the function
// drawPet() uses, into the sketch's own canvas, then copied into the sheet. So the sheet shows
// what the board draws, at a fixed scale.
//
//   tamapoke_accsheet [--sprites DIR] [--out DIR] [--acc NAME] [--scale N] [--pose NAME]...
//                     [--dex N]... [--all-frames]
//
// One PNG per pose and 16 species (sheet-<pose>-<first>-<last>.png): poses stand (IDLE), walkl,
// walkr, eat, sad (Hurt), pose, nod, breath and sit; all of them unless --pose names some. Each
// cell is a species: its frame 0, then the frames where the head is highest and lowest (when
// they differ), with the species' number over it, grey when the pose is hidden by accfix.h and
// red when a frame has no head at all. --dex keeps only the species named (one sheet per pose:
// sheet-<pose>-dex.png), and --all-frames shows every frame of each.
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../accessory.h"
#include "../accfix.h"
#include "../app.h"
#include "../dex.h"
#include "../scene.h"
#include "../sdmon.h"
#include "boot.h"
#include "png.h"
#include "simhw.h"

void setup();

namespace {

struct Pose {
  const char *name;
  uint8_t act;
};
const Pose POSES[] = {
  { "stand", PMD_IDLE }, { "walkl", PMD_WALKL }, { "walkr", PMD_WALKR }, { "eat", PMD_EAT },
  { "sad", PMD_HURT },   { "pose", PMD_POSE },   { "nod", PMD_NOD },     { "breath", PMD_BREATH },
  { "sit", PMD_SIT },
};
const int PER_SHEET = 16, COLS = 4, LABEL_H = 18, PAD = 4;
const uint16_t BG = 0xE73C, CELL_BG = 0xFFFF, LINE = 0xC618;

struct Sheet {
  int w, h;
  std::vector<uint16_t> px;
  void fill(int x, int y, int ww, int hh, uint16_t c) {
    for (int j = y; j < y + hh; j++)
      for (int i = x; i < x + ww; i++)
        if (i >= 0 && j >= 0 && i < w && j < h) px[(size_t)j * w + i] = c;
  }
};

// up to three frames of a: frame 0, then the highest and the lowest head when they differ
std::vector<uint8_t> framesToShow(const PmdAct &a) {
  std::vector<uint8_t> out{ 0 };
  if (!a.head) return out;
  uint8_t hi = 0, lo = 0;
  for (uint8_t f = 1; f < a.frames; f++) {
    if (a.head[f * 3] < a.head[hi * 3]) hi = f;
    if (a.head[f * 3] > a.head[lo * 3]) lo = f;
  }
  for (uint8_t f : { hi, lo })
    if (a.head[f * 3] != a.head[0] || a.head[f * 3 + 1] != a.head[1]) {
      bool dup = false;
      for (uint8_t g : out) dup = dup || (a.head[g * 3] == a.head[f * 3] && a.head[g * 3 + 1] == a.head[f * 3 + 1]);
      if (!dup) out.push_back(f);
    }
  return out;
}

// Frame fi of action act of m with the accessory over it (unless hidden), drawn on the sketch's
// canvas as drawPet() draws them, cut down to what was drawn; `ground`: its feet's row in it
struct Tile {
  int w = 0, h = 0, ground = 0;
  std::vector<uint16_t> px;
};
Tile drawTile(const PmdMon &m, uint8_t act, uint8_t fi, uint8_t acc, uint8_t s, bool hidden) {
  const PmdAct &a = m.acts[act];
  gfx->fillScreen(CELL_BG);
  int x0 = (ACC_ART_MAX_W + 2) * s, y0 = (ACC_ART_MAX_H + 2) * s;  // room for the art all round
  int feet = a.base ? a.base : a.h;
  drawPmdFrame(m, act, fi, (uint8_t)feet, x0 + a.w * s / 2, y0 + feet * s, s);
  if (!hidden) drawAccOn(m, act, fi, x0, y0, s, acc);  // the canvas drawPmdFrame drew at (x0, y0)
  const uint16_t *fb = gfx->getFramebuffer();
  int bx0 = SIM_W, by0 = SIM_H, bx1 = -1, by1 = -1;
  for (int y = 0; y < SIM_H; y++)
    for (int x = 0; x < SIM_W; x++)
      if (fb[y * SIM_W + x] != CELL_BG) {
        bx0 = x < bx0 ? x : bx0;
        bx1 = x > bx1 ? x : bx1;
        by0 = y < by0 ? y : by0;
        by1 = y > by1 ? y : by1;
      }
  Tile t;
  if (bx1 < 0) return t;
  t.w = bx1 - bx0 + 1;
  t.h = by1 - by0 + 1;
  t.ground = y0 + feet * s - by0;
  t.px.resize((size_t)t.w * t.h);
  for (int y = 0; y < t.h; y++)
    for (int x = 0; x < t.w; x++) t.px[(size_t)y * t.w + x] = fb[(by0 + y) * SIM_W + bx0 + x];
  return t;
}

bool sheetFor(const Pose &pose, const std::vector<int> &dexes, const std::string &file, uint8_t acc, uint8_t s,
              bool allFrames, const std::string &out) {
  struct Cell {
    std::string label;
    uint16_t ink;
    std::vector<Tile> tiles;
  };
  std::vector<Cell> cells;
  int cellW = 0, above = 0, below = 0;
  for (int dex : dexes) {
    PmdMon m;
    if (!m.load((uint8_t)dex)) fprintf(stderr, "accsheet: no sprite for #%d\n", dex);
    const PmdAct &a = m.acts[pose.act];
    bool mirrored;
    const AccFix *fix = accFixFor(dex, pose.act, mirrored);
    bool hidden = fix && fix->hide;
    bool missing = false;
    if (a.frames && a.head)
      for (uint8_t f = 0; f < a.frames; f++) missing = missing || !a.head[f * 3 + 2];
    Cell c;
    char label[48];
    snprintf(label, sizeof(label), "#%d %s%s%s", dex, dexName(dex), fix ? (hidden ? " hide" : " fix") : "",
             a.frames ? "" : " -");
    c.label = label;
    c.ink = missing ? 0xE8E4 : hidden ? 0x8410 : 0x0000;
    int w = PAD;
    std::vector<uint8_t> fs = framesToShow(a);
    if (allFrames) {
      fs.clear();
      for (uint8_t f = 0; f < a.frames; f++) fs.push_back(f);
    }
    if (a.frames)
      for (uint8_t fi : fs) {
        Tile t = drawTile(m, pose.act, fi, acc, s, hidden);
        w += t.w + 2 * PAD;
        above = t.ground > above ? t.ground : above;
        below = t.h - t.ground > below ? t.h - t.ground : below;
        c.tiles.push_back(std::move(t));
      }
    int lw = (int)c.label.size() * 12 + 2 * PAD;
    w = w > lw ? w : lw;
    cellW = w > cellW ? w : cellW;
    cells.push_back(std::move(c));
    m.unload();
  }
  int cellH = LABEL_H + above + below + 2 * PAD;
  const int cols = allFrames ? 1 : COLS;  // every frame: one species a row
  Sheet sh;
  sh.w = cols * (cellW + PAD) + PAD;
  sh.h = ((int)(cells.size() + cols - 1) / cols) * (cellH + PAD) + PAD;
  sh.px.assign((size_t)sh.w * sh.h, BG);
  const uint16_t *fb = gfx->getFramebuffer();
  for (size_t n = 0; n < cells.size(); n++) {
    const Cell &c = cells[n];
    int cx0 = PAD + (int)(n % cols) * (cellW + PAD), cy0 = PAD + (int)(n / cols) * (cellH + PAD);
    sh.fill(cx0, cy0, cellW, cellH, CELL_BG);
    gfx->fillScreen(CELL_BG);  // the label: drawn on the canvas, then copied
    gfx->setTextSize(2);
    gfx->setTextColor(c.ink);
    gfx->setCursor(0, 0);
    gfx->print(c.label.c_str());
    for (int j = 0; j < 16; j++)
      for (int i = 0; i < cellW - PAD && i < SIM_W; i++) sh.px[(size_t)(cy0 + 2 + j) * sh.w + cx0 + PAD + i] = fb[j * SIM_W + i];
    int x = cx0 + PAD, groundY = cy0 + LABEL_H + PAD + above;
    for (const Tile &t : c.tiles) {
      for (int j = 0; j < t.h; j++)
        for (int i = 0; i < t.w; i++) sh.px[(size_t)(groundY - t.ground + j) * sh.w + x + i] = t.px[(size_t)j * t.w + i];
      sh.fill(x - PAD / 2, groundY, t.w + PAD, 1, LINE);  // its feet's line
      x += t.w + 2 * PAD;
    }
  }
  std::string path = out + "/sheet-" + pose.name + "-" + file + ".png";
  if (!pngWriteRgb565(path, sh.px.data(), sh.w, sh.h, false)) {
    fprintf(stderr, "accsheet: cannot write %s\n", path.c_str());
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  std::string sprites = "tools/sdcard", out = "acc-sheets";
  uint8_t acc = ACC_CAP, s = 3;
  std::vector<const Pose *> poses;
  std::vector<int> only;
  bool allFrames = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    bool more = i + 1 < argc;
    if (a == "--sprites" && more) sprites = argv[++i];
    else if (a == "--out" && more) out = argv[++i];
    else if (a == "--acc" && more) {
      int id = accFromKey(argv[++i]);
      if (id <= ACC_NONE) {
        fprintf(stderr, "accsheet: no accessory '%s'\n", argv[i]);
        return 2;
      }
      acc = (uint8_t)id;
    } else if (a == "--scale" && more) {
      int v = atoi(argv[++i]);
      if (v < 1 || v > 5) {
        fprintf(stderr, "accsheet: --scale is 1 to 5\n");
        return 2;
      }
      s = (uint8_t)v;
    } else if (a == "--dex" && more) {
      int d = atoi(argv[++i]);
      if (d < 1 || d > DEX_COUNT) {
        fprintf(stderr, "accsheet: --dex is 1 to %d\n", DEX_COUNT);
        return 2;
      }
      only.push_back(d);
    } else if (a == "--all-frames") {
      allFrames = true;
    } else if (a == "--pose" && more) {
      const char *p = argv[++i];
      const Pose *found = nullptr;
      for (const Pose &q : POSES)
        if (!strcmp(q.name, p)) found = &q;
      if (!found) {
        fprintf(stderr, "accsheet: no pose '%s' (stand walkl walkr eat sad pose nod breath sit)\n", p);
        return 2;
      }
      poses.push_back(found);
    } else {
      fprintf(stderr,
              "usage: tamapoke_accsheet [--sprites DIR] [--out DIR] [--acc NAME] [--scale 1-5] [--pose NAME]...\n"
              "                         [--dex N]... [--all-frames]\n");
      return 2;
    }
  }
  if (poses.empty())
    for (const Pose &q : POSES) poses.push_back(&q);
  if (!simMkdirs(out)) {
    fprintf(stderr, "accsheet: cannot create %s\n", out.c_str());
    return 2;
  }
  SimBootConfig boot;
  boot.sprites = sprites;
  simBootPrepare(boot);
  setup();  // the sketch's boot: the canvas, the sprite folder mounted
  int sheets = 0;
  for (const Pose *p : poses) {
    if (!only.empty()) {
      if (!sheetFor(*p, only, "dex", acc, s, allFrames, out)) return 1;
      sheets++;
      continue;
    }
    for (int first = 1; first <= DEX_COUNT; first += PER_SHEET) {
      int last = first + PER_SHEET - 1 < DEX_COUNT ? first + PER_SHEET - 1 : DEX_COUNT;
      std::vector<int> dexes;
      for (int d = first; d <= last; d++) dexes.push_back(d);
      char file[16];
      snprintf(file, sizeof(file), "%03d-%03d", first, last);
      if (!sheetFor(*p, dexes, file, acc, s, allFrames, out)) return 1;
      sheets++;
    }
  }
  printf("accsheet: %d sheets in %s (%s at scale %u)\n", sheets, out.c_str(), ACC_KEYS[acc], s);
  return 0;
}
