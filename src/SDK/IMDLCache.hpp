#pragma once

#include "declarations.hpp"
#include "IAppSystem.hpp"

#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
struct studiohdr_t;
struct studiohwdata_t;
struct vcollide_t;
struct virtualmodel_t;
struct vertexFileHeader_t;

//-----------------------------------------------------------------------------
// Reference to a loaded studiomdl 
//-----------------------------------------------------------------------------
typedef unsigned short MDLHandle_t;

enum
{
	MDLHANDLE_INVALID = (MDLHandle_t)~0
};

//-----------------------------------------------------------------------------
// Cache data types
//-----------------------------------------------------------------------------
enum MDLCacheDataType_t
{
	MDLCACHE_STUDIOHDR = 0,
	MDLCACHE_STUDIOHWDATA,
	MDLCACHE_VCOLLIDE,
	MDLCACHE_ANIMBLOCK,
	MDLCACHE_VIRTUALMODEL,
	MDLCACHE_VERTEXES,
	MDLCACHE_DECODEDANIMBLOCK,
};

//-----------------------------------------------------------------------------
// Flush flags
//-----------------------------------------------------------------------------
enum MDLCacheFlush_t
{
	MDLCACHE_FLUSH_STUDIOHDR = 0x01,
	MDLCACHE_FLUSH_STUDIOHWDATA = 0x02,
	MDLCACHE_FLUSH_VCOLLIDE = 0x04,
	MDLCACHE_FLUSH_ANIMBLOCK = 0x08,
	MDLCACHE_FLUSH_VIRTUALMODEL = 0x10,
	MDLCACHE_FLUSH_AUTOPLAY = 0x20,
	MDLCACHE_FLUSH_VERTEXES = 0x40,
	MDLCACHE_FLUSH_IGNORELOCK = 0x80000000,
	MDLCACHE_FLUSH_ALL = 0xFFFFFFFF
};

//-----------------------------------------------------------------------------
// The main MDL cacher 
//-----------------------------------------------------------------------------
class IMDLCache : public sdk::IAppSystem
{
public:
	virtual void SetCacheNotify(void* pNotify) = 0;                                       // 9
	virtual MDLHandle_t FindMDL(const char* pMDLRelativePath) = 0;                         // 10
	virtual int AddRef(MDLHandle_t handle) = 0;                                            // 11
	virtual int Release(MDLHandle_t handle) = 0;                                           // 12
	virtual int GetRef(MDLHandle_t handle) = 0;                                            // 13
	virtual studiohdr_t* GetStudioHdr(MDLHandle_t handle) = 0;                             // 14
	virtual studiohwdata_t* GetHardwareData(MDLHandle_t handle) = 0;                       // 15
	virtual vcollide_t* GetVCollide(MDLHandle_t handle) = 0;                               // 16
	virtual unsigned char* GetAnimBlock(MDLHandle_t handle, int nBlock) = 0;               // 17
	virtual virtualmodel_t* GetVirtualModel(MDLHandle_t handle) = 0;                       // 18
	virtual int GetAutoplayList(MDLHandle_t handle, unsigned short** pOut) = 0;             // 19
	virtual vertexFileHeader_t* GetVertexData(MDLHandle_t handle) = 0;                     // 20
	virtual void TouchAllData(MDLHandle_t handle) = 0;                                     // 21
	virtual void SetUserData(MDLHandle_t handle, void* pData) = 0;                         // 22
	virtual void* GetUserData(MDLHandle_t handle) = 0;                                     // 23
	virtual bool IsErrorModel(MDLHandle_t handle) = 0;                                     // 24
	virtual void Flush(MDLCacheFlush_t nFlushFlags = MDLCACHE_FLUSH_ALL) = 0;              // 25
	virtual void Flush(MDLHandle_t handle, int nFlushFlags = MDLCACHE_FLUSH_ALL) = 0;      // 26
	virtual const char* GetModelName(MDLHandle_t handle) = 0;                              // 27
	virtual virtualmodel_t* GetVirtualModelFast(const studiohdr_t* pStudioHdr, MDLHandle_t handle) = 0; // 28
	virtual void BeginLock() = 0;                                                          // 29
	virtual void EndLock() = 0;                                                            // 30
	virtual int* GetFrameUnlockCounterPtrOLD() = 0;                                       // 31
	virtual void FinishPendingLoads() = 0;                                                 // 32
	virtual vcollide_t* GetVCollideEx(MDLHandle_t handle, bool synchronousLoad = true) = 0; // 33
	virtual bool GetVCollideSize(MDLHandle_t handle, int* pVCollideSize) = 0;               // 34
	virtual bool GetAsyncLoad(MDLCacheDataType_t type) = 0;                                // 35
	virtual bool SetAsyncLoad(MDLCacheDataType_t type, bool async) = 0;                     // 36
	virtual void BeginMapLoad() = 0;                                                       // 37
	virtual void EndMapLoad() = 0;                                                         // 38
	virtual void MarkAsLoaded(MDLHandle_t handle) = 0;                                     // 39
	virtual void InitPreloadData(bool rebuild) = 0;                                        // 40
	virtual void ShutdownPreloadData() = 0;                                                // 41
	virtual bool IsDataLoaded(MDLHandle_t handle, MDLCacheDataType_t type) = 0;             // 42
	virtual int* GetFrameUnlockCounterPtr(MDLCacheDataType_t type) = 0;                    // 43
	virtual studiohdr_t* LockStudioHdr(MDLHandle_t handle) = 0;                            // 44
	virtual void UnlockStudioHdr(MDLHandle_t handle) = 0;                                  // 45
	virtual bool PreloadModel(MDLHandle_t handle) = 0;                                     // 46
	virtual void ResetErrorModelStatus(MDLHandle_t handle) = 0;                            // 47
	virtual void MarkFrame() = 0;                                                          // 48
};
