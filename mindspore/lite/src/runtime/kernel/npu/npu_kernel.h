/**
 * Copyright 2020 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_LITE_SRC_RUNTIME_AGENT_NPU_KERNEL_NPU_KERNEL_H_
#define MINDSPORE_LITE_SRC_RUNTIME_AGENT_NPU_KERNEL_NPU_KERNEL_H_

#include <vector>
#include "src/lite_kernel.h"
#include "include/errorcode.h"
#include "include/graph/graph.h"
#include "src/kernel_registry.h"

using mindspore::kernel::LiteKernel;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_OK;
namespace mindspore::kernel {
class NPUKernel : public LiteKernel {
 public:
  NPUKernel(OpParameter *parameter, const std::vector<lite::Tensor *> &inputs,
            const std::vector<lite::Tensor *> &outputs)
      : LiteKernel(parameter, inputs, outputs, nullptr, nullptr) {}
  ~NPUKernel() override = default;

  int Run() override { return RET_ERROR; }

  virtual int IsSupport(const std::vector<lite::Tensor *> &inputs, const std::vector<lite::Tensor *> &outputs,
                        OpParameter *opParameter) {
    return RET_OK;
  }

  virtual ge::Operator *GetNPUOp() = 0;

  virtual void SetNPUInputs(const std::vector<mindspore::lite::Tensor *> &inputs,
                            const std::vector<lite::Tensor *> &outputs,
                            const std::vector<ge::Operator *> &npu_inputs) = 0;
};
template <class T>
kernel::LiteKernel *NPUKernelCreator(const std::vector<lite::Tensor *> &inputs,
                                     const std::vector<lite::Tensor *> &outputs, OpParameter *opParameter,
                                     const lite::InnerContext *ctx, const kernel::KernelKey &desc,
                                     const mindspore::lite::PrimitiveC *primitive) {
  auto *kernel = new (std::nothrow) T(opParameter, inputs, outputs);
  if (kernel == nullptr) {
    MS_LOG(ERROR) << "kernel " << opParameter->name_ << "is nullptr.";
    free(opParameter);
    return nullptr;
  }

  auto ret = kernel->IsSupport(inputs, outputs, opParameter);
  if (ret != RET_OK) {
    return nullptr;
  }
  return kernel;
}
}  // namespace mindspore::kernel
#endif  // LITE_MINDSPORE_LITE_SRC_RUNTIME_AGENT_NPU_KERNEL_NPUKERNEL_H_
