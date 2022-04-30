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
	auto queue_family_index {
		physical_device.find_first_queue_family_with_capabilities(
			queue_flag::graphics
		)
	};

	if(!physical_device.get_surface_support(surface, queue_family_index)) {
		platform::error("surface isn't supported").new_line();
		return 1;
	}

	auto device = physical_device.create<vk::device>(
		queue_family_index,
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
		array{ subpass_description{ color_attachment } },
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
			span{ &pcbas, 1 }
		},
		pipeline_viewport_state_create_info {
			viewport_count{ 1 },
			scissor_count{ 1 }
		},
		pipeline_dynamic_state_create_info { dynamic_states }
	);

	handle<swapchain> swapchain;

	while(!platform::should_close()) {
		auto surface_capabilities {
			physical_device.get_surface_capabilities(surface)
		};

		device.create<vk::swapchain>(
			surface,
			swapchain,
			surface_capabilities.min_image_count,
			surface_format,
			surface_capabilities.current_extent,
			image_usages {
				image_usage::color_attachment
			},
			sharing_mode::exclusive,
			present_mode::fifo,
			clipped{ false },
			surface_capabilities.current_transform,
			composite_alpha::opaque
		);

		uint32 images_count = device.get_swapchain_image_count(swapchain);
		handle<vk::image> swapchain_images[images_count];
		images_count = device.get_swapchain_images(swapchain, span{ swapchain_images, images_count });
		span images{ swapchain_images, images_count };

		struct per_image {
			handle<vk::image_view> view;
			handle<vk::command_buffer> command_buffer;
			handle<vk::framebuffer> framebuffer;
			handle<vk::fence> fence;
		};

		for(auto image : images) {
			
		}

		while(!platform::should_close()) {
			platform::begin();
			platform::end();
		}
	}
}