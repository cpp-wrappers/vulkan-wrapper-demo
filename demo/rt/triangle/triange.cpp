#if 0
src_dir=`dirname ${BASH_SOURCE[0]}`

mkdir -p ${src_dir}/build

#glslangValidator -e main -o ${src_dir}/build/triangle.vert.spv -V ${src_dir}/triangle.vert
#glslangValidator -e main -o ${src_dir}/build/triangle.frag.spv -V ${src_dir}/triangle.frag

. ${src_dir}/../../build.sh $@

exit 0
#endif

#include "platform_implementation.hpp"

#include "vk/physical_device/extension_properties/acceleration_structure_properties.hpp"

int main() {
	using namespace vk;

	auto instance_and_surface = platform::create_instance_and_surface(vk::api_version{ vk::major{1}, vk::minor{2} });
	auto instance = instance_and_surface.get<handle<vk::instance>>();
	//auto surface = instance_and_surface.get<handle<vk::surface>>();

	auto physical_device = instance.get_first_physical_device();

	vk::physical_device_acceleration_structure_properties as_props{};

	physical_device.get_properties(as_props);
}