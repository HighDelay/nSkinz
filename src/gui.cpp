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

// Case-insensitive substring search
static bool contains_ci(const std::string& haystack, const char* needle)
{
	if (!needle || needle[0] == '\0') return true;
	std::string lower_haystack = haystack;
	std::string lower_needle = needle;
	std::transform(lower_haystack.begin(), lower_haystack.end(), lower_haystack.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	std::transform(lower_needle.begin(), lower_needle.end(), lower_needle.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return lower_haystack.find(lower_needle) != std::string::npos;
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

void draw_gui()
{
	ImGui::SetNextWindowSize(ImVec2(750, 550));
	if(ImGui::Begin("nSkinz", nullptr,
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_AlwaysAutoResize |
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
		ImGui::Spacing();

		ImGui::Checkbox("Enable Model Changer", &model_changer::g_enabled);
		ImGui::Checkbox("Enable Custom Sound Replacements (Redirects to csgo/sound/custom/)", &model_changer::g_enable_custom_sounds);
		ImGui::Spacing();

		if (ImGui::Button("Scan Installed Models"))
			model_changer::scan_installed_models();
		if (model_changer::g_models_scanned)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "(%d models)", (int)model_changer::g_installed_models.size());
		}

		ImGui::Text("Hook Status:");
		ImGui::SameLine();
		if (model_changer::g_hook_active)
			ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", model_changer::g_hook_status);
		else
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", model_changer::g_hook_status);

		ImGui::Text("sv_pure Bypass:");
		ImGui::SameLine();
		if (model_changer::g_svpure_bypassed)
			ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", model_changer::g_svpure_status);
		else
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", model_changer::g_svpure_status);

		ImGui::Spacing();

		// --- Predefined model categories for quick selection ---
		static const char* categories[] = {
			"-- Select Category --",
			"Knives (CT)",
			"Knives (T)",
			"Pistols",
			"Rifles",
			"SMGs",
			"Shotguns",
			"Machine Guns",
			"Player Models (CT)",
			"Player Models (T)"
		};

		// Predefined original model substrings per category
		static const std::vector<std::vector<const char*>> predefined_models = {
			{}, // placeholder for "Select Category"
			// Knives CT
			{ "knife_default_ct.mdl", "knife_bayonet.mdl", "knife_flip.mdl", "knife_gut.mdl",
			  "knife_karambit.mdl", "knife_m9_bayonet.mdl", "knife_butterfly.mdl",
			  "knife_falchion_advanced.mdl", "knife_push.mdl", "knife_survival_bowie.mdl",
			  "knife_tactical.mdl" },
			// Knives T
			{ "knife_default_t.mdl", "knife_bayonet.mdl", "knife_flip.mdl", "knife_gut.mdl",
			  "knife_karambit.mdl", "knife_m9_bayonet.mdl", "knife_butterfly.mdl" },
			// Pistols
			{ "v_pist_glock18.mdl", "v_pist_hkp2000.mdl", "v_pist_p250.mdl",
			  "v_pist_fiveseven.mdl", "v_pist_tec9.mdl", "v_pist_deagle.mdl",
			  "v_pist_elite.mdl", "v_pist_revolver.mdl", "v_pist_cz_75.mdl" },
			// Rifles
			{ "v_rif_ak47.mdl", "v_rif_m4a1.mdl", "v_rif_m4a1_s.mdl",
			  "v_rif_aug.mdl", "v_rif_sg556.mdl", "v_rif_famas.mdl",
			  "v_rif_galilar.mdl", "v_snip_awp.mdl", "v_snip_ssg08.mdl",
			  "v_snip_scar20.mdl", "v_snip_g3sg1.mdl" },
			// SMGs
			{ "v_smg_mp9.mdl", "v_smg_mac10.mdl", "v_smg_mp7.mdl",
			  "v_smg_ump45.mdl", "v_smg_p90.mdl", "v_smg_bizon.mdl",
			  "v_smg_mp5sd.mdl" },
			// Shotguns
			{ "v_shot_nova.mdl", "v_shot_xm1014.mdl", "v_shot_sawedoff.mdl",
			  "v_shot_mag7.mdl" },
			// Machine Guns
			{ "v_mach_m249.mdl", "v_mach_negev.mdl" },
			// Player Models CT
			{ "ctm_fbi.mdl", "ctm_gign.mdl", "ctm_sas.mdl", "ctm_st6.mdl",
			  "ctm_swat.mdl", "ctm_idf.mdl" },
			// Player Models T
			{ "tm_anarchist.mdl", "tm_balkan.mdl", "tm_leet.mdl",
			  "tm_phoenix.mdl", "tm_pirate.mdl", "tm_professional.mdl",
			  "tm_separatist.mdl" }
		};

		// --- Model replacement list ---
		auto& rules = model_changer::g_replacements;
		static int selected_rule = 0;

		ImGui::Text("Model Replacement Rules: (%d)", (int)rules.size());
		ImGui::Spacing();

		// Add/Remove buttons
		// Add/Remove buttons
		if (ImGui::Button("Add Rule"))
		{
			rules.push_back(model_replacement());
			selected_rule = (int)rules.size() - 1;
		}

		ImGui::Spacing();
		ImGui::Separator();

		// Display each rule
		for (int i = 0; i < (int)rules.size(); i++)
		{
			auto& rule = rules[i];
			ImGui::PushID(i);

			char header_label[128];
			if (rule.original[0] != '\0')
				sprintf_s(header_label, "Rule #%d: %s -> %s###rule%d", i + 1, rule.original, rule.replacement[0] ? rule.replacement : "(not set)", i);
			else
				sprintf_s(header_label, "Rule #%d: (empty)###rule%d", i + 1, i);

			if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Checkbox("Enabled", &rule.enabled);
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
				if (ImGui::Button("Delete Rule", ImVec2(100, 20)))
				{
					rules.erase(rules.begin() + i);
					ImGui::PopID();
					break;
				}
				ImGui::Spacing();

				// ---- ORIGINAL MODEL ----
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "What model to replace:");
				static int cat_index = 0;
				ImGui::Combo("Category", &cat_index, categories, IM_ARRAYSIZE(categories));
				if (cat_index > 0 && cat_index < (int)predefined_models.size())
				{
					const auto& models = predefined_models[cat_index];
					static int model_index = 0;
					if (model_index >= (int)models.size()) model_index = 0;
					ImGui::Combo("Pick Original", &model_index, [](void* data, int idx) -> const char* {
						return (*reinterpret_cast<const std::vector<const char*>*>(data))[idx];
					}, (void*)&models, (int)models.size(), 8);
					if (ImGui::Button("Set as Original"))
						if (model_index >= 0 && model_index < (int)models.size())
							strcpy_s(rule.original, models[model_index]);
				}
				ImGui::InputText("Original##o", rule.original, sizeof(rule.original));
				ImGui::Spacing();

				// ---- REPLACEMENT MODEL ----
				ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Replace with:");
				if (model_changer::g_models_scanned && !model_changer::g_installed_models.empty())
				{
					static char mdl_search[64] = "";
					ImGui::InputText("Search Models##s", mdl_search, sizeof(mdl_search));
					static std::vector<int> filt;
					filt.clear();
					for (int j = 0; j < (int)model_changer::g_installed_models.size(); j++)
						if (contains_ci(model_changer::g_installed_models[j], mdl_search))
							filt.push_back(j);
					if (!filt.empty())
					{
						ImGui::Text("%d matching models:", (int)filt.size());
						static int pick = 0;
						if (pick >= (int)filt.size()) pick = 0;
						struct LD { const std::vector<int>* f; const std::vector<std::string>* m; };
						LD ld = { &filt, &model_changer::g_installed_models };
						ImGui::ListBox("##pick", &pick, [](void* d, int idx) -> const char* {
							auto* p = reinterpret_cast<LD*>(d);
							return p->m->at(p->f->at(idx)).c_str();
						}, &ld, (int)filt.size(), 5);
						if (ImGui::Button("Use as Replacement"))
							if (pick >= 0 && pick < (int)filt.size())
								strcpy_s(rule.replacement, model_changer::g_installed_models[filt[pick]].c_str());
					}
					else
						ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No models match search.");
				}
				else
					ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "Click 'Scan Installed Models' to browse.");
				ImGui::InputText("Replacement##r", rule.replacement, sizeof(rule.replacement));
			}

			ImGui::PopID();
			ImGui::Spacing();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Apply button
		if (ImGui::Button("Precache & Apply", ImVec2(ImGui::GetContentRegionAvail().x, 30)))
		{
			model_changer::precache_models();
			g_engine->ClientCmd_Unrestricted("record x;stop");
		}

		if (ImGui::Button("Save Config", ImVec2(ImGui::GetContentRegionAvail().x / 2 - 4, 30)))
			model_changer::save_config();
		ImGui::SameLine();
		if (ImGui::Button("Load Config", ImVec2(ImGui::GetContentRegionAvail().x, 30)))
			model_changer::load_config();
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
			"Models apply automatically. Click 'Precache & Apply' after adding new rules.");

		ImGui::Spacing();

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
		ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("build : 10/03/26").x - 20);
		ImGui::Text("build : 10/03/26");

		ImGui::End();
	}
}
