#if 0
src_dir=`dirname ${BASH_SOURCE[0]}`

mkdir -p ${src_dir}/build

glslangValidator -e main -o ${src_dir}/build/draw.vert.spv -V ${src_dir}/draw.vert
glslangValidator -e main -o ${src_dir}/build/draw.frag.spv -V ${src_dir}/draw.frag

. ${src_dir}/../build.sh $@ --asset draw.vert.spv --asset draw.frag.spv

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
		extension{ "VK_KHR_swapchain" }
	);

	auto surface_format = physical_device.get_first_surface_format(surface);

	auto image = device.create<vk::image>(
		image_type::two_d,
		format::r8_g8_b8_a8_unorm,
		extent<3>{ 100, 100, 1 },
		image_tiling::optimal,
		image_usages { image_usage::storage, image_usage::transfer_src }
	);

	auto image_memory_requirements = device.get_memory_requirements(image);

	auto image_memory = device.allocate<device_memory>(
		image_memory_requirements.size,
		physical_device.find_first_memory_type_index(
			vk::memory_properties { memory_property::device_local },
			image_memory_requirements.memory_type_indices
		)
	);

	device.bind_memory(image, image_memory);

	auto image_view {
		device.create<vk::image_view>(
			image,
			format::r8_g8_b8_a8_unorm,
			image_view_type::two_d
		)
	};

	array color_attachment {
		color_attachment_reference{ 0, image_layout::color_attachment_optimal }
	};

	auto render_pass = device.create<vk::render_pass>(
		subpass_description{ color_attachment },
		array{ attachment_description{
			surface_format.format,
			load_op{ attachment_load_op::clear },
			store_op{ attachment_store_op::store },
			final_layout{ image_layout::present_src }
		}}
	);

	auto descriptor_pool = device.create<vk::descriptor_pool>(
		max_sets{ 1 },
		array{ descriptor_pool_size {
			descriptor_type::storage_image
		}}
	);

	auto set_layout = device.create<descriptor_set_layout>(
		array {
			descriptor_set_layout_binding {
				descriptor_binding{ 0 },
				descriptor_type::storage_image,
				shader_stages { shader_stage::fragment }
			}
		}
	);

	auto descriptor_set {
		device.allocate<vk::descriptor_set>(set_layout, descriptor_pool)
	};

	device.update_descriptor_set(
		write_descriptor_set {
			descriptor_set,
			dst_binding{ 0 },
			dst_array_element{ 0 },
			descriptor_type::storage_image,
			array {
				descriptor_image_info {
					image_view,
					image_layout::general
				}
			}
		}
	);

	auto vert = platform::read_shader_module(device, "draw.vert.spv");
	auto frag = platform::read_shader_module(device, "draw.frag.spv");

	auto pipeline_layout = device.create<vk::pipeline_layout>(
		array{ set_layout }
	);

	array dynamic_states { dynamic_state::viewport, dynamic_state::scissor };

	pipeline_color_blend_attachment_state pcbas {
		enable_blend{ false },
		src_color_blend_factor{ blend_factor::one },
		dst_color_blend_factor{ blend_factor::zero },
		color_blend_op{ blend_op::add },
		src_alpha_blend_factor{ blend_factor::one },
		dst_alpha_blend_factor{ blend_factor::zero },
		alpha_blend_op{ blend_op::add },
		color_components{ color_component::r, color_component::g, color_component::b, color_component::a }
	};

	auto pipeline = device.create<vk::pipeline>(
		pipeline_layout,
		render_pass,
		primitive_topology::triangle_strip,
		subpass{ 0 },
		array {
			pipeline_shader_stage_create_info {
				vert,
				shader_stage::vertex,
				entrypoint_name{ "main" }
			},
			pipeline_shader_stage_create_info {
				frag,
				shader_stage::fragment,
				entrypoint_name{ "main" }
			}
		},
		pipeline_multisample_state_create_info{},
		pipeline_vertex_input_state_create_info {},
		pipeline_rasterization_state_create_info {
			polygon_mode::fill,
			cull_mode::back,
			front_face::counter_clockwise
		},
		pipeline_color_blend_state_create_info {
			single_view{ pcbas }
		},
		pipeline_viewport_state_create_info {
			viewport_count{ 1 },
			scissor_count{ 1 }
		},
		pipeline_dynamic_state_create_info { dynamic_states }
	);

	handle<swapchain> swapchain;

	auto command_pool = device.create<vk::command_pool>(queue_family);
	auto queue = device.get_queue(queue_family, queue_index{ 0 });

	while(!platform::should_close()) {
		auto surface_caps {
			physical_device.get_surface_capabilities(surface)
		};

		swapchain = device.create<vk::swapchain>(
			surface,
			swapchain,
			surface_caps.min_image_count,
			surface_format,
			surface_caps.current_extent,
			image_usages {
				image_usage::color_attachment
			},
			sharing_mode::exclusive,
			present_mode::fifo,
			clipped{ false },
			surface_caps.current_transform,
			composite_alpha::opaque
		);

		uint32 images_count = device.get_swapchain_image_count(swapchain);
		handle<vk::image> swapchain_images[images_count];
		images_count = device.get_swapchain_images(
			swapchain, span{ swapchain_images, images_count }
		);
		span images{ swapchain_images, images_count };

		struct per_image {
			handle<vk::image_view>     view;
			handle<vk::command_buffer> command_buffer;
			handle<vk::framebuffer>    framebuffer;
			handle<vk::fence>          fence;
		};
		per_image per_image_storage[images_count];

		for(nuint index = 0; index < images_count; ++index) {
			per_image_storage[index].view = device.create<vk::image_view>(
				images[index],
				image_view_type::two_d,
				surface_format.format
			);
			per_image_storage[index].command_buffer = {
				device.allocate<vk::command_buffer>(
					command_pool, command_buffer_level::primary
				)
			};

			per_image_storage[index].framebuffer = {
				device.create<vk::framebuffer>(
					render_pass,
					array{ per_image_storage[index].view },
					extent<3>{ surface_caps.current_extent, 1 }
				)
			};

			per_image_storage[index].command_buffer
				.begin()
				.cmd_begin_render_pass(
					render_pass, per_image_storage[index].framebuffer,
					render_area{ surface_caps.current_extent },
					clear_value{ clear_color_value{} }
				)
				.cmd_bind_pipeline(pipeline, pipeline_bind_point::graphics)
				.cmd_set_viewport(surface_caps.current_extent)
				.cmd_set_scissor(surface_caps.current_extent)
				.cmd_draw(vertex_count{ 4 })
				.cmd_end_render_pass()
				.end();

			per_image_storage[index].fence = device.create<vk::fence>();
		}

		auto swapchain_semaphore = device.create<vk::semaphore>();
		auto submit_semaphore = device.create<vk::semaphore>();

		while(!platform::should_close()) {
			platform::begin();

			auto result = device.try_acquire_next_image(
				swapchain, swapchain_semaphore
			);

			if(result.is_unexpected()) {
				if(
					result.get_unexpected().suboptimal() &&
					result.get_unexpected().out_of_date()
				) break;
				platform::error("acquire next image").new_line();
				return 1;
			}

			auto index = result.get_expected();

			device.wait_for_fence(per_image_storage[index].fence);
			device.reset_fence(per_image_storage[index].fence);

			queue.submit(
				per_image_storage[index].command_buffer,
				per_image_storage[index].fence,
				pipeline_stages{ pipeline_stage::color_attachment_output },
				wait_semaphore{ swapchain_semaphore },
				signal_semaphore{ submit_semaphore }
			);

			vk::result present_result = queue.try_present(
				wait_semaphore{ submit_semaphore }, swapchain, index
			);

			if(!present_result.success()) {
				if(
					present_result.suboptimal() ||
					present_result.out_of_date()
				) break;

				platform::error("present").new_line();
				return 1;
			}

			platform::end();
		}
	}
}