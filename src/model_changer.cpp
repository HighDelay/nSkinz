#include "model_changer.hpp"
#include "SDK.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Global state
namespace model_changer
{
	std::vector<model_replacement> g_replacements;
	bool g_enabled = true;
	bool g_enable_custom_sounds = true;
	std::vector<std::string> g_installed_models;
	bool g_models_scanned = false;
	std::string g_models_root;
	operation_status g_last_operation_status = operation_status::none;
	std::string g_last_operation_message = "Ready.";
	bool g_hook_active = false;
	const char* g_hook_status = "Not initialized";
	bool g_svpure_bypassed = false;
	const char* g_svpure_status = "Not initialized";
}

// Interface pointers (defined in nSkinz.cpp)
extern IMDLCache* g_mdl_cache;

static void set_operation(model_changer::operation_status status, const std::string& message)
{
	model_changer::g_last_operation_status = status;
	model_changer::g_last_operation_message = message;
}

template <size_t Size>
static void copy_config_string(char (&destination)[Size], const std::string& source)
{
	strncpy_s(destination, source.c_str(), _TRUNCATE);
}

// ========================================================
// INetworkStringTable for model precaching
// ========================================================

class INetworkStringTable
{
public:
	int AddString(bool bIsServer, const char* value, int length = -1, const void* userdata = nullptr)
	{
		typedef int(__thiscall* fn)(void*, bool, const char*, int, const void*);
		return get_vfunc<fn>(this, 8)(this, bIsServer, value, length, userdata);
	}

	int FindStringIndex(const char* string)
	{
		typedef int(__thiscall* fn)(void*, const char*);
		return get_vfunc<fn>(this, 12)(this, string);
	}
};

class CNetworkStringTableContainer
{
public:
	INetworkStringTable* FindTable(const char* tableName)
	{
		typedef INetworkStringTable* (__thiscall* fn)(void*, const char*);
		return get_vfunc<fn>(this, 3)(this, tableName);
	}
};

static CNetworkStringTableContainer* g_string_table_container = nullptr;

static std::vector<std::string> g_custom_sounds_list;
static bool g_custom_sounds_scanned = false;

static bool patch_mdl_internal_name(const char* original, const char* replacement);

