#pragma once
#include "SDK/IMDLCache.hpp"

#include <vector>
#include <string>
#include <Windows.h>

// A single model replacement rule
struct model_replacement
{
	bool enabled = true;
	char original[128] = "";     // Substring to match in the model path
	char replacement[256] = "";  // Full replacement path
	int precached_index = -1;    // Cached model index after precaching
	bool is_patched = false;     // Whether internal name is patched in the .mdl header
};

namespace model_changer
{
	extern std::vector<model_replacement> g_replacements;
	extern bool g_enabled;
	extern bool g_enable_custom_sounds;

	// Retrieves the precached index of a custom model if a rule matches
	int get_replacement_index(const char* original_model_name);

	// Installed model files scanned from game directory
	extern std::vector<std::string> g_installed_models;
	extern bool g_models_scanned;

	// Hook status
	extern bool g_hook_active;
	extern const char* g_hook_status;
	extern bool g_svpure_bypassed;
	extern const char* g_svpure_status;

	auto initialize() -> void;
	auto uninitialize() -> void;

	auto scan_installed_models() -> void;
	auto save_config() -> void;
	auto load_config() -> void;

	// Precache all replacement models into the string table
	auto precache_models() -> void;
}
