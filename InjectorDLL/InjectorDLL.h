#pragma once

#include <Windows.h>

#include <string>

#include "jni.h"

#include "spdlog\spdlog.h"
#include "spdlog\sinks\stdout_color_sinks.h"

struct InjectorJNIData {
	std::string jarPath;
	std::string className;
	std::string methodName;
};

enum JARInjectResult {
	JIR_OK, JIR_FileNotFound, JIR_ClassNotFound, JIR_MethodNotFound
};

void startJNILoading(InjectorJNIData* injectorData);

jobject findNamedThread(std::string targetName);
JARInjectResult injectJavaCode(InjectorJNIData& injectorData, jobject& parentClassLoader);