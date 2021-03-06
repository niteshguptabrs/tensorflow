/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/lite/delegates/gpu/cl/kernels/mean_stddev_normalization.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "tensorflow/lite/delegates/gpu/cl/kernels/cl_test.h"
#include "tensorflow/lite/delegates/gpu/common/operations.h"
#include "tensorflow/lite/delegates/gpu/common/status.h"

using ::testing::FloatNear;
using ::testing::Pointwise;

namespace tflite {
namespace gpu {
namespace cl {
namespace {

// Parameterized test: mean, difference, tolerance.
// Input is constructed as [mean-2*diff, mean-diff, mean+diff, mean+2*diff]
class MeanStddevNormalizationTest
    : public OpenCLOperationTest,
      public testing::WithParamInterface<std::tuple<float, float, float>> {};

TEST_P(MeanStddevNormalizationTest, SeparateBatches) {
  const float mean = std::get<0>(GetParam());
  const float diff = std::get<1>(GetParam());
  const float tolerance = std::get<2>(GetParam());

  TensorFloat32 src_tensor;
  src_tensor.shape = BHWC(1, 1, 1, 4);
  src_tensor.data = {mean - 2 * diff, mean - diff, mean + diff,
                     mean + 2 * diff};
  for (auto storage : env_.GetSupportedStorages()) {
    for (auto precision : env_.GetSupportedPrecisions()) {
      OperationDef op_def;
      op_def.precision = precision;
      auto data_type = DeduceDataTypeFromPrecision(precision);
      op_def.src_tensors.push_back({data_type, storage, Layout::BHWC});
      op_def.dst_tensors.push_back({data_type, storage, Layout::BHWC});
      TensorFloat32 dst_tensor;
      auto operation = CreateMeanStdDevNormalization(op_def);
      ASSERT_OK(ExecuteGPUOperation({src_tensor}, creation_context_, &operation,
                                    BHWC(1, 1, 1, 4), &dst_tensor));

      std::vector<float> expected_output;
      if (diff == 0.0f) {
        expected_output.assign({0.0f, 0.0f, 0.0f, 0.0f});
      } else {
        const float ksqrt16 = std::sqrt(1.6f);
        const float ksqrt04 = std::sqrt(0.4f);
        expected_output.assign({-ksqrt16, -ksqrt04, ksqrt04, ksqrt16});
      }
      EXPECT_THAT(dst_tensor.data,
                  Pointwise(FloatNear(tolerance), expected_output));
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    uKernels, MeanStddevNormalizationTest,
    testing::Values(
        std::make_tuple(0.0f, 0.0f, 0.0f),         // zero mean, zero variance
        std::make_tuple(0.0f, 0.01f, 2.53e-5f),    // zero mean, small variance
        std::make_tuple(0.0f, 100.0f, 1.20e-7f),   // zero mean, large variance
        std::make_tuple(0.01f, 0.0f, 0.0f),        // small mean, zero variance
        std::make_tuple(0.01f, 0.01f, 2.53e-5f),   // small mean, small variance
        std::make_tuple(0.01f, 100.0f, 1.20e-7f),  // small mean, large variance
        std::make_tuple(100.0f, 0.0f, 0.0f),       // large mean, zero variance
        std::make_tuple(100.0f, 0.01f, 1.81e-4f),  // large mean, small variance
        std::make_tuple(100.0f, 100.0f, 1.20e-7f)  // large mean, large variance
        ));

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MeanStddevNormalizationTest);

TEST_F(OpenCLOperationTest, MeanStddevNormalizationAllBatches) {
  TensorFloat32 src_tensor;
  src_tensor.shape = BHWC(9, 1, 1, 4);
  src_tensor.data = {
      0.0f,     0.0f,    0.0f,    0.0f,     // zero mean, zero variance
      -0.02f,   -0.01f,  0.01f,   0.02f,    // zero mean, small variance
      -200.0f,  -100.0f, 100.0f,  200.0f,   // zero mean, large variance
      0.01f,    0.01f,   0.01f,   0.01f,    // small mean, zero variance
      -0.01f,   0.0f,    0.02f,   0.03f,    // small mean, small variance
      -199.99f, -99.99f, 100.01f, 200.01f,  // small mean, large variance
      100.0f,   100.0f,  100.0f,  100.0f,   // large mean, zero variance
      99.98f,   99.99f,  100.01f, 100.02f,  // large mean, small variance
      -100.0f,  0.0f,    200.0f,  300.0f,   // large mean, large variance
  };
  for (auto storage : env_.GetSupportedStorages()) {
    for (auto precision : env_.GetSupportedPrecisions()) {
      OperationDef op_def;
      op_def.precision = precision;
      auto data_type = DeduceDataTypeFromPrecision(precision);
      op_def.src_tensors.push_back({data_type, storage, Layout::BHWC});
      op_def.dst_tensors.push_back({data_type, storage, Layout::BHWC});
      TensorFloat32 dst_tensor;
      auto operation = CreateMeanStdDevNormalization(op_def);
      ASSERT_OK(ExecuteGPUOperation({src_tensor}, creation_context_, &operation,
                                    BHWC(9, 1, 1, 4), &dst_tensor));

      const float ksqrt16 = std::sqrt(1.6f);
      const float ksqrt04 = std::sqrt(0.4f);
      const std::vector<float> expected_output = {
          0.0f,     0.0f,     0.0f,    0.0f,     // zero mean, zero variance
          -ksqrt16, -ksqrt04, ksqrt04, ksqrt16,  // zero mean, small variance
          -ksqrt16, -ksqrt04, ksqrt04, ksqrt16,  // zero mean, large variance
          0.0f,     0.0f,     0.0f,    0.0f,     // small mean, zero variance
          -ksqrt16, -ksqrt04, ksqrt04, ksqrt16,  // small mean, small variance
          -ksqrt16, -ksqrt04, ksqrt04, ksqrt16,  // small mean, large variance
          0.0f,     0.0f,     0.0f,    0.0f,     // large mean, zero variance
          -ksqrt16, -ksqrt04, ksqrt04, ksqrt16,  // large mean, small variance
          -ksqrt16, -ksqrt04, ksqrt04, ksqrt16,  // large mean, large variance
      };
      EXPECT_THAT(dst_tensor.data,
                  Pointwise(FloatNear(1.81e-4f), expected_output));
    }
  }
}

}  // namespace
}  // namespace cl
}  // namespace gpu
}  // namespace tflite