static void scan_sounds_directory(const std::string& base_path, const std::string& relative_path, std::vector<std::string>& results)
{
	WIN32_FIND_DATAA find_data;
	HANDLE h_find = FindFirstFileA((base_path + relative_path + "*").c_str(), &find_data);
	if (h_find == INVALID_HANDLE_VALUE) return;

	do
	{
		const std::string name = find_data.cFileName;
		if (name == "." || name == "..") continue;

		if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			&& !(find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
			scan_sounds_directory(base_path, relative_path + name + "\\", results);
		else if (name.length() > 4)
		{
			std::string ext = name.substr(name.length() - 4);
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			if (ext == ".wav" || ext == ".mp3")
			{
				std::string rel = relative_path + name;
				std::replace(rel.begin(), rel.end(), '\\', '/');
				results.push_back("custom/" + rel);
			}
		}
	} while (FindNextFileA(h_find, &find_data));
	FindClose(h_find);
}

// ========================================================
// Raw VMT Hook for FindMDL
// ========================================================

typedef MDLHandle_t(__thiscall* FindMDL_fn)(void*, char*);
static FindMDL_fn g_original_find_mdl = nullptr;
static DWORD* g_mdl_original_vmt = nullptr;
static DWORD* g_mdl_custom_vmt = nullptr;
static DWORD* g_mdl_instance = nullptr;
static int g_mdl_vmt_size = 0;

MDLHandle_t __fastcall hkFindMDL(void* ecx, void* edx, char* FilePath)
{
	if (model_changer::g_enable_custom_sounds && g_string_table_container)
	{
		if (!g_custom_sounds_scanned)
		{
			char exe_path[MAX_PATH];
			GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
			std::string game_dir = exe_path;
			auto last_slash = game_dir.find_last_of("\\/");
			if (last_slash != std::string::npos) game_dir = game_dir.substr(0, last_slash + 1);

			std::string sounds_dir = game_dir + "csgo\\sound\\custom\\";
			scan_sounds_directory(sounds_dir, "", g_custom_sounds_list);
			g_custom_sounds_scanned = true;
		}

		if (!g_custom_sounds_list.empty())
		{
			auto* sound_table = g_string_table_container->FindTable("soundprecache");
			if (sound_table)
			{
				if (sound_table->FindStringIndex(g_custom_sounds_list[0].c_str()) == ((int)-1))
				{
					for (const auto& snd : g_custom_sounds_list)
					{
						sound_table->AddString(false, snd.c_str());
						sound_table->AddString(false, (std::string(")") + snd).c_str());
						sound_table->AddString(false, (std::string("*") + snd).c_str());
						sound_table->AddString(false, (std::string("*)") + snd).c_str());
					}
				}
			}
		}
	}

	if (model_changer::g_enabled && FilePath)
	{
		for (auto& rule : model_changer::g_replacements)
		{
			if (rule.enabled && rule.original[0] != '\0' && rule.replacement[0] != '\0')
			{
				if (strstr(FilePath, rule.original))
				{
					if (!rule.is_patched)
					{
						patch_mdl_internal_name(rule.original, rule.replacement);
						rule.is_patched = true;
					}

					// Pass new path to original (model-frog approach)
					return g_original_find_mdl(ecx, (char*)rule.replacement);
				}
			}
		}
	}
	return g_original_find_mdl(ecx, FilePath);
}

// ========================================================
// Engine Sound Hook (Custom Sounds)
// ========================================================

#include <unordered_map>

typedef void(__thiscall* EmitSound1_fn)(void*, void*, int, int, const char*, unsigned int, const char*, float, int, int, int, int, const void*, const void*, void*, bool, float, int, int);
static EmitSound1_fn g_original_emit_sound = nullptr;
static DWORD* g_snd_original_vmt = nullptr;
static DWORD* g_snd_custom_vmt = nullptr;
static DWORD* g_snd_instance = nullptr;

static std::unordered_map<std::string, std::string> g_sound_cache;

void __fastcall hkEmitSound1(void* ecx, void* edx, void* filter, int iEntIndex, int iChannel, const char* pSoundEntry, unsigned int nSoundEntryHash, const char* pSample, float flVolume, int iSoundLevel, int nSeed, int iFlags, int iPitch, const void* pOrigin, const void* pDirection, void* pUtlVecOrigins, bool bUpdatePositions, float soundtime, int speakerentity, int unk)
{
	if (model_changer::g_enabled && model_changer::g_enable_custom_sounds && pSample)
	{
		std::string sample = pSample;

		bool is_weapon_modded = false;
		size_t weapon_pos = sample.find("weapons/");
		if (weapon_pos != std::string::npos)
		{
			size_t start = weapon_pos + 8; // length of "weapons/"
			size_t end = sample.find("/", start);
			if (end != std::string::npos)
			{
				std::string wpn_name = sample.substr(start, end - start);
				for (const auto& rule : model_changer::g_replacements)
				{
					if (rule.enabled && rule.original[0] != '\0' && rule.replacement[0] != '\0')
					{
						if (strstr(rule.original, wpn_name.c_str()) != nullptr)
						{
							is_weapon_modded = true;
							break;
						}
					}
				}
			}
		}

		if (weapon_pos != std::string::npos && !is_weapon_modded)
		{
			// If it's a weapon sound but the underlying weapon has no custom model equipped, abort the override
			return g_original_emit_sound(ecx, filter, iEntIndex, iChannel, pSoundEntry, nSoundEntryHash, pSample, flVolume, iSoundLevel, nSeed, iFlags, iPitch, pOrigin, pDirection, pUtlVecOrigins, bUpdatePositions, soundtime, speakerentity, unk);
		}

		auto it = g_sound_cache.find(sample);
		if (it != g_sound_cache.end())
		{
			// Value was found in cache. If not empty, it's a valid custom sound.
			if (!it->second.empty())
			{
				void* g_engine_client = platform::get_interface("engine.dll", "VEngineClient014");
				if (g_engine_client)
				{
					typedef int(__thiscall* GetLocalPlayer_fn)(void*);
					int local_player = get_vfunc<GetLocalPlayer_fn>(g_engine_client, 12)(g_engine_client);

					if (iEntIndex == local_player)
					{
						float vol = (flVolume <= 0.0f) ? 1.0f : flVolume;
						char vol_str[32];
						snprintf(vol_str, sizeof(vol_str), "%.2f", vol * 0.6f);

						std::string vol_cmd = "playvol \"" + it->second + "\" " + vol_str;
						typedef void(__thiscall* ExecuteClientCmd_fn)(void*, const char*);
						get_vfunc<ExecuteClientCmd_fn>(g_engine_client, 108)(g_engine_client, vol_cmd.c_str());
						return; // Mute the original default sound for the local player's custom weapon
					}
				}

				// If not local player (or engine unavailable), play the original default sound natively in 3D
				return g_original_emit_sound(ecx, filter, iEntIndex, iChannel, pSoundEntry, nSoundEntryHash, pSample, flVolume, iSoundLevel, nSeed, iFlags, iPitch, pOrigin, pDirection, pUtlVecOrigins, bUpdatePositions, soundtime, speakerentity, unk);
			}
		}
		else
		{
			size_t weapon_pos = sample.find("weapons/");
			if (weapon_pos != std::string::npos)
			{
				std::string bare_sample = sample.substr(weapon_pos); // Strips `)`, `~`, `*` etc entirely!
				std::string clean_sample = "custom/" + bare_sample;

				char exe_path[MAX_PATH];
				GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
				std::string game_dir = exe_path;
				auto last_slash = game_dir.find_last_of("\\/");
				if (last_slash != std::string::npos) game_dir = game_dir.substr(0, last_slash + 1);
				std::string full_path = game_dir + "csgo\\sound\\" + clean_sample;

				// Filename fuzzy matching to allow arbitrary directory structures like `custom/weapons/m4a1_s/m4a1_silencer_01.wav`
				if (GetFileAttributesA(full_path.c_str()) == INVALID_FILE_ATTRIBUTES)
				{
					if (!g_custom_sounds_scanned)
					{
						std::string sounds_dir = game_dir + "csgo\\sound\\custom\\";
						scan_sounds_directory(sounds_dir, "", g_custom_sounds_list);
						g_custom_sounds_scanned = true;
					}

					auto last_slash_idx = bare_sample.find_last_of("/\\");
					std::string just_filename = (last_slash_idx != std::string::npos) ? bare_sample.substr(last_slash_idx + 1) : bare_sample;

					for (const auto& scanned : g_custom_sounds_list)
					{
						if (scanned.length() >= just_filename.length())
						{
							if (scanned.compare(scanned.length() - just_filename.length(), just_filename.length(), just_filename) == 0)
							{
								clean_sample = scanned;
								full_path = game_dir + "csgo\\sound\\" + clean_sample;
								break;
							}
						}
					}
				}

				// Fuzzy matching for e.g., awp_01.wav -> awp1.wav
				if (GetFileAttributesA(full_path.c_str()) == INVALID_FILE_ATTRIBUTES)
				{
					size_t underscore_zero = clean_sample.find("_0");
					if (underscore_zero != std::string::npos)
					{
						std::string stripped_clean = clean_sample;
						stripped_clean.erase(underscore_zero, 2);
						std::string stripped_path = game_dir + "csgo\\sound\\" + stripped_clean;
						if (GetFileAttributesA(stripped_path.c_str()) != INVALID_FILE_ATTRIBUTES)
						{
							clean_sample = stripped_clean;
							full_path = stripped_path;
						}
					}
				}

				if (GetFileAttributesA(full_path.c_str()) != INVALID_FILE_ATTRIBUTES)
				{
					g_sound_cache[sample] = clean_sample;
					
					if (g_string_table_container)
					{
						auto* sound_table = g_string_table_container->FindTable("soundprecache");
						if (sound_table) sound_table->AddString(false, clean_sample.c_str());
					}

					if (g_engine_sound) g_engine_sound->PrecacheSound(clean_sample.c_str(), true, true);
					
					void* g_engine_client = platform::get_interface("engine.dll", "VEngineClient014");
					if (g_engine_client)
					{
						typedef int(__thiscall* GetLocalPlayer_fn)(void*);
						int local_player = get_vfunc<GetLocalPlayer_fn>(g_engine_client, 12)(g_engine_client);

						if (iEntIndex == local_player)
						{
							float vol = (flVolume <= 0.0f) ? 1.0f : flVolume;
							char vol_str[32];
							snprintf(vol_str, sizeof(vol_str), "%.2f", vol * 0.6f);

							std::string vol_cmd = "playvol \"" + clean_sample + "\" " + vol_str;
							typedef void(__thiscall* ExecuteClientCmd_fn)(void*, const char*);
							get_vfunc<ExecuteClientCmd_fn>(g_engine_client, 108)(g_engine_client, vol_cmd.c_str());
							return; // Mute the original default sound for the local player's custom weapon
						}
					}

					// If not local player, play the original default sound natively in 3D
					return g_original_emit_sound(ecx, filter, iEntIndex, iChannel, pSoundEntry, nSoundEntryHash, pSample, flVolume, iSoundLevel, nSeed, iFlags, iPitch, pOrigin, pDirection, pUtlVecOrigins, bUpdatePositions, soundtime, speakerentity, unk);
				}
				else
				{
					g_sound_cache[sample] = ""; // Not found on disk
				}
			}
			else
			{
				g_sound_cache[sample] = ""; // Not a weapon sound
			}
		}
	}

	g_original_emit_sound(ecx, filter, iEntIndex, iChannel, pSoundEntry, nSoundEntryHash, pSample, flVolume, iSoundLevel, nSeed, iFlags, iPitch, pOrigin, pDirection, pUtlVecOrigins, bUpdatePositions, soundtime, speakerentity, unk);
}

// ========================================================
// sv_pure Bypass
// ========================================================

static void* g_filesystem = nullptr;
static DWORD* g_fs_original_vmt = nullptr;
static DWORD* g_fs_custom_vmt = nullptr;
static DWORD* g_fs_instance = nullptr;

typedef bool(__thiscall* LooseFilesAllowed_fn)(void*);
static LooseFilesAllowed_fn g_original_loose_files = nullptr;

bool __fastcall hkLooseFilesAllowed(void* ecx, void* edx)
{
	return true;
}

static uint8_t* g_cl_pure_whitelist_addr = nullptr;
static uint8_t  g_cl_pure_original_bytes[16] = {};
static int      g_cl_pure_patch_size = 0;

static uint8_t* ScanPattern(const char* module_name, const char* pattern)
{
	auto handle = GetModuleHandleA(module_name);
	if (!handle) return nullptr;

	auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(handle);
	auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
		reinterpret_cast<uint8_t*>(handle) + dosHeader->e_lfanew);
	auto size = ntHeaders->OptionalHeader.SizeOfImage;
	auto scanBytes = reinterpret_cast<uint8_t*>(handle);

	std::vector<int> bytes;
	auto start = const_cast<char*>(pattern);
	auto end = const_cast<char*>(pattern) + strlen(pattern);
	for (auto current = start; current < end; ++current)
	{
		if (*current == '?') {
			++current;
			if (*current == '?') ++current;
			bytes.push_back(-1);
		}
		else
			bytes.push_back(strtoul(current, &current, 16));
	}

	auto s = bytes.size();
	auto d = bytes.data();
	for (auto i = 0ul; i < size - s; ++i)
	{
		bool found = true;
		for (auto j = 0ul; j < s; ++j)
		{
			if (scanBytes[i + j] != d[j] && d[j] != -1) { found = false; break; }
		}
		if (found) return &scanBytes[i];
	}
	return nullptr;
}

