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
#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_MAXUNPOOL3DGRAD_CPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_MAXUNPOOL3DGRAD_CPU_KERNEL_H_
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include "backend/kernel_compiler/cpu/cpu_kernel.h"
#include "backend/kernel_compiler/cpu/cpu_kernel_factory.h"

namespace mindspore {
namespace kernel {
template <typename DATA_T, typename INDICES_T>
class MaxUnpool3DGradCPUKernel : public CPUKernel {
 public:
  MaxUnpool3DGradCPUKernel() = default;
  ~MaxUnpool3DGradCPUKernel() override = default;

  void InitKernel(const CNodePtr &kernel_node) override;

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
              const std::vector<AddressPtr> &outputs) override;

 private:
  void OutPutInitKernel(DATA_T *rawOutput, size_t length);
  CNodeWeakPtr node_wpt_;
  std::vector<size_t> input_shape_;
  std::vector<size_t> grads_shape_;
  std::vector<size_t> indices_shape_;
  std::vector<size_t> output_shape_;
  std::string data_format_;
};

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeUInt8)
                        .AddInputAttr(kNumberTypeUInt8)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeUInt8),
                      MaxUnpool3DGradCPUKernel, uint8_t, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeUInt8)
                        .AddInputAttr(kNumberTypeUInt8)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeUInt8),
                      MaxUnpool3DGradCPUKernel, uint8_t, int64_t);

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeUInt16)
                        .AddInputAttr(kNumberTypeUInt16)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeUInt16),
                      MaxUnpool3DGradCPUKernel, uint16_t, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeUInt16)
                        .AddInputAttr(kNumberTypeUInt16)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeUInt16),
                      MaxUnpool3DGradCPUKernel, uint16_t, int64_t);

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeUInt32)
                        .AddInputAttr(kNumberTypeUInt32)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeUInt32),
                      MaxUnpool3DGradCPUKernel, uint32_t, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeUInt32)
                        .AddInputAttr(kNumberTypeUInt32)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeUInt32),
                      MaxUnpool3DGradCPUKernel, uint32_t, int64_t);

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeUInt64)
                        .AddInputAttr(kNumberTypeUInt64)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeUInt64),
                      MaxUnpool3DGradCPUKernel, uint64_t, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeUInt64)
                        .AddInputAttr(kNumberTypeUInt64)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeUInt64),
                      MaxUnpool3DGradCPUKernel, uint64_t, int64_t);

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeInt8)
                        .AddInputAttr(kNumberTypeInt8)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeInt8),
                      MaxUnpool3DGradCPUKernel, int8_t, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeInt8)
                        .AddInputAttr(kNumberTypeInt8)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeInt8),
                      MaxUnpool3DGradCPUKernel, int8_t, int64_t);

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeInt16)
                        .AddInputAttr(kNumberTypeInt16)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeInt16),
                      MaxUnpool3DGradCPUKernel, int16_t, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeInt16)
                        .AddInputAttr(kNumberTypeInt16)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeInt16),
                      MaxUnpool3DGradCPUKernel, int16_t, int64_t);

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeInt32)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeInt32),
                      MaxUnpool3DGradCPUKernel, int32_t, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeInt32)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeInt32),
                      MaxUnpool3DGradCPUKernel, int32_t, int64_t);

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeInt64)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeInt64),
                      MaxUnpool3DGradCPUKernel, int64_t, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeInt64)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeInt64),
                      MaxUnpool3DGradCPUKernel, int64_t, int64_t);

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeFloat16)
                        .AddInputAttr(kNumberTypeFloat16)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeFloat16),
                      MaxUnpool3DGradCPUKernel, float16, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeFloat16)
                        .AddInputAttr(kNumberTypeFloat16)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeFloat16),
                      MaxUnpool3DGradCPUKernel, float16, int64_t);

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeFloat32)
                        .AddInputAttr(kNumberTypeFloat32)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeFloat32),
                      MaxUnpool3DGradCPUKernel, float, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeFloat32)
                        .AddInputAttr(kNumberTypeFloat32)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeFloat32),
                      MaxUnpool3DGradCPUKernel, float, int64_t);

MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeFloat64)
                        .AddInputAttr(kNumberTypeFloat64)
                        .AddInputAttr(kNumberTypeInt32)
                        .AddOutputAttr(kNumberTypeFloat64),
                      MaxUnpool3DGradCPUKernel, double, int32_t);
MS_REG_CPU_KERNEL_T_S(MaxUnpool3DGrad,
                      KernelAttr()
                        .AddInputAttr(kNumberTypeFloat64)
                        .AddInputAttr(kNumberTypeFloat64)
                        .AddInputAttr(kNumberTypeInt64)
                        .AddOutputAttr(kNumberTypeFloat64),
                      MaxUnpool3DGradCPUKernel, double, int64_t);
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_MAXUNPOOL3DGRAD_CPU_KERNEL_H_
