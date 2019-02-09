#include "InjectorDLL.h"

#include <sstream>
#include <fstream>

JNIEnv* env;
JavaVM* jvm;

const std::string getClassName(JNIEnv* env, jobject jObject) {
	if (jObject == nullptr) {
		return "nullptr";
	}

	jclass cls = env->GetObjectClass(jObject);
	jmethodID mid = env->GetMethodID(cls, "getClass", "()Ljava/lang/Class;");
	jobject clsObj = env->CallObjectMethod(jObject, mid);
	cls = env->GetObjectClass(clsObj);
	jmethodID getNameID = env->GetMethodID(cls, "getName", "()Ljava/lang/String;");
	jstring className = static_cast<jstring>(env->CallObjectMethod(clsObj, getNameID));
	const char* classNameC = env->GetStringUTFChars(className, false);

	return std::string(classNameC);
}

void startJNILoading(InjectorJNIData* injectorData) {
	auto logger = spdlog::stderr_color_mt("JNILoader");

	logger->info("Starting discovery of Java VMs...");

	jsize vmCount;
	if (JNI_GetCreatedJavaVMs(&jvm, 1, &vmCount) != JNI_OK || vmCount == 0) {
		logger->error("Failed to find the JVM!");
		return;
	}

	jint getEnvRes = jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
	if (getEnvRes == JNI_EDETACHED) {
		getEnvRes = jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr);
	}

	if (getEnvRes != JNI_OK) {
		logger->error("Failed to attach to the JVM!");
		return;
	}

	logger->info("Attached to JVM!");
	logger->info("Starting thread discovery...");

	jobject clientThread = findNamedThread("Client thread");

	if (clientThread == nullptr) {
		logger->error("Failed to find Client thread!");
		return;
	}

	logger->debug("Getting thread ClassLoader...");

	jclass threadClass = env->FindClass("java/lang/Thread");
	jmethodID getContextClassloaderID = env->GetMethodID(threadClass, "getContextClassLoader", "()Ljava/lang/ClassLoader;");

	jobject clientClassLoader = env->CallObjectMethod(clientThread, getContextClassloaderID);

	if (clientClassLoader == nullptr) {
		logger->error("Failed to get Client ClassLoader!");
		return;
	}

	logger->debug("Client ClassLoader: " + getClassName(env, clientClassLoader));

	logger->info("Discovered Client thread and ClassLoader!");

	JARInjectResult injectResult = injectJavaCode(*injectorData, clientClassLoader);

	if (injectResult != JIR_OK) {
		MessageBoxA(nullptr, "Something went wrong while injecting Java code. Check the logs for more information.", "Injection Error", MB_OK);
	}

	logger->info("Injection completed.");
}

