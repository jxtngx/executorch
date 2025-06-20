/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <executorch/backends/cadence/fusion_g3/operators/operators.h>

#include <cmath>

#include <xa_nnlib_kernels_api.h>

#include <executorch/backends/cadence/fusion_g3/operators/xt_macros.h>
#include <executorch/kernels/portable/cpu/scalar_utils.h>
#include <executorch/kernels/portable/cpu/util/elementwise_util.h>
#include <executorch/kernels/portable/cpu/util/math_util.h>
#include <executorch/runtime/kernel/kernel_includes.h>
#include <executorch/runtime/platform/assert.h>

using ::executorch::aten::Scalar;
using ::executorch::aten::ScalarType;
using ::executorch::aten::Tensor;
using ::executorch::runtime::canCast;
using ::executorch::runtime::Error;
using ::executorch::runtime::KernelRuntimeContext;
using std::optional;
using std::string_view;

namespace cadence {
namespace impl {
namespace G3 {
namespace native {

namespace {

ScalarType get_common_type(ScalarType a_type, ScalarType b_type) {
  if (executorch::runtime::isFloatingType(a_type) &&
      executorch::runtime::isFloatingType(b_type)) {
    return executorch::runtime::promoteTypes(a_type, b_type);
  } else if (executorch::runtime::isFloatingType(a_type)) {
    return a_type;
  } else if (executorch::runtime::isFloatingType(b_type)) {
    return b_type;
  }
  return ScalarType::Float;
}

} // namespace

Tensor& div_out(
    KernelRuntimeContext& ctx,
    const Tensor& a,
    const Tensor& b,
    Tensor& out) {
#ifdef OP_ARG_CHECK
  // Check Dim Order
  ET_KERNEL_CHECK(
      ctx,
      executorch::runtime::tensors_have_same_dim_order(a, b, out),
      InvalidArgument,
      out);

  // Resize
  ET_KERNEL_CHECK(
      ctx,
      torch::executor::resize_to_broadcast_target_size(a, b, out) == Error::Ok,
      InvalidArgument,
      out);
#endif
  // @lint-ignore CLANGTIDY facebook-hte-CArray
  static constexpr const char op_name[] = "div.out";

  int kTensorDimensionLimit = 5;

  int inp1_shape[kTensorDimensionLimit];
  int inp2_shape[kTensorDimensionLimit];
  int out_shape[kTensorDimensionLimit];

  bool broadcast = false;

  int max_dim = a.dim() > b.dim() ? a.dim() : b.dim();
  max_dim = out.dim() > max_dim ? out.dim() : max_dim;

  bool optimized = true;

  for (int i = 0; i < max_dim; i++) {
    out_shape[i] = 1;
    inp1_shape[i] = 1;
    inp2_shape[i] = 1;
  }

  int offset_out = max_dim - out.dim();
  int offset_inp1 = max_dim - a.dim();
  int offset_inp2 = max_dim - b.dim();

  for (int i = 0; i < out.dim(); i++) {
    out_shape[i + offset_out] = out.size(i);
  }
  for (int i = 0; i < a.dim(); i++) {
    inp1_shape[i + offset_inp1] = a.size(i);
  }
  for (int i = 0; i < b.dim(); i++) {
    inp2_shape[i + offset_inp2] = b.size(i);
  }

  /*find broadcast*/
  for (int i = 0; i < out.dim(); i++) {
    if (((inp1_shape[i]) != (out_shape[i])) ||
        ((inp2_shape[i]) != (out_shape[i]))) {
      broadcast = true;
    }
  }

  if (((broadcast) && (max_dim > kTensorDimensionLimit)) ||
      (!(((a.scalar_type() == ScalarType::Int) ||
          (a.scalar_type() == ScalarType::Float)) &&
         (a.scalar_type() == b.scalar_type()) &&
         (out.scalar_type() == ScalarType::Float)))) {
    optimized = false;
  }

  if ((a.scalar_type() == ScalarType::Int) && (optimized)) {
    const int* const inp1_data = a.const_data_ptr<int>();
    const int* const inp2_data = b.const_data_ptr<int>();
    float* const out_data = out.mutable_data_ptr<float>();

    if (b.numel() == 1) {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_scalar_32x32_f32,
          out_data,
          inp1_data,
          inp2_data[0],
          out.numel());
    } else if (broadcast) {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_broadcast_5D_32x32_f32,
          out_data,
          out_shape,
          inp1_data,
          inp1_shape,
          inp2_data,
          inp2_shape,
          max_dim);
    } else {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_32x32_f32,
          out_data,
          inp1_data,
          inp2_data,
          out.numel());
    }
  } else if ((a.scalar_type() == ScalarType::Float) && (optimized)) {
    const float* const inp1_data = a.const_data_ptr<float>();
    const float* const inp2_data = b.const_data_ptr<float>();
    float* const out_data = out.mutable_data_ptr<float>();

    int mode = 0;

    if (b.numel() == 1) {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_scalar_f32xf32_f32,
          out_data,
          inp1_data,
          inp2_data[0],
          mode,
          out.numel());
    } else if (broadcast) {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_broadcast_5D_f32xf32_f32,
          out_data,
          out_shape,
          inp1_data,
          inp1_shape,
          inp2_data,
          inp2_shape,
          mode,
          max_dim);
    } else {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_f32xf32_f32,
          out_data,
          inp1_data,
          inp2_data,
          mode,
          out.numel());
    }
  } else {
    ScalarType common_type = get_common_type(a.scalar_type(), b.scalar_type());
    ScalarType compute_type =
        torch::executor::native::utils::get_compute_type(common_type);

    ET_SWITCH_FLOAT_TYPES(compute_type, ctx, op_name, CTYPE_COMPUTE, [&]() {
      torch::executor::native::utils::apply_bitensor_elementwise_fn<
          CTYPE_COMPUTE,
          op_name>(
          [](const CTYPE_COMPUTE val_a, const CTYPE_COMPUTE val_b) {
            return val_a / val_b;
          },
          ctx,
          a,
          torch::executor::native::utils::SupportedTensorDtypes::REALHBBF16,
          b,
          torch::executor::native::utils::SupportedTensorDtypes::REALHBBF16,
          out,
          torch::executor::native::utils::SupportedTensorDtypes::FLOATHBF16);
    });
  }

  return out;
}

