#pragma once

#include "platform_implementation.hpp"

#include "vk/acceleration_structure/build_geometry_info.hpp"
#include "vk/acceleration_structure/get_build_sizes.hpp"
#include "vk/acceleration_structure/transform_matrix.hpp"
#include "vk/acceleration_structure/instance.hpp"

struct as_t {
	vk::guarded_handle<vk::acceleration_structure> handle;

	vk::guarded_handle<vk::buffer> buffer;
	vk::guarded_handle<vk::device_memory> device_memory;

	vk::guarded_handle<vk::buffer> scratch_buffer;
	vk::guarded_handle<vk::device_memory> scratch_device_memory;
};