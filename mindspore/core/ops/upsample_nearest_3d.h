/**
 * Copyright 2022 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_CORE_OPS_UPSAMPLE_NEAREST_3D_H_
#define MINDSPORE_CORE_OPS_UPSAMPLE_NEAREST_3D_H_
#include <map>
#include <vector>
#include <string>
#include <memory>

#include "ops/base_operator.h"
#include "mindapi/base/types.h"

namespace mindspore {
namespace ops {
constexpr auto kNameUpsampleNearest3D = "UpsampleNearest3D";
class MIND_API UpsampleNearest3D : public BaseOperator {
 public:
  MIND_API_BASE_MEMBER(UpsampleNearest3D);
  UpsampleNearest3D() : BaseOperator(kNameUpsampleNearest3D) { InitIOName({"x"}, {"y"}); }
  std::vector<int64_t> get_output_size_attr() const;
  std::vector<float> get_scales_attr() const;
};
using PrimUpsampleNearest3D = std::shared_ptr<UpsampleNearest3D>;
}  // namespace ops
}  // namespace mindspore

#endif  // MINDSPORE_CORE_OPS_UPSAMPLE_NEAREST_3D_H_
