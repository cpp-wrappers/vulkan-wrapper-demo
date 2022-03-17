#pragma once

#include "platform_implementation.hpp"

struct storage_image_t {
	vk::guarded_handle<vk::image> handle;
	vk::guarded_handle<vk::device_memory> device_memory;
	vk::guarded_handle<vk::image_view> view;
};

inline storage_image_t
create_storage_image(
	vk::extent<2> extent,
	vk::guarded_handle<vk::device>& device,
	vk::handle<vk::physical_device>& physical_device,
	vk::guarded_handle<vk::command_buffer>& command_buffer,
	vk::handle<vk::queue> queue
) {
	using namespace vk;

	storage_image_t img{};

	img.handle = device.create_guarded<vk::image>(
		image_create_flags{},
		image_type::two_d,
		format::b8_g8_r8_a8_uint,
		vk::extent<3>{ extent.width(), extent.height(), 1 },
		mip_levels{ 1 },
		array_layers{ 1 },
		sample_count{ 1 },
		image_tiling::optimal,
		image_usages{ image_usage::transfer_src, image_usage::storage },
		sharing_mode::exclusive,
		span<vk::queue_family_index>{ nullptr, 0 },
		initial_layout{ image_layout::undefined }
	);

	auto memory_requirements = img.handle.get_memory_requirements();

	img.device_memory = device.allocate_guarded<vk::device_memory>(
		physical_device.find_first_memory_type_index(
			memory_property::device_local,
			memory_requirements.memory_type_indices
		),
		memory_requirements.size
	);

	img.handle.bind_memory(img.device_memory);

	image_subresource_range subresource_range {
		image_aspects{ image_aspect::color }
	};

	img.view = device.create_guarded<vk::image_view>(
		image_view_type::two_d,
		format::b8_g8_r8_a8_uint,
		subresource_range,
		get_handle(img.handle),
		component_mapping{}
	);

	command_buffer.begin(command_buffer_usage::one_time_submit);
	command_buffer.cmd_pipeline_barrier(
		src_stages{ pipeline_stage::all_commands },
		dst_stages{ pipeline_stage::all_commands },
		array{
			image_memory_barrier {
				.new_layout{ image_layout::general },
				.image{ get_handle(img.handle) },
				.subresource_range = subresource_range
			}
		}
	);
	command_buffer.end();
	queue.submit(command_buffer);

	device.wait_idle();

	return img;
}