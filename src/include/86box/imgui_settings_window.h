#pragma once
#include <vector>
#include <tuple>
#include <string>

static std::vector<std::pair<std::string, std::string>> allfilefilter
{
#ifdef _WIN32
	{"All Files", "*.*"}
#else
    {"All Files", "*"}
#endif
};

struct FileOpenSaveRequest
{
	std::vector<std::pair<std::string, std::string>>& filters = allfilefilter;
	void (*filefunc3params)(uint8_t, char*, uint8_t) = nullptr;
	void (*filefunc2params)(uint8_t, char*) = nullptr;
	void (*filefunc2paramsalt)(char*, uint8_t) = nullptr;
	bool save = false;
	bool wp = false;
	uint8_t id = 0;
};

namespace ImGuiSettingsWindow {
	void Render();
	void InitSettings();
	extern bool showSettingsWindow;
}
#ifdef __APPLE__
bool FileOpenSaveMacOSModal(char* res, size_t n, std::vector<std::pair<std::string, std::string>>& filters = allfilefilter, bool save = false);
#endif