static int CountVMTMethods(DWORD* vmt)
{
	int count = 0;
	while (!IsBadCodePtr((FARPROC)vmt[count])) count++;
	return count;
}

// ========================================================
// Model Precaching & Header Patching
// ========================================================

// Patches ALL occurrences of the original model name inside the custom .mdl file.
// Requires the custom model to have the exact same filename length as the original
// (e.g., replace 'v_' with 'c_'). If lengths do not match, it returns false.
static bool patch_mdl_internal_name(const char* original, const char* replacement)
{
	if (!original || !replacement || original[0] == '\0' || replacement[0] == '\0')
		return false;

	// Extract base names (without paths or extensions)
	std::string orig_name = original;
	auto slash = orig_name.find_last_of("\\/");
	if (slash != std::string::npos) orig_name = orig_name.substr(slash + 1);
	auto dot = orig_name.find_last_of('.');
	if (dot != std::string::npos) orig_name = orig_name.substr(0, dot);

	std::string repl_name = replacement;
	slash = repl_name.find_last_of("\\/");
	if (slash != std::string::npos) repl_name = repl_name.substr(slash + 1);
	dot = repl_name.find_last_of('.');
	if (dot != std::string::npos) repl_name = repl_name.substr(0, dot);

	if (orig_name.empty() || orig_name.length() != repl_name.length())
	{
		return false;
	}

	char exe_path[MAX_PATH];
	GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
	std::string game_dir = exe_path;
	auto last_slash = game_dir.find_last_of("\\/");
	if (last_slash != std::string::npos) game_dir = game_dir.substr(0, last_slash + 1);

	std::string full_path = game_dir + "csgo\\" + replacement;

	// Read entire file
	std::vector<uint8_t> file_data;
	FILE* f = nullptr;
	if (fopen_s(&f, full_path.c_str(), "rb") == 0 && f)
	{
		fseek(f, 0, SEEK_END);
		const long file_size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (file_size <= 0)
		{
			fclose(f);
			return false;
		}
		const auto size = static_cast<size_t>(file_size);
		file_data.resize(size);
		const auto bytes_read = fread(file_data.data(), 1, size, f);
		fclose(f);
		if (bytes_read != size)
			return false;
	}
	else
	{
		return false;
	}

	// Verify it's an MDL
	if (file_data.size() < 4 || *reinterpret_cast<uint32_t*>(file_data.data()) != 0x54534449)
	{
		return false;
	}

	int patched_count = 0;
	// Binary replace all occurrences of orig_name with repl_name
	for (size_t i = 0; i + orig_name.length() <= file_data.size(); ++i)
	{
		if (memcmp(&file_data[i], orig_name.data(), orig_name.length()) == 0)
		{
			memcpy(&file_data[i], repl_name.data(), repl_name.length());
			patched_count++;
		}
	}

	if (patched_count > 0)
	{
		if (fopen_s(&f, full_path.c_str(), "wb") == 0 && f)
		{
			fwrite(file_data.data(), 1, file_data.size(), f);
			fclose(f);
		}
		else
		{
			return false;
		}
	}

	return true;
}

