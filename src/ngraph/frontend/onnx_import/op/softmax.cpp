//*****************************************************************************
// Copyright 2017-2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#include <numeric>

#include "exceptions.hpp"
#include "ngraph/op/softmax.hpp"
#include "softmax.hpp"

namespace ngraph
{
    namespace onnx_import
    {
        namespace op
        {
            namespace set_1
            {
                NodeVector softmax(const Node& node)
                {
                    NodeVector inputs{node.get_ng_inputs()};
                    auto data = inputs.at(0);
                    auto data_shape = data->get_shape();

                    int axis = node.get_attribute_value<int64_t>("axis", 1);

                    if (axis < 0)
                    {
                        axis = data_shape.size() + axis;
                    }

                    ASSERT_VALID_ARGUMENT(node, axis < data_shape.size())
                        << "provided 'axis' value:" << axis
                        << " is out of input tensor dimensions range.";

                    return {std::make_shared<ngraph::op::set1::Softmax>(data, axis)};
                }

            } // namespace set_1

        } //namespace op

    } // namespace onnx_import

} // namespace ngraph
