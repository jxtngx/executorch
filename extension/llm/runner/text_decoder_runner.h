/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Given inputs, run a text decoder in LLM and return the output.

#pragma once

#include <executorch/extension/llm/runner/io_manager/io_manager.h>
#include <executorch/extension/llm/sampler/sampler.h>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>
#include <executorch/runtime/platform/compiler.h>

namespace executorch {
namespace extension {
namespace llm {

class ET_EXPERIMENTAL TextDecoderRunner {
 public:
  explicit TextDecoderRunner(Module* module, IOManager* io_manager);

  virtual ~TextDecoderRunner() = default;

  /**
   * Run LLM text decoder with inputs to generate next token.
   * @param input The input to the LLM Module.
   * @param start_pos The starting position in KV cache of the input in the LLM
   * Module.
   * @return The output of the LLM Module. This will be a tensor of logits.
   */
  virtual ::executorch::runtime::Result<executorch::aten::Tensor> step(
      TensorPtr& input,
      int64_t start_pos);

  /**
   * Load the Module for text decode purpose.
   * @return The error code.
   */
  virtual ::executorch::runtime::Error load() {
    return module_->load_method("forward");
  }

  /**
   * Check if the required methods in the Module is loaded.
   * @return True if the Module is loaded, false otherwise.
   */
  virtual bool is_method_loaded() {
    return module_->is_method_loaded("forward");
  }

  inline void stop() {
    should_stop_ = true;
  }

  /**
   * Sample the next token from the logits tensor.
   * @param logits_tensor The logits tensor.
   * @param temperature The temperature parameter used to control randomness in
   * sampling.
   * @return The next token.
   */
  inline int32_t logits_to_token(
      const executorch::aten::Tensor& logits_tensor,
      const float temperature = 0.0f) {
    int32_t result = 0;
    ET_SWITCH_THREE_TYPES(
        Float,
        Half,
        BFloat16,
        logits_tensor.scalar_type(),
        unused,
        "logits_to_token",
        CTYPE,
        [&]() {
          // If the logit_tensor rank is 3, the shape is [batch, seq_length,
          // vocab_size], get the last logits, sample and return. Else the model
          // outputs the last logit, directly sample and return.
          auto* logits = logits_tensor.mutable_data_ptr<CTYPE>();
          ssize_t vocab_size = logits_tensor.size(logits_tensor.dim() - 1);
          if (logits_tensor.dim() == 3) {
            auto num_tokens = logits_tensor.size(1);
            logits += (num_tokens - 1) * vocab_size;
          }
          // @lint-ignore CLANGTIDY facebook-hte-Deprecated
          Sampler sampler(vocab_size, temperature);
          result = sampler.sample(logits);
        });
    return result;
  }

 protected:
  /**
   * Note: TextDecoderRunner does not own the Module or IOManager instance. It
   * is expected that the outer class (likely Runner) manages the lifecycle of
   * them. This means that the responsibility for creating, maintaining, and
   * destroying the Module lies outside of TextDecoderRunner. Ensure that the
   * Module remains valid for the duration of TextDecoderRunner's usage.
   */
  Module* module_;
  IOManager* io_manager_;
  bool should_stop_{false};
};

} // namespace llm
} // namespace extension
} // namespace executorch

namespace torch {
namespace executor {
// TODO(T197294990): Remove these deprecated aliases once all users have moved
// to the new `::executorch` namespaces.
using ::executorch::extension::llm::TextDecoderRunner;
} // namespace executor
} // namespace torch