JARInjectResult injectJavaCode(InjectorJNIData& injectorData, jobject& parentClassLoader) {
	auto logger = spdlog::stdout_color_mt("JavaInjector");

	std::ifstream fileTestStream(injectorData.jarPath);
	if (!fileTestStream) {
		logger->error("Failed to find JAR file!");

		return JIR_FileNotFound;
	}

	jclass javaNetURLClass;
	javaNetURLClass = env->FindClass("java/net/URL");

	jmethodID urlClassConstructor;
	urlClassConstructor = env->GetMethodID(javaNetURLClass, "<init>", "(Ljava/lang/String;)V");

	std::ostringstream urlStream;
	urlStream << "file:/" << injectorData.jarPath;

	jobject injectFileURL;
	jstring jstr = env->NewStringUTF(urlStream.str().c_str());
	injectFileURL = env->NewObject(javaNetURLClass, urlClassConstructor, jstr);

	jobjectArray classloaderSourceArray;
	classloaderSourceArray = env->NewObjectArray(1, javaNetURLClass, injectFileURL);

	jclass javaNetURLClassLoaderClass;
	javaNetURLClassLoaderClass = env->FindClass("java/net/URLClassLoader");

	jmethodID urlClassLoaderNewInstance;
	urlClassLoaderNewInstance = env->GetStaticMethodID(javaNetURLClassLoaderClass, "newInstance", "([Ljava/net/URL;Ljava/lang/ClassLoader;)Ljava/net/URLClassLoader;");

	jobject injectClassLoader;
	injectClassLoader = env->CallStaticObjectMethod(javaNetURLClassLoaderClass, urlClassLoaderNewInstance, classloaderSourceArray, parentClassLoader);

	logger->debug("Created new URLClassLoader");

	jmethodID loadClassMethod;
	loadClassMethod = env->GetMethodID(javaNetURLClassLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

	jclass javaLangClass = env->FindClass("java/lang/Class");

	jclass javaLangStringClass = env->FindClass("java/lang/String");
	jstring injectClassName = env->NewStringUTF(injectorData.className.c_str());

	jclass injectClass = (jclass)env->CallObjectMethod(injectClassLoader, loadClassMethod, injectClassName);

	if (injectClass == nullptr) {
		logger->error("Failed to find Injected class!");

		return JIR_ClassNotFound;
	}

	logger->debug("Got class reference!");

	jmethodID injectClassNewInstance = env->GetMethodID(javaLangClass, "newInstance", "()Ljava/lang/Object;");

	jobject injectedObject = env->CallObjectMethod(injectClass, injectClassNewInstance);

	logger->info("Got Injected object: " + getClassName(env, injectedObject));

	jmethodID pwnMethod = env->GetMethodID(injectClass, injectorData.methodName.c_str(), "()V");
	if (pwnMethod == nullptr) {
		logger->error("Failed to get injected method!");

		return JIR_MethodNotFound;
	}

	std::ostringstream methodMessage;
	methodMessage << "Got injection entry point at " << pwnMethod;
	logger->info(methodMessage.str());

	env->CallVoidMethod(injectedObject, pwnMethod);

	logger->info("Successfully injected Java code!");

	return JIR_OK;
}

jobject findNamedThread(std::string targetName) {
	auto logger = spdlog::stderr_color_mt("ThreadFinder");

	jclass threadClass = env->FindClass("java/lang/Thread");
	jmethodID getCurrentThreadID = env->GetStaticMethodID(threadClass, "currentThread", "()Ljava/lang/Thread;");
	jobject currentThread = env->CallStaticObjectMethod(threadClass, getCurrentThreadID);

	logger->info("Current thread: " + getClassName(env, currentThread));

	jmethodID getGroupID = env->GetMethodID(threadClass, "getThreadGroup", "()Ljava/lang/ThreadGroup;");
	jobject threadGroup = env->CallObjectMethod(currentThread, getGroupID);
	jmethodID getParentID = env->GetMethodID(env->GetObjectClass(threadGroup), "getParent", "()Ljava/lang/ThreadGroup;");

	while (env->CallObjectMethod(threadGroup, getParentID) != nullptr) {
		threadGroup = env->CallObjectMethod(threadGroup, getParentID);
	}

	jmethodID getNameID = env->GetMethodID(env->GetObjectClass(threadGroup), "getName", "()Ljava/lang/String;");
	jstring groupName = static_cast<jstring>(env->CallObjectMethod(threadGroup, getNameID));

	const char* groupNameC = env->GetStringUTFChars(groupName, false);

	logger->info("Root ThreadGroup: " + getClassName(env, threadGroup) + " (" + groupNameC + ")");

	jmethodID activeCountID = env->GetMethodID(env->GetObjectClass(threadGroup), "activeCount", "()I");
	jint activeCount = env->CallIntMethod(threadGroup, activeCountID);

	jmethodID enumerateID = env->GetMethodID(env->GetObjectClass(threadGroup), "enumerate", "([Ljava/lang/Thread;)I");
	jobjectArray threads = env->NewObjectArray(activeCount, threadClass, nullptr);
	jint threadsCopied = env->CallIntMethod(threadGroup, enumerateID, threads);

	std::ostringstream threadCopyMessage;
	threadCopyMessage << "Copied " << threadsCopied << " threads from the root ThreadGroup!";
	logger->info(threadCopyMessage.str());

	jobject discoveredThread = nullptr;

	jmethodID threadGetNameID = env->GetMethodID(threadClass, "getName", "()Ljava/lang/String;");
	jmethodID threadGetIDID = env->GetMethodID(threadClass, "getId", "()J");
	jmethodID getContextClassloaderID = env->GetMethodID(threadClass, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
	for (char i = 0; i < threadsCopied; i++) {
		jobject thread = env->GetObjectArrayElement(threads, i);

		jstring threadName = static_cast<jstring>(env->CallObjectMethod(thread, threadGetNameID));
		const char* threadNameC = env->GetStringUTFChars(threadName, false);

		jlong threadID = env->CallLongMethod(thread, threadGetIDID);

		std::ostringstream threadMessage;
		threadMessage << "Thread (" << threadNameC << "); ID: " << threadID << " CL: " << getClassName(env, env->CallObjectMethod(thread, getContextClassloaderID));
		logger->debug(threadMessage.str());

		if (targetName.compare(threadNameC) == 0) {
			discoveredThread = thread;
		}

		env->ReleaseStringUTFChars(threadName, threadNameC);
	}

	return discoveredThread;
}