auto model_changer::precache_models() -> void
{
	if (!g_string_table_container)
	{
		set_operation(operation_status::error, "Apply failed: model precache table interface is unavailable.");
		return;
	}
	if (!g_model_info || !g_mdl_cache)
	{
		set_operation(operation_status::error, "Apply failed: model interfaces are unavailable.");
		return;
	}

	auto* precache_table = g_string_table_container->FindTable("modelprecache");
	if (!precache_table)
	{
		set_operation(operation_status::error, "Apply failed: modelprecache is not available until a map is loaded.");
		return;
	}

	int enabled_count = 0;
	int applied_count = 0;
	int incomplete_count = 0;
	int failed_count = 0;
	int recovered_count = 0;

	for (auto& rule : g_replacements)
	{
		if (!rule.enabled)
			continue;

		++enabled_count;
		rule.precached_index = -1;
		if (rule.original[0] == '\0' || rule.replacement[0] == '\0')
		{
			++incomplete_count;
			continue;
		}

		// Patch the internal names inside the custom .mdl so it doesn't conflict with VPK
		rule.is_patched = patch_mdl_internal_name(rule.original, rule.replacement);

		// Add the path once; adding duplicates needlessly grows the network string table.
		if (precache_table->FindStringIndex(rule.replacement) == -1)
			precache_table->AddString(false, rule.replacement);

		// A failed load is sticky in MDLCache004. The fuller interface from the
		// reference implementation lets us reset and retry that cached error.
		auto handle = g_original_find_mdl
			? g_original_find_mdl(g_mdl_cache, rule.replacement)
			: g_mdl_cache->FindMDL(rule.replacement);
		bool is_error_model = handle == MDLHANDLE_INVALID || g_mdl_cache->IsErrorModel(handle);
		if (is_error_model && handle != MDLHANDLE_INVALID && g_mdl_vmt_size > 47)
		{
			g_mdl_cache->ResetErrorModelStatus(handle);
			handle = g_original_find_mdl
				? g_original_find_mdl(g_mdl_cache, rule.replacement)
				: g_mdl_cache->FindMDL(rule.replacement);
			is_error_model = handle == MDLHANDLE_INVALID || g_mdl_cache->IsErrorModel(handle);
			if (!is_error_model)
				++recovered_count;
		}

		if (!is_error_model && g_mdl_vmt_size > 46)
			g_mdl_cache->PreloadModel(handle);

		// Force the engine to load the model into memory first
		g_model_info->FindOrLoadModel(rule.replacement);

		// Get model index
		rule.precached_index = g_model_info->GetModelIndex(rule.replacement);
		if (!is_error_model && rule.precached_index > 0)
			++applied_count;
		else
		{
			rule.precached_index = -1;
			++failed_count;
		}
	}

	if (enabled_count == 0)
	{
		set_operation(operation_status::warning, "Nothing to apply: there are no enabled rules.");
		return;
	}

	char summary[256];
	snprintf(summary, sizeof(summary), "Applied %d of %d enabled rules", applied_count, enabled_count);
	std::string message = summary;
	if (incomplete_count > 0)
		message += "; " + std::to_string(incomplete_count) + " incomplete";
	if (failed_count > 0)
		message += "; " + std::to_string(failed_count) + " failed to load";
	if (recovered_count > 0)
		message += "; recovered " + std::to_string(recovered_count) + " cached error model";
	message += ".";

	set_operation(
		failed_count == 0 && incomplete_count == 0 ? operation_status::success
			: (applied_count > 0 ? operation_status::warning : operation_status::error),
		message);
}

