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

#ifndef MINDSPORE_CORE_OPS_PAD_V3_GRAD_H_
#define MINDSPORE_CORE_OPS_PAD_V3_GRAD_H_
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mindapi/base/types.h"
#include "ops/base_operator.h"

namespace mindspore {
namespace ops {
constexpr auto kNamePadV3Grad = "PadV3Grad";
/// \brief Pads the input tensor according to the paddings. Refer to Python API
/// @ref mindspore.ops.PadV3Grad for more details.
class MIND_API PadV3Grad : public BaseOperator {
 public:
  MIND_API_BASE_MEMBER(PadV3Grad);
  /// \brief Constructor.
  PadV3Grad() : BaseOperator(kNamePadV3Grad) { InitIOName({"x", "paddings"}, {"y"}); }
  explicit PadV3Grad(const std::string k_name) : BaseOperator(k_name) {}
  std::string get_mode() const;
  bool get_paddings_contiguous() const;
  std::vector<int64_t> get_paddings() const;
};
MIND_API abstract::AbstractBasePtr PadV3GradInfer(const abstract::AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                                  const std::vector<abstract::AbstractBasePtr> &input_args);
}  // namespace ops
}  // namespace mindspore

#endif  // MINDSPORE_CORE_OPS_PadV3Grad_H_
