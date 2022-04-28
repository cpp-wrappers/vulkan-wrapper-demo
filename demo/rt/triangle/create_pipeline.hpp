#pragma once

#include "platform_implementation.hpp"

#include "vk/ray_tracing/shader_group_create_info.hpp"

struct ray_tracing_pipeline_t {
	handle<vk::pipeline> handle;
	array<vk::ray_tracing_shader_group_create_info, 3> shader_groups;
};

ray_tracing_pipeline_t create_raytracing_pipeline(handle<vk::device> device) {
	using namespace vk;

	array bindings {
		descriptor_set_layout_binding {
			descriptor_binding{ 0 },
			descriptor_type::acceleration_structure,
			shader_stages{ shader_stage::raygen }
		},
		descriptor_set_layout_binding {
			descriptor_binding{ 1 },
			descriptor_type::storage_image,
			shader_stages{ shader_stage::raygen }
		}
	};

	auto descritor_set_layout {
		device.create<vk::descriptor_set_layout>(bindings)
	};

	auto pipeline_layout {
		device.create<vk::pipeline_layout>(
			array{ descritor_set_layout }
		)
	};

	array stages {
		pipeline_shader_stage_create_info {
			platform::read_shader_module(device, "gen.rgen"),
			shader_stages{ shader_stage::raygen },
			entrypoint_name{ "main" }
		},
		pipeline_shader_stage_create_info {
			platform::read_shader_module(device, "miss.rmiss"),
			shader_stages{ shader_stage::miss },
			entrypoint_name{ "main" }
		},
		pipeline_shader_stage_create_info {
			platform::read_shader_module(device, "closest.rchit"),
			shader_stages{ shader_stage::closest_hit },
			entrypoint_name{ "main" }
		}
	};

	
}