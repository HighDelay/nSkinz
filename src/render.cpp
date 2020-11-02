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
#include "render.hpp"
#include "SDK.hpp"
#include "Utilities/platform.hpp"

// Renderer for windows. Maybe sometime i'll make a linux one

#include <d3d9.h>
#include <intrin.h>

#include <imgui.h>
#include <imgui_impl_dx9.h>

// Implement this somewhere
extern void draw_gui();

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace render
{
	static vmt_smart_hook* s_d3d_hook;

	static bool s_ready = false;
	static bool s_active = false;

	struct d3d9_state
	{
		D3DMATRIX transform_world[4];
		D3DMATRIX transform_view;
		D3DMATRIX transform_projection;
		D3DMATRIX transform_texture[8];
		DWORD srgbwrite;
		DWORD colorwrite;
		DWORD fvf;

		IDirect3DVertexBuffer9* stream_data = nullptr;
		UINT stream_offset;
		UINT stream_stride;

		IDirect3DBaseTexture9* texture = nullptr;
		IDirect3DSurface9* target = nullptr;
		IDirect3DIndexBuffer9* indices = nullptr;
		IDirect3DVertexDeclaration9* vertex_declaration = nullptr;
		IDirect3DPixelShader9* pixel_shader = nullptr;
		IDirect3DVertexShader9* vertex_shader = nullptr;

		~d3d9_state()
		{
			if (stream_data)
				stream_data->Release();
			if (texture)
				texture->Release();
			if (target)
				target->Release();
			if (indices)
				indices->Release();
			if (vertex_declaration)
				vertex_declaration->Release();
			if (pixel_shader)
				pixel_shader->Release();
			if (vertex_shader)
				vertex_shader->Release();
		}

		auto save(IDirect3DDevice9* device) -> void
		{
			device->GetTransform(D3DTS_WORLD, &transform_world[0]);
			device->GetTransform(D3DTS_WORLD1, &transform_world[1]);
			device->GetTransform(D3DTS_WORLD2, &transform_world[2]);
			device->GetTransform(D3DTS_WORLD3, &transform_world[3]);
			device->GetTransform(D3DTS_VIEW, &transform_view);
			device->GetTransform(D3DTS_PROJECTION, &transform_projection);
			for (auto i = 0; i < 8; ++i)
				device->GetTransform(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0 + i), &transform_texture[i]);

			device->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgbwrite);
			device->GetRenderState(D3DRS_SRGBWRITEENABLE, &colorwrite);
			device->GetFVF(&fvf);

			device->GetStreamSource(0, &stream_data, &stream_offset, &stream_stride);

			device->GetTexture(0, &texture);
			device->GetRenderTarget(0, &target);
			device->GetIndices(&indices);
			device->GetVertexDeclaration(&vertex_declaration);
			device->GetPixelShader(&pixel_shader);
			device->GetVertexShader(&vertex_shader);
		}

		auto load(IDirect3DDevice9* device) const -> void
		{
			device->SetTransform(D3DTS_WORLD, &transform_world[0]);
			device->SetTransform(D3DTS_WORLD1, &transform_world[1]);
			device->SetTransform(D3DTS_WORLD2, &transform_world[2]);
			device->SetTransform(D3DTS_WORLD3, &transform_world[3]);
			device->SetTransform(D3DTS_VIEW, &transform_view);
			device->SetTransform(D3DTS_PROJECTION, &transform_projection);
			for (auto i = 0; i < 8; ++i)
				device->SetTransform(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0 + i), &transform_texture[i]);

			device->SetRenderState(D3DRS_SRGBWRITEENABLE, srgbwrite);
			device->SetRenderState(D3DRS_SRGBWRITEENABLE, colorwrite);
			device->SetFVF(fvf);

			device->SetStreamSource(0, stream_data, stream_offset, stream_stride);

			device->SetTexture(0, texture);
			device->SetRenderTarget(0, target);
			device->SetIndices(indices);
			device->SetVertexDeclaration(vertex_declaration);
			device->SetPixelShader(pixel_shader);
			device->SetVertexShader(vertex_shader);
		}
	};

	struct Reset
	{
		static auto COM_DECLSPEC_NOTHROW STDMETHODCALLTYPE hooked(IDirect3DDevice9* thisptr, D3DPRESENT_PARAMETERS* params) -> HRESULT
		{
			if(!s_ready)
				return m_original(thisptr, params);

			ImGui_ImplDX9_InvalidateDeviceObjects();

			const auto result = m_original(thisptr, params);

			ImGui_ImplDX9_CreateDeviceObjects();

			return result;
		}

		static decltype(&hooked) m_original;
	};

	decltype(Reset::m_original) Reset::m_original;

	struct EndScene
	{
		static auto COM_DECLSPEC_NOTHROW STDMETHODCALLTYPE hooked(IDirect3DDevice9* thisptr) -> HRESULT
		{
			static void* ret_addr;
			if (!ret_addr)
				ret_addr = _ReturnAddress();

			if(_ReturnAddress() == ret_addr)
			{
				// Save the state to prevent messing up stuff
				// Doesn't work yet
				//d3d9_state state;
				//state.save(thisptr);

				IDirect3DStateBlock9* state;
				thisptr->CreateStateBlock(D3DSBT_PIXELSTATE, &state);

				//fix drawing without cl_showfps
				thisptr->SetRenderState(D3DRS_COLORWRITEENABLE, 0xFFFFFFFF);

				//thisptr->SetRenderState(D3DRS_SRGBWRITEENABLE, 0);

				static void* saved_hwnd;

				if (s_active)
				{
					if (!saved_hwnd)
					{
						// We could do this with cvars, but too much work to implement a whole cvar interface just for this.
						//g_engine->ClientCmd_Unrestricted("cl_mouseenable 0");
						//g_input_system->EnableInput(false);
						//mouse_enabled = false;
						std::swap(saved_hwnd, g_input_system->get_window());
					}

					ImGui::GetIO().MouseDrawCursor = true;

					ImGui_ImplDX9_NewFrame();

					draw_gui();

					ImGui::Render();
				}
				else
				{
					if (saved_hwnd)
					{
						//g_engine->ClientCmd_Unrestricted("cl_mouseenable 1");
						g_input_system->EnableInput(true); //reenabling this fixed the movement.
						//mouse_enabled = true;
						std::swap(saved_hwnd, g_input_system->get_window());
					}
				}

				//state.load(thisptr);
				state->Apply();
				state->Release();
			}

			return m_original(thisptr);
		}

		static decltype(&hooked) m_original;
	};

	decltype(EndScene::m_original) EndScene::m_original;

	static auto set_imgui_style() -> void
	{
			auto& style = ImGui::GetStyle();

			style.ChildWindowRounding = 0.f;
			style.GrabRounding = 0.f;
			style.WindowRounding = 0.f;
			style.ScrollbarSize = 12.0f;
			style.ScrollbarRounding = 0.f;
			style.FrameRounding = 0.f;
			style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	
			ImVec4* colors = ImGui::GetStyle().Colors;
			colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
			colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
			colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
			colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
			colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
			colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
			colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.22f, 0.54f);
			colors[ImGuiCol_FrameBgHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.40f);
			colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 0.67f);
			colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
			colors[ImGuiCol_TitleBgActive] = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
			colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
			colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
			colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
			colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
			colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
			colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
			colors[ImGuiCol_CheckMark] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
			colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
			colors[ImGuiCol_SliderGrabActive] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
			colors[ImGuiCol_Button] = ImVec4(0.44f, 0.44f, 0.44f, 0.40f);
			colors[ImGuiCol_ButtonHovered] = ImVec4(0.46f, 0.47f, 0.48f, 1.00f);
			colors[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
			colors[ImGuiCol_Header] = ImVec4(0.70f, 0.70f, 0.70f, 0.31f);
			colors[ImGuiCol_HeaderHovered] = ImVec4(0.70f, 0.70f, 0.70f, 0.80f);
			colors[ImGuiCol_HeaderActive] = ImVec4(0.48f, 0.50f, 0.52f, 1.00f);
			colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
			colors[ImGuiCol_SeparatorHovered] = ImVec4(0.72f, 0.72f, 0.72f, 0.78f);
			colors[ImGuiCol_SeparatorActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
			colors[ImGuiCol_ResizeGrip] = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
			colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
			colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);
			colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
			colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
			colors[ImGuiCol_PlotHistogram] = ImVec4(0.73f, 0.60f, 0.15f, 1.00f);
			colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
			colors[ImGuiCol_TextSelectedBg] = ImVec4(0.87f, 0.87f, 0.87f, 0.35f);
			colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

			auto& io = ImGui::GetIO();

			io.Fonts->AddFontDefault();
	}

	static auto s_hwnd = HWND(nullptr);
	static auto s_original_wnd_proc = WNDPROC(nullptr);

	static auto does_imgui_handle_message(const UINT message_type) -> bool
	{
		switch(message_type)
		{
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_MOUSEWHEEL:
		case WM_MOUSEMOVE:
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
		case WM_CHAR:
			return true;
		default:
			return false;
		}
	}

	static auto handle_input_message(const UINT message_type, const WPARAM w_param, const LPARAM l_param) -> bool
	{
		if(!s_ready)
			return false;

		if(message_type == WM_KEYUP && w_param == VK_INSERT)
			s_active = !s_active;

		if(s_active && does_imgui_handle_message(message_type))
		{
			ImGui_ImplWin32_WndProcHandler(s_hwnd, message_type, w_param, l_param);
			return true;
		}

		return false;
	}

	static auto CALLBACK wndproc_hook(const HWND hwnd, const UINT message, const WPARAM w_param, const LPARAM l_param) -> LRESULT
	{
		if(handle_input_message(message, w_param, l_param))
			return 0;
		
		return CallWindowProcW(s_original_wnd_proc, hwnd, message, w_param, l_param);
	};

	static auto swap_wndproc(const HWND hwnd, WNDPROC new_proc) -> WNDPROC
	{
		return reinterpret_cast<WNDPROC>(SetWindowLongW(hwnd, GWLP_WNDPROC, LONG_PTR(new_proc)));
	}

	auto initialize() -> void
	{
		if(s_ready)
		{
			assert(false);
			return;
		}

		s_hwnd = HWND(g_input_system->get_window());
		assert(s_hwnd);

		s_original_wnd_proc = swap_wndproc(s_hwnd, &wndproc_hook);
		assert(s_original_wnd_proc);

		const auto device_addr = platform::find_pattern("shaderapidx9.dll", "\xA1\x00\x00\x00\x00\x50\x8B\x08\xFF\x51\x0C", "x????xxxxxx");
		const auto device = **reinterpret_cast<IDirect3DDevice9***>(device_addr + 1);
		assert(device);

		s_d3d_hook = new vmt_smart_hook(device);
		s_d3d_hook->apply_hook<Reset>(16);
		s_d3d_hook->apply_hook<EndScene>(42);

		const auto result = ImGui_ImplDX9_Init(s_hwnd, device);
		assert(result);
		if(result)
			s_ready = true;

		set_imgui_style();
	}

	auto uninitialize() -> void
	{
		if(!s_ready)
			return;

		s_ready = false;

		swap_wndproc(s_hwnd, s_original_wnd_proc);
		delete s_d3d_hook;

		// In case our hooks are still running
		Sleep(100);
	}
}
