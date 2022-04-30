#if 0
. ./`dirname ${BASH_SOURCE[0]}`/../build.sh $@
exit 0
#endif

#include "platform_implementation.hpp"

int main() {
	using namespace vk;

	auto [instance, surface] = platform::create_instance_and_surface();
	auto physical_device = instance.get_first_physical_device();
	auto queue_family = physical_device.find_first_graphics_queue_family();

	if(!physical_device.get_surface_support(surface, queue_family)) {
		platform::error("surface isn't supported").new_line();
		return 1;
	}

	auto device = physical_device.create<vk::device>(
		queue_family,
		extension { "VK_KHR_swapchain" }
	);

	auto surface_format = physical_device.get_first_surface_format(surface);

	auto surface_capabilities {
		physical_device.get_surface_capabilities(surface)
	};

	auto swapchain = device.create<vk::swapchain>(
		surface,
		surface_capabilities.min_image_count,
		surface_capabilities.current_extent,
		surface_format,
		image_usages {
			image_usage::color_attachment,
			image_usage::transfer_dst
		},
		sharing_mode::exclusive,
		present_mode::immediate,
		clipped{ true },
		surface_transform::identity,
		composite_alpha::opaque
	);

	uint32 images_count = (uint32)device.get_swapchain_image_count(swapchain);
	handle<image> images_storage[images_count];
	images_count = device.get_swapchain_images(
		swapchain, span{ images_storage, images_count }
	);
	span images{ images_storage, images_count };

	auto command_pool = device.create<vk::command_pool>(queue_family);

	handle<command_buffer> command_buffers_storage[images_count];
	span<handle<command_buffer>> command_buffers{
		command_buffers_storage,
		images_count
	};

	vk::allocate_command_buffers(
		device,
		command_pool,
		command_buffer_level::primary,
		command_buffers
	);

	image_subresource_range image_subresource_range {
		image_aspects{ image_aspect::color }
	};

	for(nuint i = 0; i < images_count; ++i) {
		auto command_buffer = command_buffers[i];

		image_memory_barrier image_memory_barrier_from_present_to_clear {
			.src_access = src_access{ access::memory_read },
			.dst_access = dst_access{ access::transfer_write },
			.old_layout = old_layout{ image_layout::undefined },
			.new_layout = new_layout{ image_layout::transfer_dst_optimal },
			.src_queue_family_index{ vk::queue_family_ignored },
			.dst_queue_family_index{ vk::queue_family_ignored },
			.image = images[i],
			.subresource_range = image_subresource_range
		};

		image_memory_barrier image_memory_barrier_from_clear_to_present {
			.src_access = src_access{ access::transfer_write },
			.dst_access = dst_access{ access::memory_read },
			.old_layout = old_layout{ image_layout::transfer_dst_optimal },
			.new_layout = new_layout{ image_layout::present_src },
			.src_queue_family_index{ vk::queue_family_ignored },
			.dst_queue_family_index{ vk::queue_family_ignored },
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

	auto swapchain_image_semaphore = device.create<vk::semaphore>();
	auto rendering_finished_semaphore = device.create<vk::semaphore>();

	auto presentation_queue {
		device.get_queue(queue_family, vk::queue_index{ 0 })
	};

	while (!platform::should_close()) {
		platform::begin();

		auto result {
			device.try_acquire_next_image(swapchain, swapchain_image_semaphore)
		};

		if(result.is_unexpected()) {
			if(result.get_unexpected().suboptimal()) break;
			platform::error("can't acquire swapchain image").new_line();
			return 1;
		}

		vk::image_index image_index = result;

		presentation_queue.submit(
			wait_semaphore{ swapchain_image_semaphore },
			pipeline_stages{ pipeline_stage::transfer },
			command_buffers[(uint32)image_index],
			signal_semaphore{ rendering_finished_semaphore }
		);

		presentation_queue.present(
			wait_semaphore{ rendering_finished_semaphore },
			swapchain,
			image_index
		);

		platform::end();
	}

	device.wait_idle();

	device.free_command_buffers(command_pool, command_buffers);
}