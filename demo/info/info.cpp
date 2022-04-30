#if 0
. ./`dirname ${BASH_SOURCE[0]}`/../build.sh $@
exit 0
#endif

#include "platform_implementation.hpp"
#include <vk/instance/layer/properties/for_each.hpp>

static nuint tabs = 0;

void print(auto... vs) {
	for(nuint i = 0; i < tabs; ++i) platform::info('\t');
	platform::info(vs...);
}

void println(auto... vs) {
	print(vs...);
	platform::info.new_line();
}

template<bool t>
void block(char open, char close, auto f) {
	if(t) println(open);
	else {
		platform::info(open, '\n');
		platform::info('\n');
	}

	++tabs;
	f();
	--tabs;

	println(close);
}

void array_block(auto name, auto f) {
	print(name, ": ");
	block<false>('[', ']', f);
}

void object_block(auto f) {
	block<true>('{', '}', f);
}

void object_block(auto name, auto f) {
	print(name, ": ");
	object_block(f);
}

int main() {
	using namespace vk;

	auto instance = platform::create_instance();

	array_block("physical devices", [&]() {
		instance.for_each_physical_device([](handle<physical_device> device) {
			auto props = device.get_properties();

			println("api version: ",
				props.api_version.variant, ".",
				props.api_version.major, ".",
				props.api_version.minor, ".",
				props.api_version.patch
			);

			println("driver version: ", props.driver_version);
			println("vendor id: ", props.vendor_id);
			println("device id: ", props.device_id);

			const char* type_name;
			switch(props.type) {
				case physical_device_type::other
					: type_name = "other"; break;
				case physical_device_type::integrated_gpu
					: type_name = "integrated gpu"; break;
				case physical_device_type::discrete_gpu
					: type_name = "discrete gpu"; break;
				case physical_device_type::virtual_gpu
					: type_name = "virtual gpu"; break;
				case physical_device_type::cpu
					: type_name = "cpu"; break;
			}

			println("device type: ", type_name);
			println("name: ", props.name);

			array_block("queue family properties", [&] {
				device.for_each_queue_family_properties([](auto props) {
					object_block([&]{
						println("count: ", props.count);
						println(
							"min image transfer granularity: ",
							props.min_image_transfer_granularity[0], ", ", 
							props.min_image_transfer_granularity[1], ", ",
							props.min_image_transfer_granularity[2]
						);
						println(
							"graphics: ",
							props.flags.get(queue_flag::graphics)
						);
						println(
							"compute: ",
							props.flags.get(queue_flag::compute)
						);
						println(
							"transfer: ",
							props.flags.get(queue_flag::transfer)
						);
						println(
							"sparse binding: ",
							props.flags.get(queue_flag::sparse_binding)
						);
					});
				});
			});

			array_block("device extensions properties", [&] {
				device.for_each_device_extension_properties([](auto props) {
					object_block([&]{
						println("name: ", props.name);
						println("spec version: ", props.spec_version);
					});
				});
			});
		});

		array_block("instance_layers", [&] {
			for_each_instance_layer_properties([](auto props) {
				object_block([&]{
					println(
						"name: ",
						props.name
					);
					println(
						"spec version: ",
						props.spec_version
					);
					println(
						"implementation version: ",
						props.implementation_version
					);
					println(
						"description: ",
						props.description
					);
				});
			});
		});
	});
}