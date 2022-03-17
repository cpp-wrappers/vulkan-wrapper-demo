#pragma once

#include "as.hpp"

inline as_t create_blas(
	vk::guarded_handle<vk::device>& device,
	vk::handle<vk::physical_device>& physical_device,
	vk::guarded_handle<vk::command_buffer>& command_buffer,
	vk::handle<vk::queue> queue
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

	auto vertex_buffer = device.create_guarded<vk::buffer>(
		buffer_usages {
			buffer_usage::shader_device_address,
			buffer_usage::acceleration_structure_build_input_read_only
		},
		buffer_size{ sizeof blas_data_t::vertices }
	);

	auto index_buffer = device.create_guarded<vk::buffer>(
		buffer_usages {
			buffer_usage::shader_device_address,
			buffer_usage::acceleration_structure_build_input_read_only
		},
		buffer_size{ sizeof blas_data_t::indices }
	);

	auto transform_buffer = device.create_guarded<vk::buffer>(
		buffer_usages {
			buffer_usage::shader_device_address,
			buffer_usage::acceleration_structure_build_input_read_only
		},
		buffer_size{ sizeof blas_data_t::matrix }
	);

	auto data_memory = device.allocate_guarded<vk::device_memory>(
		physical_device.find_first_memory_type_index(
			memory_properties {
				memory_property::host_visible,
				memory_property::host_coherent
			},
			vertex_buffer.get_memory_requirements().memory_type_indices
			&
			index_buffer.get_memory_requirements().memory_type_indices
			&
			transform_buffer.get_memory_requirements().memory_type_indices
		),
		memory_size{ sizeof(blas_data_t) },
		memory_allocate_flags_info {
			.flags = memory_allocate_flags {
				memory_allocate_flag::address
			}
		}
	);

	vertex_buffer.bind_memory(data_memory,  memory_offset{ offsetof(blas_data_t, vertices) });
	index_buffer.bind_memory(data_memory, memory_offset{ offsetof(blas_data_t, indices) });
	transform_buffer.bind_memory(data_memory, memory_offset{ offsetof(blas_data_t, matrix) });

	void* mapped_data_memory;
	data_memory.map(&mapped_data_memory, memory_size{ sizeof(blas_data_t) });
	memccpy(mapped_data_memory, &blas_data, 1, sizeof(blas_data));

	as::geometry_triangles_data triangles_data {
		.format = format::r32_g32_b32_sfloat,
		.vertex_data{ vertex_buffer.get_device_address() },
		.vertex_stride{ sizeof blas_data_t::vertices },
		.max_vertex = 3,
		.index_type = index_type::uint32,
		.index_data{ index_buffer.get_device_address() },
		.transform_data{ transform_buffer.get_device_address() }
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

	blas.scratch_buffer = device.create_guarded<buffer>(
		buffer_size {
			build_sizes.acceleration_structure_size
		},
		buffer_usages{ buffer_usage::storage_buffer, buffer_usage::shader_device_address }
	);

	blas.scratch_device_memory = device.allocate_guarded<device_memory>(
		physical_device.find_first_memory_type_index(
			memory_properties{ memory_property::device_local },
			blas.scratch_buffer.get_memory_requirements().memory_type_indices
		),
		memory_size{ build_sizes.acceleration_structure_size },
		memory_allocate_flags_info {
			.flags = memory_allocate_flags {
				memory_allocate_flag::address
			}
		}
	);

	blas.scratch_buffer.bind_memory(blas.scratch_device_memory);

	blas.buffer = device.create_guarded<buffer>(
		buffer_size {
			build_sizes.acceleration_structure_size
		},
		buffer_usages{ buffer_usage::acceleration_structure_storage }
	);

	blas.device_memory = device.allocate_guarded<device_memory>(
		physical_device.find_first_memory_type_index(
			{},
			blas.buffer.get_memory_requirements().memory_type_indices
		),
		memory_size{ build_sizes.acceleration_structure_size },
		memory_allocate_flags_info {
			.flags = memory_allocate_flags {
				memory_allocate_flag::address
			}
		}
	);

	blas.buffer.bind_memory(blas.device_memory);

	blas.handle = device.create_guarded<acceleration_structure>(
		blas.buffer,
		memory_size{ build_sizes.acceleration_structure_size },
		acceleration_structure_type::bottom_level
	);

	build_geometry_info.dst = vk::get_handle(blas.handle);
	build_geometry_info.scratch_data.device_address = blas.scratch_buffer.get_device_address();

	command_buffer.begin(command_buffer_usages{ command_buffer_usage::one_time_submit });

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

	return move(blas);
}