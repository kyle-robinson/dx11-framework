#pragma once
#include "window/WindowContainer.h"
#include "utility/Timer.h"

class Application : public WindowContainer
{
public:
	bool Initialize(
		HINSTANCE hInstance,
		const std::string& windowTitle,
		const std::string& windowClass,
		int width,
		int height
	);
	bool ProcessMessages() noexcept;
	void Update();
	void Render();
private:
	float multiplier = 1.0f;
	SYSTEM_INFO siSysInfo;
	Timer timer;
};

