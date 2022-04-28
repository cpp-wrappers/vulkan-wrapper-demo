#pragma once

#include "as.hpp"

#include "vk/acceleration_structure/instance.hpp"
#include "vk/acceleration_structure/get_device_address.hpp"
#include "vk/buffer/get_device_address.hpp"

inline as_t create_tlas(
	handle<vk::device> device,
	handle<vk::physical_device> physical_device,
	handle<vk::command_buffer> command_buffer,
	handle<vk::queue> queue,
	as_t& blas
) {
	as_t tlas{};

	using namespace vk;

	transform_matrix transform_matrix {{
		{ 1.0F, 0.0F, 0.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F, 0.0F },
		{ 0.0F, 0.0F, 1.0F, 0.0F }
	}};

	as::instance as_instance {
		.transform = transform_matrix,
		.mask = 0xFF,
		.acceleration_structure_reference{
			get_device_address(device, blas.handle)
		}
	};

	auto instance_buffer = device.create<vk::buffer>(
		buffer_size { sizeof(as_instance) },
		buffer_usages {
			buffer_usage::shader_device_address,
			buffer_usage::acceleration_structure_build_input_read_only
		}
	);

	auto instance_memory = device.allocate<device_memory>(
		physical_device.find_first_memory_type_index(
			memory_properties {
				memory_property::host_visible, memory_property::host_coherent
			},
			get_memory_requirements(device, instance_buffer).memory_type_indices
		),
		memory_size{ sizeof(as_instance) },
		memory_allocate_flags_info {
			.flags = memory_allocate_flags {
				memory_allocate_flag::address
			}
		}
	);

	device.bind_memory(instance_buffer, instance_memory);
	void* mapped_instance_memory;
	device.map_memory(
		instance_memory,
		memory_size{ sizeof(as_instance) },
		&mapped_instance_memory
	);
	memcpy(mapped_instance_memory, &as_instance, sizeof(as_instance));

	as::instances_data instance_data {
		.data{ get_device_address(device, instance_buffer) }
	};

	as::geometry geometry {
		.type = geometry_type::instances,
		.geometry{ as::geometry_data{ .instances = instance_data } },
		.flags{ geometry_flag::opaque }
	};

	as::build_geometry_info build_geometry_info {
		.type = as::type::top_level,
		.mode = as::build_mode::build,
		.geometry_count = 1,
		.geometries = &geometry
	};

	const uint32 primitive_counts = 1;

	auto build_sizes_info = get_build_sizes(
		device,
		acceleration_structure_build_type::device,
		build_geometry_info,
		&primitive_counts
	);

	tlas.buffer = device.create<buffer>(
		memory_size { build_sizes_info.acceleration_structure_size },
		buffer_usages {
			buffer_usage::acceleration_structure_storage,
			buffer_usage::shader_device_address
		}
	);

	auto buffer_memory_requirements {
		get_memory_requirements(device, tlas.buffer)
	};

	tlas.device_memory = device.allocate<device_memory>(
		physical_device.find_first_memory_type_index(
			memory_properties{ memory_property::device_local },
			buffer_memory_requirements.memory_type_indices
		),
		buffer_memory_requirements.size,
		memory_allocate_flags_info {
			.flags = memory_allocate_flags {
				memory_allocate_flag::address
			}
		}
	);

	device.bind_memory(tlas.buffer, tlas.device_memory);

	tlas.handle = device.create<acceleration_structure>(
		build_sizes_info.acceleration_structure_size,
		as::type::top_level,
		tlas.buffer
	);

	tlas.scratch_buffer = device.create<buffer>(
		memory_size{ build_sizes_info.build_scratch_size },
		buffer_usages{ buffer_usage::storage_buffer, buffer_usage::shader_device_address }
	);

	auto scratch_buffer_requirements {
		get_memory_requirements(device, tlas.scratch_buffer)
	};

	tlas.scratch_device_memory = device.allocate<device_memory>(
		physical_device.find_first_memory_type_index(
			memory_properties{ memory_property::device_local },
			scratch_buffer_requirements.memory_type_indices
		),
		scratch_buffer_requirements.size,
		memory_allocate_flags_info {
			.flags = memory_allocate_flags {
				memory_allocate_flag::address
			}
		}
	);

	device.bind_memory(tlas.scratch_buffer, tlas.scratch_device_memory);

	build_geometry_info.dst = tlas.handle;
	build_geometry_info.scratch_data.device_address = {
		get_device_address(device, tlas.scratch_buffer)
	};

	const as::build_range_info build_range_info {
		.primitive_count = 1
	};

	command_buffer.begin(command_buffer_usages{ command_buffer_usage::one_time_submit });
	command_buffer.cmd_build_acceleration_structure(
		device,
		array{ build_geometry_info },
		array{ &build_range_info }
	);

	command_buffer.end();

	queue.submit(command_buffer);
	device.wait_idle();

	return tlas;
}