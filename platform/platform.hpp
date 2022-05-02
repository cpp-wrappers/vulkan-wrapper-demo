#pragma once

#include <core/number.hpp>
#include <core/c_string.hpp>
#include <core/span.hpp>
#include <core/view_on_stack.hpp>

#include <math/matrix.hpp>

#include "vk/instance/handle.hpp"
#include "vk/instance/create.hpp"
#include "vk/instance/layer/is_supported.hpp"
#include "vk/instance/extension/is_supported.hpp"
#include "vk/debug/report/callback/create.hpp"
#include "vk/unexpected_handler.hpp"

namespace platform {

	struct logger {
		void* raw;

		inline const logger& string(const char*, nuint length) const;

		const logger& operator () (const char*) const;
		const logger& operator () (char) const;

		const logger& new_line() const {
			(*this)('\n');
			return *this;
		}

		template<unsigned_integer I>
		const logger& operator() (I i) const {
			for_each_digit_in_number(
				number{ i }, base{ 10 }, [this](nuint digit) {
					(*this)(char(digit + '0'));
				}
			);
			return *this;
		}

		template<signed_integer I>
		const logger& operator() (I i) const {
			if(i < 0) {
				(*this)("-");
				(*this)(uint_of_size_of<I>(-i));
			}
			else {
				(*this)(uint_of_size_of<I>( i));
			}
			return *this;
		}

		const logger& operator() (bool b) const {
			if(b) (*this)("true");
			else (*this)("false");
			return *this;
		}

		template<typename... Args>
		requires(sizeof...(Args) >= 2)
		const logger& operator () (Args... args) const {
			( (*this)(args), ... );
			return *this;
		}

		auto& operator() (c_string str) const {
			(*this)(str.begin());
			return *this;
		}

	}; // logger

	extern logger info;
	extern logger error;

	nuint file_size(const char*);
	void read_file(const char*, span<char> buffer);

	struct image_info {
		uint32 width;
		uint32 height;
		nuint size;
	};

	image_info read_image_info(const char* path);
	void read_image_data(const char* path, span<char> buffer);

	inline uint32 debug_report(
		flag_enum<vk::debug_report_flag>,
		vk::debug_report_object_type, uint64, nuint,
		int32, c_string, c_string message, void*
	) {
		platform::info("[vk] ", message).new_line();
		return 0;
	}

	inline handle<vk::instance>
	create_instance(
		vk::api_version api_version, const auto& extensions
	) {
		vk::layer validation_layer{ "VK_LAYER_KHRONOS_validation" };
		vk::extension debug_report_extension{ "VK_EXT_debug_report" };

		auto result = view_on_stack<vk::layer>(1)([&](auto layers) {
			bool validation_layer_is_supported {
				vk::is_instance_layer_supported(validation_layer)
			};

			nuint layers_count = 0;
			if(validation_layer_is_supported) {
				layers[layers_count++] = validation_layer;
			}

			return view_on_stack<vk::extension> {
				extensions.size() + 1
			}([&](span<vk::extension> extensions0) {
				nuint extensions_count = 0;
				for(auto e : extensions)
					extensions0[extensions_count++] = vk::extension{ e };

				if(vk::is_instance_extension_supported(debug_report_extension))
					extensions0[extensions_count++] = debug_report_extension;

				return vk::create<vk::instance>(
					vk::application_info {
						api_version
					},
					layers.cut(layers_count),
					extensions0.cut(extensions_count)
				);
			});
		});

		if(result.is_unexpected()) {
			platform::error("couldn't create instance").new_line();
			vk::unexpected_handler(result.get_unexpected());
		}

		auto instance = result.get_expected();

		if(vk::is_instance_extension_supported(debug_report_extension)) {
			[[ maybe_unused ]] auto drc {
				instance.template create<vk::debug_report_callback>(
					vk::debug_report_flags {
						vk::debug_report_flag::error,
						vk::debug_report_flag::warning,
						vk::debug_report_flag::information
					},
					platform::debug_report
				)
			};
		}

		return instance;
	}

	inline handle<vk::instance> create_instance(vk::api_version api_version) {
		return platform::create_instance(
			api_version, array<vk::extension, 0>{}
		);
	}

	inline handle<vk::instance> create_instance() {
		return platform::create_instance(
			vk::api_version{ vk::major{ 1 }, vk::minor{ 0 } }
		);
	}

	elements::of<handle<vk::instance>, handle<vk::surface>>
	create_instance_and_surface(vk::api_version api_version);

	inline elements::of<handle<vk::instance>, handle<vk::surface>>
	create_instance_and_surface() {
		return create_instance_and_surface(
			vk::api_version{ vk::major{ 1 }, vk::minor{ 0 } }
		);
	}

	inline handle<vk::shader_module>
	read_shader_module(handle<vk::device> device, const char* path) {
		auto size = platform::file_size(path);
		char src[size];
		platform::read_file(path, span{ src, size });
		return device.create<vk::shader_module>(
			vk::code_size{ (uint32) size }, vk::code{ (uint32*) src }
		);
	}

	inline math::vector<float, 2> get_cursor_position();

	inline bool should_close();
	inline void begin();
	inline void end();

} // platform

#include <vk/unexpected_handler.hpp>

extern "C" [[ noreturn ]] void abort();

[[ noreturn ]]
inline void vk::unexpected_handler() {
	abort();
}

[[ noreturn ]]
inline void vk::unexpected_handler(vk::result) {
	abort();
}

#include <glfw/unexpected_handler.hpp>

[[ noreturn ]]
inline void glfw::unexpected_handler() {
	abort();
}

[[ noreturn ]]
inline void glfw::unexpected_handler(glfw::error) {
	abort();
}