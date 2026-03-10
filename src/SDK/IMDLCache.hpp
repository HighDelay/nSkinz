#pragma once

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
class IMDLCache
{
public:
	virtual void SetCacheNotify(void* pNotify) = 0;                                       // 0
	virtual MDLHandle_t FindMDL(const char* pMDLRelativePath) = 0;                         // 1
	virtual int AddRef(MDLHandle_t handle) = 0;                                            // 2
	virtual int Release(MDLHandle_t handle) = 0;                                           // 3
	virtual int GetRef(MDLHandle_t handle) = 0;                                            // 4
	virtual studiohdr_t* GetStudioHdr(MDLHandle_t handle) = 0;                             // 5
	virtual studiohwdata_t* GetHardwareData(MDLHandle_t handle) = 0;                       // 6
	virtual vcollide_t* GetVCollide(MDLHandle_t handle) = 0;                               // 7
	virtual unsigned char* GetAnimBlock(MDLHandle_t handle, int nBlock) = 0;               // 8
	virtual virtualmodel_t* GetVirtualModel(MDLHandle_t handle) = 0;                       // 9
	virtual int GetAutoplayList(MDLHandle_t handle, unsigned short** pOut) = 0;             // 10
	virtual vertexFileHeader_t* GetVertexData(MDLHandle_t handle) = 0;                     // 11
	virtual void TouchAllData(MDLHandle_t handle) = 0;                                     // 12
	virtual void SetUserData(MDLHandle_t handle, void* pData) = 0;                         // 13
	virtual void* GetUserData(MDLHandle_t handle) = 0;                                     // 14
	virtual bool IsErrorModel(MDLHandle_t handle) = 0;                                     // 15
	virtual void Flush(MDLCacheFlush_t nFlushFlags = MDLCACHE_FLUSH_ALL) = 0;              // 16
	virtual void Flush(MDLHandle_t handle, int nFlushFlags = MDLCACHE_FLUSH_ALL) = 0;      // 17
	virtual const char* GetModelName(MDLHandle_t handle) = 0;                              // 18
	virtual virtualmodel_t* GetVirtualModelFast(const studiohdr_t* pStudioHdr, MDLHandle_t handle) = 0; // 19
	virtual void BeginLock() = 0;                                                          // 20
	virtual void EndLock() = 0;                                                            // 21
	virtual int* GetFrameUnlockCounterPtrOLD() = 0;                                       // 22
	virtual void FinishPendingLoads() = 0;                                                 // 23
};
