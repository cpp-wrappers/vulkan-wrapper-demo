while [ $# -gt 0 ]; do
	case $1 in
	--run)
		run=1
		;;
	--sanitize)
		sanitize=1
		;;
	--verbose)
		verbose=1
		;;
	esac

	shift
done

if [ -z $CXX ]; then
	CXX=clang++
fi

declare -a args

args+=(-std=c++20)
args+=(-Wall)
args+=(-Wextra)
#args+=(-ftime-trace)
#args+=(-c)
#args+=(-Os)
args+=(-g)
args+=(-nostdinc++)
args+=(-fmodules)
args+=(-fno-exceptions)
args+=(-fno-rtti)
args+=(-I${root_dir}/platform)
args+=(-I${root_dir}/../core/include)
args+=(-I${root_dir}/../math/include)
args+=(-I${root_dir}/../vulkan-wrapper/include)
args+=(-I${root_dir}/../glfw-wrapper/include)
args+=(-Xclang -fimplicit-module-maps)

if [ -v verbose ]; then
	args+=(-v)
fi

if [ -v sanitize ]; then
	args+=(-fsanitize=address)
	args+=(-fsanitize=undefined)
fi

mkdir -p "${platform_dir}/build"

declare -a libs

args+=(-lpng)
args+=(-fuse-ld=lld)

if [[ $OS == Windows_NT ]]; then
	args+=(-static)
	libs+=(-lgdi32)
	libs+=(-lglfw3)
	libs+=(/c/Windows/System32/vulkan-1.dll)
	#libs+=(-lvulkan)
else
	libs+=(-lglfw)
	libs+=(-lvulkan)
fi

mkdir -p "${src_dir}/build"

if ! $CXX \
	${args[@]} \
	-o ${src_dir}/build/${src_name} \
	${src_path} \
	${libs[@]}

	then
	exit 1
fi

if [ -v run ]; then
	pushd ${src_dir}/build
	./${src_name}
	popd
fi