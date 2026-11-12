#include "framework.h"
#include "../panelpower.h"

TEST(panel, sleep_is_brightness_0_then_display_off_then_sleep_in) {
  PanelStep s[8];
  int n = panelSleepProgram(s, 8);
  CHECK_EQ(n, 5);
  CHECK_EQ((int)s[0].kind, (int)PANEL_REG);
  CHECK_EQ((int)s[0].a, 0x51);
  CHECK_EQ((int)s[0].b, 0);
  CHECK_EQ((int)s[1].a, 0x28);
  CHECK_EQ((int)s[2].waitMs, 120);
  CHECK_EQ((int)s[3].a, 0x10);
  CHECK_EQ((int)s[4].waitMs, 120);
}

TEST(panel, wake_is_sleep_out_then_display_on_then_brightness) {
  PanelStep s[8];
  int n = panelWakeProgram(s, 8, 180);
  CHECK_EQ(n, 4);
  CHECK_EQ((int)s[0].a, 0x11);
  CHECK_EQ((int)s[1].waitMs, 120);
  CHECK_EQ((int)s[2].a, 0x29);
  CHECK_EQ((int)s[3].a, 0x51);
  CHECK_EQ((int)s[3].b, 180);
}

TEST(panel, the_sequence_is_sent_once_per_edge) {
  bool asleep = false;
  CHECK(panelSleepEdge(asleep, true));
  CHECK(!panelSleepEdge(asleep, true));
  CHECK(panelSleepEdge(asleep, false));
  CHECK(!panelSleepEdge(asleep, false));
}
