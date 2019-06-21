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

#include "detection_output.hpp"

#include "ngraph/op/constant.hpp"

using namespace std;
using namespace ngraph;

op::DetectionOutput::DetectionOutput(const std::shared_ptr<Node>& box_logits,
                                     const std::shared_ptr<Node>& class_preds,
                                     const std::shared_ptr<Node>& proposals,
                                     const std::shared_ptr<Node>& aux_class_preds,
                                     const std::shared_ptr<Node>& aux_box_preds,
                                     const DetectionOutputAttrs& attrs)
    : Op("DetectionOutput",
         check_single_output_args(
             {box_logits, class_preds, proposals, aux_class_preds, aux_box_preds}))
    , m_attrs(attrs)
{
    constructor_validate_and_infer_types();
}

void op::DetectionOutput::validate_and_infer_types()
{
    if (get_input_partial_shape(0).is_static())
    {
        auto box_logits_shape = get_input_partial_shape(0).to_shape();
        set_output_type(
            0, element::f32, Shape{1, 1, m_attrs.keep_top_k[0] * box_logits_shape[0], 7});
    }
    else
    {
        set_output_type(0, element::f32, PartialShape::dynamic());
    }
}

shared_ptr<Node> op::DetectionOutput::copy_with_new_args(const NodeVector& new_args) const
{
    check_new_args_count(this, new_args);
    return make_shared<DetectionOutput>(
        new_args.at(0), new_args.at(1), new_args.at(2), new_args.at(3), new_args.at(4), m_attrs);
}