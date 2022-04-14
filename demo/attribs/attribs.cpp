#if 0
src_dir=`dirname ${BASH_SOURCE[0]}`

mkdir -p ${src_dir}/build

glslangValidator -e main -o ${src_dir}/build/attribs.vert.spv -V ${src_dir}/attribs.vert
glslangValidator -e main -o ${src_dir}/build/attribs.frag.spv -V ${src_dir}/attribs.frag

. ${src_dir}/../build.sh $@ --asset attribs.vert.spv --asset attribs.frag.spv

exit 0
#endif

#include <math/geometry/coordinate_system/cartesian/vector.hpp>

#include "platform_implementation.hpp"

int main() {
	using namespace vk;

	auto [instance, surface] = platform::create_instance_and_surface();
	handle<physical_device> physical_device = instance.get_first_physical_device();
	auto queue_family_index = physical_device.find_first_queue_family_index_with_capabilities(queue_flag::graphics);

	platform::info("graphics family index: ", (uint32)queue_family_index).new_line();

	if(!physical_device.get_surface_support(surface, queue_family_index)) {
		platform::error("surface isn't supported").new_line();
		return 1;
	}

	auto device = physical_device.create_device(
		queue_family_index,
		queue_priority{ 1.0F },
		extension_name{ "VK_KHR_swapchain" }
	);

	using namespace math::geometry::cartesian;

	struct data_t {
		vector<float, 4> position;
		array<float, 4> color;
	};

	data_t data[] = {
		{ {-0.5F, 0.5F, 0.0F, 1.0F }, { 1.0, 0.0, 0.0, 1.0 } },
		{ { 0.5F, 0.5F, 0.0F, 1.0F }, { 1.0, 1.0, 1.0, 1.0 } },
		{ {-0.5F,-0.5F, 0.0F, 1.0F }, { 1.0, 1.0, 1.0, 1.0 } },
		{ { 0.5F,-0.5F, 0.0F, 1.0F }, { 0.0, 0.0, 1.0, 1.0 } }
	};

	auto buffer = device.create<vk::buffer>(
		buffer_size{ sizeof(data) },
		buffer_usages{ buffer_usage::vertex_buffer },
		sharing_mode::exclusive
	);

	vertex_input_binding_description vertex_binding_description {
		binding{ 0 },
		stride{ sizeof(data_t) },
	};

	array vertex_input_attribute_descriptions {
		vertex_input_attribute_description {
			location{ 0 },
			binding{ 0 },
			format::r32_g32_b32_a32_sfloat,
			offset{ __builtin_offsetof(data_t, position) }
		},
		vertex_input_attribute_description {
			location{ 1 },
			binding{ 0 },
			format::r32_g32_b32_a32_sfloat,
			offset{ __builtin_offsetof(data_t, color) }
		}
	};

	handle<device_memory> device_memory = device.allocate<vk::device_memory>(
		physical_device.find_first_memory_type_index(
			memory_properties{ memory_property::host_visible },
			device.get_memory_requirements(buffer).memory_type_indices
		),
		memory_size{ sizeof(data) }
	);

	device.bind_memory(buffer, device_memory);

	uint8* ptr;
	device.map_memory(device_memory, memory_size{ sizeof(data) }, (void**) &ptr);

	for(nuint i = 0; i < sizeof(data); ++i) *ptr++ = ((uint8*)&data)[i];

	device.flush_mapped_memory_range(device_memory, memory_size{ sizeof(data) });
	device.unmap_memory(device_memory);

	surface_format surface_format = physical_device.get_first_surface_format(surface);

	array color_attachments {
		color_attachment_reference{ 0, image_layout::color_attachment_optimal }
	};

	auto render_pass = device.create<vk::render_pass>(
		array{ subpass_description {
			color_attachments
		} },
		array{ subpass_dependency {
			src_subpass{ vk::subpass_external },
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

	auto vertex_shader = platform::read_shader_module(device, "attribs.vert.spv");
	auto fragment_shader = platform::read_shader_module(device, "attribs.frag.spv");

	auto pipeline_layout = device.create<vk::pipeline_layout>();

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
		pipeline_vertex_input_state_create_info {
			span{ &vertex_binding_description, 1 },
			vertex_input_attribute_descriptions
		},
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

	auto command_pool = device.create<vk::command_pool>(
		queue_family_index,
		command_pool_create_flags {
			command_pool_create_flag::reset_command_buffer,
			command_pool_create_flag::transient
		}
	);

	struct rendering_resource {
		handle<command_buffer> command_buffer;
		handle<semaphore> image_acquire;
		handle<semaphore> finish;
		handle<fence> fence;
		handle<framebuffer> framebuffer;
	};

	array<rendering_resource, 2> rendering_resources{};

	for(auto& rr : rendering_resources) {
		rr.command_buffer = device.allocate<command_buffer>(command_pool, command_buffer_level::primary);
	}

	handle<swapchain> swapchain{};
	auto queue = device.get_queue(queue_family_index, queue_index{ 0 });

	while(!platform::should_close()) {
		surface_capabilities surface_capabilities = physical_device.get_surface_capabilities(surface);

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

		handle<image> images_storage[images_count];
		span images{ images_storage, images_count };
		device.get_swapchain_images(swapchain, images);

		handle<image_view> image_views_raw[images_count];
		span image_views{ image_views_raw, images_count };

		for(nuint i = 0; i < images_count; ++i) {
			image_views[i] = device.create<image_view>(
				images[i],
				surface_format.format,
				image_view_type::two_d,
				component_mapping{},
				image_subresource_range{ image_aspects{ image_aspect::color } }
			);
		}

		for(auto& rr : rendering_resources) {
			rr.fence = device.create<fence>(fence_create_flag::signaled);
			rr.image_acquire = device.create<semaphore>();
			rr.finish = device.create<semaphore>();
		}

		nuint rendering_resource_index = 0;

		while (!platform::should_close()) {
			rendering_resource& rr = rendering_resources[rendering_resource_index];
			
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

			image_index image_index = result;

			rr.framebuffer = device.create<framebuffer>(
				render_pass,
				array{ image_views[(uint32)image_index] },
				extent<3>{ surface_capabilities.current_extent.width(), surface_capabilities.current_extent.height(), 1 }
			);

			auto& command_buffer = rr.command_buffer;

			command_buffer
				.begin(command_buffer_usage::one_time_submit)
				.cmd_begin_render_pass(
					render_pass, rr.framebuffer,
					render_area{ surface_capabilities.current_extent },
					array{ clear_value { clear_color_value{ 0.0, 0.0, 0.0, 0.0 } } }
				)
				.cmd_bind_pipeline(pipeline, pipeline_bind_point::graphics)
				.cmd_set_viewport(surface_capabilities.current_extent)
				.cmd_set_scissor(surface_capabilities.current_extent)
				.cmd_bind_vertex_buffer(buffer)
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

			if(!present_result.success()) {
				if(present_result.suboptimal() || present_result.out_of_date()) break;
				platform::error("present").new_line();
				return 1;
			}

			platform::end();
		}

		device.wait_idle();
	}
}