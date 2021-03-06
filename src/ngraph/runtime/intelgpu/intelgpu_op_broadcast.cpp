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

#include "ngraph/code_writer.hpp"
#include "ngraph/runtime/intelgpu/intelgpu_kernels.hpp"
#include "ngraph/runtime/intelgpu/intelgpu_op_custom_kernels.hpp"

#include "ngraph/util.hpp"

using namespace std;
using namespace ngraph;
using namespace ngraph::runtime::intelgpu;

static CustomKernels::krnl_info do_sum_to_scalar_operation(const string& input_name,
                                                           const Shape& input_shape,
                                                           const element::Type& input_type,
                                                           const string& output_name,
                                                           const Shape& output_shape,
                                                           const element::Type& output_type,
                                                           const AxisSet& axis)
{
    const string function_name = "sum_to_scalar_" + output_name;
    const string input_type_str = get_opencl_type_name(input_type);
    const string output_type_str = get_opencl_type_name(output_type);
    const size_t main_loop_count = shape_size(input_shape);
    const size_t vect_channels = 32;
    CodeWriter writer;
    vector<size_t> gws = {32};
    vector<size_t> lws = {vect_channels};

    // The kernel name and parameters
    writer << "__attribute__((intel_reqd_sub_group_size(" << vect_channels << ")))\n"
           << "__kernel void " << function_name << "(const __global " << input_type_str
           << " *input0, __global " << output_type_str << " *output)\n";
    writer.block_begin();
    { // Main function body

        writer << "//  input array dims: input0" << array_dims(input_shape) << "\n"
               << "// output array dims: output" << array_dims(output_shape) << "\n"
               << output_type_str << " result = 0.0f;\n"
               << "const uint id = get_sub_group_local_id();\n"
               << "uint element_id = id;\n"
               << "for (uint i = 0; i < " << main_loop_count << " / " << vect_channels
               << "; ++i)\n";
        writer.block_begin();
        {
            writer << "result += input0[element_id];\n"
                   << "element_id += " << vect_channels << ";\n";
        }
        writer.block_end();

        writer << "if (element_id < " << main_loop_count << ")\n";
        writer.block_begin();
        {
            writer << "result += input0[element_id];\n";
        }
        writer.block_end();

        writer << output_type_str << " sub_group_result = sub_group_reduce_add(result);\n";

        writer << "if (id == 0)\n";
        writer.block_begin();
        {
            writer << "*output = sub_group_result;\n";
        }
        writer.block_end();
    } // End of function bracket
    writer.block_end();

    const CustomKernelInfo op_bcast_sum(output_name,
                                        output_shape,
                                        output_type,
                                        {input_name},
                                        {writer.get_code()},
                                        function_name,
                                        gws,
                                        lws);
    return {op_bcast_sum};
}

// This implements Broadcast and Sum nGraph operations.
// input_shape (bcast) or output_shape (sum) can be empty.
// If the shape is empty it means scalar
static CustomKernels::krnl_info
    do_bcast_sum_operation(const shared_ptr<Node>& op, const AxisSet& axis, bool is_bcast)
{
    const string& input_name = op->get_input_tensor_name(0);
    const Shape& input_shape = op->get_input_shape(0);
    const element::Type& input_type = op->get_input_element_type(0);
    const string& output_name = op->get_output_tensor_name(0);
    const Shape& output_shape = op->get_output_shape(0);
    const element::Type& output_type = op->get_output_element_type(0);
    string function_name = is_bcast ? "broadcast_" : "sum_";
    function_name += output_name;
    CodeWriter writer;
    vector<size_t> gws;

    gen_func_def(writer,
                 function_name,
                 {get_opencl_type_name(input_type)},
                 {input_shape},
                 get_opencl_type_name(output_type),
                 output_shape);
    writer.block_begin();
    {
        if (is_bcast)
        {
            // Broadcast loops
            gws = generate_loops(writer, output_shape, true);

            writer << "output" << access_dims(output_shape) << " = input0"
                   << access_dims(output_shape, "i", axis) << ";\n";

            // Closing brackets for Broadcast loop
            generate_loops(writer, output_shape, false);
        }
        else
        {
            // corner case with scalar
            if (output_shape.empty() || (!output_shape.empty() && (shape_size(output_shape) == 1)))
            {
                return do_sum_to_scalar_operation(input_name,
                                                  input_shape,
                                                  input_type,
                                                  output_name,
                                                  output_shape,
                                                  output_type,
                                                  axis);
            }

            const string opencl_type_name = get_opencl_type_name(output_type);
            const string reduction_init_acc = opencl_type_name + " result = 0.0f;\n" +
                                              opencl_type_name + " compensation = 0.0f;\n";
            const string reduction_str =
                "output" + access_dims(input_shape, "i", axis) + " = result;\n";

            // Generate loops related to input order with GWS
            gws = generate_loops_w_axes(writer, input_shape, true, axis, reduction_init_acc);

            writer << opencl_type_name << " y = input0" << access_dims(input_shape)
                   << " - compensation;\n"
                   << opencl_type_name << " t = result + y;\n"
                   << "compensation = (t - result) - y;\n"
                   << "result = t;\n";

            // Close brackets related to input order with reduction
            generate_loops_w_axes(writer, input_shape, false, axis, reduction_str);
        }
    } // End of function bracket
    writer.block_end();

    const CustomKernelInfo op_bcast_sum(output_name,
                                        output_shape,
                                        output_type,
                                        {input_name},
                                        {writer.get_code()},
                                        function_name,
                                        gws);
    return {op_bcast_sum};
}

