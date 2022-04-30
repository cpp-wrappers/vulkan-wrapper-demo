#if 0
src_dir=`dirname ${BASH_SOURCE[0]}`

mkdir -p ${src_dir}/build

glslangValidator -e main -o ${src_dir}/build/triangle.vert.spv -V ${src_dir}/triangle.vert
glslangValidator -e main -o ${src_dir}/build/triangle.frag.spv -V ${src_dir}/triangle.frag

. ${src_dir}/../build.sh $@ --asset triangle.vert.spv --asset triangle.frag.spv

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
		queue_family, extension{ "VK_KHR_swapchain" }
	);

	auto surface_format = physical_device.get_first_surface_format(surface);

	array refs {
		color_attachment_reference{ 0, image_layout::color_attachment_optimal }
	};

	auto render_pass = device.create<vk::render_pass>(
		array {
	/* 0 */ attachment_description {
				surface_format.format,
				load_op{ attachment_load_op::clear },
				store_op{ attachment_store_op::store },
				final_layout{ image_layout::present_src }
			}
		},
		array {
	/* 0 */ subpass_description{ refs }
		},
		array {
			subpass_dependency {
				src_subpass{ subpass_external },
				dst_subpass{ 0 },
				src_stages{ pipeline_stage::color_attachment_output },
				dst_stages{ pipeline_stage::color_attachment_output }
			}
		}
	);

	auto vertex_shader = platform::read_shader_module(
		device, "triangle.vert.spv"
	);

	auto fragment_shader = platform::read_shader_module(
		device, "triangle.frag.spv"
	);

	pipeline_color_blend_attachment_state pcbas {
		enable_blend{ false },
		src_color_blend_factor{ blend_factor::one },
		dst_color_blend_factor{ blend_factor::zero },
		color_blend_op{ blend_op::add },
		src_alpha_blend_factor{ blend_factor::one },
		dst_alpha_blend_factor{ blend_factor::zero },
		alpha_blend_op{ blend_op::add },
	};

	auto pipeline_layout = device.create<vk::pipeline_layout>();

	array dynamic_states { dynamic_state::viewport, dynamic_state::scissor };

	auto pipeline = device.create<vk::pipeline>(
		render_pass, subpass{ 0 }, pipeline_layout,
		primitive_topology::triangle_list,
		array {
			pipeline_shader_stage_create_info {
				shader_stage::vertex,
				vertex_shader,
				entrypoint_name{ "main" }
			},
			pipeline_shader_stage_create_info {
				shader_stage::fragment,
				fragment_shader,
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
			viewport_count{ 1 }, scissor_count{ 1 }
		},
		pipeline_dynamic_state_create_info { dynamic_states }
	);

	auto command_pool = device.create<vk::command_pool>(queue_family);
	handle<vk::swapchain> swapchain{};

	auto queue = device.get_queue(queue_family, queue_index{ 0 });

	while(!platform::should_close()) {
		auto surface_caps {
			physical_device.get_surface_capabilities(surface)
		};

		swapchain = device.create<vk::swapchain>(
			surface,
			surface_caps.min_image_count,
			surface_caps.current_extent,
			surface_format,
			image_usages {
				image_usage::color_attachment,
				image_usage::transfer_dst
			},
			sharing_mode::exclusive,
			present_mode::fifo,
			clipped{ true },
			surface_transform::identity,
			composite_alpha::opaque,
			swapchain
		);

		uint32 images_count = device.get_swapchain_image_count(swapchain);

		handle<image> images_storage[images_count];
		images_count = device.get_swapchain_images(
			swapchain, span{ images_storage, images_count }
		);
		span images{ images_storage, images_count };

		handle<image_view> image_views_raw[images_count];
		span image_views{ image_views_raw, images_count };

		for(nuint i = 0; i < images_count; ++i) {
			image_views[i] = device.create<image_view>(
				images[i],
				surface_format.format,
				image_view_type::two_d
			);
		}

		handle<framebuffer> framebuffers_raw[images_count];
		span framebuffers{ framebuffers_raw, images_count };

		for(nuint i = 0; i < images_count; ++i) {
			framebuffers[i] = device.create<framebuffer>(
				render_pass,
				array{ image_views[i] },
				extent<3>{ surface_caps.current_extent, 1 }
			);
		}

		handle<command_buffer> command_buffers_storage[images_count];
		span command_buffers{ command_buffers_storage, images_count };
		vk::allocate_command_buffers(
			device,
			command_pool,
			command_buffer_level::primary,
			command_buffers
		);

		for(nuint i = 0; i < images_count; ++i) {
			auto command_buffer = command_buffers[i];

			command_buffer
				.begin(command_buffer_usage::simultaneius_use)
				.cmd_begin_render_pass(
					render_pass, framebuffers[i],
					render_area{ surface_caps.current_extent },
					array {
						clear_value { clear_color_value{ 0.0, 0.0, 0.0, 0.0 } }
					}
				)
				.cmd_bind_pipeline(pipeline, pipeline_bind_point::graphics)
				.cmd_set_viewport(surface_caps.current_extent)
				.cmd_set_scissor(surface_caps.current_extent)
				.cmd_draw(vertex_count{ 3 })
				.cmd_end_render_pass()
				.end();
		}

		auto swapchain_image_semaphore = device.create<semaphore>();
		auto rendering_finished_semaphore = device.create<semaphore>();

		while (!platform::should_close()) {
			platform::begin();

			auto result = device.try_acquire_next_image(
				swapchain, swapchain_image_semaphore
			);

			if(result.is_unexpected()) {
				if(
					result.get_unexpected().suboptimal() ||
					result.get_unexpected().out_of_date()
				) break;

				platform::error("acquire next image").new_line();
				return 1;
			}

			image_index image_index = result;

			queue.submit(
				wait_semaphore{ swapchain_image_semaphore },
				pipeline_stages{ pipeline_stage::color_attachment_output },
				command_buffers[image_index],
				signal_semaphore{ rendering_finished_semaphore }
			);

			vk::result present_result = queue.try_present(
				wait_semaphore{ rendering_finished_semaphore },
				swapchain,
				image_index
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

		device.wait_idle();

		device.free_command_buffers(command_pool, command_buffers);
	}
}