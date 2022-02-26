#include <string.h>
#include <stdlib.h>

#include <android/looper.h>
#include <android/native_app_glue/android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/native_activity.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/sensor.h>

//#include <spng.h>

#include "vk/headers.hpp"
#include "vulkan/vulkan_android.h"
#include <glm/gtx/transform.hpp>

#include "../platform.hpp"

struct message_buffer {
	int prio;
	static constexpr nuint buf_size = 256;
	char buf[buf_size]{ 0 };
	nuint size = 0;

	void add(const char* str, nuint length) {
		while(length-- > 0) {
			char ch = str[0];
			++str;

			if(ch == '\n') {
				if(size >= buf_size) abort();
				buf[size] = 0;
				__android_log_write(prio, "vulkan", buf);
				size = 0;
			}
			else {
				if(size >= buf_size - 1) abort();
				buf[size++] = ch;
			}
		}
	}

	void add(const char* str) {
		while(char ch = *(str++)) {
			if(ch == '\n') {
				if(size >= buf_size) abort();
				buf[size] = 0;
				__android_log_write(prio, "vulkan", buf);
				size = 0;
			}
			else {
				if(size >= buf_size - 1) abort();
				buf[size++] = ch;
			}
		}
	}
};

static message_buffer info_message_buffer{ ANDROID_LOG_INFO };
static message_buffer error_message_buffer{ ANDROID_LOG_ERROR };

const platform::logger& platform::logger::string(const char* str, nuint length) const {
	((message_buffer*)raw)->add(str, length);
	return *this;
}

const platform::logger& platform::logger::operator () (const char* str) const {
	((message_buffer*)raw)->add(str);
	return *this;
}

const platform::logger& platform::logger::operator () (char ch) const {
	((message_buffer*)raw)->add(&ch, 1);
	return *this;
}

namespace platform {
	logger info { &info_message_buffer };
	logger error { &error_message_buffer };
}

static android_app* app;

nuint platform::file_size(const char* path) {
	AAsset* asset = AAssetManager_open(app->activity->assetManager, path, AASSET_MODE_BUFFER);
	auto size = AAsset_getLength(asset);
	AAsset_close(asset);
	return size;
}

void platform::read_file(const char* path, span<char> buff) {
	AAsset* asset = AAssetManager_open(app->activity->assetManager, path, AASSET_MODE_BUFFER);
	char* asset_buff = (char*) AAsset_getBuffer(asset);
	memcpy(buff.data(), asset_buff, buff.size());
	AAsset_close(asset);
}

/*platform::image_info platform::read_image_info(const char *path) {
	auto file_size = platform::file_size(path);
	char storage[file_size];
	span buff{ storage, file_size };

	read_file(path, buff);

	spng_ctx *ctx = spng_ctx_new(0);
	spng_set_png_buffer(ctx, buff.data(), buff.size());

	spng_ihdr ihdr;
	if(spng_get_ihdr(ctx, &ihdr)) {
		platform::error("couldn't get ihdr").new_line();
		abort();
	}

	nuint size;
	spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &size);

	spng_ctx_free(ctx);

	return {
		.width = (uint32) ihdr.width,
		.height = (uint32) ihdr.height,
		.size = size
	};
}

void platform::read_image_data(const char *path, span<char> buff) {
	auto file_size = platform::file_size(path);
	char storage[file_size];
	span file_buff{ storage, file_size };

	read_file(path, file_buff);

	spng_ctx *ctx = spng_ctx_new(0);
	spng_set_png_buffer(ctx, file_buff.data(), file_buff.size());

	//spng_ihdr ihdr;
	//if(spng_get_ihdr(ctx, &ihdr)) {
	//	platform::error("couldn't get ihdr").new_line();
	//	abort();
	//}

	//nuint size;
	//spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &size);

	spng_decode_image(ctx, buff.data(), buff.size(), SPNG_FMT_RGBA8, 0);

	spng_ctx_free(ctx);
}*/

static array<vk::extension_name, 2> required_instance_extensions{ "VK_KHR_surface", "VK_KHR_android_surface" };

span<vk::extension_name> platform::get_required_instance_extensions() { return span{ required_instance_extensions.data(), required_instance_extensions.size() }; }

