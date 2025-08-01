load("@fbsource//xplat/executorch/build:runtime_wrapper.bzl", "runtime")

def define_common_targets():
    runtime.cxx_library(
        name = "irunner",
        exported_headers = [
            "irunner.h",
        ],
        visibility = [
            "@EXECUTORCH_CLIENTS",
        ],
    )

    runtime.cxx_library(
        name = "stats",
        exported_headers = [
            "stats.h",
            "util.h",
        ],
        visibility = [
            "@EXECUTORCH_CLIENTS",
        ],
    )

    for aten in (True, False):
        aten_suffix = "_aten" if aten else ""

        runtime.cxx_library(
            name = "text_decoder_runner" + aten_suffix,
            exported_headers = ["text_decoder_runner.h"],
            srcs = ["text_decoder_runner.cpp"],
            visibility = [
                "@EXECUTORCH_CLIENTS",
            ],
            exported_deps = [
                ":stats",
                "//executorch/kernels/portable/cpu/util:arange_util" + aten_suffix,
                "//executorch/extension/llm/sampler:sampler" + aten_suffix,
                "//executorch/extension/llm/runner/io_manager:io_manager" + aten_suffix,
                "//executorch/extension/module:module" + aten_suffix,
                "//executorch/extension/tensor:tensor" + aten_suffix,
            ],
        )

        runtime.cxx_library(
            name = "text_prefiller" + aten_suffix,
            exported_headers = ["text_prefiller.h"],
            srcs = ["text_prefiller.cpp"],
            visibility = [
                "@EXECUTORCH_CLIENTS",
            ],
            exported_deps = [
                ":text_decoder_runner" + aten_suffix,
                "//pytorch/tokenizers:headers",
                "//executorch/extension/module:module" + aten_suffix,
                "//executorch/extension/tensor:tensor" + aten_suffix,
            ],
        )

        runtime.cxx_library(
            name = "text_token_generator" + aten_suffix,
            exported_headers = ["text_token_generator.h"],
            visibility = [
                "@EXECUTORCH_CLIENTS",
            ],
            exported_deps = [
                ":text_decoder_runner" + aten_suffix,
                "//pytorch/tokenizers:headers",
                "//executorch/extension/module:module" + aten_suffix,
                "//executorch/extension/tensor:tensor" + aten_suffix,
            ],
        )

        runtime.cxx_library(
            name = "image_prefiller" + aten_suffix,
            exported_headers = ["image_prefiller.h", "image.h"],
            visibility = [
                "@EXECUTORCH_CLIENTS",
            ],
            exported_deps = [
                "//executorch/extension/module:module" + aten_suffix,
            ],
        )

        runtime.cxx_library(
            name = "runner_lib" + aten_suffix,
            exported_headers = [
                "multimodal_runner.h",
                "text_llm_runner.h",
            ],
            srcs = [
                "text_llm_runner.cpp",
            ],
            visibility = [
                "@EXECUTORCH_CLIENTS",
            ],
            compiler_flags = [
                "-Wno-missing-prototypes",
            ],
            exported_deps = [
                ":image_prefiller" + aten_suffix,
                ":irunner",
                ":text_decoder_runner" + aten_suffix,
                ":text_prefiller" + aten_suffix,
                ":text_token_generator" + aten_suffix,
                "//executorch/extension/llm/runner/io_manager:io_manager" + aten_suffix,
                "//pytorch/tokenizers:hf_tokenizer",
                "//pytorch/tokenizers:llama2c_tokenizer",
                "//pytorch/tokenizers:sentencepiece",
                "//pytorch/tokenizers:tiktoken",
            ],
        )
