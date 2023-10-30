/**
 * Copyright 2023 Huawei Technologies Co., Ltd
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

#include "kernel/pyboost/op/erf.h"
#include "kernel/pyboost/py_boost_utils.h"
#include "abstract/ops/primitive_infer_map.h"

namespace mindspore {
namespace kernel {
namespace pyboost {
void Erf::CastInput() {}

tensor::TensorPtr Erf::Call(const tensor::TensorPtr &x) {
  // TODO: kernel_mod->launch
  return mindspore::tensor::TensorPtr();
}
}  // namespace pyboost
}  // namespace kernel
}  // namespace mindspore