# Copyright (c) Meta Platforms, Inc. and affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

# Kernel library for optimized kernels. Please this file formatted by running:
# ~~~
# cmake-format -i CMakeLists.txt
# ~~~

cmake_minimum_required(VERSION 3.19)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

# Source root directory for executorch.
if(NOT EXECUTORCH_ROOT)
  set(EXECUTORCH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
endif()

set(_common_compile_options -Wno-deprecated-declarations)

# Note for apple platform we can rely on Accelerate framework Will come back to
# this
include(${CMAKE_CURRENT_LIST_DIR}/External/EigenBLAS.cmake)
list(APPEND _common_compile_options -DET_BUILD_WITH_BLAS)

# For us to set CPU_CAPABILITY_AVX2 we need to detect architecture plus
# processor. The way aten has implemented this is slightly different. We
# probably need to figure out how to detect compiler flag that suggest we are
# compiling for avx2 for now punting this to come back

include(${EXECUTORCH_ROOT}/tools/cmake/Utils.cmake)
include(${EXECUTORCH_ROOT}/tools/cmake/Codegen.cmake)

# Build cpublas.
list(TRANSFORM _optimized_cpublas__srcs PREPEND "${EXECUTORCH_ROOT}/")
add_library(cpublas STATIC ${_optimized_cpublas__srcs})
target_include_directories(cpublas PRIVATE ${TORCH_INCLUDE_DIRS})
target_link_libraries(
  cpublas PUBLIC executorch_core eigen_blas extension_threadpool
)
target_compile_options(cpublas PUBLIC ${_common_compile_options})

# Generate C++ bindings to register kernels into both PyTorch (for AOT) and
# Executorch (for runtime). Here select all ops in optimized.yaml
set(_yaml "${CMAKE_CURRENT_LIST_DIR}/optimized.yaml")
gen_selected_ops(LIB_NAME "optimized_ops_lib" OPS_SCHEMA_YAML "${_yaml}")

generate_bindings_for_kernels(
  LIB_NAME "optimized_ops_lib" FUNCTIONS_YAML
  ${CMAKE_CURRENT_SOURCE_DIR}/optimized.yaml ADD_EXCEPTION_BOUNDARY
)
message("Generated files ${gen_command_sources}")

list(TRANSFORM _optimized_kernels__srcs PREPEND "${EXECUTORCH_ROOT}/")
add_library(optimized_kernels ${_optimized_kernels__srcs})
target_include_directories(
  optimized_kernels PRIVATE ${TORCH_INCLUDE_DIRS}
                            "${EXECUTORCH_ROOT}/third-party/pocketfft"
)
target_compile_definitions(
  optimized_kernels PRIVATE "ET_USE_PYTORCH_HEADERS=ET_HAS_EXCEPTIONS"
)
target_link_libraries(
  optimized_kernels PUBLIC executorch_core cpublas extension_threadpool
                           kernels_util_all_deps
)
target_compile_options(optimized_kernels PUBLIC ${_common_compile_options})
# Build a library for _optimized_kernels_srcs
#
# optimized_ops_lib: Register optimized ops kernels into Executorch runtime
gen_operators_lib(
  LIB_NAME "optimized_ops_lib" KERNEL_LIBS optimized_kernels DEPS
  executorch_core
)

install(
  # eigen_blas doesn't export itself, so we have to do our own install to export
  # it.
  TARGETS cpublas optimized_kernels optimized_ops_lib eigen_blas
  EXPORT ExecuTorchTargets
  DESTINATION lib
  PUBLIC_HEADER DESTINATION include/executorch/kernels/optimized/
)
