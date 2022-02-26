#if 0
src_dir=`dirname ${BASH_SOURCE[0]}`

mkdir -p ${src_dir}/build

glslangValidator -e main -o ${src_dir}/build/texture.vert.spv -V ${src_dir}/texture.vert
glslangValidator -e main -o ${src_dir}/build/texture.frag.spv -V ${src_dir}/texture.frag
cp ${src_dir}/leaf.png ${src_dir}/build/leaf.png

. ${src_dir}/../build.sh $@ --asset texture.vert.spv --asset texture.frag.spv --asset leaf.png

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

	platform::image_info image_info = platform::read_image_info("leaf.png");
	platform::info("image width: ", image_info.width, ", height: ", image_info.height, ", size: ", image_info.size).new_line();
	char image_data_storage[image_info.size];
	span image_data{ image_data_storage, image_info.size };
	platform::read_image_data("leaf.png", image_data);

	auto image = device.create_guarded<vk::image>(
		image_create_flags{},
		image_type::two_d,
		format::r8_g8_b8_a8_unorm,
		extent<3>{ image_info.width, image_info.height, 1 },
		mip_levels{ 1 },
		array_layers{ 1 },
		sample_count{ 1 },
		image_tiling::optimal,
		image_usages {
			image_usage::transfer_dst,
			image_usage::sampled
		},
		sharing_mode::exclusive,
		span<vk::queue_family_index>{ nullptr, 0 },
		initial_layout{ image_layout::undefined }
	);

	auto image_memory = device.allocate_guarded<device_memory>(
		memory_size{ image_info.size },
		physical_device.get_index_of_first_memory_type(
			memory_properties{ memory_property::device_local },
			device.get_image_memory_requirements(image).memory_type_indices
		)
	);

	image.bind_memory(image_memory);

	auto staging_buffer = device.create_guarded<buffer>(
		buffer_size{ image_info.size },
		buffer_usages{ buffer_usage::transfer_src },
		sharing_mode::exclusive
	);

	auto staging_buffer_memory = device.allocate_guarded<device_memory>(
		memory_size{ image_info.size },
		physical_device.get_index_of_first_memory_type(
			memory_properties{ memory_property::host_visible },
			device.get_buffer_memory_requirements(staging_buffer).memory_type_indices
		)
	);

	staging_buffer.bind_memory(staging_buffer_memory);

	uint8* image_data_ptr;
	staging_buffer_memory.map(memory_size{ image_info.size }, (void**)&image_data_ptr);
	for(unsigned i = 0; i < image_info.size; ++i) image_data_ptr[i] = image_data[i];
	staging_buffer_memory.flush_mapped(memory_size{ image_info.size });
	staging_buffer_memory.unmap();

	auto image_view = device.create_guarded<vk::image_view>(
		image,
		format::r8_g8_b8_a8_unorm,
		image_view_type::two_d,
		component_mapping{},
		image_subresource_range{ image_aspects{ image_aspect::color } }
	);

	struct data_t {
		float position[4];
		float tex[2];
	};

	data_t data[] = {
		{ { -0.5,  0.5, 0.0, 1.0 }, { 0.0, 1.0 } },
		{ {  0.5,  0.5, 0.0, 1.0 }, { 1.0, 1.0 } },
		{ { -0.5, -0.5, 0.0, 1.0 }, { 0.0, 0.0 } },
		{ {  0.5, -0.5, 0.0, 1.0 }, { 1.0, 0.0 } }
	};

	auto buffer = device.create_guarded<vk::buffer>(
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
			format::r32_g32_sfloat,
			offset{ __builtin_offsetof(data_t, tex) }
		}
	};

	guarded_handle<device_memory> device_memory = device.allocate_guarded<vk::device_memory>(
		physical_device.get_index_of_first_memory_type(
			memory_properties{ memory_property::host_visible },
			device.get_buffer_memory_requirements(buffer).memory_type_indices
		),
		memory_size{ sizeof(data) }
	);

	buffer.bind_memory(device_memory);

	uint8* ptr;
	device_memory.map(memory_size{ sizeof(data) }, (void**) &ptr);

	for(nuint i = 0; i < sizeof(data); ++i) *ptr++ = ((uint8*)&data)[i];

	device_memory.flush_mapped(memory_size{ sizeof(data) });
	device_memory.unmap();

	surface_format surface_format = physical_device.get_first_surface_format(surface);

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

	auto vertex_shader = platform::read_shader_module(device, "texture.vert.spv");
	auto fragment_shader = platform::read_shader_module(device, "texture.frag.spv");

	auto sampler = device.create_guarded<vk::sampler>(
		mag_filter{ filter::nearest },
		min_filter{ filter::nearest },
		mipmap_mode::nearest,
		address_mode_u{ address_mode::clamp_to_edge },
		address_mode_v{ address_mode::clamp_to_edge },
		address_mode_w{ address_mode::clamp_to_edge }
	);

	auto descriptor_pool = device.create_guarded<vk::descriptor_pool>(
		max_sets{ 1 },
		array {
			descriptor_pool_size { descriptor_type::combined_image_sampler, descriptor_count{ 1 } }
		}
	);

	auto set_layout = device.create_guarded<descriptor_set_layout>(
		descriptor_set_layout_create_flags{},
		array {
			descriptor_set_layout_binding {
				descriptor_binding{ 0 },
				descriptor_type::combined_image_sampler,
				shader_stages{ shader_stage::fragment }
			}
		}
	);

	auto set = descriptor_pool.allocate_descriptor_set(set_layout);

	device.update_descriptor_set(
		write_descriptor_set {
			set,
			dst_binding{ 0 },
			dst_array_element{ 0 },
			descriptor_type::combined_image_sampler,
			array {
				descriptor_image_info {
					sampler, image_view, image_layout::shader_read_only_optimal
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
		guarded_handle<command_buffer> command_buffer;
		guarded_handle<semaphore> image_acquire;
		guarded_handle<semaphore> finish;
		guarded_handle<fence> fence;
		guarded_handle<framebuffer> framebuffer;
	};

	array<rendering_resource, 2> rendering_resources{};

	for(auto& rr : rendering_resources) {
		rr.command_buffer = command_pool.allocate_guarded<command_buffer>(command_buffer_level::primary);
	}

	rendering_resources[0].command_buffer
		.begin(command_buffer_usages{ command_buffer_usage::one_time_submit })
		.cmd_pipeline_barrier(
			src_stages{ pipeline_stage::top_of_pipe },
			dst_stages{ pipeline_stage::transfer },
			array {
				image_memory_barrier {
					.dst_access{ access::transfer_write },
					.old_layout{ image_layout::undefined },
					.new_layout{ image_layout::transfer_dst_optimal },
					.image = get_handle(image),
					.subresource_range {
						image_aspects{ image_aspect::color }
					}
				}
			}
		)
		.cmd_copy_buffer_to_image(
			staging_buffer, image, image_layout::transfer_dst_optimal,
			array {
				buffer_image_copy {
					.image_subresource {
						.aspects{ image_aspect::color },
						.layer_count = 1
					},
					.image_extent{
						image_info.width, image_info.height, 1
					}
				}
			}
		)
		.cmd_pipeline_barrier(
			src_stages{ pipeline_stage::transfer },
			dst_stages{ pipeline_stage::fragment_shader },
			array {
				image_memory_barrier {
					.src_access{ access::transfer_write },
					.dst_access{ access::shader_read },
					.old_layout{ image_layout::transfer_dst_optimal },
					.new_layout{ image_layout::shader_read_only_optimal },
					.image = get_handle(image),
					.subresource_range {
						image_aspects{ image_aspect::color }
					}
				}
			}
		)
		.end();


	guarded_handle<swapchain> swapchain{};
	auto queue = device.get_queue(queue_family_index, queue_index{ 0 });

	queue.submit(rendering_resources[0].command_buffer);

	device.wait_idle();

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
			rr.fence = device.create_guarded<fence>(fence_create_flag::signaled);
			rr.image_acquire = device.create_guarded<semaphore>();
			rr.finish = device.create_guarded<semaphore>();
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

			image_index image_index = result;

			rr.framebuffer = device.create_guarded<framebuffer>(
				render_pass,
				array{ image_views[(uint32)image_index].handle() },
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
				.cmd_bind_descriptor_set(pipeline_bind_point::graphics, pipeline_layout, set)
				.cmd_set_viewport(surface_capabilities.current_extent)
				.cmd_set_scissor(surface_capabilities.current_extent)
				.cmd_bind_vertex_buffer(buffer)
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