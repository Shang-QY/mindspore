/**
 * Copyright 2021 Huawei Technologies Co., Ltd
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

#include "src/custom_common.h"
#include "include/errorcode.h"
#include "include/registry/kernel_interface.h"

namespace mindspore {
/**
 * CustomAddInfer is a child class to infer current node output's information, including format, data_type and shape.
 * if inputs' shape exist -1, don't worry, which shows that shape will be inferred when running.
 */
class CustomAddInfer : public kernel::KernelInterface {
 public:
  CustomAddInfer() = default;
  ~CustomAddInfer() = default;

  int Infer(const std::vector<tensor::MSTensor *> &inputs, const std::vector<tensor::MSTensor *> &outputs,
            const schema::Primitive *primitive) override {
    outputs[0]->set_format(inputs[0]->format());
    outputs[0]->set_data_type(inputs[0]->data_type());
    auto ret = common::CheckInputs(inputs);
    if (ret != lite::RET_OK) {
      outputs[0]->set_shape({-1});  // shape{-1} shows that shape need to be inferred when running.
      return ret;
    }
    outputs[0]->set_shape(inputs[0]->shape());
    return lite::RET_OK;
  }
};
std::shared_ptr<kernel::KernelInterface> CustomAddInferCreator() { return std::make_shared<CustomAddInfer>(); }
REGISTER_CUSTOM_KERNEL_INTERFACE(Tutorial, Custom_Add, CustomAddInferCreator)
}  // namespace mindspore
