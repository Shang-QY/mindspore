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

#include "plugin/device/ascend/kernel/pyboost/baddbmm_ascend.h"
#include <algorithm>
#include <functional>
#include <memory>
#include "ir/tensor.h"
#include "runtime/device/device_address_utils.h"
#include "runtime/hardware/device_context_manager.h"
#include "transform/acl_ir/op_api_exec.h"
#include "plugin/device/ascend/kernel/pyboost/aclnn_utils.h"

namespace mindspore {
namespace kernel {
namespace pyboost {
void BaddbmmAscend::Launch(const tensor::TensorPtr &input, const tensor::TensorPtr &batch1,
                           const tensor::TensorPtr &batch2, const ScalarPtr &beta, const ScalarPtr &alpha,
                           const tensor::TensorPtr &output) {
  auto device_context = PyBoostUtils::GetDeviceContext(kAscendDevice);
  PrepareOpInputs(device_context, input, batch1, batch2);
  PrepareOpOutputs(device_context, output);
  auto stream_ptr = device_context->device_res_manager_->GetStream(kDefaultStreamIndex);
  LAUNCH_ACLNN(aclnnBaddbmm, stream_ptr, input, batch1, batch2, beta, alpha, output);
}

tensor::TensorPtr BaddbmmAscend::Call(const tensor::TensorPtr &input, const tensor::TensorPtr &batch1,
                                      const tensor::TensorPtr &batch2, const ScalarPtr &beta, const ScalarPtr &alpha) {
  MS_LOG(DEBUG) << "Call start";
  InferOutput(input, batch1, batch2, beta, alpha);
  MS_LOG(DEBUG) << "Infer end";

  if (outputs_.size() != 1) {
    MS_LOG(EXCEPTION) << "Baddbmm output size should be 1, but got " << outputs_.size();
  }

  Launch(input, batch1, batch2, beta, alpha, outputs_[0]);
  MS_LOG(DEBUG) << "Launch end";
  return outputs_[0];
}
}  // namespace pyboost
}  // namespace kernel
}  // namespace mindspore
