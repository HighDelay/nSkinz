#pragma once
#include "declarations.hpp"

namespace sdk
{
	struct SndInfo_t;
	class IAudioDevice;

	class IEngineSound
	{
	public:
		// We only need EmitSound1 which is index 5
		virtual bool PrecacheSound(const char* pSample, bool bPreload = false, bool bIsTieBreak = false) = 0;
		virtual bool IsSoundPrecached(const char* pSample) = 0;
		virtual void PrefetchSound(const char* pSample) = 0;
		virtual bool IsLoopingSound(const char* pSample) = 0;
		virtual float GetPrecachedSoundDuration(const char* pSample) = 0;

		virtual void EmitSound1(void* filter, int iEntIndex, int iChannel, const char* pSoundEntry, unsigned int nSoundEntryHash, const char* pSample, float flVolume, int iSoundLevel, int nSeed, int iFlags, int iPitch, const void* pOrigin, const void* pDirection, void* pUtlVecOrigins, bool bUpdatePositions, float soundtime, int speakerentity, int unk) = 0;

		virtual void EmitSound2(void* filter, int iEntIndex, int iChannel, const char* pSoundEntry, unsigned int nSoundEntryHash, const char* pSample, float flVolume, float flAttenuation, int nSeed, int iFlags, int iPitch, const void* pOrigin, const void* pDirection, void* pUtlVecOrigins, bool bUpdatePositions, float soundtime, int speakerentity) = 0;
	};
}
