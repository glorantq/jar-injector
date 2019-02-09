#include "InjectorDLL.h"

#include <iostream>
#include <filesystem>
#include <fstream>

#include "nlohmann\json.hpp"

struct HWND_Data {
	unsigned long processID;
	HWND windowHandle;
};

namespace filesystem = std::experimental::filesystem;

BOOL CALLBACK EnumWindowsCallback(HWND handle, LPARAM lParam) {
	HWND_Data& hwndData = *(reinterpret_cast<HWND_Data*>(lParam));
	unsigned long processId = 0;

	GetWindowThreadProcessId(handle, &processId);

	if (hwndData.processID != processId || !(GetWindow(handle, GW_OWNER) == reinterpret_cast<HWND>(0) && IsWindowVisible(handle))) {
		return TRUE;
	}

	hwndData.windowHandle = handle;

	return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	if (ul_reason_for_call != DLL_PROCESS_ATTACH) {
		return TRUE;
	}

	HWND_Data windowData = { GetCurrentProcessId(), 0 };
	EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&windowData));

	HWND windowHandle = windowData.windowHandle;

	if (windowHandle == 0) {
		std::cerr << "Failed to find game window!" << std::endl;
		return TRUE;
	}

	char windowName[256];
	GetWindowTextA(windowHandle, windowName, 256);

	AllocConsole();
	SetConsoleTitleA(windowName);
	SetConsoleCtrlHandler(NULL, true);

	FILE* fIn;
	FILE* fOut;

	freopen_s(&fIn, "conin$", "r", stdin);
	freopen_s(&fOut, "conout$", "w", stdout);
	freopen_s(&fOut, "conout$", "w", stderr);

	auto logger = spdlog::stdout_color_mt("DllMain");

	#ifdef _DEBUG
		spdlog::set_level(spdlog::level::debug);
	#else 
		spdlog::set_level(spdlog::level::info);
	#endif

	logger->info("Injected DLL into target process!");

	logger->info("Checking for JSON file...");

	filesystem::path tempDirectoryPath = filesystem::temp_directory_path();
	filesystem::path configPath = tempDirectoryPath / filesystem::path("injector_config.json");

	logger->debug("Configuration path: " + configPath.string());

	std::ifstream configFileStream(configPath);
	if (!configFileStream) {
		logger->error("Configuration file not found!");
		return TRUE;
	}

	nlohmann::json configData;
	configFileStream >> configData;
	configFileStream.close();

	std::string jarPath = configData["jar_path"];
	logger->debug("JAR Path: " + jarPath);

	std::string className = configData["class_name"];
	logger->debug("Class Name: " + className);

	std::string methodName = configData["method_name"];
	logger->debug("Method Name: " + methodName);

	InjectorJNIData* jniData = new InjectorJNIData;
	jniData->jarPath = jarPath;
	jniData->className = className;
	jniData->methodName = methodName;

	logger->info("Loaded configuration!");

	logger->info("Starting thread for JNI loading...");

	CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(startJNILoading), jniData, 0, 0);

    return TRUE;
}