int model_changer::get_replacement_index(const char* original_model_name)
{
	if (!g_enabled || !original_model_name) return -1;
	for (const auto& rule : g_replacements)
	{
		if (rule.enabled && rule.original[0] != '\0' && rule.precached_index > 0
			&& strstr(original_model_name, rule.original))
			return rule.precached_index;
	}
	return -1;
}

// ========================================================
// JSON Config
// ========================================================

static void model_to_json(json& j, const model_replacement& o)
{
	j = json{
		{"enabled", o.enabled},
		{"original", std::string(o.original)},
		{"replacement", std::string(o.replacement)}
	};
}

static void model_from_json(const json& j, model_replacement& o)
{
	if (j.contains("enabled")) o.enabled = j["enabled"].get<bool>();
	if (j.contains("original"))
		copy_config_string(o.original, j["original"].get<std::string>());
	if (j.contains("replacement"))
		copy_config_string(o.replacement, j["replacement"].get<std::string>());
	o.precached_index = -1;
	o.is_patched = false;
}

auto model_changer::save_config() -> void
{
	try
	{
		json j;
		j["enabled"] = g_enabled;
		j["custom_sounds"] = g_enable_custom_sounds;
		json rules_arr = json::array();
		for (const auto& rule : g_replacements)
		{
			json rj;
			model_to_json(rj, rule);
			rules_arr.push_back(rj);
		}
		j["rules"] = rules_arr;
		auto of = std::ofstream("nSkinz_models.json");
		if (!of.good())
		{
			set_operation(operation_status::error, "Save failed: could not open nSkinz_models.json.");
			return;
		}
		of << j.dump(4);
		of.flush();
		if (!of.good())
		{
			set_operation(operation_status::error, "Save failed while writing nSkinz_models.json.");
			return;
		}
		set_operation(operation_status::success,
			"Saved " + std::to_string(g_replacements.size()) + " rules to nSkinz_models.json.");
	}
	catch (const std::exception& error)
	{
		set_operation(operation_status::error, std::string("Save failed: ") + error.what());
	}
}

