#if 0
src_dir=`dirname ${BASH_SOURCE[0]}`

mkdir -p ${src_dir}/build

glslangValidator -V100 -e main -o ${src_dir}/build/sky.vert.spv -V ${src_dir}/sky.vert
glslangValidator -V100 -e main -o ${src_dir}/build/sky.frag.spv -V ${src_dir}/sky.frag

. ${src_dir}/../build.sh $@ --asset sky.vert.spv --asset sky.frag.spv

exit 0
#endif

#include "platform_implementation.hpp"

#include <math/geometry/frustum.hpp>

#include <time.h>

inline timespec start_time{};
inline math::matrix<float, 4, 4> view_matrix;

int main() {
	using namespace vk;

	clock_gettime(CLOCK_REALTIME, &start_time);

	auto instance_and_surface = platform::create_instance_and_surface();

	handle<instance> instance = instance_and_surface.get<handle<vk::instance>>();
	handle<surface> surface = instance_and_surface.get<handle<vk::surface>>();

	handle<physical_device> physical_device = instance.get_first_physical_device();
	auto queue_family_index = physical_device.find_first_queue_family_index_with_capabilities(vk::queue_flag::graphics);

	if(!physical_device.get_surface_support(surface, queue_family_index)) {
		platform::error("surface isn't supported by queue family").new_line();
		return 1;
	}

	auto device = physical_device.create_device(
		queue_family_index,
		queue_priority{ 1.0 },
		extension_name{ "VK_KHR_swapchain" }
	);

	struct uniform_info_t {
		math::matrix<float, 4, 4> proj_view_inversed;
		float t;
	};

	auto uniform_buffer = device.create<vk::buffer>(
		buffer_size{ sizeof(uniform_info_t) },
		buffer_usages{ buffer_usage::uniform_buffer, buffer_usage::transfer_dst },
		sharing_mode::exclusive
	);

	auto uniform_buffer_memory_requirements = device.get_memory_requirements(uniform_buffer);

	auto memory_for_uniform_buffer = device.allocate<device_memory>(
		memory_size{ uniform_buffer_memory_requirements.size },
		physical_device.find_first_memory_type_index(
			memory_properties{ memory_property::device_local },
			uniform_buffer_memory_requirements.memory_type_indices
		)
	);

	device.bind_memory(uniform_buffer, memory_for_uniform_buffer);

	auto staging_uniform_buffer = device.create<buffer>(
		buffer_size{ sizeof(uniform_info_t) },
		buffer_usages{ buffer_usage::transfer_src },
		sharing_mode::exclusive
	);

	auto staging_uniform_buffer_memory_requirements = device.get_memory_requirements(staging_uniform_buffer);

	auto memory_for_staging_uniform_buffer = device.allocate<device_memory>(
		memory_size{ staging_uniform_buffer_memory_requirements.size },
		physical_device.find_first_memory_type_index(
			memory_properties{ memory_property::host_visible },
			staging_uniform_buffer_memory_requirements.memory_type_indices
		)
	);

	device.bind_memory(staging_uniform_buffer, memory_for_staging_uniform_buffer);

	uniform_info_t* uniform_info_ptr;
	device.map_memory(
		memory_for_staging_uniform_buffer,
		whole_size,
		(void**) &uniform_info_ptr
	);

	auto set_layout = device.create<descriptor_set_layout>(
		array {
			descriptor_set_layout_binding {
				descriptor_binding{ 0 },
				descriptor_type::uniform_buffer,
				shader_stages{ shader_stage::fragment }
			}
		}
	);

	auto descriptor_pool = device.create<vk::descriptor_pool>(
		max_sets{ 1 },
		array {
			descriptor_pool_size {
				descriptor_type::uniform_buffer,
				descriptor_count{ 1 }
			}
		}
	);

	auto set = device.allocate<descriptor_set>(descriptor_pool, set_layout);

	device.update_descriptor_set(
		write_descriptor_set {
			set,
			dst_binding{ 0 },
			dst_array_element{ 0 },
			descriptor_type::uniform_buffer,
			array {
				descriptor_buffer_info {
					uniform_buffer,
					whole_size
				}
			}
		}
	);

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

	vk::surface_format surface_format = physical_device.get_first_surface_format(surface);

	array color_attachments {
		color_attachment_reference{ 0, image_layout::color_attachment_optimal }
	};

	auto render_pass = device.create<vk::render_pass>(
		array{ subpass_description {
			color_attachments
		} },
		array{ subpass_dependency {
			src_subpass{ subpass_external},
			dst_subpass{ 0 },
			src_stages{ pipeline_stage::color_attachment_output },
			dst_stages{ pipeline_stage::color_attachment_output }
		} },
		array{ attachment_description {
			surface_format.format,
			load_op{ attachment_load_op::clear },
			store_op{ attachment_store_op::store },
			final_layout{ image_layout::present_src }
		} }
	);

	auto vertex_shader = platform::read_shader_module(device, "sky.vert.spv");
	auto fragment_shader = platform::read_shader_module(device, "sky.frag.spv");

	auto pipeline = device.create<vk::pipeline>(
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

	auto command_pool = device.create<vk::command_pool>(
		queue_family_index,
		command_pool_create_flags {
			command_pool_create_flag::reset_command_buffer,
			command_pool_create_flag::transient
		}
	);

	struct rendering_resource {
		handle<vk::command_buffer> command_buffer;
		handle<vk::semaphore> image_acquire;
		handle<vk::semaphore> finish;
		handle<vk::fence> fence;
		handle<vk::framebuffer> framebuffer;
	};

	array<rendering_resource, 2> rendering_resources{};

	for(auto& rr : rendering_resources) {
		rr.command_buffer = device.allocate<vk::command_buffer>(command_pool, command_buffer_level::primary);
	}

	handle<vk::swapchain> swapchain{};
	auto queue = device.get_queue(queue_family_index, queue_index{ 0 });

	while(!platform::should_close()) {
		vk::surface_capabilities surface_capabilities = physical_device.get_surface_capabilities(surface);

		{
			auto old_swapchain = move(swapchain);

			swapchain = device.create<vk::swapchain>(
				surface,
				surface_capabilities.min_image_count,
				surface_capabilities.current_extent,
				surface_format,
				image_usages{ image_usage::color_attachment, image_usage::transfer_dst },
				sharing_mode::exclusive,
				present_mode::fifo,
				clipped{ true },
				surface_transform::identity,
				composite_alpha::opaque,
				old_swapchain
			);
		}

		uint32 images_count = (uint32) device.get_swapchain_image_count(swapchain);

		handle<vk::image> images_storage[images_count];
		span images{ images_storage, images_count };
		device.get_swapchain_images(swapchain, images);

		handle<vk::image_view> image_views_raw[images_count];
		span image_views{ image_views_raw, images_count };

		for(nuint i = 0; i < images_count; ++i) {
			image_views[i] = device.create<vk::image_view>(
				images[i],
				surface_format.format,
				image_view_type::two_d
			);
		}

		for(auto& rr : rendering_resources) {
			rr.fence = device.create<vk::fence>(fence_create_flag::signaled);
			rr.image_acquire = device.create<vk::semaphore>();
			rr.finish = device.create<vk::semaphore>();
		}

		nuint rendering_resource_index = 0;

		while (!platform::should_close()) {
			double x, y;
			glfwGetCursorPos(window, &x, &y);
			array<float, 2> camera_rotation;
			camera_rotation[0] = x / 100.0;
			camera_rotation[1] = y / 100.0;

			view_matrix =
				rotation(camera_rotation[1], math::geometry::cartesian::vector<float, 3>(1.0F, 0.0F, 0.0F)) *
				rotation(camera_rotation[0], math::geometry::cartesian::vector<float, 3>(0.0F, 1.0F, 0.0F));

			auto& rr = rendering_resources[rendering_resource_index];
			
			++rendering_resource_index %= rendering_resources.size();
			
			platform::begin();

			device.wait_for_fence(rr.fence);
			device.reset_fence(rr.fence);

			auto result = device.try_acquire_next_image(swapchain, rr.image_acquire);
			if(result.is_unexpected()) {
				if(result.get_unexpected().suboptimal() || result.get_unexpected().out_of_date()) break;
				platform::error("acquire next image").new_line();
				return 1;
			}

			vk::image_index image_index = result;

			rr.framebuffer = device.create<vk::framebuffer>(
				render_pass,
				array{ image_views[(uint32)image_index] },
				extent<3>{ surface_capabilities.current_extent.width(), surface_capabilities.current_extent.height(), 1 }
			);

			auto frustum = math::geometry::perspective(
				95.0F / 360.0F * 2 * 3.14F,
				float(surface_capabilities.current_extent.width()) / float(surface_capabilities.current_extent.height()),
				0.1F,
				1000.0F
			);

			uniform_info_ptr->proj_view_inversed = (frustum * view_matrix).inversed().transposed();

			timespec ts{};
			clock_gettime(CLOCK_REALTIME, &ts);
			float t = ts.tv_sec - start_time.tv_sec;
			t += (ts.tv_nsec / 1000000.0F - start_time.tv_nsec / 1000000.0F) / 1000.0F;

			uniform_info_ptr->t = t;
			device.flush_mapped_memory_range(memory_for_staging_uniform_buffer, staging_uniform_buffer_memory_requirements.size);

			auto& command_buffer = rr.command_buffer;

			command_buffer
				.begin(vk::command_buffer_usage::one_time_submit)
				.cmd_copy_buffer(
					src_buffer{ staging_uniform_buffer },
					dst_buffer{ uniform_buffer },
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
				wait_semaphore{ rr.image_acquire },
				signal_semaphore{ rr.finish },
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
				return 1;
			}

			platform::end();
		}

		device.wait_idle();
	}
}