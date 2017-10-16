// ----------------------------------------------------------------------------
// Copyright 2017 Nervana Systems Inc.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// ----------------------------------------------------------------------------

#include <cassert>
#include <cmath>

#include "ngraph/runtime/utils.hpp"

std::shared_ptr<ngraph::runtime::Tuple> ngraph::runtime::make_tuple(
    const std::vector<std::shared_ptr<ngraph::runtime::Value>>& elements)
{
    return std::make_shared<ngraph::runtime::Tuple>(elements);
}

template <typename ET>
bool ngraph::runtime::all_close(
    const std::shared_ptr<ngraph::runtime::ParameterizedTensorView<ET>>& a,
    const std::shared_ptr<ngraph::runtime::ParameterizedTensorView<ET>>& b,
    typename ET::type rtol,
    typename ET::type atol)
{
    // Check that the layouts are compatible
    if (*a->get_tensor_view_layout() != *b->get_tensor_view_layout())
    {
        throw ngraph_error("Cannot compare tensors with different layouts");
    }

    if (a->get_shape() != b->get_shape())
        return false;

    return ngraph::runtime::all_close(a->get_vector(), b->get_vector(), rtol, atol);
}

template bool ngraph::runtime::all_close<ngraph::element::Float32>(
    const std::shared_ptr<ngraph::runtime::ParameterizedTensorView<ngraph::element::Float32>>& a,
    const std::shared_ptr<ngraph::runtime::ParameterizedTensorView<ngraph::element::Float32>>& b,
    ngraph::element::Float32::type rtol,
    ngraph::element::Float32::type atol);

template bool ngraph::runtime::all_close<ngraph::element::Float64>(
    const std::shared_ptr<ngraph::runtime::ParameterizedTensorView<ngraph::element::Float64>>& a,
    const std::shared_ptr<ngraph::runtime::ParameterizedTensorView<ngraph::element::Float64>>& b,
    ngraph::element::Float64::type rtol,
    ngraph::element::Float64::type atol);

template <typename T>
bool ngraph::runtime::all_close(const std::vector<T>& a, const std::vector<T>& b, T rtol, T atol)
{
    assert(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::abs(a[i] - b[i]) > atol + rtol * std::abs(b[i]))
        {
            return false;
        }
    }
    return true;
}

template bool ngraph::runtime::all_close<float>(const std::vector<float>& a,
                                                const std::vector<float>& b,
                                                float rtol,
                                                float atol);

template bool ngraph::runtime::all_close<double>(const std::vector<double>& a,
                                                 const std::vector<double>& b,
                                                 double rtol,
                                                 double atol);
