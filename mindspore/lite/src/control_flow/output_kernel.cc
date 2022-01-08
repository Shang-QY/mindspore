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

#include "src/control_flow/output_kernel.h"
#include "src/tensor.h"
#include "src/inner_kernel.h"

namespace mindspore::kernel {
int OutputKernel::Run() {
  for (size_t i = 0; i < in_tensors().size(); ++i) {
    auto src_tensor = in_tensors()[i];
    auto dst_tensor = out_tensors()[i];
    memcpy(dst_tensor->data(), src_tensor->data(), src_tensor->Size());
  }
  return lite::RET_OK;
}

LiteKernel *OutputKernel::Create(std::vector<lite::Tensor *> in_tensors, std::vector<lite::Tensor *> out_tensors,
                                 const lite::InnerContext *ctx) {
  auto *param = reinterpret_cast<OpParameter *>(malloc(sizeof(OpParameter)));
  if (param == nullptr) {
    MS_LOG(ERROR) << "malloc OpParameter failed.";
    return nullptr;
  }
  memset(param, 0, sizeof(OpParameter));
  param->type_ = schema::PrimitiveType_NONE;
  auto inner_kernel = new OutputKernel(param, in_tensors, out_tensors, ctx);
  MS_CHECK_TRUE_MSG(inner_kernel != nullptr, nullptr, "new inner kernel failed.");
  std::shared_ptr<kernel::Kernel> shared_kernel(inner_kernel);
  auto *lite_kernel = new LiteKernel(shared_kernel);
  return lite_kernel;
}
int OutputKernel::PreProcess() {
  if (in_tensors().size() != out_tensors().size()) {
    MS_LOG(ERROR) << "output kernel in_tensors size is not same as out_tensors size.";
    return lite::RET_ERROR;
  }
  for (size_t i = 0; i < in_tensors().size(); ++i) {
    auto src_tensor = in_tensors()[i];
    auto dst_tensor = out_tensors()[i];
    dst_tensor->set_shape(src_tensor->shape());
    dst_tensor->set_format(src_tensor->format());
  }
  return InnerKernel::PreProcess();
}
}  // namespace mindspore::kernel
