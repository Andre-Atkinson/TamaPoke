// The pet's own life on the main screen (#22): idlelife.cpp runs the planner in idleplan.h
// from drawPetPMD() (scene.cpp) and draws the ball and the emotes. The activity itself is in
// PetBeh (scene.h): beh.activity (IdleAct), with beh.mode 1 while it walks somewhere.
#pragma once
#include <stdint.h>

// Back to standing and looking up, for IDLE_ATTEND_MS: a touch, PWR or BOOT (wakeUI), or a
// call (#9). A walk after a dragging finger (#21) still gets there first.
void idleInterrupt(uint32_t now);
// Forget everything (a new sprite: ensureMon): the next frame picks at once.
void idleReset();
// Every frame of drawPetPMD() that the planner decides (the pet awake, happy, and not
// following a finger). full: petSceneFree(), the full repertoire; otherwise the calm one.
void idleUpdate(uint32_t now, bool full);
// A walk (beh.mode 1) reached beh.targetX.
void idleArrived(uint32_t now);
// The pace of a walk the planner started, px/s.
float idleWalkSpeed();
// What drawPetPMD() draws while the pet is not walking (beh.mode 0 or 2): the PMD action,
// how far up a hop has it (dy, added), and the animation's start and looping.
uint8_t idleFrameAct(uint32_t now, int &dy, uint32_t &t0, bool &loop);
// After the pet: the ball and the emote, only on a frame the planner ran with full set.
// headTop: the top of the pet's sprite on screen (for the emote).
void idleDrawOver(uint32_t now, int petX, int headTop);

#ifdef HOST_SIM
// The simulator's "idle ACT" step: the next pick on a free, lit main screen is this activity
// (if the sprite can show it), at once.
void idleForceNext(uint8_t act);
#endif
