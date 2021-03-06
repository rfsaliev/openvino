//*****************************************************************************
// Copyright 2017-2020 Intel Corporation
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

#include <algorithm>
#include <iterator>
#include <locale>

#include "ngraph/attribute_visitor.hpp"
#include "ngraph/op/add.hpp"
#include "ngraph/op/clamp.hpp"
#include "ngraph/op/multiply.hpp"
#include "ngraph/op/subtract.hpp"
#include "ngraph/op/util/rnn_cell_base.hpp"
#include "ngraph/util.hpp"

using namespace std;
using namespace ngraph;

// Modify input vector in-place and return reference to modified vector.
static vector<string> to_lower_case(const vector<string>& vs)
{
    vector<string> res(vs);
    transform(begin(res), end(res), begin(res), [](string& s) { return to_lower(s); });
    return res;
}

op::util::RNNCellBase::RNNCellBase()
    : m_clip(0.f)
    , m_hidden_size(0)
{
}

op::util::RNNCellBase::RNNCellBase(size_t hidden_size,
                                   float clip,
                                   const vector<string>& activations,
                                   const vector<float>& activations_alpha,
                                   const vector<float>& activations_beta)
    : m_hidden_size(hidden_size)
    , m_clip(clip)
    , m_activations(to_lower_case(activations))
    , m_activations_alpha(activations_alpha)
    , m_activations_beta(activations_beta)
{
}

bool ngraph::op::util::RNNCellBase::visit_attributes(AttributeVisitor& visitor)
{
    visitor.on_attribute("hidden_size", m_hidden_size);
    visitor.on_attribute("activations", m_activations);
    visitor.on_attribute("activations_alpha", m_activations_alpha);
    visitor.on_attribute("activations_beta", m_activations_beta);
    visitor.on_attribute("clip", m_clip);
    return true;
}

void ngraph::op::util::RNNCellBase::validate_input_rank_dimension(
    const std::vector<ngraph::PartialShape>& input)
{
    enum
    {
        X,
        initial_hidden_state,
        W,
        R,
        B
    };

    // Verify static ranks for all inputs
    for (size_t i = 0; i < input.size(); i++)
    {
        NODE_VALIDATION_CHECK(dynamic_cast<ngraph::Node*>(this),
                              (input[i].rank().is_static()),
                              "RNNCellBase supports only static rank for input tensors. Input ",
                              i);
    }

    // Verify input dimension against values provided in spec (LSTMCell_1.md)
    for (size_t i = 0; i < input.size(); i++)
    {
        if (i == B)
        {
            // verify only B input dimension which is 1D
            NODE_VALIDATION_CHECK(dynamic_cast<ngraph::Node*>(this),
                                  (input[i].rank().get_length() == 1),
                                  "RNNCellBase B input tensor dimension is not correct.");
        }
        else
        {
            // Verify all other input dimensions which are 2D tensor types
            NODE_VALIDATION_CHECK(dynamic_cast<ngraph::Node*>(this),
                                  (input[i].rank().get_length() == 2),
                                  "RNNCellBase input tensor dimension is not correct for ",
                                  i,
                                  " input parameter. Current input length: ",
                                  input[i].rank().get_length(),
                                  ", expected: 2.");
        }
    }

    // Compare input_size dimension for X and W inputs
    const auto& x_pshape = input.at(X);
    const auto& w_pshape = input.at(W);

    NODE_VALIDATION_CHECK(dynamic_cast<ngraph::Node*>(this),
                          (x_pshape[1].compatible(w_pshape[1])),
                          "RNNCellBase mismatched input_size dimension.");
}

op::util::ActivationFunction op::util::RNNCellBase::get_activation_function(size_t idx) const
{
    // Normalize activation function case.
    std::string func_name = m_activations.at(idx);
    std::locale loc;
    std::transform(func_name.begin(), func_name.end(), func_name.begin(), [&loc](char c) {
        return std::tolower(c, loc);
    });

    op::util::ActivationFunction afunc = get_activation_func_by_name(func_name);

    // Set activation functions parameters (if any)
    if (m_activations_alpha.size() > idx)
    {
        afunc.set_alpha(m_activations_alpha.at(idx));
    }
    if (m_activations_beta.size() > idx)
    {
        afunc.set_beta(m_activations_beta.at(idx));
    }

    return afunc;
}

shared_ptr<Node> op::util::RNNCellBase::add(const Output<Node>& lhs, const Output<Node>& rhs)
{
    return {make_shared<op::v1::Add>(lhs, rhs)};
}

shared_ptr<Node> op::util::RNNCellBase::sub(const Output<Node>& lhs, const Output<Node>& rhs)
{
    return {make_shared<op::v1::Subtract>(lhs, rhs)};
}

shared_ptr<Node> op::util::RNNCellBase::mul(const Output<Node>& lhs, const Output<Node>& rhs)
{
    return {make_shared<op::v1::Multiply>(lhs, rhs)};
}

shared_ptr<Node> op::util::RNNCellBase::clip(const Output<Node>& data) const
{
    if (m_clip == 0.f)
    {
        return data.get_node_shared_ptr();
    }

    return make_shared<op::Clamp>(data, -m_clip, m_clip);
}
