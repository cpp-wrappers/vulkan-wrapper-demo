#include "../platform.hpp"

#include <glfw/init.hpp>
#include <glfw/vulkan.hpp>
#include <glfw/poll_events.hpp>
#include <glfw/window/create_surface.hpp>
#include <glfw/window/create.hpp>
#include <glfw/window/hint.hpp>

#include <stdio.h>
#include <string.h>
#include <png.h>

extern "C" void abort();

inline const platform::logger&
platform::logger::string(const char* str, nuint length) const {
	fwrite(str, 1, length, (FILE*) raw);
	fflush((FILE*) raw);
	return *this;
}

inline const platform::logger&
platform::logger::operator () (const char* str) const {
	fputs(str, (FILE*) raw);
	fflush((FILE*) raw);
	return *this;
}

inline const platform::logger&
platform::logger::operator () (char c) const {
	fputc(c, (FILE*) raw);
	fflush((FILE*) raw);
	return *this;
}

namespace platform {
	inline logger info{ stdout };
	inline logger error{ stderr };
}

inline nuint platform::file_size(const char* path) {
	FILE* f = fopen(path, "rb");
	if(f == nullptr) {
		platform::error("couldn't open file: ", path, '\n');
		abort();
	}
	fseek(f, 0, SEEK_END);
	nuint size = ftell(f);
	fclose(f);
	return size;
}

inline void platform::read_file(const char* path, span<char> buff) {
	FILE* f = fopen(path, "rb");
	if(f == nullptr) {
		platform::error("couldn't open file ", path, '\n');
		abort();
	}

	auto result = fread(buff.data(), 1, buff.size(), f);
	fclose(f);

	if(result != buff.size()) {
		if (feof(f))
			platform::error("EOF");
		else if (ferror(f))
			platform::error("error");
		else
			platform::error("result != size");

		platform::error(
			" while reading file: ", path,
			", buffer size: ", buff.size(),
			"read: ", result, '\n'
		);

		abort();
	}
}

inline platform::image_info platform::read_image_info(const char* path) {
	FILE* f = fopen(path, "rb");
	if(f == nullptr) {
		platform::error("couldn't open file: ", path, '\n');
		abort();
	}

	png_image image {
		.version = PNG_IMAGE_VERSION
	};

	if(!png_image_begin_read_from_stdio(&image, f)) {
		abort();
	}

	fclose(f);

	return {
		.width = image.width,
		.height = image.height,
		.size = PNG_IMAGE_SIZE(image)
	};
}

inline void platform::read_image_data(const char* path, span<char> buffer) {
	FILE* f = fopen(path, "rb");
	if(f == nullptr) {
		platform::error("couldn't open file: ", path, '\n');
		abort();
	}

	png_structp png_ptr = png_create_read_struct(
		PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr
	);

	if(!png_ptr) {
		error("couldn't create png_structp").new_line(); abort();
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr) {
		error("couldn't create png_infop").new_line(); abort();
	}

	png_init_io(png_ptr, f);

	png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, nullptr);

	auto rows = png_get_rows(png_ptr, info_ptr);

	for(unsigned r = 0; r < png_get_image_height(png_ptr, info_ptr); ++r) {
		for(unsigned x = 0; x < png_get_rowbytes(png_ptr, info_ptr); ++x) {
			buffer[png_get_rowbytes(png_ptr, info_ptr) * r + x] = rows[r][x];
		}
	}

	fclose(f);

	png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
}

static inline handle<glfw::window> window;

elements::of<handle<vk::instance>, handle<vk::surface>> inline
platform::create_instance_and_surface(vk::api_version api_version) {
	if (!glfw::try_init()) {
		platform::error("glfw init failed").new_line();
		abort();
	}

	if(!glfw::is_vulkan_supported()) {
		platform::error("vulkan is not supported").new_line();
		abort();
	}

	//glfwSetErrorCallback([](int error_code, const char* description) {
	//	platform::error(
	//	"[glfw] error code: ", uint32(error_code),
	//	", description: ", description
	//).new_line();
	//});

	glfw::window_hint(glfw::window_hint::client_api, glfw::window_hint::no_api);
	auto window0 = glfw::try_create_window(
		glfw::width{ 640 }, glfw::height{ 640 }, glfw::title{ "Vulkan" }
	);

	if (window0.is_unexpected()) {
		platform::error("window creation failed").new_line();
		abort();
	}
	
	window = window0.get_expected();

	auto required_extensions = glfw::get_required_instance_extensions();

	handle<vk::instance> instance {
		create_instance(api_version, required_extensions)
	};

	auto surface0 = glfw::try_create_window_surface(
		instance,
		window
	);

	if(surface0.is_unexpected()) {
		platform::error("surface creation failed").new_line();
		abort();
	}

	return { instance, surface0.get_expected() };
}

inline math::vector<float, 2> platform::get_cursor_position() {
	auto [x, y] = window.get_cursor_pos();
	return { (float) x, (float) y };
}

inline bool platform::should_close() {
	return glfwWindowShouldClose(window);
}

#include <math/geometry/coordinate_system/cartesian/vector.hpp>

inline math::matrix<float, 4, 4>
rotation(float angle, math::geometry::cartesian::vector<float, 3> a) {
	float c = __builtin_cos(angle);
	float s = __builtin_sin(angle);

	a = a * (1.0F / a.length());
	auto t = (1.0F - c) * a;

	return {{
		{ t[0]*a[0] + c     , t[1]*a[0] - s*a[2], t[2]*a[0] + s*a[1], 0 },
		{ t[0]*a[1] + s*a[2], t[1]*a[1] + c     , t[2]*a[1] - s*a[0], 0 },
		{ t[0]*a[2] - s*a[1], t[1]*a[2] + s*a[0], t[2]*a[2] + c     , 0 },
		{                  0,                  0,                  0, 1 }
	}};
}

void platform::begin() {
}

inline void platform::end() {
	glfw::poll_events();
}