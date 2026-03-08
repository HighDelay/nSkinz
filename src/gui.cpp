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
	ImGui::SetNextWindowSize(ImVec2(700, 500));
	if(ImGui::Begin("nSkinz", nullptr,
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings))
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

		ImGui::Text("nSkinz for CSGO Legacy - modified by HighDel4y");
		ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("build : 08/03/26").x - 20);
		ImGui::Text("build : 08/03/26");

		ImGui::End();
	}
}
