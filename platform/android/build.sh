declare -a assets

while [ $# -gt 0 ]; do
	case $1 in
	--run)
		run=1
		;;
	--sanitize)
		sanitize=1
		;;
	--asset)
		shift
		assets+=($1)
	esac

	shift
done

declare -a args

#args+=(-g)
args+=(-fno-exceptions)
args+=(-fno-rtti)
args+=(-flto)
args+=(-O2)
args+=(-g0)
args+=(-fpic)
args+=(-v)
args+=(--sysroot=${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/sysroot)
args+=(-isystem${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/c++/v1)
args+=(-isystem${ANDROID_NDK_ROOT}/sources)
args+=(-idirafter${root_dir}/../core/include)
args+=(-idirafter${root_dir}/../vulkan-wrapper/include)
args+=(-idirafter${root_dir}/../libspng/spng)
args+=(-idirafter${root_dir}/include)
args+=(-idirafter/usr/include)

if [ -v sanitize ]; then
	args+=(-fsanitize=address)
	args+=(-fsanitize=undefined)
fi

CXX=${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android32-clang++
C=${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android32-clang

if ! $C \
	-c \
	${args[@]} \
	-ftemplate-backtrace-limit=0 \
	-o ${platform_dir}/build/glue.o \
	${ANDROID_NDK_ROOT}/sources/android/native_app_glue/android_native_app_glue.c

	then
	exit 1
fi

#if ! $C \
#	-c \
#	${args[@]} \
#	-ftemplate-backtrace-limit=0 \
#	-o ${platform_dir}/build/spng.o \
#	${root_dir}/../libspng/spng/spng.c
#
#	then
#	exit 1
#fi

args+=(-std=c++20)
args+=(-nostdinc++)

if ! $CXX \
	-c \
	${args[@]} \
	-o ${platform_dir}/build/platform.o \
	${platform_dir}/platform.cpp \

	then
	exit 1
fi

build_dir=${src_dir}/build
pushd ${build_dir}

target=arm64-v8a
shared_lib_path=lib/${target}/libo.so

mkdir -p `dirname ${shared_lib_path}`

if ! $CXX \
	${args[@]} \
	-shared \
	-u ANativeActivity_onCreate \
	-lvulkan -landroid -llog -lc -lm \
	${platform_dir}/build/platform.o \
	${platform_dir}/build/glue.o \
	-o ${shared_lib_path} \
	${src_path}

	then
	exit 1
fi

${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip ${shared_lib_path}

aapt_path=${ANDROID_SDK_ROOT}/build-tools/32.0.0-rc1/aapt
android_jar_path=${ANDROID_SDK_ROOT}/platforms/android-31/android.jar
output_path=${src_name}.apk
unsigned_output_path=${output_path}.unsigned

$aapt_path package \
	-m -J gen/ \
	-M ${platform_dir}/AndroidManifest.xml \
	-I ${android_jar_path} \
	-f \
	-F ${unsigned_output_path}

mkdir -p assets
declare -a apk_assets

for asset in ${assets[@]}; do
	cp ${asset} assets/${asset}
	apk_assets+=(assets/${asset})
done

$aapt_path add ${unsigned_output_path} ${shared_lib_path} ${apk_assets[@]}

jarsigner -keystore ${platform_dir}/debug.keystore -storepass android -signedjar ${output_path} ${unsigned_output_path} key

if [ -v run ]; then
	${ANDROID_SDK_ROOT}/platform-tools/adb install -r ${output_path}
fi

popd