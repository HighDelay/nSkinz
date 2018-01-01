#pragma once
#include "declarations.hpp"
#include "IClientEntity.hpp"
#include "../Utilities/netvar_manager.hpp"

namespace sdk
{
	class C_BaseEntity : public IClientEntity
	{
	public:
		NETVAR_OFFSET(GetIndex, "CBaseEntity", "m_bIsAutoaimTarget", +0x4, int);
		NETVAR(GetModelIndex, "CBaseEntity", "m_nModelIndex", unsigned);

		void SetModelIndex(const int index)
		{
			get_vfunc<void(__thiscall*)(C_BaseEntity*, int)>(this, 75)(this, index);
		}
	};

	class C_BaseCombatCharacter : public C_BaseEntity
	{
	public:
		NETVAR(GetWeapons, "CBaseCombatCharacter", "m_hMyWeapons", std::array<CBaseHandle, MAX_WEAPONS>);
		PNETVAR(GetWearables, "CBaseCombatCharacter", "m_hMyWearables", CBaseHandle);
	};

	class C_BasePlayer : public C_BaseCombatCharacter
	{
	public:
		NETVAR(GetLifeState, "CBasePlayer", "m_lifeState", LifeState);
		NETVAR(GetViewModel, "CBasePlayer", "m_hViewModel[0]", CBaseHandle);
	};

	class C_BaseCombatWeapon : public C_BaseEntity
	{
	public:
		NETVAR(GetViewModelIndex, "CBaseCombatWeapon", "m_iViewModelIndex", int);
	};

	class C_BaseAttributableItem : public C_BaseCombatWeapon
	{
	public:
		NETVAR(GetAccountID, "CBaseAttributableItem", "m_iAccountID", int);
		NETVAR(GetItemDefinitionIndex, "CBaseAttributableItem", "m_iItemDefinitionIndex", int);
		NETVAR(GetItemIDHigh, "CBaseAttributableItem", "m_iItemIDHigh", int);
		NETVAR(GetEntityQuality, "CBaseAttributableItem", "m_iEntityQuality", int);
		NETVAR(GetCustomName, "CBaseAttributableItem", "m_szCustomName", char[32]);
		NETVAR(GetFallbackPaintKit, "CBaseAttributableItem", "m_nFallbackPaintKit", unsigned);
		NETVAR(GetFallbackSeed, "CBaseAttributableItem", "m_nFallbackSeed", unsigned);
		NETVAR(GetFallbackWear, "CBaseAttributableItem", "m_flFallbackWear", float);
		NETVAR(GetFallbackStatTrak, "CBaseAttributableItem", "m_nFallbackStatTrak", unsigned);
	};

	class C_BaseViewModel : public C_BaseEntity
	{
	public:
		NETVAR(GetOwner, "CBaseViewModel", "m_hOwner", CBaseHandle);
		NETVAR(GetWeapon, "CBaseViewModel", "m_hWeapon", CBaseHandle);
		NETPROP(GetSequenceProp, "CBaseViewModel", "m_nSequence");
	};
}