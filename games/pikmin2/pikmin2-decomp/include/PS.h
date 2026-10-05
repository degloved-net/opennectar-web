#ifndef _PS_H
#define _PS_H

#include "types.h"

struct Vec;
struct JAISound;
struct JAISe;

namespace Game {
struct EnemyBase;
} // namespace Game

namespace PSM {
struct Creature;
struct SeSound;
} // namespace PSM

void PSChangeBgm_ChallengeGame();

void PSPause_StartMenuOn();
void PSPause_StartMenuOff();

void PSPauseOn(u8, u8);
void PSPauseOff();

void PSStart2DStream(u32);
void PSStop2DStream();

void PSStartChallengeTimeUpStream();
JAISound* PSStartEnemyDownSmokeSE(Game::EnemyBase*, f32);
JAISound* PSStartEnemyDownWatSE(Game::EnemyBase*, f32);
JAISound* PSStartEnemyFatalHitSE(Game::EnemyBase*, f32);
JAISound* PSStartEnemyGhostSE(Game::EnemyBase*, f32);
PSM::SeSound* PSStartSoundVec(u32, Vec*);
JAISe* PSStartTreasureLaderSE(f32);
JAISe* PSStartTresureLaderNoiseSE(u8, f32, f32);
void PSMuteSE_on2D();
void PSMuteOffSE_on2D();

void PSPlayCaveHoleSound(PSM::Creature*);

#endif
