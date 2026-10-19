#include "pc_p2_captains.h"
#include "og/newScreen/Ground.h"
#ifdef PIKI_PC_PORT
void pcHudWideBegin(J2DPerspGraph*);
void pcHudWideEnd(J2DPerspGraph*);
#include "pc_p2_coop.h"
extern "C" void pc_gfx_set_view_subrect(float x0, float y0, float x1, float y1);
extern "C" void pc_gfx_clear_view_subrect(void);
extern "C" void pc_gfx_set_view_aspect_override(float aspect);
extern "C" float pc_gfx_get_window_aspect_ratio(void);
extern "C" void pc_gfx_set_hud_wide(int enabled);
extern "C" void pc_gfx_set_hud_virtual_size(int w, int h);
#endif
#include "og/newScreen/Cave.h"
#include "og/newScreen/ogUtil.h"
#include "og/Screen/SunMeter.h"
#include "og/Screen/DopingScreen.h"
#include "og/Screen/NaviLifeGauge.h"
#include "og/Screen/PikminCounter.h"
#include "og/Screen/BloGroup.h"
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

static const char unusedObjGroundArray[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/**
 * @note Address: 0x8030DEB8
 * @note Size: 0x94
 */
ObjGround::ObjGround(char const* name)
    : mFadeLevel(0.0f)
    , mScale(0.0f)
{
	mName              = name;
	mDisp              = nullptr;
	mOtakara           = nullptr;
	mBloGroup          = nullptr;
	mSunMeter          = nullptr;
	mDoping            = nullptr;
	mLifeGauge1        = nullptr;
	mLifeGauge2        = nullptr;
	mPikiCounter       = nullptr;
	mSensorScreen      = nullptr;
	mIsTotalPokoActive = false;
	mPokos             = 0;
	mTotalPokoTimer    = 0.0f;
#ifdef PIKI_PC_PORT
	mPcPikiCounter2 = nullptr;
#endif
}

/**
 * @note Address: 0x8030DF4C
 * @note Size: 0xAC
 */
ObjGround::~ObjGround()
{
}

/**
 * @note Address: 0x8030DFF8
 * @note Size: 0x464
 */
void ObjGround::doCreate(JKRArchive* arc)
{
	og::Screen::DispMemberGround* disp = static_cast<og::Screen::DispMemberGround*>(getDispMember());
	if (disp->isID(OWNER_OGA, MEMBER_GROUND)) {
		mDisp = disp;

	} else if (disp->isID(OWNER_OGA, MEMBER_DUMMY)) {
		mDisp = new og::Screen::DispMemberGround;

	} else {
		JUT_PANICLINE(186, "ERR! in ObjCave Create失敗！\n"); // yes they used the wrong class name - lmao
	}

	mSunMeter     = new og::Screen::SunMeter;
	mDoping       = new og::Screen::DopingScreen;
	mLifeGauge1   = new og::Screen::NaviLifeGauge;
	mLifeGauge2   = new og::Screen::NaviLifeGauge;
	mPikiCounter  = new og::Screen::PikminCounter;
	mSensorScreen = new P2DScreen::Mgr_tuning;

#ifdef PIKI_PC_PORT
	mBloGroup = new og::Screen::BloGroup(7);
#else
	mBloGroup = new og::Screen::BloGroup(6);
#endif
	mBloGroup->addBlo("sun_meter.blo", mSunMeter, 0x1040000, arc);
	mBloGroup->addBlo("doping.blo", mDoping, 0x1040000, arc);
	mBloGroup->addBlo("orima.blo", mLifeGauge1, 0x1040000, arc);
	mBloGroup->addBlo("orima.blo", mLifeGauge2, 0x1040000, arc);
	mBloGroup->addBlo("gr_pikmin.blo", mPikiCounter, 0x1040000, arc);
#ifdef PIKI_PC_PORT
	mPcPikiCounter2 = new og::Screen::PikminCounter;
	mBloGroup->addBlo("gr_pikmin.blo", mPcPikiCounter2, 0x1040000, arc);
#endif
	mBloGroup->addBlo("sensor.blo", mSensorScreen, 0x1040000, arc);

	mSunMeter->setCallBack();
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
	mPokos          = mDisp->mDataGame.mPokoCount;
	mTotalPokoTimer = 0.0f;

	mOtakara = new og::Screen::OtakaraSensor;
	mOtakara->init(mSensorScreen->search(MC8("Nhari")), mSensorScreen->search(MC8("Nsensor")), mDisp->mRadarState);
	mDoping->setDopingEnable(mDisp->mUnlockedSpicy, mDisp->mUnlockedBitter);
}

/**
 * @note Address: 0x8030E45C
 * @note Size: 0x29C
 */
void ObjGround::commonUpdate()
{
	if (!og::newScreen::checkMovieActive()) {
		mSunMeter->mCurrentTime = mDisp->mDataGame.mSunGaugeRatio;

		og::Screen::DispMemberGround* disp = mDisp;
		if (disp->mOlimarData.mActiveNaviID) {
			mDoping->setParam(disp->mOlimarData);
			mPikiCounter->setParam(mDisp->mDataGame, mDisp->mOlimarData);
		} else {
			mDoping->setParam(disp->mLouieData);
			mPikiCounter->setParam(mDisp->mDataGame, mDisp->mLouieData);
		}

		// Selector de capitanes: retrato del personaje de cada capitán (el
		// original del capitán 2 ya sigue la deuda en pc_p2_captain_character).
#ifdef PIKI_PC_PORT
		// Co-op: el contador de cada capitán, en su mitad (pcDrawHudCoop).
		if (pc_p2_coop_active() && pc_p2_coop_split()) {
			mPikiCounter->setParam(mDisp->mDataGame, mDisp->mOlimarData);
			mPcPikiCounter2->setParam(mDisp->mDataGame, mDisp->mLouieData);
		}
#endif
		mLifeGauge1->pcSetCharacter(pc_p2_captain_loaded_character(0), 0);
		mLifeGauge2->pcSetCharacter(pc_p2_captain_loaded_character(1), 1);
	}

	u16 width  = System::getRenderModeObj()->fbWidth;
	u16 height = System::getRenderModeObj()->efbHeight;

	mBloGroup->rotate((f32)width / 2, (f32)height / 2, J2DROTATE_Z, 0.0f);
	f32 cosTheta = -(cosf((mScale + 1.0f) * PI / 2));
	mBloGroup->scale((1.0f - cosTheta) * 0.4f + 1.0f);
	mBloGroup->update();

	mOtakara->setParam(mDisp->mTreasureDist, mDisp->mRadarState, mDisp->mRadarEnabled, mDisp->mAllTreasuresGotten);
	mOtakara->updateInit();

	if (mDisp->mHasRadar) {
		mOtakara->show();
		mOtakara->update();
		mOtakara->adjPos(msVal.mSensorX, msVal.mSensorY);
		mOtakara->adjScale(msVal.mSensorScale);
		mOtakara->setCompleteEfxOffset(ObjCave::msVal.mTreasureRadarCompEfxXPos, ObjCave::msVal.mTreasureRadarCompEfxYPos);
		mOtakara->setAppearEfxOffset(ObjCave::msVal.mTreasureRadarAppearEfxXPos, ObjCave::msVal.mTreasureRadarAppearEfxYPos);

	} else {
		mOtakara->hide();
	}

	if (mDisp->mHasBitter) {
		mDoping->openDopingUp();
		mDoping->openDopingKey();
	}

	if (mDisp->mHasSpicy) {
		mDoping->openDopingDown();
		mDoping->openDopingKey();
	}

	mDoping->adjPos(msVal.mDopingX, msVal.mDopingY);
}

/**
 * @note Address: 0x8030E6F8
 * @note Size: 0x24
 */
bool ObjGround::doUpdate()
{
	commonUpdate();
	return false;
}

/**
 * @note Address: 0x8030E71C
 * @note Size: 0x90
 */
void ObjGround::doDraw(Graphics& gfx)
{
	if (mDisp->mIsNotDay1) {
		mSunMeter->show();
	} else {
		mSunMeter->hide();
	}

#ifdef PIKI_PC_PORT
	pcHudWideBegin(&gfx.mPerspGraph);
	if (mBloGroup) {
		P2DScreen::Mgr* views[2][2] = { { mLifeGauge1, mPikiCounter }, { mLifeGauge2, mPcPikiCounter2 } };
		pcDrawHudCoop(gfx, mBloGroup, views, mPcPikiCounter2);
	}
#else
	if (mBloGroup)
		mBloGroup->draw(&gfx.mPerspGraph);
#endif

	if (mDisp->mHasRadar)
		mOtakara->draw(gfx.mPerspGraph);
#ifdef PIKI_PC_PORT
	pcHudWideEnd(&gfx.mPerspGraph);
#endif
}

/**
 * @note Address: 0x8030E7AC
 * @note Size: 0x14
 */
bool ObjGround::doStart(::Screen::StartSceneArg const*)
{
	mFadeLevel = 0.0f;
	mScale     = 0.0f;
	return true;
}

/**
 * @note Address: 0x8030E7C0
 * @note Size: 0x10
 */
bool ObjGround::doEnd(::Screen::EndSceneArg const*)
{
	mFadeLevel = 0.0f;
	return true;
}

/**
 * @note Address: 0x8030E7D0
 * @note Size: 0x74
 */
bool ObjGround::doUpdateFadein()
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
 * @note Address: 0x8030E844
 * @note Size: 0x4
 */
void ObjGround::doUpdateFadeinFinish()
{
}

/**
 * @note Address: 0x8030E848
 * @note Size: 0xC
 */
void ObjGround::doUpdateFinish()
{
	mFadeLevel = 0.0f;
}

/**
 * @note Address: 0x8030E854
 * @note Size: 0x7C
 */
bool ObjGround::doUpdateFadeout()
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
 * @note Address: 0x8030E8D0
 * @note Size: 0x4
 */
void ObjGround::doUpdateFadeoutFinish()
{
}

ObjGround::StaticValues ObjGround::msVal;

#ifdef PIKI_PC_PORT
namespace {
// Visibilidad de los paneles de una pantalla, para dibujar solo una parte y
// dejarla como estaba.
struct PcPaneVis {
	J2DPane* mPane;
	bool mVisible;
};
const int kPcPaneVisMax = 512;
PcPaneVis sPcPaneVis[kPcPaneVisMax];
int sPcPaneVisCount = 0;

void pcSaveAndHide(J2DPane* pane)
{
	if (sPcPaneVisCount < kPcPaneVisMax) {
		sPcPaneVis[sPcPaneVisCount].mPane    = pane;
		sPcPaneVis[sPcPaneVisCount].mVisible = pane->isVisible();
		sPcPaneVisCount++;
	}
	pane->hide();
	for (JSUTree<J2DPane>* it = pane->getPaneTree()->getFirstChild(); it; it = it->getNextChild()) {
		pcSaveAndHide(it->getObject());
	}
}

void pcRestoreVis()
{
	for (int i = 0; i < sPcPaneVisCount; i++) {
		if (sPcPaneVis[i].mVisible) {
			sPcPaneVis[i].mPane->show();
		} else {
			sPcPaneVis[i].mPane->hide();
		}
	}
	sPcPaneVisCount = 0;
}

bool pcSavedVisible(J2DPane* pane)
{
	for (int i = 0; i < sPcPaneVisCount; i++) {
		if (sPcPaneVis[i].mPane == pane) {
			return sPcPaneVis[i].mVisible;
		}
	}
	return pane->isVisible();
}

// Vuelve a mostrar lo que estaba visible dentro de una rama.
void pcShowSaved(J2DPane* pane)
{
	if (pcSavedVisible(pane)) {
		pane->show();
	}
	for (JSUTree<J2DPane>* it = pane->getPaneTree()->getFirstChild(); it; it = it->getNextChild()) {
		pcShowSaved(it->getObject());
	}
}

// Dibuja de `screen` solo la rama `part` (con sus padres), a pantalla completa.
void pcDrawOnly(P2DScreen::Mgr* screen, J2DPane* part, Graphics& gfx, J2DPerspGraph* graf)
{
	sPcPaneVisCount = 0;
	pcSaveAndHide(screen);
	pcShowSaved(part);
	part->show();
	for (JSUTree<J2DPane>* up = part->getPaneTree()->getParent(); up; up = up->getObject()->getPaneTree()->getParent()) {
		up->getObject()->show();
	}
	screen->draw(gfx, *graf);
	pcRestoreVis();
}
} // namespace

/**
 * HUD de historia (superficie y cueva). Fuera del co-op, el grupo entero salvo
 * el segundo contador (extra). En co-op (docs/PLAN_COOP.md §6.6), la vida y el
 * contador de cada capitán dentro de su mitad (arriba o a la izquierda Olimar),
 * con el HUD ancho de esa mitad, y lo demás (sol, sprays, radar, Pokos) una
 * vez a pantalla completa.
 */
void pcDrawHudCoop(Graphics& gfx, og::Screen::BloGroup* group, P2DScreen::Mgr* views[2][2], P2DScreen::Mgr* extra)
{
	J2DPerspGraph* graf = &gfx.mPerspGraph;
	Graphics drawGfx;
	const bool coop = pc_p2_coop_active() && pc_p2_coop_split();
	for (int i = 0; i < group->mScreenNumCurrent; i++) {
		P2DScreen::Mgr* screen = group->mScreens[i];
		bool perView           = screen == extra;
		for (int v = 0; v < 2 && coop; v++) {
			perView |= screen == views[v][0] || screen == views[v][1];
		}
		if (!perView) {
			screen->draw(drawGfx, *graf);
		}
	}
	if (!coop) {
		return;
	}
	// La burbuja del día (o del subnivel) está en el contador: en las mitades
	// no, y una sola vez arriba a la derecha de la pantalla, como en 1P.
	J2DPane* dayIcons[2] = { nullptr, nullptr };
	bool dayVisible[2]   = { false, false };
	for (int v = 0; v < 2; v++) {
		if (views[v][1]) {
			dayIcons[v] = views[v][1]->search(MC8("Ndayicon"));
		}
		if (dayIcons[v]) {
			dayVisible[v] = dayIcons[v]->isVisible();
			dayIcons[v]->hide();
		}
	}
	// Cada pieza de un jugador va en su propia caja (aspecto GX, ~4:3) dentro de su mitad
	// (arriba/abajo o izquierda/derecha, como HorizonalSplitter::split2): la
	// vida pegada abajo a la izquierda y el contador abajo a la derecha. Con
	// la caja en 4:3 no se ensancha ni se ancla nada: cada pieza conserva su
	// sitio dentro de la caja y la caja la lleva al borde de la mitad.
	const f32 aspect    = pc_gfx_get_window_aspect_ratio(); // ancho/alto de la ventana
	const bool vertical = pc_p2_coop_vertical() != 0;
	// La caja tiene el aspecto del espacio GX del juego y se mapea con escala
	// uniforme (HUD wide de pc_gfx, como el co-op de Pikmin 1); sin esto el
	// subrectángulo se mapea con el aspecto de la ventana y sale aplastado.
	const int gxW      = System::getRenderModeObj()->fbWidth;
	const int gxH      = System::getRenderModeObj()->efbHeight;
	const f32 boxRatio = f32(gxW) / f32(gxH);
	pc_gfx_set_hud_wide(1);
	pc_gfx_set_hud_virtual_size(gxW, gxH);
	for (int v = 0; v < 2; v++) {
		// Mitad en coordenadas normalizadas, origen abajo-izquierda (GL). Con la
		// cámara fusionada, el capitán 1 puede estar a la derecha o abajo.
		const int side = pc_p2_coop_view_side(v);
		const f32 hx   = vertical ? (side == 0 ? 0.0f : 0.5f) : 0.0f;
		const f32 hy   = vertical ? 0.0f : (side == 0 ? 0.5f : 0.0f);
		const f32 hw = vertical ? 0.5f : 1.0f;
		const f32 hh = vertical ? 1.0f : 0.5f;
		// Caja 4:3 lo más grande que quepa (en unidades de altura de ventana).
		const f32 halfW = hw * aspect, halfH = hh;
		f32 boxW = halfH * boxRatio, boxH = halfH;
		if (boxW > halfW) {
			boxW = halfW;
			boxH = halfW / boxRatio;
		}
		const f32 bw = boxW / aspect; // a fracción del ancho de la ventana
		for (int j = 0; j < 2; j++) {
			if (!views[v][j]) {
				continue;
			}
			const f32 bx = j == 0 ? hx : hx + hw - bw;
			pc_gfx_set_view_subrect(bx, hy, bx + bw, hy + boxH);
			pc_gfx_set_view_aspect_override(boxRatio);
			graf->setPort();
			views[v][j]->draw(drawGfx, *graf);
		}
	}
	pc_gfx_set_hud_virtual_size(0, 0);
	pc_gfx_set_hud_wide(0);
	pc_gfx_clear_view_subrect();
	pc_gfx_set_view_aspect_override(0.0f);
	graf->setPort();
	for (int v = 0; v < 2; v++) {
		if (dayIcons[v] && dayVisible[v]) {
			dayIcons[v]->show();
		}
	}
	if (dayIcons[0] && dayVisible[0]) {
		pcDrawOnly(views[0][1], dayIcons[0], drawGfx, graf);
	}
}
#endif

} // namespace newScreen
} // namespace og
