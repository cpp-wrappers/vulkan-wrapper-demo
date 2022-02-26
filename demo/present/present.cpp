#if 0
src_dir=`dirname ${BASH_SOURCE[0]}`

mkdir -p ${src_dir}/build

glslangValidator -V100 -e main -o ${src_dir}/build/present.vert.spv -V ${src_dir}/present.vert
glslangValidator -V100 -e main -o ${src_dir}/build/present.frag.spv -V ${src_dir}/present.frag

. ${src_dir}/../build.sh $@ --asset present.vert.spv --asset present.frag.spv

exit 0
#endif

#include "platform.hpp"

//#include <glm/mat4x4.hpp>
//#include <glm/ext/matrix_clip_space.hpp>
//#include <glm/ext/matrix_transform.hpp>
//#include <glm/gtx/transform.hpp>
#include <math/geometry/frustum.hpp>

#include <time.h>

static timespec start_time{};

void entrypoint() {
	using namespace vk;

	clock_gettime(CLOCK_REALTIME, &start_time);

	auto instance = platform::create_instance();
	auto surface = platform::create_surface(instance);
	vk::handle<vk::physical_device> physical_device = instance.get_first_physical_device();
	auto queue_family_index = physical_device.get_first_queue_family_index_with_capabilities(vk::queue_flag::graphics);

	if(!physical_device.get_surface_support(surface, queue_family_index)) {
		platform::error("surface isn't supported by queue family").new_line();
		return;
	}

	auto device = physical_device.create_guarded_device(
		queue_family_index,
		queue_priority{ 1.0 },
		extension_name{ "VK_KHR_swapchain" }
	);

	struct uniform_info_t {
		math::matrix<float, 4, 4> proj_view_inversed;
		float t;
	};

	auto uniform_buffer = device.create_guarded<vk::buffer>(
		buffer_size{ sizeof(uniform_info_t) },
		buffer_usages{ buffer_usage::uniform_buffer, buffer_usage::transfer_dst },
		sharing_mode::exclusive
	);

	auto uniform_buffer_memory_requirements = device.get_buffer_memory_requirements(uniform_buffer);

	auto memory_for_uniform_buffer = device.allocate_guarded<device_memory>(
		memory_size{ uniform_buffer_memory_requirements.size },
		physical_device.get_index_of_first_memory_type(
			memory_properties{ memory_property::device_local },
			uniform_buffer_memory_requirements.memory_type_indices
		)
	);

	uniform_buffer.bind_memory(memory_for_uniform_buffer);

	auto staging_uniform_buffer = device.create_guarded<buffer>(
		buffer_size{ sizeof(uniform_info_t) },
		buffer_usages{ buffer_usage::transfer_src },
		sharing_mode::exclusive
	);

	auto staging_uniform_buffer_memory_requirements = device.get_buffer_memory_requirements(staging_uniform_buffer);

	auto memory_for_staging_uniform_buffer = device.allocate_guarded<device_memory>(
		memory_size{ staging_uniform_buffer_memory_requirements.size },
		physical_device.get_index_of_first_memory_type(
			memory_properties{ memory_property::host_visible },
			staging_uniform_buffer_memory_requirements.memory_type_indices
		)
	);

	staging_uniform_buffer.bind_memory(memory_for_staging_uniform_buffer);

	uniform_info_t* uniform_info_ptr;
	memory_for_staging_uniform_buffer.map(
		whole_size,
		(void**) &uniform_info_ptr
	);

	auto set_layout = device.create_guarded<descriptor_set_layout>(
		array {
			descriptor_set_layout_binding {
				descriptor_binding{ 0 },
				descriptor_type::uniform_buffer,
				shader_stages{ shader_stage::fragment }
			}
		}
	);

	auto descriptor_pool = device.create_guarded<vk::descriptor_pool>(
		max_sets{ 1 },
		array {
			descriptor_pool_size {
				descriptor_type::uniform_buffer,
				descriptor_count{ 1 }
			}
		}
	);

	auto set = descriptor_pool.allocate_descriptor_set(set_layout);

	device.update_descriptor_set(
		write_descriptor_set {
			set,
			dst_binding{ 0 },
			dst_array_element{ 0 },
			descriptor_type::uniform_buffer,
			array {
				descriptor_buffer_info {
					.buffer = get_handle(uniform_buffer),
					.size{ whole_size }
				}
			}
		}
	);

	auto pipeline_layout = device.create_guarded<vk::pipeline_layout>(
		array{ get_handle(set_layout) }
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

	vk::surface_format surface_format = physical_device.get_first_surface_format(surface);

	array color_attachments {
		color_attachment_reference{ 0, image_layout::color_attachment_optimal }
	};

	auto render_pass = device.create_guarded<vk::render_pass>(
		array{ subpass_description {
			color_attachments
		} },
		array{ subpass_dependency {
			src_subpass{ VK_SUBPASS_EXTERNAL },
			dst_subpass{ 0 },
			src_stages{ pipeline_stage::color_attachment_output },
			dst_stages{ pipeline_stage::color_attachment_output }
		} },
		array{ attachment_description {
			surface_format.format,
			load_op{ attachment_load_op::clear },
			store_op{ attachment_store_op::store },
			final_layout{ image_layout::present_src_khr }
		} }
	);

	auto vertex_shader = platform::read_shader_module(device, "present.vert.spv");
	auto fragment_shader = platform::read_shader_module(device, "present.frag.spv");

	auto pipeline = device.create_guarded<vk::pipeline>(
		subpass{ 0 },
		pipeline_layout, render_pass,
		primitive_topology::triangle_strip,
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
		pipeline_vertex_input_state_create_info {},
		pipeline_rasterization_state_create_info {
			polygon_mode::fill,
			cull_mode::back,
			front_face::counter_clockwise
		},
		pipeline_color_blend_state_create_info {
			span{ &pcbas, 1 }
		},
		pipeline_viewport_state_create_info {
			viewport_count{ 1 },
			scissor_count{ 1 }
		},
		pipeline_dynamic_state_create_info { dynamic_states }
	);

	auto command_pool = device.create_guarded<vk::command_pool>(
		queue_family_index,
		command_pool_create_flags {
			command_pool_create_flag::reset_command_buffer,
			command_pool_create_flag::transient
		}
	);

	struct rendering_resource {
		guarded_handle<vk::command_buffer> command_buffer;
		guarded_handle<vk::semaphore> image_acquire;
		guarded_handle<vk::semaphore> finish;
		guarded_handle<vk::fence> fence;
		guarded_handle<vk::framebuffer> framebuffer;
	};

	array<rendering_resource, 2> rendering_resources{};

	for(auto& rr : rendering_resources) {
		rr.command_buffer = command_pool.allocate_guarded<vk::command_buffer>(command_buffer_level::primary);
	}

	guarded_handle<vk::swapchain> swapchain{};
	auto queue = device.get_queue(queue_family_index, queue_index{ 0 });

	while(!platform::should_close()) {
		vk::surface_capabilities surface_capabilities = physical_device.get_surface_capabilities(surface);

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

		handle<vk::image> images_storage[images_count];
		span images{ images_storage, images_count };
		swapchain.get_images(images);

		guarded_handle<vk::image_view> image_views_raw[images_count];
		span image_views{ image_views_raw, images_count };

		for(nuint i = 0; i < images_count; ++i) {
			image_views[i] = device.create_guarded<vk::image_view>(
				images[i],
				surface_format.format,
				image_view_type::two_d,
				component_mapping{},
				image_subresource_range{ image_aspects{ image_aspect::color } }
			);
		}

		for(auto& rr : rendering_resources) {
			rr.fence = device.create_guarded<vk::fence>(fence_create_flag::signaled);
			rr.image_acquire = device.create_guarded<vk::semaphore>();
			rr.finish = device.create_guarded<vk::semaphore>();
		}

		nuint rendering_resource_index = 0;

		while (!platform::should_close()) {
			auto& rr = rendering_resources[rendering_resource_index];
			
			++rendering_resource_index %= rendering_resources.size();
			
			platform::begin();

			rr.fence.wait();
			rr.fence.reset();

			auto result = swapchain.try_acquire_next_image(rr.image_acquire);
			if(result.is_unexpected()) {
				if(result.get_unexpected().suboptimal() || result.get_unexpected().out_of_date()) break;
				platform::error("acquire next image").new_line();
				return;
			}

			vk::image_index image_index = result;

			rr.framebuffer = device.create_guarded<vk::framebuffer>(
				render_pass,
				array{ image_views[(uint32)image_index].handle() },
				extent<3>{ surface_capabilities.current_extent.width(), surface_capabilities.current_extent.height(), 1 }
			);

			auto frustum = math::geometry::perspective(
				95.0F / 360.0F * 2 * 3.14F,
				float(surface_capabilities.current_extent.width()) / float(surface_capabilities.current_extent.height()),
				0.1F,
				1000.0F
			);

			uniform_info_ptr->proj_view_inversed = (frustum * platform::view_matrix).inversed().transposed();

			timespec ts{};
			clock_gettime(CLOCK_REALTIME, &ts);
			float t = ts.tv_sec - start_time.tv_sec;
			t += (ts.tv_nsec / 1000000.0F - start_time.tv_nsec / 1000000.0F) / 1000.0F;

			uniform_info_ptr->t = t;
			memory_for_staging_uniform_buffer.flush_mapped(staging_uniform_buffer_memory_requirements.size);

			auto& command_buffer = rr.command_buffer;

			command_buffer
				.begin(vk::command_buffer_usage::one_time_submit)
				.cmd_copy_buffer(
					src_buffer{ get_handle(staging_uniform_buffer) },
					dst_buffer{ get_handle(uniform_buffer) },
					array{
						buffer_copy {
							.size{ sizeof(uniform_info_t) }
						}
					}
				)
				.cmd_pipeline_barrier(
					src_stages{ pipeline_stage::transfer },
					dst_stages{ pipeline_stage::vertex_shader },
					array {
						buffer_memory_barrier {
							src_access{ access::transfer_write },
							dst_access{ access::uniform_read },
							uniform_buffer,
							whole_size
						}
					}
				)
				.cmd_begin_render_pass(
					render_pass, rr.framebuffer,
					render_area{ surface_capabilities.current_extent },
					array{ clear_value { clear_color_value{ 0.0, 0.0, 0.0, 0.0 } } }
				)
				.cmd_bind_pipeline(pipeline, pipeline_bind_point::graphics)
				.cmd_bind_descriptor_set(pipeline_bind_point::graphics, pipeline_layout, set)
				.cmd_set_viewport(surface_capabilities.current_extent)
				.cmd_set_scissor(surface_capabilities.current_extent)
				.cmd_draw(vertex_count{ 4 })
				.cmd_end_render_pass()
				.end();

			queue.submit(
				command_buffer,
				wait_semaphore{ rr.image_acquire }, signal_semaphore{ rr.finish },
				pipeline_stages{ pipeline_stage::color_attachment_output },
				rr.fence
			);

			vk::result present_result = queue.try_present(
				wait_semaphore{ rr.finish },
				swapchain,
				image_index
			);

			if(present_result.suboptimal() || present_result.out_of_date()) {
				break;
			}
			if(present_result.error()) {
				platform::error("present").new_line();
				return;
			}

			platform::end();
		}

		device.wait_idle();
	}
}