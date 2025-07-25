# Copyright (c) Meta Platforms, Inc. and affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

#
# Build llm runner lib.
#
# ### Editing this file ###
#
# This file should be formatted with
# ~~~
# cmake-format -i CMakeLists.txt
# ~~~
# It should also be cmake-lint clean.
#

if(NOT EXECUTORCH_ROOT)
  set(EXECUTORCH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
endif()

include(${EXECUTORCH_ROOT}/tools/cmake/Utils.cmake)
include(${EXECUTORCH_ROOT}/tools/cmake/Codegen.cmake)

#
# The `_<target>_srcs` lists are defined by including ${EXECUTORCH_SRCS_FILE}.
#
set(EXECUTORCH_SRCS_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/../../../executorch_srcs.cmake"
)

extract_sources(${EXECUTORCH_SRCS_FILE})

include(${EXECUTORCH_SRCS_FILE})

# build llm runner library
list(TRANSFORM _extension_llm_runner__srcs PREPEND "${EXECUTORCH_ROOT}/")

target_include_directories(
  extension_module INTERFACE ${_common_include_directories}
)

add_library(extension_llm_runner STATIC ${_extension_llm_runner__srcs})

# add tokenizers
set(SUPPORT_REGEX_LOOKAHEAD ON)
# llama/runner/CMakeLists.txt builds a shared library libllama_runner.so that
# transitively depends on tokenizers. Need to build tokenizers with -fPIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(
  ${EXECUTORCH_ROOT}/extension/llm/tokenizers
  ${CMAKE_CURRENT_BINARY_DIR}/../../../extension/llm/tokenizers
)

set(runner_deps executorch_core extension_module extension_tensor tokenizers)

target_link_libraries(extension_llm_runner PUBLIC ${runner_deps})
set_target_properties(extension_llm_runner PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(
  extension_llm_runner
  INTERFACE ${_common_include_directories}
            ${EXECUTORCH_ROOT}/extension/llm/tokenizers/include
)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()
