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
#include <cmath>
#include <map>
#include "backend/kernel_compiler/cpu/max_unpool2d_cpu_kernel.h"
#include "runtime/device/cpu/cpu_device_address.h"

namespace mindspore {
namespace kernel {
namespace {
constexpr size_t kMaxUnpool2DInputsNum = 2;
constexpr size_t kMaxUnpool2DOutputsNum = 1;
constexpr size_t kInputIndex0 = 0;
constexpr size_t kInputIndex1 = 1;
constexpr size_t kInputIndex2 = 2;
constexpr size_t kInputIndex3 = 3;
}  // namespace

template <typename DATA_T, typename INDICES_T>
void MaxUnpool2DCPUKernel<DATA_T, INDICES_T>::InitKernel(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  kernel_name_ = AnfAlgo::GetCNodeName(kernel_node);
  node_wpt_ = kernel_node;
  input_shape_ = AnfAlgo::GetInputDeviceShape(kernel_node, kInputIndex0);
  indices_shape_ = AnfAlgo::GetInputDeviceShape(kernel_node, kInputIndex1);
  output_shape_ = AnfAlgo::GetOutputDeviceShape(kernel_node, kInputIndex0);
  data_format_ = AnfAlgo::GetNodeAttr<string>(kernel_node, FORMAT);
}

template <typename DATA_T, typename INDICES_T>
void MaxUnpool2DCPUKernel<DATA_T, INDICES_T>::OutPutInitKernel(DATA_T *raw_output, size_t length) {
  for (size_t s = 0; s < length; s++) {
    raw_output[s] = (DATA_T)0;
  }
}

template <typename DATA_T, typename INDICES_T>
bool MaxUnpool2DCPUKernel<DATA_T, INDICES_T>::Launch(const std::vector<kernel::AddressPtr> &inputs,
                                                     const std::vector<kernel::AddressPtr> &,
                                                     const std::vector<kernel::AddressPtr> &outputs) {
  auto node = node_wpt_.lock();
  if (!node) {
    MS_LOG(EXCEPTION) << "node_wpt_ is expired.";
  }
  CHECK_KERNEL_INPUTS_NUM(inputs.size(), kMaxUnpool2DInputsNum, kernel_name_);
  CHECK_KERNEL_OUTPUTS_NUM(outputs.size(), kMaxUnpool2DOutputsNum, kernel_name_);

  if (outputs[kInputIndex0]->size == 0) {
    MS_LOG(WARNING) << "MaxUnpool2D output memory size should be greater than 0, but got 0.";
    return false;
  }
  auto *raw_input = reinterpret_cast<DATA_T *>(inputs[kInputIndex0]->addr);
  auto *raw_indices = reinterpret_cast<INDICES_T *>(inputs[kInputIndex1]->addr);
  auto *raw_output = reinterpret_cast<DATA_T *>(outputs[kInputIndex0]->addr);
  if (data_format_ == "NHWC") {
    size_t num_batch = input_shape_[kInputIndex0];
    size_t input_height = input_shape_[kInputIndex1];
    size_t input_width = input_shape_[kInputIndex2];
    size_t num_channels = input_shape_[kInputIndex3];
    size_t oheight = output_shape_[kInputIndex1];
    size_t owidth = output_shape_[kInputIndex2];
    size_t length = num_batch * oheight * owidth * num_channels;
    OutPutInitKernel(raw_output, length);
    for (size_t n = 0; n < num_batch; n++) {
      size_t noutput_offset = n * num_channels * owidth * oheight;
      size_t n_input_offset = n * num_channels * input_width * input_height;
      DATA_T *output_p_k = raw_output + noutput_offset;
      DATA_T *input_p_k = raw_input + n_input_offset;
      INDICES_T *ind_p_k = raw_indices + n_input_offset;
      size_t maxp;
      size_t ind_p_k_id;
      for (size_t k = 0; k < num_channels; k++) {
        for (size_t i = 0; i < input_height; i++) {
          for (size_t j = 0; j < input_width; j++) {
            ind_p_k_id = i * input_width * num_channels + j * num_channels + k;
            maxp = ind_p_k[ind_p_k_id];
            if (ind_p_k[ind_p_k_id] < 0 || maxp >= owidth * oheight) {
              MS_LOG(EXCEPTION) << "MaxUnpool2D: internal error, output_size H * W should "
                                   "be bigger than some indicis, now H * W is "
                                << owidth * oheight << " and value of argmax is " << maxp << "." << std::endl;
              return false;
            } else {
              output_p_k[maxp * num_channels + k] = input_p_k[ind_p_k_id];
            }
          }
        }
      }
    }
  } else {
    size_t num_batch = input_shape_[kInputIndex0];
    size_t input_height = input_shape_[kInputIndex2];
    size_t input_width = input_shape_[kInputIndex3];
    size_t num_channels = input_shape_[kInputIndex1];
    size_t oheight = output_shape_[kInputIndex2];
    size_t owidth = output_shape_[kInputIndex3];
    size_t length = num_batch * oheight * owidth * num_channels;
    OutPutInitKernel(raw_output, length);
    for (size_t n = 0; n < num_batch; n++) {
      size_t noutput_offset = n * num_channels * owidth * oheight;
      size_t n_input_offset = n * num_channels * input_width * input_height;
      size_t k = 0;
      for (k = 0; k < num_channels; k++) {
        size_t final_output_offset = noutput_offset + k * owidth * oheight;
        size_t final_input_offset = n_input_offset + k * input_width * input_height;
        DATA_T *output_p_k = raw_output + final_output_offset;
        DATA_T *input_p_k = raw_input + final_input_offset;
        INDICES_T *ind_p_k = raw_indices + final_input_offset;
        size_t maxp;
        size_t ind_p_k_id;
        for (size_t i = 0; i < input_height; i++) {
          for (size_t j = 0; j < input_width; j++) {
            ind_p_k_id = i * input_width + j;
            maxp = ind_p_k[ind_p_k_id];
            if (ind_p_k[ind_p_k_id] < 0 || maxp >= owidth * oheight) {
              MS_LOG(EXCEPTION) << "MaxUnpool2D: internal error, output_size H * W should "
                                   "be bigger than some indicis, now H * W is "
                                << owidth * oheight << " and value of argmax is " << maxp << "." << std::endl;
              return false;
            } else {
              output_p_k[maxp] = input_p_k[ind_p_k_id];
            }
          }
        }
      }
    }
  }
  return true;
}
}  // namespace kernel
}  // namespace mindspore