// This implements Min and Max operations depends on is_min parameter
static CustomKernels::krnl_info
    do_max_min_operation(const shared_ptr<op::util::ArithmeticReduction>& op, bool is_min)
{
    const string& input_name = op->get_input_tensor_name(0);
    const Shape& input_shape = op->get_input_shape(0);
    const string& output_name = op->get_output_tensor_name(0);
    const Shape& output_shape = op->get_output_shape(0);
    const element::Type& output_type = op->get_output_element_type(0);
    const AxisSet& axis = op->get_reduction_axes();
    const string function_name = "min_max_" + output_name;
    const size_t input_size = shape_size<Shape>(input_shape);
    const string& init_value = get_opencl_type_min_max_value(output_type, !is_min);
    const string& operation = is_min ? " < " : " > ";
    CodeWriter writer;

    gen_func_def(writer,
                 function_name,
                 {get_opencl_type_name(output_type)},
                 {input_shape},
                 get_opencl_type_name(output_type),
                 output_shape);

    writer.block_begin();
    {
        // Initialization loop
        size_t var_idx = 0;
        for (auto const& i : output_shape)
        {
            writer << "for (uint i" << var_idx << " = 0; i" << var_idx << " < " << i << "; ++i"
                   << var_idx << ")\n";
            writer.block_begin();
            ++var_idx;
        }

        writer << "output" << access_dims(output_shape) << " = " << init_value << ";\n";

        // Closing brackets for initialization loop
        for (auto const& i : output_shape)
        {
            writer.block_end();
        }

        if (input_size && !input_shape.empty())
        {
            // Main operation loop
            var_idx = 0;
            for (auto const& i : input_shape)
            {
                writer << "for (uint i" << var_idx << " = 0; i" << var_idx << " < " << i << "; ++i"
                       << var_idx << ")\n";
                writer.block_begin();
                ++var_idx;
            }

            writer << "if (input0" << access_dims(input_shape) << operation << "output"
                   << access_dims(input_shape, "i", axis) << ")\n";
            writer.block_begin();
            {
                writer << "output" << access_dims(input_shape, "i", axis) << " = input0"
                       << access_dims(input_shape) << ";\n";
            }
            writer.block_end();

            // Closing brackets for loop
            for (auto const& i : input_shape)
            {
                writer.block_end();
            }
        }
    } // End of function bracket
    writer.block_end();

    const CustomKernelInfo op_min_max(
        output_name, output_shape, output_type, {input_name}, {writer.get_code()}, function_name);
    return {op_min_max};
}

CustomKernels::krnl_info CustomKernels::build_krnl(const shared_ptr<op::Product>& op) const
{
    const string& input_name = op->get_input_tensor_name(0);
    const Shape& input_shape = op->get_input_shape(0);
    const string& output_name = op->get_output_tensor_name(0);
    const Shape& output_shape = op->get_output_shape(0);
    const element::Type& output_type = op->get_output_element_type(0);
    const AxisSet& axis = op->get_reduction_axes();
    const string function_name = "product_" + output_name;
    const size_t input_size = shape_size<Shape>(input_shape);
    CodeWriter writer;

    gen_func_def(writer,
                 function_name,
                 {get_opencl_type_name(output_type)},
                 {input_shape},
                 get_opencl_type_name(output_type),
                 output_shape);

    writer.block_begin();
    {
        // Initialization loop
        size_t var_idx = 0;
        for (auto const& i : output_shape)
        {
            writer << "for (uint i" << var_idx << " = 0; i" << var_idx << " < " << i << "; ++i"
                   << var_idx << ")\n";
            writer.block_begin();
            ++var_idx;
        }

        writer << "output" << access_dims(output_shape) << " = 1;\n";

        // Closing brackets for initialization loop
        for (auto const& i : output_shape)
        {
            writer.block_end();
        }

        if (input_size && !input_shape.empty())
        {
            // Main operation loop
            var_idx = 0;
            for (auto const& i : input_shape)
            {
                writer << "for (uint i" << var_idx << " = 0; i" << var_idx << " < " << i << "; ++i"
                       << var_idx << ")\n";
                writer.block_begin();
                ++var_idx;
            }

            writer << "output" << access_dims(input_shape, "i", axis) << " *= input0"
                   << access_dims(input_shape) << ";\n";

            // Closing brackets for loop
            for (auto const& i : input_shape)
            {
                writer.block_end();
            }
        }
    } // End of function bracket
    writer.block_end();

    const CustomKernelInfo op_product(
        output_name, output_shape, output_type, {input_name}, {writer.get_code()}, function_name);
    return {op_product};
}

CustomKernels::krnl_info CustomKernels::build_krnl(const shared_ptr<op::Broadcast>& op) const
{
    return do_bcast_sum_operation(op, op->get_broadcast_axes(), true);
}

CustomKernels::krnl_info CustomKernels::build_krnl(const shared_ptr<op::Sum>& op) const
{
    return do_bcast_sum_operation(op, op->get_reduction_axes(), false);
}

CustomKernels::krnl_info CustomKernels::build_krnl(const shared_ptr<op::Max>& op) const
{
    return do_max_min_operation(op, false);
}

CustomKernels::krnl_info CustomKernels::build_krnl(const shared_ptr<op::Min>& op) const
{
    return do_max_min_operation(op, true);
}
