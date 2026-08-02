/* This file is part of nSkinz by namazso, licensed under the MIT license:
*
* MIT License
*
* Copyright (c) namazso 2018
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include "config.hpp"
#include "item_definitions.hpp"
#include "SDK.hpp"
#include "kit_parser.hpp"
#include "update_check.hpp"

#include <imgui.h>
#include <functional>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "model_changer.hpp"

namespace ImGui
{
	// ImGui ListBox lambda binder
	static bool ListBox(const char* label, int* current_item,  std::function<const char*(int)> lambda, int items_count, int height_in_items)
	{
		return ImGui::ListBox(label, current_item, [](void* data, int idx) -> const char*
		{
			return (*reinterpret_cast<std::function<const char*(int)>*>(data))(idx);
		}, &lambda, items_count, height_in_items);
	}
}

// Allocation-free case-insensitive substring search for live UI filters.
static bool contains_ci(std::string_view haystack, std::string_view needle)
{
	if (needle.empty())
		return true;
	return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
		[](char left, char right)
		{
			return std::tolower(static_cast<unsigned char>(left))
				== std::tolower(static_cast<unsigned char>(right));
		}) != haystack.end();
}

// Filtered combo: shows an InputText search box and a ListBox with filtered results
static bool FilteredCombo(const char* label, int* current_item, char* search_buf, int search_buf_size,
	const std::vector<game_data::paint_kit>& kits, std::vector<int>& filtered_indices)
{
	ImGui::PushID(label);

	// Search input
	char search_label[64];
	sprintf_s(search_label, "Search##%s", label);
	ImGui::InputText(search_label, search_buf, search_buf_size);

	// Build filtered index list
	filtered_indices.clear();
	for (int i = 0; i < (int)kits.size(); i++)
	{
		if (contains_ci(kits[i].name, search_buf))
			filtered_indices.push_back(i);
	}

	// Find current item in filtered list
	int filtered_current = 0;
	for (int i = 0; i < (int)filtered_indices.size(); i++)
	{
		if (filtered_indices[i] == *current_item)
		{
			filtered_current = i;
			break;
		}
	}

	// Show combo with filtered results
	bool changed = false;
	const auto* filtered_ptr = &filtered_indices;
	const auto* kits_ptr = &kits;

	struct combo_data { const std::vector<int>* indices; const std::vector<game_data::paint_kit>* kits; };
	combo_data cd = { filtered_ptr, kits_ptr };

	if (ImGui::Combo(label, &filtered_current, [](void* data, int idx) -> const char*
	{
		auto* cd = reinterpret_cast<combo_data*>(data);
		return cd->kits->at(cd->indices->at(idx)).name.c_str();
	}, &cd, (int)filtered_indices.size(), 10))
	{
		if (filtered_current >= 0 && filtered_current < (int)filtered_indices.size())
		{
			*current_item = filtered_indices[filtered_current];
			changed = true;
		}
	}

	ImGui::PopID();
	return changed;
}

namespace
{
	struct model_target_preset
	{
		int category;
		const char* label;
		const char* match;
	};

	static const char* const model_target_categories[] =
	{
		"All targets",
		"Knives",
		"Pistols",
		"Rifles & snipers",
		"SMGs",
		"Heavy weapons",
		"Player models",
		"Arms & gloves"
	};

	// These are match strings, not replacement paths. Short family matches are
	// intentional for player variants; enabled rules are evaluated top to bottom.
	static const model_target_preset model_target_presets[] =
	{
		{ 1, "Any knife viewmodel", "models/weapons/v_knife_" },
		{ 1, "Default knife (CT)", "v_knife_default_ct.mdl" },
		{ 1, "Default knife (T)", "v_knife_default_t.mdl" },
		{ 1, "Bayonet", "v_knife_bayonet.mdl" },
		{ 1, "Classic Knife", "v_knife_css.mdl" },
		{ 1, "Flip Knife", "v_knife_flip.mdl" },
		{ 1, "Gut Knife", "v_knife_gut.mdl" },
		{ 1, "Karambit", "v_knife_karam.mdl" },
		{ 1, "M9 Bayonet", "v_knife_m9_bay.mdl" },
		{ 1, "Huntsman Knife", "v_knife_tactical.mdl" },
		{ 1, "Falchion Knife", "v_knife_falchion_advanced.mdl" },
		{ 1, "Bowie Knife", "v_knife_survival_bowie.mdl" },
		{ 1, "Butterfly Knife", "v_knife_butterfly.mdl" },
		{ 1, "Shadow Daggers", "v_knife_push.mdl" },
		{ 1, "Paracord Knife", "v_knife_cord.mdl" },
		{ 1, "Survival Knife", "v_knife_canis.mdl" },
		{ 1, "Ursus Knife", "v_knife_ursus.mdl" },
		{ 1, "Navaja Knife", "v_knife_gypsy_jackknife.mdl" },
		{ 1, "Nomad Knife", "v_knife_outdoor.mdl" },
		{ 1, "Stiletto Knife", "v_knife_stiletto.mdl" },
		{ 1, "Talon Knife", "v_knife_widowmaker.mdl" },
		{ 1, "Skeleton Knife", "v_knife_skeleton.mdl" },

		{ 2, "Desert Eagle", "v_pist_deagle.mdl" },
		{ 2, "Dual Berettas", "v_pist_elite.mdl" },
		{ 2, "Five-SeveN", "v_pist_fiveseven.mdl" },
		{ 2, "Glock-18", "v_pist_glock18.mdl" },
		{ 2, "P2000", "v_pist_hkp2000.mdl" },
		{ 2, "P250", "v_pist_p250.mdl" },
		{ 2, "Tec-9", "v_pist_tec9.mdl" },
		{ 2, "USP-S", "v_pist_223.mdl" },
		{ 2, "CZ75-Auto", "v_pist_cz_75.mdl" },
		{ 2, "R8 Revolver", "v_pist_revolver.mdl" },

		{ 3, "AK-47", "v_rif_ak47.mdl" },
		{ 3, "AUG", "v_rif_aug.mdl" },
		{ 3, "FAMAS", "v_rif_famas.mdl" },
		{ 3, "Galil AR", "v_rif_galilar.mdl" },
		{ 3, "M4A4", "v_rif_m4a1.mdl" },
		{ 3, "M4A1-S", "v_rif_m4a1_s.mdl" },
		{ 3, "SG 553", "v_rif_sg556.mdl" },
		{ 3, "AWP", "v_snip_awp.mdl" },
		{ 3, "SSG 08", "v_snip_ssg08.mdl" },
		{ 3, "SCAR-20", "v_snip_scar20.mdl" },
		{ 3, "G3SG1", "v_snip_g3sg1.mdl" },

		{ 4, "MP9", "v_smg_mp9.mdl" },
		{ 4, "MAC-10", "v_smg_mac10.mdl" },
		{ 4, "MP7", "v_smg_mp7.mdl" },
		{ 4, "MP5-SD", "v_smg_mp5sd.mdl" },
		{ 4, "UMP-45", "v_smg_ump45.mdl" },
		{ 4, "P90", "v_smg_p90.mdl" },
		{ 4, "PP-Bizon", "v_smg_bizon.mdl" },

		{ 5, "Nova", "v_shot_nova.mdl" },
		{ 5, "XM1014", "v_shot_xm1014.mdl" },
		{ 5, "Sawed-Off", "v_shot_sawedoff.mdl" },
		{ 5, "MAG-7", "v_shot_mag7.mdl" },
		{ 5, "M249", "v_mach_m249.mdl" },
		{ 5, "Negev", "v_mach_negev.mdl" },

		{ 6, "Any CT player variant", "/ctm_" },
		{ 6, "Any T player variant", "/tm_" },
		{ 6, "FBI family (CT)", "ctm_fbi" },
		{ 6, "GIGN family (CT)", "ctm_gign" },
		{ 6, "GSG-9 family (CT)", "ctm_gsg9" },
		{ 6, "IDF family (CT)", "ctm_idf" },
		{ 6, "SAS family (CT)", "ctm_sas" },
		{ 6, "SEAL Team 6 family (CT)", "ctm_st6" },
		{ 6, "SWAT family (CT)", "ctm_swat" },
		{ 6, "Anarchist family (T)", "tm_anarchist" },
		{ 6, "Balkan family (T)", "tm_balkan" },
		{ 6, "Leet family (T)", "tm_leet" },
		{ 6, "Phoenix family (T)", "tm_phoenix" },
		{ 6, "Pirate family (T)", "tm_pirate" },
		{ 6, "Professional family (T)", "tm_professional" },
		{ 6, "Separatist family (T)", "tm_separatist" },

		{ 7, "Any arms model", "/arms/" },
		{ 7, "Default gloves (CT)", "v_glove_hardknuckle.mdl" },
		{ 7, "Default gloves (T)", "v_glove_fingerless.mdl" },
		{ 7, "Bloodhound gloves", "v_glove_bloodhound.mdl" },
		{ 7, "Broken Fang gloves", "v_glove_bloodhound_brokenfang.mdl" },
		{ 7, "Hydra gloves", "v_glove_bloodhound_hydra.mdl" },
		{ 7, "Hand Wraps", "v_glove_handwrap_leathery.mdl" },
		{ 7, "Moto Gloves", "v_glove_motorcycle.mdl" },
		{ 7, "Specialist Gloves", "v_glove_specialist.mdl" },
		{ 7, "Sport Gloves", "v_glove_sporty.mdl" },
		{ 7, "Driver Gloves", "v_glove_slick.mdl" }
	};

	template <size_t Size>
	static bool set_model_rule_text(char (&destination)[Size], const char* value, model_replacement& rule)
	{
		if (!value || std::strcmp(destination, value) == 0)
			return false;
		strncpy_s(destination, value, _TRUNCATE);
		rule.precached_index = -1;
		rule.is_patched = false;
		return true;
	}

	static void invalidate_model_rule(model_replacement& rule)
	{
		rule.precached_index = -1;
		rule.is_patched = false;
	}

	static const char* model_basename(const char* path)
	{
		if (!path || path[0] == '\0')
			return "";
		const auto slash = std::strrchr(path, '/');
		const auto backslash = std::strrchr(path, '\\');
		const auto separator = !slash ? backslash : (!backslash || slash > backslash ? slash : backslash);
		return separator ? separator + 1 : path;
	}

	static bool equals_ci(const std::string& left, const char* right)
	{
		if (!right || left.size() != std::strlen(right))
			return false;
		for (size_t i = 0; i < left.size(); ++i)
		{
			if (std::tolower(static_cast<unsigned char>(left[i]))
				!= std::tolower(static_cast<unsigned char>(right[i])))
				return false;
		}
		return true;
	}

	static bool has_mdl_extension(const char* path)
	{
		if (!path)
			return false;
		const auto length = std::strlen(path);
		return length >= 4
			&& std::tolower(static_cast<unsigned char>(path[length - 4])) == '.'
			&& std::tolower(static_cast<unsigned char>(path[length - 3])) == 'm'
			&& std::tolower(static_cast<unsigned char>(path[length - 2])) == 'd'
			&& std::tolower(static_cast<unsigned char>(path[length - 1])) == 'l';
	}

	static bool matches_model_type(const std::string& path, int type)
	{
		switch (type)
		{
		case 1: return contains_ci(path, "models/weapons/");
		case 2: return contains_ci(path, "models/player/");
		case 3: return contains_ci(path, "/arms/") || contains_ci(path, "glove");
		default: return true;
		}
	}

	static int suggested_model_type(const char* match)
	{
		if (!match)
			return 0;
		if (std::strstr(match, "ctm_") || std::strstr(match, "tm_"))
			return 2;
		if (std::strstr(match, "glove") || std::strstr(match, "/arms/"))
			return 3;
		return 1;
	}

	static const model_target_preset* find_target_preset(const char* match)
	{
		if (!match)
			return nullptr;
		for (const auto& preset : model_target_presets)
			if (std::strcmp(match, preset.match) == 0)
				return &preset;
		return nullptr;
	}

	static ImVec4 operation_color(model_changer::operation_status status)
	{
		switch (status)
		{
		case model_changer::operation_status::success: return ImVec4(0.35f, 0.95f, 0.45f, 1.0f);
		case model_changer::operation_status::warning: return ImVec4(1.0f, 0.78f, 0.28f, 1.0f);
		case model_changer::operation_status::error: return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
		default: return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
		}
	}

	static void draw_model_changer_tab()
	{
		auto& rules = model_changer::g_replacements;
		static int selected_rule = -1;
		static int target_category = 0;
		static int installed_type = 0;
		static char rule_search[64] = "";
		static char target_search[64] = "";
		static char installed_search[96] = "";
		static std::vector<int> filtered_models;
		static bool initial_scan_done = false;

		if (!initial_scan_done)
		{
			initial_scan_done = true;
			if (!model_changer::g_models_scanned)
				model_changer::scan_installed_models();
		}

		if (rules.empty())
			selected_rule = -1;
		else
			selected_rule = std::clamp(selected_rule < 0 ? 0 : selected_rule, 0, static_cast<int>(rules.size()) - 1);

		// Compact global controls and diagnostics.
		if (ImGui::BeginChild("##model_status", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 2.65f), ImGuiChildFlags_Borders))
		{
			ImGui::Checkbox("Enable replacements", &model_changer::g_enabled);
			ImGui::SameLine();
			ImGui::Checkbox("Custom weapon sounds", &model_changer::g_enable_custom_sounds);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Redirect matching local weapon sounds to csgo/sound/custom/.");

			const auto active_color = ImVec4(0.35f, 0.95f, 0.45f, 1.0f);
			const auto failed_color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
			ImGui::TextColored(model_changer::g_hook_active ? active_color : failed_color,
				"FindMDL: %s", model_changer::g_hook_active ? "Active" : "Unavailable");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", model_changer::g_hook_status);
			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();
			ImGui::TextColored(model_changer::g_svpure_bypassed ? active_color : failed_color,
				"Custom files: %s", model_changer::g_svpure_bypassed ? "Allowed" : "Unavailable");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", model_changer::g_svpure_status);

			char refresh_label[96];
			snprintf(refresh_label, sizeof(refresh_label), "%s (%d)",
				model_changer::g_models_scanned ? "Refresh model list" : "Scan installed models",
				static_cast<int>(model_changer::g_installed_models.size()));
			const float refresh_width = ImGui::CalcTextSize(refresh_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - refresh_width);
			if (ImGui::Button(refresh_label))
				model_changer::scan_installed_models();
			if (ImGui::IsItemHovered() && !model_changer::g_models_root.empty())
				ImGui::SetTooltip("Scanned folder:\n%s", model_changer::g_models_root.c_str());
		}
		ImGui::EndChild();

		const float footer_height = ImGui::GetFrameHeightWithSpacing() * 2.65f;
		if (ImGui::BeginChild("##model_workspace", ImVec2(0, -footer_height)))
		{
			const float available_width = ImGui::GetContentRegionAvail().x;
			const float list_width = std::clamp(available_width * 0.36f, 285.0f, 340.0f);

			if (ImGui::BeginChild("##model_rules", ImVec2(list_width, 0), ImGuiChildFlags_Borders))
			{
				ImGui::Text("Rules (%d)", static_cast<int>(rules.size()));
				ImGui::SameLine();
				ImGui::TextDisabled("top match wins");
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::InputTextWithHint("##rule_search", "Filter rules...", rule_search, sizeof(rule_search));

				const float controls_height = ImGui::GetFrameHeightWithSpacing() * 2.15f;
				if (ImGui::BeginChild("##rule_rows", ImVec2(0, -controls_height)))
				{
					int visible_rules = 0;
					for (int i = 0; i < static_cast<int>(rules.size()); ++i)
					{
						auto& rule = rules[i];
						if (!contains_ci(rule.original, rule_search) && !contains_ci(rule.replacement, rule_search))
							continue;

						++visible_rules;
						ImGui::PushID(i);
						ImGui::Checkbox("##enabled", &rule.enabled);
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip(rule.enabled ? "Disable this rule" : "Enable this rule");
						ImGui::SameLine();

						char row_label[512];
						snprintf(row_label, sizeof(row_label), "%02d  %s\n     -> %s", i + 1,
							rule.original[0] ? model_basename(rule.original) : "Choose target",
							rule.replacement[0] ? model_basename(rule.replacement) : "Choose replacement");
						const bool incomplete = rule.original[0] == '\0' || rule.replacement[0] == '\0';
						const ImVec4 row_color = !rule.enabled ? ImVec4(0.55f, 0.55f, 0.55f, 1.0f)
							: incomplete ? ImVec4(1.0f, 0.72f, 0.25f, 1.0f)
							: rule.precached_index > 0 ? ImVec4(0.45f, 0.95f, 0.5f, 1.0f)
							: ImGui::GetStyleColorVec4(ImGuiCol_Text);
						ImGui::PushStyleColor(ImGuiCol_Text, row_color);
						if (ImGui::Selectable(row_label, selected_rule == i, 0,
							ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2.1f)))
							selected_rule = i;
						ImGui::PopStyleColor();
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::Text("Match: %s", rule.original[0] ? rule.original : "(not set)");
							ImGui::Text("Use:   %s", rule.replacement[0] ? rule.replacement : "(not set)");
							ImGui::TextDisabled(rule.precached_index > 0 ? "Applied (index %d)" : "Not applied to the current map", rule.precached_index);
							ImGui::EndTooltip();
						}
						ImGui::PopID();
					}
					if (visible_rules == 0)
						ImGui::TextDisabled(rules.empty() ? "No rules yet." : "No rules match this filter.");
				}
				ImGui::EndChild();

				const float gap = ImGui::GetStyle().ItemSpacing.x;
				const float third = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
				if (ImGui::Button("+ New", ImVec2(third, 0)))
				{
					rules.emplace_back();
					selected_rule = static_cast<int>(rules.size()) - 1;
				}
				ImGui::SameLine();
				ImGui::BeginDisabled(selected_rule < 0);
				if (ImGui::Button("Duplicate", ImVec2(third, 0)))
				{
					auto copy = rules[selected_rule];
					invalidate_model_rule(copy);
					rules.insert(rules.begin() + selected_rule + 1, copy);
					++selected_rule;
				}
				ImGui::SameLine();
				if (ImGui::Button("Remove", ImVec2(third, 0)))
				{
					rules.erase(rules.begin() + selected_rule);
					selected_rule = rules.empty() ? -1 : (std::min)(selected_rule, static_cast<int>(rules.size()) - 1);
				}
				ImGui::EndDisabled();

				const float half = (ImGui::GetContentRegionAvail().x - gap) / 2.0f;
				ImGui::BeginDisabled(selected_rule <= 0);
				if (ImGui::Button("Move up", ImVec2(half, 0)))
				{
					std::swap(rules[selected_rule], rules[selected_rule - 1]);
					--selected_rule;
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(selected_rule < 0 || selected_rule >= static_cast<int>(rules.size()) - 1);
				if (ImGui::Button("Move down", ImVec2(half, 0)))
				{
					std::swap(rules[selected_rule], rules[selected_rule + 1]);
					++selected_rule;
				}
				ImGui::EndDisabled();
			}
			ImGui::EndChild();

			ImGui::SameLine();
			if (ImGui::BeginChild("##model_editor", ImVec2(0, 0), ImGuiChildFlags_Borders))
			{
				if (selected_rule < 0)
				{
					ImGui::Text("Create a rule to get started");
					ImGui::TextWrapped("A rule matches an original game model and redirects it to one of the loose .mdl files installed under the game folder.");
					if (ImGui::Button("Create first rule"))
					{
						rules.emplace_back();
						selected_rule = 0;
					}
				}
				else
				{
					auto& rule = rules[selected_rule];
					ImGui::Checkbox("Rule enabled", &rule.enabled);
					ImGui::SameLine();
					if (!rule.enabled)
						ImGui::TextDisabled("Disabled");
					else if (rule.original[0] == '\0' || rule.replacement[0] == '\0')
						ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "Incomplete");
					else if (rule.precached_index > 0)
						ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "Applied (model index %d)", rule.precached_index);
					else
						ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Ready to apply");
					ImGui::Separator();

					ImGui::Text("1. Choose the original model");
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.42f);
					ImGui::Combo("##target_category", &target_category, model_target_categories, IM_ARRAYSIZE(model_target_categories));
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Filter the target presets by model type.");
					ImGui::SameLine();

					const auto selected_preset = find_target_preset(rule.original);
					const char* target_preview = selected_preset ? selected_preset->label
						: (rule.original[0] ? "Custom match" : "Choose a target...");
					ImGui::SetNextItemWidth(-FLT_MIN);
					if (ImGui::BeginCombo("##target_preset", target_preview))
					{
						ImGui::SetNextItemWidth(-FLT_MIN);
						ImGui::InputTextWithHint("##target_filter", "Search presets...", target_search, sizeof(target_search));
						ImGui::Separator();
						int shown = 0;
						for (const auto& preset : model_target_presets)
						{
							if ((target_category != 0 && preset.category != target_category)
								|| (!contains_ci(preset.label, target_search) && !contains_ci(preset.match, target_search)))
								continue;
							++shown;
							const bool selected = std::strcmp(rule.original, preset.match) == 0;
							if (ImGui::Selectable(preset.label, selected))
							{
								set_model_rule_text(rule.original, preset.match, rule);
								installed_type = suggested_model_type(preset.match);
							}
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("Matches paths containing:\n%s", preset.match);
							if (selected)
								ImGui::SetItemDefaultFocus();
						}
						if (shown == 0)
							ImGui::TextDisabled("No presets match the filter.");
						ImGui::EndCombo();
					}

					ImGui::SetNextItemWidth(-FLT_MIN);
					if (ImGui::InputTextWithHint("##original_path", "Match substring (advanced)", rule.original, sizeof(rule.original)))
						invalidate_model_rule(rule);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("The first enabled rule whose substring occurs in the requested model path wins.");

					ImGui::Separator();
					ImGui::Text("2. Choose the replacement model");
					static const char* const installed_types[] = { "All models", "Weapons", "Players", "Arms / gloves" };
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.34f);
					ImGui::Combo("##installed_type", &installed_type, installed_types, IM_ARRAYSIZE(installed_types));
					ImGui::SameLine();
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::InputTextWithHint("##installed_search", "Search installed models...", installed_search, sizeof(installed_search));

					filtered_models.clear();
					for (int i = 0; i < static_cast<int>(model_changer::g_installed_models.size()); ++i)
					{
						const auto& model = model_changer::g_installed_models[i];
						if (matches_model_type(model, installed_type) && contains_ci(model, installed_search))
							filtered_models.push_back(i);
					}

					const float model_list_height = ImGui::GetTextLineHeightWithSpacing() * 5.4f;
					if (ImGui::BeginListBox("##installed_models", ImVec2(-FLT_MIN, model_list_height)))
					{
						ImGuiListClipper clipper;
						clipper.Begin(static_cast<int>(filtered_models.size()));
						while (clipper.Step())
						{
							for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
							{
								const auto& model = model_changer::g_installed_models[filtered_models[row]];
								const bool selected = equals_ci(model, rule.replacement);
								if (ImGui::Selectable(model.c_str(), selected))
									set_model_rule_text(rule.replacement, model.c_str(), rule);
								if (ImGui::IsItemHovered())
									ImGui::SetTooltip("Click to use:\n%s", model.c_str());
							}
						}
						if (filtered_models.empty())
							ImGui::TextDisabled(model_changer::g_models_scanned
								? "No installed models match these filters."
								: "Scan installed models to browse replacements.");
						ImGui::EndListBox();
					}
					ImGui::TextDisabled("%d shown / %d installed", static_cast<int>(filtered_models.size()),
						static_cast<int>(model_changer::g_installed_models.size()));

					ImGui::SetNextItemWidth(-FLT_MIN);
					if (ImGui::InputTextWithHint("##replacement_path", "Replacement path (models/.../*.mdl)",
						rule.replacement, sizeof(rule.replacement)))
						invalidate_model_rule(rule);

					bool installed = false;
					for (const auto& model : model_changer::g_installed_models)
					{
						if (equals_ci(model, rule.replacement))
						{
							installed = true;
							break;
						}
					}

					int shadowing_rule = -1;
					if (rule.original[0])
					{
						for (int i = 0; i < selected_rule; ++i)
						{
							if (rules[i].enabled && rules[i].original[0]
								&& std::strstr(rule.original, rules[i].original))
							{
								shadowing_rule = i;
								break;
							}
						}
					}

					if (rule.original[0] == '\0' || rule.replacement[0] == '\0')
						ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "Choose both paths before applying.");
					else if (!has_mdl_extension(rule.replacement))
						ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Replacement must end in .mdl.");
					else if (model_changer::g_models_scanned && !installed)
						ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "This path was not found in the loose model scan.");
					else
						ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "Rule paths look ready.");

					if (shadowing_rule >= 0)
						ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
							"Rule %d has a broader earlier match; move this rule above it.", shadowing_rule + 1);
				}
			}
			ImGui::EndChild();
		}
		ImGui::EndChild();

		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const float total_width = ImGui::GetContentRegionAvail().x;
		const float apply_width = total_width * 0.5f;
		const float side_width = (total_width - apply_width - gap * 2.0f) / 2.0f;
		if (ImGui::Button("Apply to current map", ImVec2(apply_width, 0)))
		{
			model_changer::precache_models();
			if (model_changer::g_last_operation_status != model_changer::operation_status::error && g_engine)
				g_engine->ClientCmd_Unrestricted("record x;stop");
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Precache enabled models and refresh the current view models.");
		ImGui::SameLine();
		if (ImGui::Button("Save rules", ImVec2(side_width, 0)))
			model_changer::save_config();
		ImGui::SameLine();
		if (ImGui::Button("Load rules", ImVec2(side_width, 0)))
		{
			model_changer::load_config();
			selected_rule = model_changer::g_replacements.empty() ? -1
				: std::clamp(selected_rule < 0 ? 0 : selected_rule, 0,
					static_cast<int>(model_changer::g_replacements.size()) - 1);
		}

		ImGui::PushStyleColor(ImGuiCol_Text, operation_color(model_changer::g_last_operation_status));
		ImGui::TextWrapped("%s", model_changer::g_last_operation_message.c_str());
		ImGui::PopStyleColor();
	}
}

void draw_gui()
{
	ImGui::SetNextWindowSize(ImVec2(900, 620));
	if(ImGui::Begin("nSkinz", nullptr,
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings))
	{

	if (ImGui::BeginTabBar("##MainTabs"))
	{

	// ========== SKIN CHANGER TAB ==========
	if (ImGui::BeginTabItem("Skin Changer"))
	{

		auto& entries = g_config.get_items();

		static auto selected_id = 0;

		ImGui::Columns(2, nullptr, false);

		// Config selection
		{
			ImGui::PushItemWidth(-1);

			char element_name[64];

			ImGui::ListBox("##config", &selected_id, [&element_name, &entries](int idx)
			{
				sprintf_s(element_name, "%s (%s)", entries.at(idx).name, game_data::weapon_names.at(entries.at(idx).definition_vector_index).name);
				return element_name;
			}, (int)entries.size(), 11);

			const auto button_size = ImVec2(ImGui::GetColumnWidth() / 2 - 12.5f, 31);

			if(ImGui::Button("Add", button_size))
			{
				entries.push_back(item_setting());
				selected_id = entries.size() - 1;
			}
			ImGui::SameLine();

			if(ImGui::Button("Remove", button_size) && entries.size() > 1)
				entries.erase(entries.begin() + selected_id);

			ImGui::PopItemWidth();
		}

		ImGui::NextColumn();

		selected_id = selected_id < int(entries.size()) ? selected_id : entries.size() - 1;

		auto& selected_entry = entries[selected_id];

		{
			// Name
			ImGui::InputText("Name", selected_entry.name, 32);

			// Item to change skins for
			ImGui::Combo("Item", &selected_entry.definition_vector_index, [](void* data, int idx) -> const char*
			{
				return game_data::weapon_names[idx].name;
			}, nullptr, (int)game_data::weapon_names.size(), 5);

			// Enabled
			ImGui::Checkbox("Enabled", &selected_entry.enabled);

			// Pattern Seed
			ImGui::InputInt("Seed", &selected_entry.seed);

			// Custom StatTrak number
			ImGui::InputInt("StatTrak", &selected_entry.stat_trak);

			// Wear Float
			ImGui::SliderFloat("Wear", &selected_entry.wear, FLT_MIN, 1.f, "%.10f", ImGuiSliderFlags_Logarithmic);

			// Paint kit with search
			static char skin_search[64] = "";
			static char glove_search[64] = "";
			static std::vector<int> filtered;

			if(selected_entry.definition_index != GLOVE_T_SIDE)
			{
				FilteredCombo("Paint Kit", &selected_entry.paint_kit_vector_index, skin_search, sizeof(skin_search),
					game_data::skin_kits, filtered);
			}
			else
			{
				FilteredCombo("Paint Kit", &selected_entry.paint_kit_vector_index, glove_search, sizeof(glove_search),
					game_data::glove_kits, filtered);
			}

			// Quality
			ImGui::Combo("Quality", &selected_entry.entity_quality_vector_index, [](void* data, int idx) -> const char*
			{
				return game_data::quality_names[idx].name;
			}, nullptr, (int)game_data::quality_names.size(), 5);

			// Yes we do it twice to decide knifes
			selected_entry.update<sync_type::KEY_TO_VALUE>();

			// Item defindex override
			if(selected_entry.definition_index == WEAPON_KNIFE)
			{
				ImGui::Combo("Knife", &selected_entry.definition_override_vector_index, [](void* data, int idx) -> const char*
				{
					return game_data::knife_names.at(idx).name;
				}, nullptr, (int)game_data::knife_names.size(), 5);
			}
			else if(selected_entry.definition_index == GLOVE_T_SIDE)
			{
				ImGui::Combo("Glove", &selected_entry.definition_override_vector_index, [](void* data, int idx) -> const char*
				{
					return game_data::glove_names.at(idx).name;
				}, nullptr, (int)game_data::glove_names.size(), 5);
			}
			else
			{
				// We don't want to override weapons other than knives or gloves
				static auto unused_value = 0;
				selected_entry.definition_override_vector_index = 0;
				ImGui::Combo("Unavailable", &unused_value, "For knives or gloves\0");
			}

			selected_entry.update<sync_type::KEY_TO_VALUE>();

			// Custom Name tag
			ImGui::InputText("Name Tag", selected_entry.custom_name, 32);
		}

		ImGui::NextColumn();

		ImGui::Columns(1, nullptr, false);

		ImGui::Separator();

		{
			ImGui::Columns(2, nullptr, false);

			ImGui::PushID("sticker");

			static auto selected_sticker_slot = 0;

			auto& selected_sticker = selected_entry.stickers[selected_sticker_slot];

			ImGui::PushItemWidth(-1);

			char element_name[64];

			ImGui::ListBox("", &selected_sticker_slot, [&selected_entry, &element_name](int idx)
			{
				auto kit_vector_index = selected_entry.stickers[idx].kit_vector_index;
				sprintf_s(element_name, "#%d (%s)", idx + 1, game_data::sticker_kits.at(kit_vector_index).name.c_str());
				return element_name;
			}, 5, 5);
			ImGui::PopItemWidth();

			ImGui::NextColumn();

			static char sticker_search[64] = "";
			static std::vector<int> sticker_filtered;
			FilteredCombo("Sticker Kit", &selected_sticker.kit_vector_index, sticker_search, sizeof(sticker_search),
				game_data::sticker_kits, sticker_filtered);

			ImGui::SliderFloat("Wear", &selected_sticker.wear, FLT_MIN, 1.f, "%.10f", ImGuiSliderFlags_Logarithmic);

			ImGui::SliderFloat("Scale", &selected_sticker.scale, 0.1f, 5.f, "%.3f");

			ImGui::SliderFloat("Rotation", &selected_sticker.rotation, 0.f, 360.f);

			ImGui::NextColumn();

			ImGui::PopID();
		}

		ImGui::Columns(1, nullptr, false);

		ImGui::Separator();

		ImGui::Columns(3, nullptr, false);

		ImGui::PushItemWidth(-1);

		// Lower buttons for modifying items and saving
		{
			const auto button_size = ImVec2(ImGui::GetColumnWidth() - 1, 20);

			if(ImGui::Button("Update", button_size))
				//(*g_client_state)->ForceFullUpdate();
				g_engine->ClientCmd_Unrestricted("record x;stop"); //this will be changed at a later date.		


			ImGui::NextColumn();

			if(ImGui::Button("Save", button_size))
				g_config.save();
			ImGui::NextColumn();

			if(ImGui::Button("Load", button_size))
				g_config.load();
			ImGui::NextColumn();
		}

		ImGui::PopItemWidth();
		ImGui::Columns(1);

	ImGui::EndTabItem();
	} // End Skin Changer tab

	// ========== MODEL CHANGER TAB ==========
	if (ImGui::BeginTabItem("Model Changer"))
	{
		draw_model_changer_tab();
		ImGui::EndTabItem();
	} // End Model Changer tab

	// ========== MISC TAB ==========
	if (ImGui::BeginTabItem("Misc"))
	{
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Hitmarker Settings:");
		ImGui::Checkbox("Enable Screen Hitmarker", &g_config.misc.hitmarker);
		ImGui::Checkbox("Enable Hit Sound", &g_config.misc.hitsound);
		
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Save Config##misc", ImVec2(ImGui::GetContentRegionAvail().x / 2 - 4, 30)))
			g_config.save();
		ImGui::SameLine();
		if (ImGui::Button("Load Config##misc", ImVec2(ImGui::GetContentRegionAvail().x, 30)))
			g_config.load();

		ImGui::EndTabItem();
	}

	ImGui::EndTabBar();
	} // End tab bar

		ImGui::Separator();
		ImGui::Text("nSkinz for CSGO Legacy - modified by HighDel4y");
		ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("build : 23/07/26").x - 20);
		ImGui::Text("build : 23/07/26");

		ImGui::End();
	}
}
