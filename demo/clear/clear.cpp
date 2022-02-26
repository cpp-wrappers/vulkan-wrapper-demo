#if 0
. ./`dirname ${BASH_SOURCE[0]}`/../build.sh $@
exit 0
#endif

#include "vk/instance/guarded_handle.hpp"
#include "vk/instance/layer_properties.hpp"

#include "platform_implementation.hpp"
#include <string.h>

int main() {
	platform::init();

	using namespace vk;

	auto instance = platform::create_instance();
	vk::handle<vk::physical_device> physical_device = instance.get_first_physical_device();
	auto queue_family_index = physical_device.get_first_queue_family_index_with_capabilities(queue_flag::graphics);
	auto device = physical_device.create_guarded_device(
		queue_family_index,
		queue_priority{ 1.0F },
		extension_name { "VK_KHR_swapchain" }
	);

	auto surface = platform::create_surface(instance);

	if(!physical_device.get_surface_support(surface, queue_family_index)) {
		platform::error("surface is not supported").new_line();
		return 1;
	}

	auto surface_format = physical_device.get_first_surface_format(surface);

	surface_capabilities surface_capabilities = physical_device.get_surface_capabilities(surface);

	auto swapchain = device.create_guarded<vk::swapchain>(
		surface,
		surface_capabilities.min_image_count,
		surface_capabilities.current_extent,
		surface_format,
		image_usages{ image_usage::color_attachment, image_usage::transfer_dst },
		sharing_mode::exclusive,
		present_mode::immediate,
		clipped{ true },
		surface_transform::identity,
		composite_alpha::opaque
	);

	uint32 images_count = (uint32)swapchain.get_image_count();
	handle<image> images_storage[images_count];
	span<handle<image>> images{ images_storage, images_count };

	swapchain.get_images(images);

	auto command_pool = device.create_guarded<vk::command_pool>(queue_family_index);

	handle<command_buffer> command_buffers_storage[images_count];
	span<handle<command_buffer>> command_buffers{ command_buffers_storage, images_count };

	command_pool.allocate_command_buffers(command_buffer_level::primary, command_buffers);

	image_subresource_range image_subresource_range { image_aspects{ image_aspect::color } };

	for(nuint i = 0; i < images_count; ++i) {
		auto command_buffer = command_buffers[i];

		image_memory_barrier image_memory_barrier_from_present_to_clear {
			.src_access = src_access{ access::memory_read },
			.dst_access = dst_access{ access::transfer_write },
			.old_layout = old_layout{ image_layout::undefined },
			.new_layout = new_layout{ image_layout::transfer_dst_optimal },
			.src_queue_family_index{ VK_QUEUE_FAMILY_IGNORED },
			.dst_queue_family_index{ VK_QUEUE_FAMILY_IGNORED },
			.image = images[i],
			.subresource_range = image_subresource_range
		};

		image_memory_barrier image_memory_barrier_from_clear_to_present {
			.src_access = src_access{ access::transfer_write },
			.dst_access = dst_access{ access::memory_read },
			.old_layout = old_layout{ image_layout::transfer_dst_optimal },
			.new_layout = new_layout{ image_layout::present_src_khr },
			.src_queue_family_index{ VK_QUEUE_FAMILY_IGNORED },
			.dst_queue_family_index{ VK_QUEUE_FAMILY_IGNORED },
			.image = images[i],
			.subresource_range = image_subresource_range
		};

		command_buffer.begin(command_buffer_usage::simultaneius_use);
		command_buffer.cmd_pipeline_barrier(
			src_stages { pipeline_stage::transfer },
			dst_stages { pipeline_stage::transfer },
			dependencies{},
			array{ image_memory_barrier_from_present_to_clear }
		);
		command_buffer.cmd_clear_color_image(
			images[i],
			image_layout::transfer_dst_optimal,
			clear_color_value{ 1.0, 0.0, 0.0, 0.0 },
			array{ image_subresource_range }
		);
		command_buffer.cmd_pipeline_barrier(
			src_stages { pipeline_stage::transfer },
			dst_stages { pipeline_stage::bottom_of_pipe },
			dependencies{},
			array{ image_memory_barrier_from_clear_to_present }
		);
		command_buffer.end();
	}

	auto swapchain_image_semaphore = device.create_guarded<vk::semaphore>();
	auto rendering_finished_semaphore = device.create_guarded<vk::semaphore>();

	auto presentation_queue = device.get_queue(queue_family_index, vk::queue_index{ 0 });

	while (!platform::should_close()) {
		platform::begin();

		auto result = swapchain.try_acquire_next_image(swapchain_image_semaphore);
		if(result.is_unexpected()) {
			if(result.get_unexpected().suboptimal()) break;
			platform::error("can't acquire swapchain image").new_line();
			return 1;
		}

		vk::image_index image_index = result;

		pipeline_stages wait_dst_stage_mask{ pipeline_stage::transfer };

		presentation_queue.submit(
			wait_semaphore{ swapchain_image_semaphore.handle() },
			pipeline_stages{ pipeline_stage::transfer },
			command_buffers[(uint32)image_index],
			signal_semaphore{ rendering_finished_semaphore.handle() }
		);

		presentation_queue.present(
			wait_semaphore{ rendering_finished_semaphore.handle() },
			swapchain.handle(),
			image_index
		);

		platform::end();
	}

	device.wait_idle();

	command_pool.free_command_buffers(command_buffers);
}