Tensor& div_out_mode(
    KernelRuntimeContext& ctx,
    const Tensor& a,
    const Tensor& b,
    optional<string_view> mode,
    Tensor& out) {
  if (!mode.has_value()) {
    return div_out(ctx, a, b, out);
  }

  auto mode_val = mode.value();

  // Check mode
  ET_KERNEL_CHECK(
      ctx, mode_val == "trunc" || mode_val == "floor", InvalidArgument, out);

#ifdef OP_ARG_CHECK
  // Check Dim Order
  ET_KERNEL_CHECK(
      ctx,
      executorch::runtime::tensors_have_same_dim_order(a, b, out),
      InvalidArgument,
      out);

  // Resize
  ET_KERNEL_CHECK(
      ctx,
      torch::executor::resize_to_broadcast_target_size(a, b, out) == Error::Ok,
      InvalidArgument,
      out);
#endif

  // @lint-ignore CLANGTIDY facebook-hte-CArray
  static constexpr const char op_name[] = "div.out_mode";

  const bool mode_is_trunc = mode_val == "trunc";
  bool div_by_zero_error = false;

  int kTensorDimensionLimit = 5;

  int inp1_shape[kTensorDimensionLimit];
  int inp2_shape[kTensorDimensionLimit];
  int out_shape[kTensorDimensionLimit];

  bool broadcast = false;

  int max_dim = a.dim() > b.dim() ? a.dim() : b.dim();
  max_dim = out.dim() > max_dim ? out.dim() : max_dim;

  bool optimized = true;

  for (int i = 0; i < max_dim; i++) {
    out_shape[i] = 1;
    inp1_shape[i] = 1;
    inp2_shape[i] = 1;
  }

  int offset_out = max_dim - out.dim();
  int offset_inp1 = max_dim - a.dim();
  int offset_inp2 = max_dim - b.dim();

  for (int i = 0; i < out.dim(); i++) {
    out_shape[i + offset_out] = out.size(i);
  }
  for (int i = 0; i < a.dim(); i++) {
    inp1_shape[i + offset_inp1] = a.size(i);
  }
  for (int i = 0; i < b.dim(); i++) {
    inp2_shape[i + offset_inp2] = b.size(i);
  }

  /*find broadcast*/
  for (int i = 0; i < out.dim(); i++) {
    if (((inp1_shape[i]) != (out_shape[i])) ||
        ((inp2_shape[i]) != (out_shape[i]))) {
      broadcast = true;
    }
  }

  if (((broadcast) && (max_dim > kTensorDimensionLimit)) ||
      (!(((a.scalar_type() == ScalarType::Int) ||
          (a.scalar_type() == ScalarType::Float)) &&
         (a.scalar_type() == b.scalar_type()) &&
         (a.scalar_type() == out.scalar_type())))) {
    optimized = false;
  }

  int mode_value = (mode_val == "trunc") ? 1 : 2;

  if ((a.scalar_type() == ScalarType::Int) && (optimized)) {
    const int* const inp1_data = a.const_data_ptr<int>();
    const int* const inp2_data = b.const_data_ptr<int>();
    int* const out_data = out.mutable_data_ptr<int>();

    if (b.numel() == 1) {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_scalar_32x32_32,
          out_data,
          inp1_data,
          inp2_data[0],
          mode_value,
          out.numel());
    } else if (broadcast) {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_broadcast_5D_32x32_32,
          out_data,
          out_shape,
          inp1_data,
          inp1_shape,
          inp2_data,
          inp2_shape,
          mode_value,
          max_dim);
    } else {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_32x32_32,
          out_data,
          inp1_data,
          inp2_data,
          mode_value,
          out.numel());
    }
  } else if ((a.scalar_type() == ScalarType::Float) && (optimized)) {
    const float* const inp1_data = a.const_data_ptr<float>();
    const float* const inp2_data = b.const_data_ptr<float>();
    float* const out_data = out.mutable_data_ptr<float>();

    if (b.numel() == 1) {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_scalar_f32xf32_f32,
          out_data,
          inp1_data,
          inp2_data[0],
          mode_value,
          out.numel());
    } else if (broadcast) {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_broadcast_5D_f32xf32_f32,
          out_data,
          out_shape,
          inp1_data,
          inp1_shape,
          inp2_data,
          inp2_shape,
          mode_value,
          max_dim);
    } else {
      XT_KERNEL_CHECK(
          ctx,
          out,
          xa_nn_elm_div_f32xf32_f32,
          out_data,
          inp1_data,
          inp2_data,
          mode_value,
          out.numel());
    }
  } else {
    // Common Dtype
    ScalarType common_type =
        executorch::runtime::promoteTypes(a.scalar_type(), b.scalar_type());
    // Compute Dtype
    ScalarType compute_type =
        torch::executor::native::utils::get_compute_type(common_type);

    // Check Common Dtype
    ET_KERNEL_CHECK(
        ctx,
        (canCast(common_type, out.scalar_type()) &&
         common_type != ScalarType::Bool),
        InvalidArgument,
        out);

    ET_SWITCH_REAL_TYPES(compute_type, ctx, op_name, CTYPE_COMPUTE, [&]() {
      torch::executor::native::utils::
          apply_bitensor_elementwise_fn<CTYPE_COMPUTE, op_name>(
              [mode_is_trunc, &div_by_zero_error](
                  const CTYPE_COMPUTE val_a, const CTYPE_COMPUTE val_b) {
                if (executorch::runtime::is_integral_type<
                        CTYPE_COMPUTE,
                        /*includeBool=*/true>::value) {
                  if (val_b == 0) {
                    div_by_zero_error = true;
                    return static_cast<CTYPE_COMPUTE>(0);
                  }
                }
                CTYPE_COMPUTE value = val_a / val_b;
                if (mode_is_trunc) {
                  value = std::trunc(value);
                } else {
                  // We established above that the mode is either trunc or
                  // floor, so it must be floor.
                  value = torch::executor::native::utils::floor_divide(
                      val_a, val_b);
                }
                return value;
              },
              ctx,
              a,
              torch::executor::native::utils::SupportedTensorDtypes::REALHBBF16,
              b,
              torch::executor::native::utils::SupportedTensorDtypes::REALHBBF16,
              out,
              torch::executor::native::utils::SupportedTensorDtypes::REALHBF16);
    });
  }

  ET_KERNEL_CHECK_MSG(
      ctx,
      !div_by_zero_error,
      InvalidArgument,
      out,
      "Div mode operation encountered integer division by zero");

  return out;
}

