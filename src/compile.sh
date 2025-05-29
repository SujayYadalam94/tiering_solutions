set -x
set -e
bazel-6.5.0 build --config=linux_cpp17 --config=linux_avx2  --define=use_ydf_tensorflow_proto=1 //:hemem_lib