auto model_changer::load_config() -> void
{
	try
	{
		auto ifile = std::ifstream("nSkinz_models.json");
		if (!ifile.good())
		{
			set_operation(operation_status::warning, "No nSkinz_models.json config exists yet.");
			return;
		}

		auto j = json::parse(ifile);
		if (!j.is_object())
			throw std::runtime_error("the config root must be an object");

		auto loaded_enabled = g_enabled;
		auto loaded_custom_sounds = g_enable_custom_sounds;
		auto loaded_rules = g_replacements;

		if (j.contains("enabled")) loaded_enabled = j["enabled"].get<bool>();
		if (j.contains("custom_sounds")) loaded_custom_sounds = j["custom_sounds"].get<bool>();
		if (j.contains("rules"))
		{
			if (!j["rules"].is_array())
				throw std::runtime_error("rules must be an array");
			loaded_rules.clear();
			for (const auto& rj : j["rules"])
			{
				if (!rj.is_object())
					throw std::runtime_error("each rule must be an object");
				model_replacement rule;
				model_from_json(rj, rule);
				loaded_rules.push_back(rule);
			}
		}

		g_enabled = loaded_enabled;
		g_enable_custom_sounds = loaded_custom_sounds;
		g_replacements = std::move(loaded_rules);
		set_operation(operation_status::success,
			"Loaded " + std::to_string(g_replacements.size()) + " rules from nSkinz_models.json; apply when in a map.");
	}
	catch (const std::exception& error)
	{
		set_operation(operation_status::error,
			std::string("Load failed; current rules were kept: ") + error.what());
	}
}

// ========================================================
// Directory scanner
// ========================================================

static void scan_directory(const std::string& base_path, const std::string& relative_path, std::vector<std::string>& results)
{
	WIN32_FIND_DATAA find_data;
	HANDLE h_find = FindFirstFileA((base_path + relative_path + "*").c_str(), &find_data);
	if (h_find == INVALID_HANDLE_VALUE) return;

	do
	{
		const std::string name = find_data.cFileName;
		if (name == "." || name == "..") continue;

		if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			&& !(find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
			scan_directory(base_path, relative_path + name + "\\", results);
		else if (name.length() > 4)
		{
			std::string ext = name.substr(name.length() - 4);
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			if (ext == ".mdl")
			{
				std::string rel = relative_path + name;
				std::replace(rel.begin(), rel.end(), '\\', '/');
				results.push_back("models/" + rel);
			}
		}
	} while (FindNextFileA(h_find, &find_data));
	FindClose(h_find);
}

auto model_changer::scan_installed_models() -> void
{
	g_installed_models.clear();
	g_models_root.clear();
	char exe_path[MAX_PATH];
	if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) == 0)
	{
		g_models_scanned = true;
		set_operation(operation_status::error, "Model scan failed: the game directory could not be resolved.");
		return;
	}
	std::string game_dir = exe_path;
	auto last_slash = game_dir.find_last_of("\\/");
	if (last_slash != std::string::npos) game_dir = game_dir.substr(0, last_slash + 1);

	std::string models_dir = game_dir + "csgo\\models\\";
	WIN32_FIND_DATAA test;
	HANDLE h_test = FindFirstFileA((models_dir + "*").c_str(), &test);
	if (h_test == INVALID_HANDLE_VALUE)
	{
		models_dir = game_dir + "models\\";
		h_test = FindFirstFileA((models_dir + "*").c_str(), &test);
		if (h_test == INVALID_HANDLE_VALUE)
		{
			g_models_scanned = true;
			set_operation(operation_status::error,
				"Model scan failed: neither csgo/models nor models was found beside the game executable.");
			return;
		}
	}
	FindClose(h_test);

	g_models_root = models_dir;
	scan_directory(models_dir, "", g_installed_models);
	std::sort(g_installed_models.begin(), g_installed_models.end());
	g_installed_models.erase(
		std::unique(g_installed_models.begin(), g_installed_models.end()),
		g_installed_models.end());
	g_models_scanned = true;
	set_operation(g_installed_models.empty() ? operation_status::warning : operation_status::success,
		g_installed_models.empty()
			? "Model scan completed, but no loose .mdl files were found."
			: "Found " + std::to_string(g_installed_models.size()) + " installed model files.");
}