Tensor& div_scalar_out(
    KernelRuntimeContext& ctx,
    const Tensor& a,
    const Scalar& b,
    Tensor& out) {
#ifdef OP_ARG_CHECK
  // Check Dim Order
  ET_KERNEL_CHECK(
      ctx,
      executorch::runtime::tensors_have_same_dim_order(a, out),
      InvalidArgument,
      out);

  // Resize
  ET_KERNEL_CHECK(
      ctx,
      executorch::runtime::resize_tensor(out, a.sizes()) == Error::Ok,
      InvalidArgument,
      out);
#endif

  bool optimized = true;

  if (!(((a.scalar_type() == ScalarType::Int) ||
         (a.scalar_type() == ScalarType::Float)) &&
        (out.scalar_type() == ScalarType::Float))) {
    optimized = false;
  }

  if ((b.isFloatingPoint()) && (a.scalar_type() == ScalarType::Int)) {
    optimized = false;
  }

  // @lint-ignore CLANGTIDY facebook-hte-CArray
  static constexpr const char op_name[] = "div.Scalar_out";

  if ((a.scalar_type() == ScalarType::Int) && (optimized)) {
    const int* const inp1_data = a.const_data_ptr<int>();
    int inp2_val;
    torch::executor::native::utils::extract_scalar(b, &inp2_val);

    float* const out_data = out.mutable_data_ptr<float>();

    XT_KERNEL_CHECK(
        ctx,
        out,
        xa_nn_elm_div_scalar_32x32_f32,
        out_data,
        inp1_data,
        inp2_val,
        out.numel());
  } else if ((a.scalar_type() == ScalarType::Float) && (optimized)) {
    const float* const inp1_data = a.const_data_ptr<float>();
    float inp2_val;
    torch::executor::native::utils::extract_scalar(b, &inp2_val);

    float* const out_data = out.mutable_data_ptr<float>();

    int mode = 0;
    XT_KERNEL_CHECK(
        ctx,
        out,
        xa_nn_elm_div_scalar_f32xf32_f32,
        out_data,
        inp1_data,
        inp2_val,
        mode,
        out.numel());
  } else {
    ScalarType common_type =
        executorch::runtime::isFloatingType(a.scalar_type())
        ? a.scalar_type()
        : ScalarType::Float;
    ScalarType compute_type =
        torch::executor::native::utils::get_compute_type(common_type);

    // Check Common Dtype
    ET_KERNEL_CHECK(
        ctx, common_type == out.scalar_type(), InvalidArgument, out);

    ET_SWITCH_FLOAT_TYPES(compute_type, ctx, op_name, CTYPE_COMPUTE, [&]() {
      const CTYPE_COMPUTE val_b =
          torch::executor::native::utils::scalar_to<CTYPE_COMPUTE>(b);
      torch::executor::native::utils::
          apply_unitensor_elementwise_fn<CTYPE_COMPUTE, op_name>(
              [val_b](const CTYPE_COMPUTE val_a) { return val_a / val_b; },
              ctx,
              a,
              torch::executor::native::utils::SupportedTensorDtypes::REALHBBF16,
              out,
              torch::executor::native::utils::SupportedTensorDtypes::
                  SAME_AS_COMMON);
    });
  }

  return out;
}

