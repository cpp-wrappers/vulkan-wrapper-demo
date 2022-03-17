#if 0
src_dir=`dirname ${BASH_SOURCE[0]}`

mkdir -p ${src_dir}/build

#glslangValidator -e main -o ${src_dir}/build/triangle.vert.spv -V ${src_dir}/triangle.vert
#glslangValidator -e main -o ${src_dir}/build/triangle.frag.spv -V ${src_dir}/triangle.frag

. ${src_dir}/../../build.sh $@

exit 0
#endif

#include "platform_implementation.hpp"

#include "create_blas.hpp"
#include "create_tlas.hpp"
#include "create_storage_image.hpp"

#include "vk/physical_device/extension_properties/acceleration_structure.hpp"
#include "vk/physical_device/extension_properties/ray_tracing_pipeline.hpp"

#include "vk/physical_device/features/buffer_device_address.hpp"
#include "vk/physical_device/features/ray_tracing_pipeline.hpp"
#include "vk/physical_device/features/vulkan_1_2.hpp"
#include "vk/physical_device/features/acceleration_structure.hpp"

int main() {
	using namespace vk;

	auto [instance, surface] = platform::create_instance_and_surface(api_version{ major{1}, minor{2} });
	auto physical_device = instance.get_first_physical_device();

	physical_device_acceleration_structure_properties as_props{};
	physical_device_ray_tracing_pipeline_properties rt_props{};

	physical_device.get_properties(as_props, rt_props);

	auto queue_family_index = physical_device.find_first_queue_family_index_with_capabilities(queue_flag::graphics);

	physical_device_buffer_device_address_features buffer_device_address_features {};
	physical_device_ray_tracing_pipeline_features ray_tracing_pipeline_features {};
	physical_device_acceleration_structure_features as_features;
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

	if(!as_features.acceleration_structure) {
		platform::error("acceleration_structure feature is not supported").new_line();
		vk::default_unexpected_handler();
	}

	buffer_device_address_features = physical_device_buffer_device_address_features { .buffer_device_address = true };
	ray_tracing_pipeline_features = physical_device_ray_tracing_pipeline_features { .ray_tracing_pipeline = true };
	as_features = physical_device_acceleration_structure_features{ .acceleration_structure = true };
	features_1_2 = physical_device_vulkan_1_2_features {
		.descriptor_indexing = true,
		.buffer_device_address = true
	};

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
		as_features,
		features_1_2
	);

	auto command_pool = device.create_guarded<vk::command_pool>(
		queue_family_index,
		command_pool_create_flags {
			command_pool_create_flag::reset_command_buffer,
			command_pool_create_flag::transient
		}
	);

	auto command_buffer = command_pool.allocate_guarded<vk::command_buffer>(command_buffer_level::primary);
	auto queue = device.get_queue(queue_family_index, queue_index{ 0 });

	as_t blas = create_blas(device, physical_device, command_buffer, queue);
	as_t tlas = create_tlas(device, physical_device, command_buffer, queue, blas);

	surface_capabilities surface_capabilities = physical_device.get_surface_capabilities(surface);

	storage_image_t storage_image = create_storage_image(surface_capabilities.current_extent, device, physical_device, command_buffer, queue);
}