// ========================================================
// Initialize / Uninitialize
// ========================================================

auto model_changer::initialize() -> void
{
	load_config();

	// ---- Get string table container for precaching ----
	g_string_table_container = reinterpret_cast<CNetworkStringTableContainer*>(
		platform::get_interface("engine.dll", "VEngineClientStringTable001"));

	// ---- Hook FindMDL ----
	if (!g_mdl_cache) { g_hook_status = "FAILED: MDLCache004 not found"; return; }

	g_mdl_instance = (DWORD*)g_mdl_cache;
	g_mdl_original_vmt = (DWORD*)*g_mdl_instance;
	g_mdl_vmt_size = CountVMTMethods(g_mdl_original_vmt);
	if (g_mdl_vmt_size <= 10) { g_hook_status = "FAILED: MDLCache VMT too small"; return; }

	g_mdl_custom_vmt = (DWORD*)malloc(g_mdl_vmt_size * sizeof(DWORD));
	if (!g_mdl_custom_vmt) { g_hook_status = "FAILED: malloc"; return; }
	memcpy(g_mdl_custom_vmt, g_mdl_original_vmt, g_mdl_vmt_size * sizeof(DWORD));

	g_original_find_mdl = (FindMDL_fn)g_mdl_original_vmt[10];
	g_mdl_custom_vmt[10] = (DWORD)&hkFindMDL;
	*g_mdl_instance = (DWORD)g_mdl_custom_vmt;

	g_hook_active = true;
	g_hook_status = "Active";

	// ---- Hook IEngineSound ----
	if (g_engine_sound)
	{
		g_snd_instance = (DWORD*)g_engine_sound;
		g_snd_original_vmt = (DWORD*)*g_snd_instance;
		int snd_vmt_size = CountVMTMethods(g_snd_original_vmt);
		if (snd_vmt_size > 5)
		{
			g_snd_custom_vmt = (DWORD*)malloc(snd_vmt_size * sizeof(DWORD));
			if (g_snd_custom_vmt)
			{
				memcpy(g_snd_custom_vmt, g_snd_original_vmt, snd_vmt_size * sizeof(DWORD));
				g_original_emit_sound = (EmitSound1_fn)g_snd_original_vmt[5];
				g_snd_custom_vmt[5] = (DWORD)&hkEmitSound1;
				*g_snd_instance = (DWORD)g_snd_custom_vmt;
			}
		}
	}

	// ---- sv_pure Bypass: LooseFilesAllowed ----
	g_filesystem = platform::get_interface("filesystem_stdio.dll", "VFileSystem017");
	if (!g_filesystem)
		g_svpure_status = "FAILED: VFileSystem not found";
	else
	{
		g_fs_instance = (DWORD*)g_filesystem;
		g_fs_original_vmt = (DWORD*)*g_fs_instance;
		int fs_vmt_size = CountVMTMethods(g_fs_original_vmt);

		if (fs_vmt_size <= 128)
			g_svpure_status = "FAILED: FileSystem VMT too small";
		else
		{
			g_fs_custom_vmt = (DWORD*)malloc(fs_vmt_size * sizeof(DWORD));
			if (g_fs_custom_vmt)
			{
				memcpy(g_fs_custom_vmt, g_fs_original_vmt, fs_vmt_size * sizeof(DWORD));
				g_original_loose_files = (LooseFilesAllowed_fn)g_fs_original_vmt[128];
				g_fs_custom_vmt[128] = (DWORD)&hkLooseFilesAllowed;
				*g_fs_instance = (DWORD)g_fs_custom_vmt;
				g_svpure_bypassed = true;
				g_svpure_status = "Active (LooseFiles)";
			}
			else g_svpure_status = "FAILED: malloc";
		}
	}

	// ---- sv_pure Bypass: CL_CheckForPureServerWhitelist ----
	g_cl_pure_whitelist_addr = ScanPattern("engine.dll",
		"8B 0D ? ? ? ? 56 83 B9 ? ? ? ? ? 7E 6E");
	if (g_cl_pure_whitelist_addr)
	{
		DWORD old_protect;
		if (VirtualProtect(g_cl_pure_whitelist_addr, 1, PAGE_EXECUTE_READWRITE, &old_protect))
		{
			g_cl_pure_original_bytes[0] = g_cl_pure_whitelist_addr[0];
			g_cl_pure_patch_size = 1;
			g_cl_pure_whitelist_addr[0] = 0xC3;
			VirtualProtect(g_cl_pure_whitelist_addr, 1, old_protect, &old_protect);
			if (g_svpure_bypassed) g_svpure_status = "Active (LooseFiles + Whitelist)";
			else { g_svpure_bypassed = true; g_svpure_status = "Active (Whitelist only)"; }
		}
	}
	else if (!g_svpure_bypassed)
		g_svpure_status = "FAILED: No patterns found";

	// ---- Patch Vertex Checksum Validation ----
	// "Error Vertex File" can come from datacache.dll or studiorender.dll
	{
		static const char* dlls_to_try[] = { "datacache.dll", "studiorender.dll", nullptr };
		bool checksum_patched = false;

		for (int dll_idx = 0; dlls_to_try[dll_idx] && !checksum_patched; ++dll_idx)
		{
			auto hMod = GetModuleHandleA(dlls_to_try[dll_idx]);
			if (!hMod)
				continue;

			auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(hMod);
			auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
				reinterpret_cast<uint8_t*>(hMod) + dosHeader->e_lfanew);
			auto imageSize = ntHeaders->OptionalHeader.SizeOfImage;
			auto imageBase = reinterpret_cast<uint8_t*>(hMod);

			// Find "Error Vertex File" string
			const char target_str[] = "Error Vertex File";
			uint8_t* err_str_addr = nullptr;
			for (auto i = 0ul; i < imageSize - sizeof(target_str); ++i)
			{
				if (memcmp(imageBase + i, target_str, sizeof(target_str) - 1) == 0)
				{
					err_str_addr = imageBase + i;
					break;
				}
			}

			if (!err_str_addr)
				continue;

			// Find ALL code references to this string and try each one
			auto str_addr_val = reinterpret_cast<uint32_t>(err_str_addr);
			bool ref_found = false;

			for (auto i = 0ul; i < imageSize - 4 && !checksum_patched; ++i)
			{
				if (*reinterpret_cast<uint32_t*>(imageBase + i) != str_addr_val)
					continue;

				ref_found = true;
				uint8_t* code_ref = imageBase + i;

				// Walk backwards up to 128 bytes for a conditional jump
				for (int walk = 1; walk <= 128 && !checksum_patched; ++walk)
				{
					uint8_t* candidate = code_ref - walk;
					if (candidate < imageBase) break;

					int patch_size = 0;
					// Jcc rel8: 70-7F (JO, JNO, JB, JNB, JZ, JNZ, JBE, JA, JS, JNS, JP, JNP, JL, JGE, JLE, JG)
					if (candidate[0] >= 0x70 && candidate[0] <= 0x7F)
						patch_size = 2;
					// Jcc rel32: 0F 80-8F
					else if (candidate[0] == 0x0F && candidate[1] >= 0x80 && candidate[1] <= 0x8F)
						patch_size = 6;

					if (patch_size == 0) continue;

					DWORD old_protect;
					if (VirtualProtect(candidate, patch_size, PAGE_EXECUTE_READWRITE, &old_protect))
					{
						memset(candidate, 0x90, patch_size); // NOP
						VirtualProtect(candidate, patch_size, old_protect, &old_protect);
						checksum_patched = true;

						if (g_svpure_bypassed)
							g_svpure_status = "Active (LooseFiles + Whitelist + ChecksumPatch)";
						else
						{
							g_svpure_bypassed = true;
							g_svpure_status = "Active (ChecksumPatch only)";
						}
					}
				}
			}
		} // for (i) code refs
		} // for (dll_idx)
	} // checksum patch

