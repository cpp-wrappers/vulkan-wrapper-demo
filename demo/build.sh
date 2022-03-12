if [ $1 != --platform ]; then
	echo "first argument is platform"
	exit 1
fi

shift
platform=$1
shift

src_path=`realpath ${BASH_SOURCE[1]}`
src_dir=`dirname ${src_path}`
src_base=`basename ${src_path}`
src_name=${src_base%.*}

global_build_script_path=`realpath ${BASH_SOURCE[0]}`
global_build_script_dir=`dirname ${global_build_script_path}`

root_dir=`realpath ${global_build_script_dir}/../`

platform_dir=${root_dir}/platform/${platform}

if ! [ -d ${platform_dir} ]; then
	echo "platform doesn't exist"
	exit 1;
fi

platform_path=${platform_dir}/build.sh

. ${platform_path} $@