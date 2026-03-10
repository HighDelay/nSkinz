#include "hitmarker.hpp"
#include "SDK.hpp"
#include "config.hpp"
#include <Windows.h>
#pragma comment(lib, "winmm.lib")
#include <imgui.h>

namespace hitmarker
{
	static double g_flHurtTime = -1000.0;

	class player_hurt_listener : public sdk::IGameEventListener2
	{
	public:
		void start()
		{
			if (g_game_event_manager)
			{
				g_game_event_manager->AddListener(this, "player_hurt", false);
			}
		}

		void stop()
		{
			if (g_game_event_manager)
			{
				g_game_event_manager->RemoveListener(this);
			}
		}

		void FireGameEvent(sdk::IGameEvent* event) override
		{
			hitmarker::on_fire_event(event);
		}

		int GetEventDebugID() override
		{
			return 42;
		}
	};

	static player_hurt_listener g_listener;

	void initialize()
	{
		g_listener.start();
	}

	void on_fire_event(sdk::IGameEvent* event)
	{
		if (!event || strcmp(event->GetName(), "player_hurt") != 0)
			return;

		if (!g_config.misc.hitmarker && !g_config.misc.hitsound)
			return;

		int attacker = g_engine->GetPlayerForUserID(event->GetInt("attacker"));
		int userid = g_engine->GetPlayerForUserID(event->GetInt("userid"));

		if (attacker == g_engine->GetLocalPlayer() && userid != g_engine->GetLocalPlayer())
		{
			if (g_config.misc.hitsound)
			{
				// Play custom hitsound using winmm
				PlaySoundA("csgo\\sound\\hitsound.wav", NULL, SND_ASYNC | SND_NODEFAULT);
			}

			if (g_config.misc.hitmarker)
			{
				g_flHurtTime = ImGui::GetTime();
			}
		}
	}

	void on_paint()
	{
		if (!g_config.misc.hitmarker) return;

		double curtime = ImGui::GetTime();
		double time_diff = curtime - g_flHurtTime;

		if (time_diff > 0.0 && time_diff <= 0.25)
		{
			ImVec2 display_size = ImGui::GetIO().DisplaySize;
			float centerX = display_size.x / 2.0f;
			float centerY = display_size.y / 2.0f;
			float lineSize = 8.0f;
			
			// Fading alpha
			float alpha = 1.0f - (float)(time_diff / 0.25f);
			ImU32 color = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
			
			ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
			
			draw_list->AddLine(ImVec2(centerX - lineSize, centerY - lineSize), ImVec2(centerX - (lineSize / 4.0f), centerY - (lineSize / 4.0f)), color, 1.0f);
			draw_list->AddLine(ImVec2(centerX - lineSize, centerY + lineSize), ImVec2(centerX - (lineSize / 4.0f), centerY + (lineSize / 4.0f)), color, 1.0f);
			draw_list->AddLine(ImVec2(centerX + lineSize, centerY + lineSize), ImVec2(centerX + (lineSize / 4.0f), centerY + (lineSize / 4.0f)), color, 1.0f);
			draw_list->AddLine(ImVec2(centerX + lineSize, centerY - lineSize), ImVec2(centerX + (lineSize / 4.0f), centerY - (lineSize / 4.0f)), color, 1.0f);
		}
	}
}
