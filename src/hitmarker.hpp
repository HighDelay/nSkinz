#pragma once

namespace sdk {
	class IGameEvent;
}

namespace hitmarker
{
	void initialize();
	void on_fire_event(sdk::IGameEvent* event);
	void on_paint();
}
