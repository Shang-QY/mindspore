# Copyright 2023 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================

import pytest
import numpy as np
import mindspore as ms
from mindspore import ops, Tensor


@ms.jit
def flatten_forward_func(x):
    return ops.auto_generate.flatten_(x)


@ms.jit
def flatten_backward_func(x):
    return ops.grad(flatten_forward_func, (0,))(x)


@pytest.mark.level0
@pytest.mark.env_onecard
@pytest.mark.platform_x86_cpu
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
def test_flatten_forward():
    """
    Feature: Ops.
    Description: test op flatten.
    Expectation: expect correct result.
    """
    x = Tensor(np.array([[[-1, 0], [1, 2], [3, 4]]]), ms.float32)
    output = flatten_forward_func(x)
    expect = [[-1., 0., 1., 2., 3., 4.]]
    assert np.allclose(output.asnumpy(), expect, rtol=0.001)


@pytest.mark.level0
@pytest.mark.env_onecard
@pytest.mark.platform_x86_cpu
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
def test_flatten_backward():
    """
    Feature: Auto grad.
    Description: test auto grad of op flatten.
    Expectation: expect correct result.
    """
    x = Tensor(np.array([[-1, 0], [1, 2], [3, 4]]), ms.float32)
    output = flatten_backward_func(x)
    expect = [[1., 1.], [1., 1.], [1., 1.]]
    assert np.allclose(output.asnumpy(), expect, rtol=0.001)


@pytest.mark.level0
@pytest.mark.env_onecard
@pytest.mark.platform_x86_cpu
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
def test_flatten_vmap():
    """
    Feature: test vmap function.
    Description: test flatten op vmap.
    Expectation: expect correct result.
    """
    x = Tensor(np.array([[[[-1., 0.], [1., 2.]]]]))
    nest_vmap = ops.vmap(ops.vmap(flatten_forward_func, in_axes=0), in_axes=0)
    output = nest_vmap(x)
    expect = [[[[-1., 0.], [1., 2.]]]]
    assert np.allclose(output.asnumpy(), expect, rtol=1e-4, atol=1e-4)
