#include "model_changer.hpp"
#include "SDK.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Global state
namespace model_changer
{
	std::vector<model_replacement> g_replacements;
	bool g_enabled = true;
	std::vector<std::string> g_installed_models;
	bool g_models_scanned = false;
	bool g_hook_active = false;
	const char* g_hook_status = "Not initialized";
	bool g_svpure_bypassed = false;
	const char* g_svpure_status = "Not initialized";
	char g_debug_info[512] = "Ready.";
}

// Interface pointers (defined in nSkinz.cpp)
extern IMDLCache* g_mdl_cache;

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

static bool patch_mdl_internal_name(const char* original, const char* replacement);

// ========================================================
// Raw VMT Hook for FindMDL
// ========================================================

typedef MDLHandle_t(__thiscall* FindMDL_fn)(void*, char*);
static FindMDL_fn g_original_find_mdl = nullptr;
static DWORD* g_mdl_original_vmt = nullptr;
static DWORD* g_mdl_custom_vmt = nullptr;
static DWORD* g_mdl_instance = nullptr;

MDLHandle_t __fastcall hkFindMDL(void* ecx, void* edx, char* FilePath)
{
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

	if (orig_name.length() != repl_name.length())
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
		size_t size = ftell(f);
		fseek(f, 0, SEEK_SET);
		file_data.resize(size);
		fread(file_data.data(), 1, size, f);
		fclose(f);
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
	for (size_t i = 0; i < file_data.size() - orig_name.length(); ++i)
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
	if (!g_string_table_container) return;

	auto* precache_table = g_string_table_container->FindTable("modelprecache");
	if (!precache_table) return;

	for (auto& rule : g_replacements)
	{
		if (!rule.enabled || rule.replacement[0] == '\0') continue;

		// Patch the internal names inside the custom .mdl so it doesn't conflict with VPK
		patch_mdl_internal_name(rule.original, rule.replacement);

		// Force the engine to load the model into memory first
		if (g_model_info)
			g_model_info->FindOrLoadModel(rule.replacement);

		// Add to precache string table
		precache_table->AddString(false, rule.replacement);

		// Get model index
		rule.precached_index = g_model_info->GetModelIndex(rule.replacement);
	}
}

int model_changer::get_replacement_index(const char* original_model_name)
{
	if (!g_enabled) return -1;
	for (const auto& rule : g_replacements)
	{
		if (rule.enabled && rule.precached_index > 0 && strstr(original_model_name, rule.original))
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
		strcpy_s(o.original, j["original"].get<std::string>().c_str());
	if (j.contains("replacement"))
		strcpy_s(o.replacement, j["replacement"].get<std::string>().c_str());
	o.precached_index = -1;
}

auto model_changer::save_config() -> void
{
	try
	{
		json j;
		j["enabled"] = g_enabled;
		json rules_arr = json::array();
		for (const auto& rule : g_replacements)
		{
			json rj;
			model_to_json(rj, rule);
			rules_arr.push_back(rj);
		}
		j["rules"] = rules_arr;
		auto of = std::ofstream("nSkinz_models.json");
		if (of.good()) of << j.dump(4);
	}
	catch (...) {}
}

auto model_changer::load_config() -> void
{
	try
	{
		auto ifile = std::ifstream("nSkinz_models.json");
		if (ifile.good())
		{
			auto j = json::parse(ifile);
			if (j.contains("enabled")) g_enabled = j["enabled"].get<bool>();
			if (j.contains("rules"))
			{
				g_replacements.clear();
				for (const auto& rj : j["rules"])
				{
					model_replacement rule;
					model_from_json(rj, rule);
					g_replacements.push_back(rule);
				}
			}
		}
	}
	catch (const std::exception&) {}
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

		if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
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
	char exe_path[MAX_PATH];
	GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
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
		if (h_test == INVALID_HANDLE_VALUE) { g_models_scanned = true; return; }
	}
	FindClose(h_test);

	scan_directory(models_dir, "", g_installed_models);
	std::sort(g_installed_models.begin(), g_installed_models.end());
	g_models_scanned = true;
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
	int mdl_vmt_size = CountVMTMethods(g_mdl_original_vmt);
	if (mdl_vmt_size <= 10) { g_hook_status = "FAILED: MDLCache VMT too small"; return; }

	g_mdl_custom_vmt = (DWORD*)malloc(mdl_vmt_size * sizeof(DWORD));
	if (!g_mdl_custom_vmt) { g_hook_status = "FAILED: malloc"; return; }
	memcpy(g_mdl_custom_vmt, g_mdl_original_vmt, mdl_vmt_size * sizeof(DWORD));

	g_original_find_mdl = (FindMDL_fn)g_mdl_original_vmt[10];
	g_mdl_custom_vmt[10] = (DWORD)&hkFindMDL;
	*g_mdl_instance = (DWORD)g_mdl_custom_vmt;

	g_hook_active = true;
	g_hook_status = "Active";

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
