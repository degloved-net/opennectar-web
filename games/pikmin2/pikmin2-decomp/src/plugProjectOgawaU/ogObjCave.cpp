#include "pc_p2_captains.h"
#include "og/newScreen/Cave.h"
#ifdef PIKI_PC_PORT
void pcHudWideBegin(J2DPerspGraph*);
void pcHudWideEnd(J2DPerspGraph*);
#include "pc_p2_coop.h"
#endif
#include "og/Screen/DopingScreen.h"
#include "og/Screen/NaviLifeGauge.h"
#include "og/Screen/PikminCounter.h"
#include "og/Screen/TotalPokoScreen.h"
#include "og/Screen/BloGroup.h"
#include "og/Screen/ogScreen.h"
#include "trig.h"
#include "System.h"
#include "nans.h"
#ifdef PIKI_PC_PORT
namespace og {
namespace newScreen {
// HUD de historia con el co-op: vida y contador de cada capitán en su mitad
// (ogObjGround.cpp). views[v] = { vida, contador } de la vista v.
void pcDrawHudCoop(Graphics& gfx, og::Screen::BloGroup* group, P2DScreen::Mgr* views[2][2], P2DScreen::Mgr* extra);
} // namespace newScreen
} // namespace og
#endif

namespace og {
namespace newScreen {

static const char objCaveUnusedArray[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/**
 * @note Address: 0x8031C008
 * @note Size: 0x9C
 */
ObjCave::ObjCave(const char* name)
{
	mFadeLevel       = 0.0f;
	mScale           = 0.0f;
	mName            = name;
	mDisp            = nullptr;
	mOtakara         = nullptr;
	mBloGroup        = nullptr;
	mDoping          = nullptr;
	mLifeGauge1      = nullptr;
	mLifeGauge2      = nullptr;
	mPikiCounter     = nullptr;
	mTotalPoko       = nullptr;
	mSensorScreen    = nullptr;
	mTotalPokoActive = false;
	mPokos           = 0;
	mTotalPokoTimer  = 0.0f;
	mPaneChika       = nullptr;
	mPaneFinalf      = nullptr;
#ifdef PIKI_PC_PORT
	mPcPikiCounter2 = nullptr;
#endif
}

/**
 * @note Address: 0x8031C0A4
 * @note Size: 0xAC
 */
ObjCave::~ObjCave()
{
}

/**
 * @note Address: 0x8031C150
 * @note Size: 0x518
 */
void ObjCave::doCreate(JKRArchive* arc)
{
	og::Screen::DispMemberCave* disp = static_cast<og::Screen::DispMemberCave*>(getDispMember());
	if (disp->isID(OWNER_OGA, MEMBER_CAVE)) {
		mDisp = disp;

	} else if (disp->isID(OWNER_OGA, MEMBER_DUMMY)) {
		mDisp = new og::Screen::DispMemberCave;

	} else {
		JUT_PANICLINE(189, "ERR! in ObjCave Create失敗！\n");
	}

	mDoping       = new og::Screen::DopingScreen;
	mLifeGauge1   = new og::Screen::NaviLifeGauge;
	mLifeGauge2   = new og::Screen::NaviLifeGauge;
	mPikiCounter  = new og::Screen::PikminCounterCave;
	mTotalPoko    = new og::Screen::TotalPokoScreen;
	mSensorScreen = new P2DScreen::Mgr_tuning;

#ifdef PIKI_PC_PORT
	mBloGroup = new og::Screen::BloGroup(7);
#else
	mBloGroup = new og::Screen::BloGroup(6);
#endif
	mBloGroup->addBlo("doping.blo", mDoping, 0x1040000, arc);
	mBloGroup->addBlo("orima.blo", mLifeGauge1, 0x1040000, arc);
	mBloGroup->addBlo("orima.blo", mLifeGauge2, 0x1040000, arc);
	mBloGroup->addBlo("cave_pikmin.blo", mPikiCounter, 0x1040000, arc);
#ifdef PIKI_PC_PORT
	mPcPikiCounter2 = new og::Screen::PikminCounterCave;
	mBloGroup->addBlo("cave_pikmin.blo", mPcPikiCounter2, 0x1040000, arc);
#endif
	mBloGroup->addBlo("grand_cave_poko.blo", mTotalPoko, 0x1040000, arc);
	mBloGroup->addBlo("sensor.blo", mSensorScreen, 0x1040000, arc);

	mDoping->setCallBack(arc);
	mLifeGauge1->setCallBack(&mDisp->mOlimarData, og::Screen::CallBack_LifeGauge::LIFEGAUGE_OLIMAR);
	disp = mDisp;
	if (disp->mPayDebt) {
		mLifeGauge2->setCallBack(&mDisp->mLouieData, og::Screen::CallBack_LifeGauge::LIFEGAUGE_PRESIDENT);
	} else {
		mLifeGauge2->setCallBack(&mDisp->mLouieData, og::Screen::CallBack_LifeGauge::LIFEGAUGE_LOUIE);
	}

	mPikiCounter->setCallBack(arc);
#ifdef PIKI_PC_PORT
	mPcPikiCounter2->setCallBack(arc);
#endif
	mTotalPoko->setCallBack(arc, msVal.mTotalPokoXPos, msVal.mTotalPokoYPos, msVal.mTotalPokoXScale, msVal.mTotalPokoYScale);
	mPaneChika  = og::Screen::TagSearch(mPikiCounter, MC8("Pchika"));
	mPaneFinalf = og::Screen::TagSearch(mPikiCounter, MC8("Nfinalf"));

	if (mDisp->mIsFinalFloor) {
		mPaneChika->hide();
		mPaneFinalf->show();
	} else {
		mPaneChika->show();
		mPaneFinalf->hide();
	}

	mPokos = mDisp->mDataGame.mPokoCount;
	mTotalPoko->setTotalPoko(mPokos);
	mTotalPokoTimer                  = 0.0f;
	mPikiCounter->mIsTotalPokoActive = false;
	mTotalPoko->closeTotalPoko();
	mTotalPoko->hideTotalPoko();

	mOtakara = new og::Screen::OtakaraSensor;

	mOtakara->init(mSensorScreen->search(MC8("Nhari")), mSensorScreen->search(MC8("Nsensor")), mDisp->mRadarState);
	mDoSensorEff = true;

	mOtakara->setSensorVec1(msVal.mTreasureRadarVecX, msVal.mTreasureRadarVecY);
	mDoping->setDopingEnable(mDisp->mIsBitterUnlocked, mDisp->mIsSpicyUnlocked);
}

/**
 * @note Address: 0x8031C668
 * @note Size: 0x3A4
 */
void ObjCave::commonUpdate()
{
	if (mDisp) {
		if (mDisp->mIsFinalFloor) {
			mPaneChika->hide();
			mPaneFinalf->show();

		} else {
			mPaneChika->show();
			mPaneFinalf->hide();
		}

		mDoping->setParam(mDisp->mOlimarData);

		og::Screen::DispMemberCave* disp = mDisp;
		if (disp->mOlimarData.mActiveNaviID) {
			mPikiCounter->setParam(disp->mDataGame, disp->mOlimarData);
		} else {
			mPikiCounter->setParam(disp->mDataGame, disp->mLouieData);
		}
#ifdef PIKI_PC_PORT
		// Co-op: el contador de cada capitán, en su mitad (pcDrawHudCoop).
		if (pc_p2_coop_active() && pc_p2_coop_split()) {
			mPikiCounter->setParam(disp->mDataGame, disp->mOlimarData);
			mPcPikiCounter2->setParam(disp->mDataGame, disp->mLouieData);
		}
#endif

		if (mTotalPokoActive) {
			mTotalPokoTimer += sys->mDeltaTime;
			if (mTotalPokoTimer > 2.0f) {
				mTotalPokoActive = false;
			}

			int pokos = mDisp->mDataGame.mPokoCount;
			if (pokos != mPokos) {
				mPokos = pokos;
				mTotalPoko->setTotalPoko(mPokos);
				mTotalPokoTimer = 0.0f;
			}

		} else if (mDisp->mDataGame.mPokoCount != mPokos) {
			mTotalPokoActive = true;
			mPokos           = mDisp->mDataGame.mPokoCount;
			mTotalPoko->setTotalPoko(mPokos);
			mTotalPokoTimer = 0.0f;
		}

		if (mTotalPokoActive) {
			mPikiCounter->mIsTotalPokoActive = true;
			mTotalPoko->showTotalPoko();

		} else {
			mPikiCounter->mIsTotalPokoActive = false;
			mTotalPoko->closeTotalPoko();
		}

		// Selector de capitanes: retrato del personaje de cada capitán (el
		// original del capitán 2 ya sigue la deuda en pc_p2_captain_character).
		mLifeGauge1->pcSetCharacter(pc_p2_captain_loaded_character(0), 0);
		mLifeGauge2->pcSetCharacter(pc_p2_captain_loaded_character(1), 1);
	}

	u16 width  = System::getRenderModeObj()->fbWidth;
	u16 height = System::getRenderModeObj()->efbHeight;

	mBloGroup->rotate((f32)width / 2, (f32)height / 2, J2DROTATE_Z, 0.0f);

	f32 cosTheta = -(cosf((mScale + 1.0f) * PI / 2));
	mBloGroup->scale((1.0f - cosTheta) * 0.4f + 1.0f);
	mBloGroup->update();

	if (mDisp->mDrawSensor && mDoSensorEff && mDisp->mAppearRadar) {
		mOtakara->startSensorEff();
		mDoSensorEff = false;
	}

	mOtakara->setParam(mDisp->mTreasureDist, mDisp->mRadarState, mDisp->mRadarEnabled, mDisp->mAllTreasureGotten);
	mOtakara->updateInit();

	if (mDisp->mDrawSensor) {
		mOtakara->show();
		mOtakara->update();
		mOtakara->adjPos(msVal.mTreasureRadarXPos, msVal.mTreasureRadarYPos);
		mOtakara->adjScale(msVal.mTreasureRadarScale);
		mOtakara->setCompleteEfxOffset(msVal.mTreasureRadarCompEfxXPos, msVal.mTreasureRadarCompEfxYPos);
		mOtakara->setAppearEfxOffset(msVal.mTreasureRadarAppearEfxXPos, msVal.mTreasureRadarAppearEfxYPos);

	} else {
		mOtakara->hide();
	}

	mDoping->setDopingEnable(mDisp->mIsBitterUnlocked, mDisp->mIsSpicyUnlocked);
	mDoping->adjPos(msVal.mDopingScreenXPos, msVal.mDopingScreenYPos);
}

/**
 * @note Address: 0x8031CA0C
 * @note Size: 0x24
 */
bool ObjCave::doUpdate()
{
	commonUpdate();
	return false;
}

/**
 * @note Address: 0x8031CA30
 * @note Size: 0x64
 */
void ObjCave::doDraw(Graphics& gfx)
{
#ifdef PIKI_PC_PORT
	pcHudWideBegin(&gfx.mPerspGraph);
	if (mBloGroup) {
		P2DScreen::Mgr* views[2][2] = { { mLifeGauge1, mPikiCounter }, { mLifeGauge2, mPcPikiCounter2 } };
		pcDrawHudCoop(gfx, mBloGroup, views, mPcPikiCounter2);
	}
#else
	if (mBloGroup) {
		mBloGroup->draw(&gfx.mPerspGraph);
	}
#endif

	if (mDisp->mDrawSensor) {
		mOtakara->draw(gfx.mPerspGraph);
	}
#ifdef PIKI_PC_PORT
	pcHudWideEnd(&gfx.mPerspGraph);
#endif
}

/**
 * @note Address: 0x8031CA94
 * @note Size: 0x14
 */
bool ObjCave::doStart(::Screen::StartSceneArg const*)
{
	mFadeLevel = 0.0f;
	mScale     = 0.0f;
	return true;
}

/**
 * @note Address: 0x8031CAA8
 * @note Size: 0x10
 */
bool ObjCave::doEnd(::Screen::EndSceneArg const*)
{
	mFadeLevel = 0.0f;
	return true;
}

/**
 * @note Address: 0x8031CAB8
 * @note Size: 0x74
 */
bool ObjCave::doUpdateFadein()
{
	bool check = false;
	mFadeLevel += sys->mDeltaTime;
	if (mFadeLevel > msVal.mFadeinTime) {
		mFadeLevel = msVal.mFadeinTime;
		check      = true;
	}

	mScale = mFadeLevel / msVal.mFadeinTime;
	commonUpdate();
	return check;
}

/**
 * @note Address: 0x8031CB2C
 * @note Size: 0x4
 */
void ObjCave::doUpdateFadeinFinish()
{
}

/**
 * @note Address: 0x8031CB30
 * @note Size: 0xC
 */
void ObjCave::doUpdateFinish()
{
	mFadeLevel = 0.0f;
}

/**
 * @note Address: 0x8031CB3C
 * @note Size: 0x7C
 */
bool ObjCave::doUpdateFadeout()
{
	bool check = false;
	mFadeLevel += sys->mDeltaTime;
	if (mFadeLevel > msVal.mFadeoutTime) {
		mFadeLevel = msVal.mFadeoutTime;
		check      = true;
	}

	mScale = 1.0f - mFadeLevel / msVal.mFadeoutTime;
	commonUpdate();
	return check;
}

/**
 * @note Address: 0x8031CBB8
 * @note Size: 0x4
 */
void ObjCave::doUpdateFadeoutFinish()
{
}

ObjCave::StaticValues ObjCave::msVal;

} // namespace newScreen
} // namespace og