Tensor& div_scalar_mode_out(
    KernelRuntimeContext& ctx,
    const Tensor& a,
    const Scalar& b,
    optional<string_view> mode,
    Tensor& out) {
  if (!mode.has_value()) {
    return div_scalar_out(ctx, a, b, out);
  }

  auto mode_val = mode.value();

  // Check mode
  ET_KERNEL_CHECK(
      ctx, mode_val == "trunc" || mode_val == "floor", InvalidArgument, out);

#ifdef OP_ARG_CHECK
  // Check Dim Order
  ET_KERNEL_CHECK(
      ctx,
      executorch::runtime::tensors_have_same_dim_order(a, out),
      InvalidArgument,
      out);

  // Resize
  ET_KERNEL_CHECK(
      ctx,
      executorch::runtime::resize_tensor(out, a.sizes()) == Error::Ok,
      InvalidArgument,
      out);
#endif

  const bool mode_is_trunc = mode_val == "trunc";

  bool optimized = true;

  if (!(((a.scalar_type() == ScalarType::Int) ||
         (a.scalar_type() == ScalarType::Float)) &&
        (a.scalar_type() == out.scalar_type()))) {
    optimized = false;
  }

  if ((b.isFloatingPoint()) && (a.scalar_type() == ScalarType::Int)) {
    optimized = false;
  }

  // @lint-ignore CLANGTIDY facebook-hte-CArray
  static constexpr const char op_name[] = "div.Scalar_mode_out";

  int mode_value = (mode_val == "trunc") ? 1 : 2;

  if ((a.scalar_type() == ScalarType::Int) && (optimized)) {
    const int* const inp1_data = a.const_data_ptr<int>();
    int inp2_val;
    torch::executor::native::utils::extract_scalar(b, &inp2_val);

    int* const out_data = out.mutable_data_ptr<int>();

    XT_KERNEL_CHECK(
        ctx,
        out,
        xa_nn_elm_div_scalar_32x32_32,
        out_data,
        inp1_data,
        inp2_val,
        mode_value,
        out.numel());
  } else if ((a.scalar_type() == ScalarType::Float) && (optimized)) {
    const float* const inp1_data = a.const_data_ptr<float>();
    float inp2_val;
    torch::executor::native::utils::extract_scalar(b, &inp2_val);

    float* const out_data = out.mutable_data_ptr<float>();

    XT_KERNEL_CHECK(
        ctx,
        out,
        xa_nn_elm_div_scalar_f32xf32_f32,
        out_data,
        inp1_data,
        inp2_val,
        mode_value,
        out.numel());
  } else {
    // Common Dtype
    ScalarType common_type =
        torch::executor::native::utils::promote_type_with_scalar(
            a.scalar_type(), b);
    // Compute Dtype
    ScalarType compute_type =
        torch::executor::native::utils::get_compute_type(common_type);

    // Check Common Dtype
    ET_KERNEL_CHECK(
        ctx,
        (canCast(common_type, out.scalar_type()) &&
         common_type != ScalarType::Bool),
        InvalidArgument,
        out);

    // Check for intergral division by zero
    ET_KERNEL_CHECK_MSG(
        ctx,
        !(executorch::runtime::isIntegralType(common_type, true) &&
          torch::executor::native::utils::scalar_to<double>(b) == 0),
        InvalidArgument,
        out,
        "Div mode operation encountered integer division by zero");

    ET_SWITCH_REAL_TYPES(compute_type, ctx, op_name, CTYPE_COMPUTE, [&]() {
      const CTYPE_COMPUTE val_b =
          torch::executor::native::utils::scalar_to<CTYPE_COMPUTE>(b);
      torch::executor::native::utils::
          apply_unitensor_elementwise_fn<CTYPE_COMPUTE, op_name>(
              [val_b, mode_is_trunc](const CTYPE_COMPUTE val_a) {
                CTYPE_COMPUTE value = val_a / val_b;
                if (mode_is_trunc) {
                  value = std::trunc(value);
                } else {
                  value = torch::executor::native::utils::floor_divide(
                      val_a, val_b);
                }
                return value;
              },
              ctx,
              a,
              torch::executor::native::utils::SupportedTensorDtypes::REALHBBF16,
              out,
              torch::executor::native::utils::SupportedTensorDtypes::REALHBF16);
    });
  }

  return out;
}

} // namespace native
} // namespace G3
} // namespace impl
} // namespace cadence
