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

import numpy as np
import pytest
import test_utils

from mindspore import ops
import mindspore as ms


@test_utils.run_with_cell
def greater_equal_forward_func(x, y):
    return ops.auto_generate.greater_equal(x, y)


@test_utils.run_with_cell
def greater_equal_backward_func(x, y):
    return ops.grad(greater_equal_forward_func, (0,))(x, y)


@pytest.mark.level1
@pytest.mark.env_onecard
@pytest.mark.platform_x86_cpu
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.parametrize('mode', [ms.GRAPH_MODE])
@test_utils.run_test_func
def test_greater_equal_forward(mode):
    """
    Feature: Ops.
    Description: test op greater_equal.
    Expectation: expect correct result.
    """
    ms.context.set_context(mode=mode)
    x = ms.Tensor(np.array([1, 2, 3]), ms.int32)
    y = ms.Tensor(np.array([1, 1, 4]), ms.int32)
    expect_out = np.array([True, True, False])
    out = greater_equal_forward_func(x, y)
    assert np.allclose(out.asnumpy(), expect_out)


@pytest.mark.level1
@pytest.mark.env_onecard
@pytest.mark.platform_x86_cpu
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.parametrize('mode', [ms.GRAPH_MODE])
@test_utils.run_test_func
def test_greater_equal_backward(mode):
    """
    Feature: Auto grad.
    Description: test auto grad of op greater_equal.
    Expectation: expect correct result.
    """
    ms.context.set_context(mode=mode)
    x = ms.Tensor(np.array([1, 2, 3]), ms.float32)
    y = ms.Tensor(np.array([1, 1, 4]), ms.float32)
    expect_out = np.array([0, 0, 0])
    grads = greater_equal_backward_func(x, y)
    assert np.allclose(grads.asnumpy(), expect_out)


@pytest.mark.level1
@pytest.mark.env_onecard
@pytest.mark.platform_x86_cpu
@pytest.mark.platform_x86_gpu_training
@pytest.mark.platform_arm_ascend_training
@pytest.mark.parametrize('mode', [ms.GRAPH_MODE])
def test_greater_equal_vmap(mode):
    """
    Feature: test vmap function.
    Description: test greater_equal op vmap.
    Expectation: expect correct result.
    """
    ms.context.set_context(mode=mode)
    in_axes = -1
    x = ms.Tensor(np.array([[[1, 2, 3]]]), ms.int32)
    y = ms.Tensor(np.array([[[1, 1, 4]]]), ms.int32)
    expect_out = np.array([[[True]], [[True]], [[False]]])
    nest_vmap = ops.vmap(ops.vmap(
        greater_equal_forward_func, in_axes=in_axes, out_axes=0), in_axes=in_axes, out_axes=0)
    out = nest_vmap(x, y)
    assert np.allclose(out.asnumpy(), expect_out)