vk::guarded_handle<vk::surface> platform::create_surface(vk::handle<vk::instance> instance) {

	VkAndroidSurfaceCreateInfoKHR ci {
		.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
		.window = app->window
	};

	vk::handle<vk::surface> surface;

	VkResult result = vkCreateAndroidSurfaceKHR(
		(VkInstance) vk::get_handle_value(instance),
		&ci,
		nullptr,
		(VkSurfaceKHR*) &surface
	);

	if(result != VK_SUCCESS) {
		error("couldn't create surface").new_line();
		abort();
	}

	return { surface, instance };
}

#include <dlfcn.h>
ASensorManager* AcquireASensorManagerInstance(android_app* app) {
	if(!app)
		return nullptr;

	typedef ASensorManager *(*PF_GETINSTANCEFORPACKAGE)(const char *name);
	void* androidHandle = dlopen("libandroid.so", RTLD_NOW);
	auto getInstanceForPackageFunc = (PF_GETINSTANCEFORPACKAGE)
		dlsym(androidHandle, "ASensorManager_getInstanceForPackage");
	if (getInstanceForPackageFunc) {
		JNIEnv* env = nullptr;
		app->activity->vm->AttachCurrentThread(&env, nullptr);

		jclass android_content_Context = env->GetObjectClass(app->activity->clazz);
		jmethodID midGetPackageName = env->GetMethodID(android_content_Context,
													"getPackageName",
													"()Ljava/lang/String;");
		auto packageName= (jstring)env->CallObjectMethod(app->activity->clazz,
														midGetPackageName);

		const char *nativePackageName = env->GetStringUTFChars(packageName, nullptr);
		ASensorManager* mgr = getInstanceForPackageFunc(nativePackageName);
		env->ReleaseStringUTFChars(packageName, nativePackageName);
		app->activity->vm->DetachCurrentThread();
		if (mgr) {
			dlclose(androidHandle);
			return mgr;
		}
	}

	typedef ASensorManager *(*PF_GETINSTANCE)();
	auto getInstanceFunc = (PF_GETINSTANCE)
		dlsym(androidHandle, "ASensorManager_getInstance");
	// by all means at this point, ASensorManager_getInstance should be available
	assert(getInstanceFunc);
	dlclose(androidHandle);

	return getInstanceFunc();
}

glm::mat4 platform::view_matrix{};
const ASensor* rot;
ASensorEventQueue* sensor_event_queue;

static void poll() {
	int identifier;
	int events;
	android_poll_source* source;

	while((identifier = ALooper_pollAll(0, nullptr, &events, (void**)&source)) >= 0) {
		if(source != nullptr) source->process(app, source);

		if(identifier == LOOPER_ID_USER) {
			ASensorEvent event;
			while(ASensorEventQueue_getEvents(sensor_event_queue, &event, 1) > 0) {
				glm::vec3 angles{ event.data[0], event.data[1], event.data[2] };
				float len = glm::length(angles);

				float angle = glm::asin(len)*2.0F;
				
				platform::view_matrix =
				glm::scale(glm::vec3(1.0F, -1.0F, 1.0F)) *
				glm::rotate(
					glm::pi<float>() / 2.0F,
					glm::vec3(0.0F, 0.0F, 1.0F)
				) *
				glm::inverse(glm::rotate(
					angle,
					angles / len
				)) *
				glm::rotate(
					glm::pi<float>() / 2.0F,
					glm::vec3(1.0F, 0.0F, 0.0F)
				);

				platform::info(
					int((platform::view_matrix * glm::vec4(0.0F, 1.0F, 0.0F, 1.0F)).y * 100)
				).new_line();
			}
		}

		if(app->destroyRequested) return;
	}
}

bool platform::should_close() {
	return app->destroyRequested;
}

void platform::begin() {
	poll();
}

void platform::end() {} 

extern "C" void android_main(android_app* app) {
	::app = app;
	platform::info("starting").new_line();

	auto sensor_manager = AcquireASensorManagerInstance(app);
	rot = ASensorManager_getDefaultSensor(sensor_manager, ASENSOR_TYPE_ROTATION_VECTOR);
	if(rot == nullptr) {
		platform::error("rotation sensor is null").new_line();
		abort();
	}

	sensor_event_queue = ASensorManager_createEventQueue(sensor_manager, app->looper, LOOPER_ID_USER, nullptr, nullptr);
	ASensorEventQueue_enableSensor(sensor_event_queue, rot);
	ASensorEventQueue_setEventRate(sensor_event_queue, rot, (1000L/200)*1000);

	while(app->window == nullptr) {
		poll();
	}

	entrypoint();
}