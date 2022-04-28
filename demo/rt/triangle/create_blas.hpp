#pragma once

#include "as.hpp"

#include <vk/buffer/get_device_address.hpp>

inline as_t
create_blas(
	handle<vk::device> device,
	handle<vk::physical_device> physical_device,
	handle<vk::command_buffer> command_buffer,
	handle<vk::queue> queue
) {
	using namespace vk;

	as_t blas{};

	struct blas_data_t {
		math::vector<float, 3> vertices[3] {
			{-1.0F, 1.0F, 0.0F },
			{ 0.0F,-1.0F, 0.0F },
			{ 1.0F, 1.0F, 0.0F }
		};
		uint32 indices[3]{ 0, 1, 2 };
		transform_matrix matrix {{
			{ 1.0F, 0.0F, 0.0F, 0.0F },
			{ 0.0F, 1.0F, 0.0F, 0.0F },
			{ 0.0F, 0.0F, 1.0F, 0.0F }
		}};
	} blas_data;

	auto vertex_buffer = device.create<vk::buffer>(
		buffer_usages {
			buffer_usage::shader_device_address,
			buffer_usage::acceleration_structure_build_input_read_only
		},
		buffer_size{ sizeof blas_data_t::vertices }
	);

	auto index_buffer = device.create<vk::buffer>(
		buffer_usages {
			buffer_usage::shader_device_address,
			buffer_usage::acceleration_structure_build_input_read_only
		},
		buffer_size{ sizeof blas_data_t::indices }
	);

	auto transform_buffer = device.create<vk::buffer>(
		buffer_usages {
			buffer_usage::shader_device_address,
			buffer_usage::acceleration_structure_build_input_read_only
		},
		buffer_size{ sizeof blas_data_t::matrix }
	);

	auto data_memory = device.allocate<vk::device_memory>(
		physical_device.find_first_memory_type_index(
			memory_properties {
				memory_property::host_visible,
				memory_property::host_coherent
			},
			device.get_memory_requirements(vertex_buffer).memory_type_indices
			&
			device.get_memory_requirements(index_buffer).memory_type_indices
			&
			device.get_memory_requirements(transform_buffer).memory_type_indices
		),
		memory_size{ sizeof(blas_data_t) },
		memory_allocate_flags_info {
			.flags = memory_allocate_flags {
				memory_allocate_flag::address
			}
		}
	);

	device.bind_memory (
		vertex_buffer,
		data_memory,
		memory_offset{ offsetof(blas_data_t, vertices) }
	);
	device.bind_memory(
		index_buffer,
		data_memory,
		memory_offset{ offsetof(blas_data_t, indices) }
	);
	device.bind_memory(
		transform_buffer,
		data_memory,
		memory_offset{ offsetof(blas_data_t, matrix) }
	);

	void* mapped_data_memory;
	device.map_memory(
		data_memory,
		&mapped_data_memory,
		memory_size{ sizeof(blas_data_t) }
	);

	memccpy(mapped_data_memory, &blas_data, 1, sizeof(blas_data));

	as::geometry_triangles_data triangles_data {
		.format = format::r32_g32_b32_sfloat,
		.vertex_data{ get_device_address(device, vertex_buffer) },
		.vertex_stride{ sizeof blas_data_t::vertices },
		.max_vertex = 3,
		.index_type = index_type::uint32,
		.index_data{ get_device_address(device, index_buffer) },
		.transform_data{ get_device_address(device, transform_buffer) }
	};

	as::geometry_data geometry_data {
		.triangles = triangles_data
	};

	as::geometry geometry {
		.type = vk::geometry_type::triangles,
		.geometry = geometry_data,
		.flags = geometry_flag::opaque
	};

	as::build_geometry_info build_geometry_info {
		.type = acceleration_structure_type::bottom_level,
		.geometry_count = 1,
		.geometries = &geometry
	};

	const uint32 primitive_counts = 1;

	auto build_sizes = get_build_sizes(
		device,
		as::build_type::device,
		build_geometry_info,
		&primitive_counts
	);

	blas.scratch_buffer = device.create<buffer>(
		memory_size { build_sizes.acceleration_structure_size },
		buffer_usages {
			buffer_usage::storage_buffer,
			buffer_usage::shader_device_address
		}
	);

	blas.scratch_device_memory = device.allocate<device_memory>(
		physical_device.find_first_memory_type_index(
			memory_properties{ memory_property::device_local },
			device.get_memory_requirements(blas.scratch_buffer)
				.memory_type_indices
		),
		memory_size{ build_sizes.acceleration_structure_size },
		memory_allocate_flags_info {
			.flags = memory_allocate_flags {
				memory_allocate_flag::address
			}
		}
	);

	device.bind_memory(blas.scratch_buffer, blas.scratch_device_memory);

	blas.buffer = device.create<buffer>(
		memory_size { build_sizes.acceleration_structure_size },
		buffer_usages{ buffer_usage::acceleration_structure_storage }
	);

	blas.device_memory = device.allocate<device_memory>(
		physical_device.find_first_memory_type_index(
			{},
			device.get_memory_requirements(blas.buffer).memory_type_indices
		),
		memory_size{ build_sizes.acceleration_structure_size },
		memory_allocate_flags_info {
			.flags = memory_allocate_flags {
				memory_allocate_flag::address
			}
		}
	);

	device.bind_memory(blas.buffer, blas.device_memory);

	blas.handle = device.create<acceleration_structure>(
		blas.buffer,
		memory_size{ build_sizes.acceleration_structure_size },
		acceleration_structure_type::bottom_level
	);

	build_geometry_info.dst = blas.handle;
	build_geometry_info.scratch_data.device_address = {
		get_device_address(device, blas.scratch_buffer)
	};

	command_buffer.begin(
		command_buffer_usages{ command_buffer_usage::one_time_submit }
	);

	const as::build_range_info build_range_info {
		.primitive_count = 3
	};
	command_buffer.cmd_build_acceleration_structure(
		device,
		array{ build_geometry_info },
		array{ &build_range_info }
	);
	command_buffer.end();

	queue.submit(command_buffer);
	device.wait_idle();

	return blas;
}