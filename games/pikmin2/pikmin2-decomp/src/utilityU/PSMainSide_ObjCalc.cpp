#include "PSM/ObjCalc.h"
#include "PSMath.h"
#include "Iterator.h"
#include "Game/Navi.h"
#ifdef PIKI_PC_PORT
#include "pc_p2_coop.h"
// Co-op a pantalla partida: como en el 2P del juego, cada sonido se oye desde
// la cámara del capitán que lo tiene más cerca (o la de la vista que ve al
// objeto). Con una sola cámara de escucha (la de Olimar), lo que pasaba junto
// a Louie sonaba lejísimos.
static bool pcCoopListeners() { return pc_p2_coop_active() && pc_p2_coop_split(); }

static u8 pcNearestNaviNo(Vec& pos, u8 fallback)
{
	f32 best = -1.0f;
	u8 no    = fallback;
	for (int i = 0; i < 2; i++) {
		Game::Navi* navi = Game::naviMgr ? Game::naviMgr->getAt(i) : nullptr;
		if (!navi || !navi->isAlive()) {
			continue;
		}
		Vector3f npos  = navi->getPosition();
		const f32 dist = PSMath::calcSquareDistance(pos, npos);
		if (best < 0.0f || dist < best) {
			best = dist;
			no   = (u8)i;
		}
	}
	return no;
}
#endif

namespace PSM {

/**
 * @note Address: 0x804728A4
 * @note Size: 0x60
 */
void ObjCalc_SingleGame::newInstance_SingleGame()
{
	new ObjCalc_SingleGame;
}

/**
 * @note Address: 0x80472904
 * @note Size: 0x8
 */
u8 ObjCalc_SingleGame::getPlayerNo(PSM::Creature* obj)
{
#ifdef PIKI_PC_PORT
	if (obj && obj->mGameObj && pcCoopListeners()) {
		return obj->mGameObj->getSound_AILOD()->mSoundVPID;
	}
#endif
	return mPlayerNum;
}

/**
 * @note Address: 0x8047290C
 * @note Size: 0x8
 */
u8 ObjCalc_SingleGame::getPlayerNo(Vec& pos)
{
#ifdef PIKI_PC_PORT
	if (pcCoopListeners()) {
		return pcNearestNaviNo(pos, mPlayerNum);
	}
#endif
	return mPlayerNum;
}

/**
 * @note Address: 0x80472914
 * @note Size: 0x5C
 */
void ObjCalc_2PGame::newInstance_2PGame()
{
	new ObjCalc_2PGame;
}

/**
 * @note Address: 0x80472970
 * @note Size: 0x3BC
 */
u8 ObjCalc_2PGame::getPlayerNo(Vec& pos)
{
	switch (mMode) {
	case OBJCALC_1: {
		return 0;

			}	case OBJCALC_0: {
		f32 dists[2] = { 100000.0f, 1000000.0f };
		Iterator<Game::Navi> it(Game::naviMgr);
		int i = 0;
		CI_LOOP(it)
		{
			Game::Navi* navi = *it;
			Vector3f npos    = navi->getPosition();
			P2ASSERTLINE(65, &npos != 0);
			P2ASSERTLINE(66, i < 2);

			dists[i] = PSMath::calcSquareDistance(pos, npos);
			i++;
		}
		if (dists[0] < dists[1]) {
			return 0;
		}
		return 1;
			}	default: {
		P2ASSERTLINE(77, false);
		return 0;
			}	}
}

/**
 * @note Address: 0x80472D2C
 * @note Size: 0xC8
 */
u8 ObjCalc_2PGame::getPlayerNo(PSM::Creature* obj)
{
	P2ASSERTLINE(87, obj);
	switch (mMode) {
	case OBJCALC_1:
		return 0;
	case OBJCALC_0:
		return obj->mGameObj->getSound_AILOD()->mSoundVPID;
	default:
		JUT_PANICLINE(125, "ありえないケース1");
		JUT_PANICLINE(128, "ありえないケース2");
		return 0;
	}
}

} // namespace PSM
