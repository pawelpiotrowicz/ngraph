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

#pragma once

#include <cmath>
#include <numeric>
#include <algorithm>

#include <iostream> //TODO

#include "ngraph/coordinate_transform.hpp"
#include "ngraph/util.hpp"

namespace ngraph
{
    namespace runtime
    {
        namespace reference
        {
            template <typename T>
            void lrn(const T* arg,
                     const AxisSet& axes,
                     T* out,
                     const Shape& arg_shape,
                     double dalpha,
                     double dbeta,
                     double dbias,
                     size_t size)
            {
                T alpha = static_cast<T>(dalpha);
                T beta = static_cast<T>(dbeta);
                T bias = static_cast<T>(dbias);

                CoordinateTransform input_transform(arg_shape);
                for (const Coordinate& in_coord : input_transform)
                {
                    T square_sum = 0;
                    auto h_axis = 2;
                    auto w_axis = 3;
                    auto begin_h = std::max<int>((int)0, (int)in_coord.at(h_axis) - (int)(size - 1) / 2);
                    auto end_h = std::min<int>((int)arg_shape.at(h_axis), (int)in_coord.at(h_axis) + (size - 1)/2 + 1);
                    auto begin_w = std::max<int>((int)0, (int)in_coord.at(w_axis) - (int)(size - 1) / 2);
                    auto end_w = std::min<int>((int)arg_shape.at(w_axis), (int)in_coord.at(w_axis) + (size - 1) / 2 + 1);
                    for (auto elem_h = begin_h; elem_h < end_h; ++elem_h)
                    {
                        for (auto elem_w = begin_w; elem_w < end_w; ++elem_w)
                        {
                            auto sum_coord = in_coord;
                            sum_coord.at(h_axis) = elem_h;
                            sum_coord.at(w_axis) = elem_w;
                            square_sum += arg[input_transform.index(sum_coord)] *
                                arg[input_transform.index(sum_coord)];
                        }
                    }
                    T x = arg[input_transform.index(in_coord)];
                    out[input_transform.index(in_coord)] =
                    x / (std::pow(bias + (alpha / size) * square_sum, beta));
                }
            }
        }
    }
}

