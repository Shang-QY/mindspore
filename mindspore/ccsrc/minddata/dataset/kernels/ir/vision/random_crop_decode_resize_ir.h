/**
 * Copyright 2020-2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MINDSPORE_CCSRC_MINDDATA_DATASET_KERNELS_IR_VISION_RANDOM_CROP_DECODE_RESIZE_IR_H_
#define MINDSPORE_CCSRC_MINDDATA_DATASET_KERNELS_IR_VISION_RANDOM_CROP_DECODE_RESIZE_IR_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "include/api/status.h"
#include "minddata/dataset/include/dataset/constants.h"
#include "minddata/dataset/include/dataset/transforms.h"
#include "minddata/dataset/kernels/ir/tensor_operation.h"
#include "minddata/dataset/kernels/ir/vision/random_resized_crop_ir.h"

namespace mindspore {
namespace dataset {

namespace vision {

constexpr char kRandomCropDecodeResizeOperation[] = "RandomCropDecodeResize";

class RandomCropDecodeResizeOperation : public RandomResizedCropOperation {
 public:
  RandomCropDecodeResizeOperation(std::vector<int32_t> size, std::vector<float> scale, std::vector<float> ratio,
                                  InterpolationMode interpolation, int32_t max_attempts);

  explicit RandomCropDecodeResizeOperation(const RandomResizedCropOperation &base);

  ~RandomCropDecodeResizeOperation();

  std::shared_ptr<TensorOp> Build() override;

  std::string Name() const override;

  Status to_json(nlohmann::json *out_json) override;
};

}  // namespace vision
}  // namespace dataset
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_MINDDATA_DATASET_KERNELS_IR_VISION_RANDOM_CROP_DECODE_RESIZE_IR_H_
