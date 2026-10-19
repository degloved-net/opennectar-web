#include <stdio.h>
#include "Dolphin/os.h"
#include "JSystem/JAudio/JAI/JAIBasic.h"
#include "JSystem/JAudio/JAI/JAInter.h"
#include "JSystem/JAudio/JAI/JAInter/Fx.h"
#include "JSystem/JAudio/JAS/JASDsp.h"
#include "types.h"

JAInter::Fx::Init* JAInter::Fx::initOnCodeFxScene;
u8 JAInter::Fx::mSceneMax;
u32* JAInter::Fx::mBufferSizeMax;
s16** JAInter::Fx::mBufferPointer;
JASDsp::FxlineConfig_** JAInter::Fx::mFxconfigTable;

/**
 * @note Address: 0x800AD6CC
 * @note Size: 0x160
 */
void JAInter::Fx::init()
{
	if (initOnCodeFxScene) {
#ifdef PIKI_PC_PORT
		// La tabla de FX viene tal cual del AAF, en big-endian: cabecera
		// {sceneMax, bufferMax x4, offsets de escena} y por escena cuatro
		// FxlineConfig_ de 0x20 bytes. Se pasa a orden host la escena 0, la que
		// el juego carga al iniciar (setFXLine con getFxconfigTable()[0]).
		{
			const u8* raw = reinterpret_cast<const u8*>(initOnCodeFxScene);
			auto be16     = [](const u8* p) { return (u16)((p[0] << 8) | p[1]); };
			auto be32     = [](const u8* p) { return (u32)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); };
			mBufferSizeMax = new (JAIGetCurrentHeap(), 4) u32[4];
			mBufferPointer = new (JAIGetCurrentHeap(), 4) s16*[4];
			setSceneMax((u8)be32(raw));
			setBufferMax(be32(raw + 4), be32(raw + 8), be32(raw + 12), be32(raw + 16));
			static JASDsp::FxlineConfig_ sScene0[4];
			const u8* scene = raw + be32(raw + 20);
			for (u8 i = 0; i < 4; i++) {
				const u8* c                 = scene + i * 0x20;
				JASDsp::FxlineConfig_& conf = sScene0[i];
				conf.mStatus                = c[0];
				conf._02                    = be16(c + 2);
				conf._04                    = (s16)be16(c + 4);
				conf._06                    = be16(c + 6);
				conf._08                    = (s16)be16(c + 8);
				conf._0A                    = (s16)be16(c + 10);
				conf.mBufferCount           = be32(c + 12);
				for (int k = 0; k < 8; k++) {
					conf._10[k] = (s16)be16(c + 16 + k * 2);
				}
				// SEND_TABLE tiene 12 entradas: un índice fuera no es una línea válida.
				if (conf._02 >= 12 || conf._06 >= 12) {
					conf.mStatus = 0;
				}
			}
			for (u8 i = 0; i < 4; i++) {
				if (getBufferSizeMax(i) != 0) {
					s16* buf = new (JAIGetCurrentHeap(), 0x20) s16[ALIGN_PREV(getBufferSizeMax(i) * 160, 2) / 2];
					setBufferPointer(i, buf);
					JASDsp::setFXLine(i, buf, &sScene0[i]);
				}
			}
			printf("[PC Port] Audio: DSP FX lines ready (%u scenes)\n", (unsigned)getSceneMax());
			return;
		}
#endif
		mBufferSizeMax = new (JAIGetCurrentHeap(), 4) u32[4];
		mBufferPointer = new (JAIGetCurrentHeap(), 4) s16*[4];
		Init* init     = initOnCodeFxScene;
		setSceneMax(init->mSceneMax);
		setBufferMax(init->mBufferMax1, init->mBufferMax2, init->mBufferMax3, init->mBufferMax4);
		JKRHeap* heap = JAIGetCurrentHeap();
		setTablePointer(new (heap, 0x20) void*[getSceneMax()]);
		for (u8 i = 0; i < getSceneMax(); i++) {
			// Very questionable, but it works
			setScenePointer(i, (reinterpret_cast<u8*>(initOnCodeFxScene) + *(int*)((u8*)(&init->mScenePointerOffsets + i))));
		}
		for (u8 i = 0; i < 4; i++) {
			if (getBufferSizeMax(i) != 0) {
				s16* buf = new (JAIGetCurrentHeap(), 0x20) s16[ALIGN_PREV(getBufferSizeMax(i) * 160, 2) / 2];
				setBufferPointer(i, buf);
				JASDsp::setFXLine(i, getBufferPointer(i), &(getFxconfigTable()[0])[i]);
			}
		}
	}
}

/**
 * @note Address: 0x800AD82C
 * @note Size: 0x8
 */
void JAInter::Fx::setSceneMax(u8 sceneMax)
{
	mSceneMax = sceneMax;
}

/**
 * @note Address: 0x800AD834
 * @note Size: 0x24
 */
void JAInter::Fx::setBufferMax(u32 max1, u32 max2, u32 max3, u32 max4)
{
	mBufferSizeMax[0] = max1;
	mBufferSizeMax[1] = max2;
	mBufferSizeMax[2] = max3;
	mBufferSizeMax[3] = max4;
}

/**
 * @note Address: 0x800AD858
 * @note Size: 0x8
 */
void JAInter::Fx::setTablePointer(void** tablePointer)
{
	mFxconfigTable = reinterpret_cast<JASDsp::FxlineConfig_**>(tablePointer);
}

/**
 * @note Address: 0x800AD860
 * @note Size: 0x10
 */
void JAInter::Fx::setBufferPointer(u8 index, s16* bufferPointer)
{
	mBufferPointer[index] = bufferPointer;
}

/**
 * @note Address: 0x800AD870
 * @note Size: 0x10
 */
void JAInter::Fx::setScenePointer(u8 index, void* scenePointer)
{
	mFxconfigTable[index] = static_cast<JASDsp::FxlineConfig_*>(scenePointer);
}

/**
 * @note Address: 0x800AD880
 * @note Size: 0x8
 */
u8 JAInter::Fx::getSceneMax()
{
	return mSceneMax;
}

/**
 * @note Address: 0x800AD888
 * @note Size: 0x10
 */
u32 JAInter::Fx::getBufferSizeMax(u8 index)
{
	return mBufferSizeMax[index];
}

/**
 * @note Address: 0x800AD898
 * @note Size: 0x10
 */
s16* JAInter::Fx::getBufferPointer(u8 index)
{
	return mBufferPointer[index];
}

/**
 * @note Address: 0x800AD8A8
 * @note Size: 0x8
 */
JASDsp::FxlineConfig_** JAInter::Fx::getFxconfigTable()
{
	return mFxconfigTable;
}

/**
 * @note Address: N/A
 * @note Size: 0x80
 */
void JAInter::Fx::clearBuffer(u8 index)
{
	// UNUSED FUNCTION
	if (mBufferPointer != nullptr) {
		for (u32 i = 0; i < (mBufferSizeMax[index] * 0xA0) / 2; i++) {
			mBufferPointer[index][i] = 0;
		}
		DCFlushRange(mBufferPointer[index], (mBufferSizeMax[index] * 0xA0) >> 1);
	}
}

/**
 * @note Address: 0x800AD8B0
 * @note Size: 0xA0
 */
void JAInter::Fx::clearAllBuffer()
{
	for (u8 i = 0; i < 4; i++) {
		clearBuffer(i);
	}
}