auto model_changer::uninitialize() -> void
{
	if (g_mdl_instance && g_mdl_original_vmt) *g_mdl_instance = (DWORD)g_mdl_original_vmt;
	if (g_mdl_custom_vmt) { free(g_mdl_custom_vmt); g_mdl_custom_vmt = nullptr; }
	
	if (g_snd_instance && g_snd_original_vmt) *g_snd_instance = (DWORD)g_snd_original_vmt;
	if (g_snd_custom_vmt) { free(g_snd_custom_vmt); g_snd_custom_vmt = nullptr; }

	if (g_fs_instance && g_fs_original_vmt) *g_fs_instance = (DWORD)g_fs_original_vmt;
	if (g_fs_custom_vmt) { free(g_fs_custom_vmt); g_fs_custom_vmt = nullptr; }
	
	if (g_cl_pure_whitelist_addr && g_cl_pure_patch_size > 0)
	{
		DWORD old_protect;
		if (VirtualProtect(g_cl_pure_whitelist_addr, g_cl_pure_patch_size, PAGE_EXECUTE_READWRITE, &old_protect))
		{
			memcpy(g_cl_pure_whitelist_addr, g_cl_pure_original_bytes, g_cl_pure_patch_size);
			VirtualProtect(g_cl_pure_whitelist_addr, g_cl_pure_patch_size, old_protect, &old_protect);
		}
	}
	g_hook_active = false;
	g_svpure_bypassed = false;
}
