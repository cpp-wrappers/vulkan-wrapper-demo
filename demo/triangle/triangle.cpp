#if 0
src_dir=`dirname ${BASH_SOURCE[0]}`

mkdir -p ${src_dir}/build

glslangValidator -e main -o ${src_dir}/build/triangle.vert.spv -V ${src_dir}/triangle.vert
glslangValidator -e main -o ${src_dir}/build/triangle.frag.spv -V ${src_dir}/triangle.frag

. ${src_dir}/../build.sh $@ --asset triangle.vert.spv --asset triangle.frag.spv

exit 0
#endif

#include "platform.hpp"

void entrypoint() {
	using namespace vk;

	auto instance = platform::create_instance();
	auto surface = platform::create_surface(instance);
	handle<physical_device> physical_device = instance.get_first_physical_device();
	auto queue_family_index = physical_device.get_first_queue_family_index_with_capabilities(queue_flag::graphics);

	platform::info("graphics family index: ", (uint32)queue_family_index).new_line();

	if(!physical_device.get_surface_support(surface, queue_family_index)) {
		platform::error("surface isn't supported").new_line();
		return;
	}

	auto device = physical_device.create_guarded_device(
		queue_family_index,
		queue_priority{ 1.0F },
		extension_name{ "VK_KHR_swapchain" }
	);

	surface_format surface_format = physical_device.get_first_surface_format(surface);

	array color_attachments {
		color_attachment_reference{ 0, image_layout::color_attachment_optimal }
	};

	auto render_pass = device.create_guarded<vk::render_pass>(
		array{ subpass_description{ color_attachments } },
		array{
			attachment_description {
				surface_format.format,
				load_op{ attachment_load_op::clear },
				store_op{ attachment_store_op::store },
				final_layout{ image_layout::present_src_khr }
			}
		},
		array{
			subpass_dependency {
				src_subpass{ VK_SUBPASS_EXTERNAL },
				dst_subpass{ 0 },
				src_stages{ pipeline_stage::color_attachment_output },
				dst_stages{ pipeline_stage::color_attachment_output }
			}
		}
	);

	auto vertex_shader = platform::read_shader_module(device, "triangle.vert.spv");
	auto fragment_shader = platform::read_shader_module(device, "triangle.frag.spv");

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

	auto pipeline_layout = device.create_guarded<vk::pipeline_layout>();

	array dynamic_states { dynamic_state::viewport, dynamic_state::scissor };

	auto pipeline = device.create_guarded<vk::pipeline>(
		pipeline_layout, render_pass,
		primitive_topology::triangle_list,
		array {
			pipeline_shader_stage_create_info {
				shader_stage::vertex,
				vertex_shader,
				entrypoint_name{ "main" }
			},
			pipeline_shader_stage_create_info {
				shader_stage::fragment,
				fragment_shader.handle(),
				entrypoint_name{ "main" }
			}
		},
		pipeline_multisample_state_create_info{},
		pipeline_vertex_input_state_create_info{},
		pipeline_rasterization_state_create_info {
			polygon_mode::fill,
			cull_mode::back,
			front_face::counter_clockwise
		},
		pipeline_color_blend_state_create_info {
			logic_op::copy,
			span{ &pcbas, 1 }
		},
		pipeline_viewport_state_create_info {
			viewport_count{ 1 },
			scissor_count{ 1 }
		},
		pipeline_dynamic_state_create_info {
			dynamic_states
		},
		subpass{ 0 }
	);

	auto command_pool = device.create_guarded<vk::command_pool>(queue_family_index);
	auto swapchain = guarded_handle<vk::swapchain>{};

	auto queue = device.get_queue(queue_family_index, queue_index{ 0 });

	while(!platform::should_close()) {
		surface_capabilities surface_capabilities = physical_device.get_surface_capabilities(surface);

		{
			auto old_swapchain = move(swapchain);

			swapchain = device.create_guarded<vk::swapchain>(
				surface,
				surface_capabilities.min_image_count,
				surface_capabilities.current_extent,
				surface_format,
				image_usages{ image_usage::color_attachment, image_usage::transfer_dst },
				sharing_mode::exclusive,
				present_mode::mailbox,
				clipped{ true },
				surface_transform::identity,
				composite_alpha::opaque,
				old_swapchain
			);
		}

		uint32 images_count = (uint32)swapchain.get_image_count();

		handle<image> images_storage[images_count];
		span images{ images_storage, images_count };
		swapchain.get_images(images);

		guarded_handle<image_view> image_views_raw[images_count];
		span image_views{ image_views_raw, images_count };

		for(nuint i = 0; i < images_count; ++i) {
			image_views[i] = device.create_guarded<image_view>(
				images[i],
				surface_format.format,
				image_view_type::two_d,
				component_mapping{},
				image_subresource_range{ image_aspects{ image_aspect::color } }
			);
		}

		guarded_handle<framebuffer> framebuffers_raw[images_count];
		span framebuffers{ framebuffers_raw, images_count };

		for(nuint i = 0; i < images_count; ++i) {
			framebuffers[i] = device.create_guarded<framebuffer>(
				render_pass,
				array{ handle<image_view>{ image_views[i].handle() } },
				extent<3>{ surface_capabilities.current_extent.width(), surface_capabilities.current_extent.height(), 1 }
			);
		}

		handle<command_buffer> command_buffers_storage[images_count];
		span command_buffers{ command_buffers_storage, images_count };
		command_pool.allocate_command_buffers(command_buffer_level::primary, command_buffers);

		for(nuint i = 0; i < images_count; ++i) {
			auto command_buffer = command_buffers[i];

			command_buffer
				.begin(command_buffer_usage::simultaneius_use)
				.cmd_begin_render_pass(
					render_pass, framebuffers[i],
					render_area{ surface_capabilities.current_extent },
					array{ clear_value { clear_color_value{ 0.0, 0.0, 0.0, 0.0 } } }
				)
				.cmd_bind_pipeline(pipeline, pipeline_bind_point::graphics)
				.cmd_set_viewport(surface_capabilities.current_extent)
				.cmd_set_scissor(surface_capabilities.current_extent)
				.cmd_draw(vertex_count{ 3 })
				.cmd_end_render_pass()
				.end();
		}

		auto swapchain_image_semaphore = device.create_guarded<semaphore>();
		auto rendering_finished_semaphore = device.create_guarded<semaphore>();

		while (!platform::should_close()) {
			platform::begin();

			auto result = swapchain.try_acquire_next_image(swapchain_image_semaphore);
			if(result.is_unexpected()) {
				if(result.get_unexpected().suboptimal() || result.get_unexpected().out_of_date()) break;
				platform::error("acquire next image").new_line();
				return;
			}

			image_index image_index = result;

			queue.submit(
				wait_semaphore{ swapchain_image_semaphore },
				pipeline_stages{ pipeline_stage::color_attachment_output },
				command_buffers[(uint32)image_index],
				signal_semaphore{ rendering_finished_semaphore }
			);

			vk::result present_result = queue.try_present(wait_semaphore{ rendering_finished_semaphore }, swapchain, image_index);

			if(!present_result.success()) {
				if(present_result.suboptimal() || present_result.out_of_date()) break;
				platform::error("present").new_line();
				return;
			}

			platform::end();
		}

		device.wait_idle();

		command_pool.free_command_buffers(command_buffers);
	}
}