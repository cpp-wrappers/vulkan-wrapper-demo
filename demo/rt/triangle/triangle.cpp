#if 0
src_dir=`dirname ${BASH_SOURCE[0]}`

mkdir -p ${src_dir}/build

#glslangValidator -e main -o ${src_dir}/build/triangle.vert.spv -V ${src_dir}/triangle.vert
#glslangValidator -e main -o ${src_dir}/build/triangle.frag.spv -V ${src_dir}/triangle.frag

. ${src_dir}/../../build.sh $@

exit 0
#endif

#include "platform_implementation.hpp"

#include "vk/physical_device/extension_properties/acceleration_structure.hpp"
#include "vk/physical_device/extension_properties/ray_tracing_pipeline.hpp"
#include "vk/physical_device/features/buffer_device_address.hpp"
#include "vk/physical_device/features/ray_tracing_pipeline.hpp"
#include "vk/physical_device/features/vulkan_1_2.hpp"
#include "vk/acceleration_structure/build_geometry_info.hpp"
#include "vk/acceleration_structure/get_build_sizes.hpp"
#include "vk/acceleration_structure/transform_matrix.hpp"

int main() {
	using namespace vk;

	auto instance_and_surface = platform::create_instance_and_surface(vk::api_version{ vk::major{1}, vk::minor{2} });
	auto instance = instance_and_surface.get<handle<vk::instance>>();
	auto surface = instance_and_surface.get<handle<vk::surface>>();

	auto physical_device = instance.get_first_physical_device();

	physical_device_acceleration_structure_properties as_props{};
	physical_device_ray_tracing_pipeline_properties rt_props{};

	physical_device.get_properties(as_props, rt_props);

	auto queue_family_index = physical_device.find_first_queue_family_index_with_capabilities(queue_flag::graphics);

	physical_device_buffer_device_address_features buffer_device_address_features {};
	physical_device_ray_tracing_pipeline_features ray_tracing_pipeline_features {};
	physical_device_vulkan_1_2_features features_1_2{};

	physical_device.get_features(features_1_2, buffer_device_address_features, ray_tracing_pipeline_features);

	if(!buffer_device_address_features.buffer_device_address || !features_1_2.buffer_device_address) {
		platform::error("buffer_device_address feature is not supported").new_line();
		vk::default_unexpected_handler();
	}

	if(!ray_tracing_pipeline_features.ray_tracing_pipeline) {
		platform::error("ray_tracing_pipeline feature is not supported").new_line();
		vk::default_unexpected_handler();
	}

	buffer_device_address_features = physical_device_buffer_device_address_features { .buffer_device_address = true };
	features_1_2 = physical_device_vulkan_1_2_features { .buffer_device_address = true };
	ray_tracing_pipeline_features = physical_device_ray_tracing_pipeline_features { .ray_tracing_pipeline = true };

	auto device = physical_device.create_guarded_device(
		queue_family_index,
		queue_priority{ 1.0F },
		extension_name{ "VK_KHR_swapchain" },
		extension_name{ "VK_KHR_acceleration_structure" },
		extension_name{ "VK_KHR_ray_tracing_pipeline" },
		extension_name{ "VK_KHR_buffer_device_address" },
		extension_name{ "VK_KHR_deferred_host_operations" },
		extension_name{ "VK_EXT_descriptor_indexing" },
		buffer_device_address_features,
		ray_tracing_pipeline_features,
		features_1_2
	);

	struct data_t {
		math::vector<float, 3> vertices[3];
		uint32 indices[3];
		transform_matrix matrix {{
			{ 1.0F, 0.0F, 0.0F, 0.0F },
			{ 0.0F, 1.0F, 0.0F, 0.0F },
			{ 0.0F, 0.0F, 1.0F, 0.0F }
		}};
	};

	auto vertex_buffer = device.create_guarded<vk::buffer>(
		buffer_usages {
			buffer_usage::shader_device_address,
			buffer_usage::acceleration_structure_build_input_read_only
		},
		buffer_size{ sizeof data_t::vertices },
		sharing_mode::exclusive
	);

	auto index_buffer = device.create_guarded<vk::buffer>(
		buffer_usages {
			buffer_usage::shader_device_address,
			buffer_usage::acceleration_structure_build_input_read_only
		},
		buffer_size{ sizeof data_t::indices },
		sharing_mode::exclusive
	);

	auto transform_buffer = device.create_guarded<vk::buffer>(
		buffer_usages {
			buffer_usage::shader_device_address,
			buffer_usage::acceleration_structure_build_input_read_only
		},
		buffer_size{ sizeof data_t::matrix },
		sharing_mode::exclusive
	);

	auto device_local_memory = device.allocate_guarded<vk::device_memory>(
		physical_device.find_first_memory_type_index(
			memory_properties {
				memory_property::host_visible,
				memory_property::host_coherent
			},
			device.get_buffer_memory_requirements(vertex_buffer).memory_type_indices
			&
			device.get_buffer_memory_requirements(index_buffer).memory_type_indices
			&
			device.get_buffer_memory_requirements(transform_buffer).memory_type_indices
		),
		memory_size{ 64 * 1024 * 1024 },
		memory_allocate_flags_info {
			.flags = memory_allocate_flags {
				memory_allocate_flag::address
			}
		}
	);

	vertex_buffer.bind_memory(device_local_memory,  memory_offset{ offsetof(data_t, vertices) });
	index_buffer.bind_memory(device_local_memory, memory_offset{ offsetof(data_t, indices) });
	transform_buffer.bind_memory(device_local_memory, memory_offset{ offsetof(data_t, matrix) });

	acceleration_structure_geometry_triangles_data triangles_data {
		.format = format::r32_g32_b32_sfloat,
		.vertex_data{ vertex_buffer.get_device_address() },
		.vertex_stride{ sizeof data_t::vertices },
		.max_vertex = 3,
		.index_type = index_type::uint32,
		.index_data{ index_buffer.get_device_address() },
		.transform_data{ transform_buffer.get_device_address() }
	};

	acceleration_structure_geometry_data geometry_data {
		.triangles = triangles_data
	};

	acceleration_structure_geometry geometry {
		.type = vk::geometry_type::triangles,
		.geometry = geometry_data,
		.flags = geometry_flag::opaque
	};

	acceleration_structure_build_geometry_info geometry_info {
		.type = acceleration_structure_type::bottom_level,
		.geometry_count = 1,
		.geometries = &geometry
	};

	const uint32 primitive_counts = 1;

	auto build_sizes = vk::get_acceleration_structure_build_sizes(
		device,
		acceleration_structure_build_type::device,
		geometry_info,
		&primitive_counts
	);

	platform::info((uint64) build_sizes.acceleration_structure_size).new_line();
	platform::info((uint64) build_sizes.update_scratch_size).new_line();
	platform::info((uint64) build_sizes.build_scratch_size).new_line();

}