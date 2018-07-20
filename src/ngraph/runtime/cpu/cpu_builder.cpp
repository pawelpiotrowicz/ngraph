/*******************************************************************************
* Copyright 2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "ngraph/runtime/cpu/cpu_builder.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include "ngraph/node.hpp"
#include "ngraph/op/abs.hpp"
#include "ngraph/op/acos.hpp"
#include "ngraph/op/add.hpp"
#include "ngraph/op/allreduce.hpp"
#include "ngraph/op/and.hpp"
#include "ngraph/op/asin.hpp"
#include "ngraph/op/atan.hpp"
#include "ngraph/op/ceiling.hpp"
#include "ngraph/op/constant.hpp"
#include "ngraph/op/cos.hpp"
#include "ngraph/op/cosh.hpp"
#include "ngraph/op/divide.hpp"
#include "ngraph/op/equal.hpp"
#include "ngraph/op/exp.hpp"
#include "ngraph/op/floor.hpp"
#include "ngraph/op/get_output_element.hpp"
#include "ngraph/op/greater.hpp"
#include "ngraph/op/greater_eq.hpp"
#include "ngraph/op/less.hpp"
#include "ngraph/op/less_eq.hpp"
#include "ngraph/op/log.hpp"
#include "ngraph/op/maximum.hpp"
#include "ngraph/op/minimum.hpp"
#include "ngraph/op/multiply.hpp"
#include "ngraph/op/negative.hpp"
#include "ngraph/op/not.hpp"
#include "ngraph/op/not_equal.hpp"
#include "ngraph/op/one_hot.hpp"
#include "ngraph/op/op.hpp"
#include "ngraph/op/or.hpp"
#include "ngraph/op/pad.hpp"
#include "ngraph/op/parameter.hpp"
#include "ngraph/op/power.hpp"
#include "ngraph/op/product.hpp"
#include "ngraph/op/reduce.hpp"
#include "ngraph/op/reduce_window.hpp"
#include "ngraph/op/relu.hpp"
#include "ngraph/op/remainder.hpp"
#include "ngraph/op/replace_slice.hpp"
#include "ngraph/op/result.hpp"
#include "ngraph/op/reverse_sequence.hpp"
#include "ngraph/op/select.hpp"
#include "ngraph/op/select_and_scatter.hpp"
#include "ngraph/op/sign.hpp"
#include "ngraph/op/sin.hpp"
#include "ngraph/op/sinh.hpp"
#include "ngraph/op/slice.hpp"
#include "ngraph/op/softmax.hpp"
#include "ngraph/op/sqrt.hpp"
#include "ngraph/op/subtract.hpp"
#include "ngraph/op/tan.hpp"
#include "ngraph/op/tanh.hpp"
#include "ngraph/runtime/cpu/cpu_kernels.hpp"
#include "ngraph/runtime/cpu/cpu_op_annotations.hpp"
#include "ngraph/runtime/cpu/kernel/abs.hpp"
#include "ngraph/runtime/cpu/kernel/add.hpp"
#include "ngraph/runtime/cpu/kernel/broadcast.hpp"
#include "ngraph/runtime/cpu/kernel/ceil.hpp"
#include "ngraph/runtime/cpu/kernel/cwise_pow.hpp"
#include "ngraph/runtime/cpu/kernel/divide.hpp"
#include "ngraph/runtime/cpu/kernel/equal.hpp"
#include "ngraph/runtime/cpu/kernel/exp.hpp"
#include "ngraph/runtime/cpu/kernel/floor.hpp"
#include "ngraph/runtime/cpu/kernel/greater.hpp"
#include "ngraph/runtime/cpu/kernel/greater_eq.hpp"
#include "ngraph/runtime/cpu/kernel/less.hpp"
#include "ngraph/runtime/cpu/kernel/less_eq.hpp"
#include "ngraph/runtime/cpu/kernel/log.hpp"
#include "ngraph/runtime/cpu/kernel/maximum.hpp"
#include "ngraph/runtime/cpu/kernel/minimum.hpp"
#include "ngraph/runtime/cpu/kernel/multiply.hpp"
#include "ngraph/runtime/cpu/kernel/negative.hpp"
#include "ngraph/runtime/cpu/kernel/not.hpp"
#include "ngraph/runtime/cpu/kernel/not_equal.hpp"
#include "ngraph/runtime/cpu/kernel/relu.hpp"
#include "ngraph/runtime/cpu/kernel/result.hpp"
#include "ngraph/runtime/cpu/kernel/sqrt.hpp"
#include "ngraph/runtime/cpu/kernel/subtract.hpp"
#include "ngraph/runtime/cpu/op/convert_layout.hpp"
#include "ngraph/type/element_type.hpp"
#include "ngraph/util.hpp"

#ifdef NGRAPH_DISTRIBUTED
#include <mpi.h>
#include "ngraph/op/allreduce.hpp"
#endif

using namespace std;
using namespace ngraph;

namespace ngraph
{
    namespace runtime
    {
        namespace cpu
        {
            template <>
            void Builder::BUILDER_DECL(ngraph::op::Add)
            {
                BUILD_BINARY_ELEMWISE_FUNCTOR(runtime::cpu::kernel::add);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Subtract)
            {
                BUILD_BINARY_ELEMWISE_FUNCTOR(runtime::cpu::kernel::subtract);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Multiply)
            {
                BUILD_BINARY_ELEMWISE_FUNCTOR(runtime::cpu::kernel::multiply);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Abs)
            {
                BUILD_UNARY_ELEMWISE_FUNCTOR(runtime::cpu::kernel::abs);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Broadcast)
            {
                std::function<void(void*, void*, const Shape&, const Shape&, const AxisSet&)>
                    kernel;

                SELECT_KERNEL(kernel, out[0].get_element_type(), runtime::cpu::kernel::broadcast);

                auto& functors = external_function->get_functors();
                auto& tensor_data = external_function->get_tensor_data();

                auto arg0_shape = args[0].get_shape();
                auto result_shape = out[0].get_shape();

                auto& arg0_tensor = tensor_data[args[0].get_name()];
                auto& out_tensor = tensor_data[out[0].get_name()];

                auto broadcast = static_cast<const ngraph::op::Broadcast*>(node);
                auto broadcast_axes = broadcast->get_broadcast_axes();

                auto functor =
                    [&, kernel, arg0_shape, result_shape, broadcast_axes](CPURuntimeContext* ctx) {
                        kernel(arg0_tensor, out_tensor, arg0_shape, result_shape, broadcast_axes);
                    };
                functors.emplace_back(functor);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Ceiling)
            {
                BUILD_UNARY_ELEMWISE_FUNCTOR(runtime::cpu::kernel::ceil);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Relu)
            {
                BUILD_UNARY_ELEMWISE_FUNCTOR(runtime::cpu::kernel::relu);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Result)
            {
                BUILD_UNARY_ELEMWISE_FUNCTOR(runtime::cpu::kernel::result);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Exp)
            {
                BUILD_UNARY_ELEMWISE_FUNCTOR(runtime::cpu::kernel::exp);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Log)
            {
                BUILD_UNARY_ELEMWISE_FUNCTOR(runtime::cpu::kernel::log);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Not)
            {
                BUILD_UNARY_ELEMWISE_FUNCTOR(runtime::cpu::kernel::logical_not);
            }

            template <>
            void Builder::BUILDER_DECL(ngraph::op::Constant)
            {
                auto& functors = external_function->get_functors();
                auto& tensor_data = external_function->get_tensor_data();

                vector<void**> dest;
                for (auto& result : external_function->get_function()->get_results())
                {
                    if (result.get() == node)
                    {
                        dest.push_back(&tensor_data[result->get_output_tensor(0).get_name()]);
                    }
                }
                auto& src = tensor_data[node->get_output_tensor(0).get_name()];
                auto size = node->get_output_tensor(0).size();
                auto functor = [&, dest, src, size](CPURuntimeContext* ctx) {
                    for (auto p : dest)
                    {
                        memcpy(*p, src, size);
                    }
                };
                functors.emplace_back(functor);
            }

#define TI(x) type_index(typeid(x))

            BuildOpMap build_dispatcher{
                {TI(ngraph::op::Parameter), &runtime::cpu::Builder::nop},
                {TI(ngraph::runtime::cpu::op::ConvertLayout),
                 &runtime::cpu::Builder::build<ngraph::runtime::cpu::op::ConvertLayout>}};

            REGISTER_OP_BUILDER(Constant);
            REGISTER_OP_BUILDER(Result);
            REGISTER_OP_BUILDER(Add);
            REGISTER_OP_BUILDER(Subtract);
            REGISTER_OP_BUILDER(Multiply);
            REGISTER_OP_BUILDER(Divide);
            REGISTER_OP_BUILDER(Power);
            REGISTER_OP_BUILDER(Abs);
            REGISTER_OP_BUILDER(Ceiling);
            REGISTER_OP_BUILDER(Floor);
            REGISTER_OP_BUILDER(Negative);
            REGISTER_OP_BUILDER(Relu);
            REGISTER_OP_BUILDER(Exp);
            REGISTER_OP_BUILDER(Log);
            REGISTER_OP_BUILDER(Sqrt);

            REGISTER_OP_BUILDER(Not);
            REGISTER_OP_BUILDER(Equal);
            REGISTER_OP_BUILDER(NotEqual);
            REGISTER_OP_BUILDER(Greater);
            REGISTER_OP_BUILDER(GreaterEq);
            REGISTER_OP_BUILDER(Less);
            REGISTER_OP_BUILDER(LessEq);
            REGISTER_OP_BUILDER(Maximum);
            REGISTER_OP_BUILDER(Minimum);
        }
    }
}
