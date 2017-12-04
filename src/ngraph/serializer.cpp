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

#include "ngraph/serializer.hpp"
#include "ngraph/ops/abs.hpp"
#include "ngraph/ops/acos.hpp"
#include "ngraph/ops/add.hpp"
#include "ngraph/ops/asin.hpp"
#include "ngraph/ops/atan.hpp"
#include "ngraph/ops/broadcast.hpp"
#include "ngraph/ops/ceiling.hpp"
#include "ngraph/ops/concatenate.hpp"
#include "ngraph/ops/constant.hpp"
#include "ngraph/ops/convert.hpp"
#include "ngraph/ops/cos.hpp"
#include "ngraph/ops/cosh.hpp"
#include "ngraph/ops/divide.hpp"
#include "ngraph/ops/dot.hpp"
#include "ngraph/ops/equal.hpp"
#include "ngraph/ops/exp.hpp"
#include "ngraph/ops/floor.hpp"
#include "ngraph/ops/function_call.hpp"
#include "ngraph/ops/get_tuple_element.hpp"
#include "ngraph/ops/greater.hpp"
#include "ngraph/ops/greater_eq.hpp"
#include "ngraph/ops/less.hpp"
#include "ngraph/ops/less_eq.hpp"
#include "ngraph/ops/log.hpp"
#include "ngraph/ops/maximum.hpp"
#include "ngraph/ops/minimum.hpp"
#include "ngraph/ops/multiply.hpp"
#include "ngraph/ops/negative.hpp"
#include "ngraph/ops/not_equal.hpp"
#include "ngraph/ops/power.hpp"
#include "ngraph/ops/reduce.hpp"
#include "ngraph/ops/remainder.hpp"
#include "ngraph/ops/reshape.hpp"
#include "ngraph/ops/select.hpp"
#include "ngraph/ops/sign.hpp"
#include "ngraph/ops/sin.hpp"
#include "ngraph/ops/sinh.hpp"
#include "ngraph/ops/slice.hpp"
#include "ngraph/ops/subtract.hpp"
#include "ngraph/ops/sum.hpp"
#include "ngraph/ops/tan.hpp"
#include "ngraph/ops/tanh.hpp"
#include "ngraph/ops/tuple.hpp"
#include "ngraph/util.hpp"

using namespace ngraph;
using namespace std;
using json = nlohmann::json;

std::shared_ptr<ngraph::Function>
    read_function(const json&, std::unordered_map<std::string, std::shared_ptr<Function>>&);

json write(const ngraph::Function&);
json write(const ngraph::Node&);
json write(const ngraph::element::Type&);

// This stupidity is caused by the fact that we do not pass element types
// by value but by reference even though they can be compared. There is no reason to pass
// them by reference EVERYWERE but here we are...
const element::Type& to_ref(const element::Type& t)
{
    if (t == element::boolean)
    {
        return element::boolean;
    }
    if (t == element::f32)
    {
        return element::f32;
    }
    if (t == element::f64)
    {
        return element::f64;
    }
    if (t == element::i8)
    {
        return element::i8;
    }
    if (t == element::i16)
    {
        return element::i16;
    }
    if (t == element::i32)
    {
        return element::i32;
    }
    if (t == element::i64)
    {
        return element::i64;
    }
    if (t == element::u8)
    {
        return element::u8;
    }
    if (t == element::u16)
    {
        return element::u16;
    }
    if (t == element::u32)
    {
        return element::u32;
    }
    if (t == element::u64)
    {
        return element::u64;
    }
    throw runtime_error("type not valid");
}

static json write_element_type(const ngraph::element::Type& n)
{
    json j;
    j["bitwidth"] = n.bitwidth();
    j["is_real"] = n.is_real();
    j["is_signed"] = n.is_signed();
    j["c_type_string"] = n.c_type_string();
    return j;
}

static const element::Type& read_element_type(const json& j)
{
    size_t bitwidth = j.at("bitwidth").get<size_t>();
    bool is_real = j.at("is_real").get<bool>();
    bool is_signed = j.at("is_signed").get<bool>();
    string c_type_string = j.at("c_type_string").get<string>();

    return to_ref(element::Type(bitwidth, is_real, is_signed, c_type_string));
}

string ngraph::serialize(shared_ptr<ngraph::Function> func)
{
    json j;
    vector<json> functions;
    traverse_functions(func,
                       [&](shared_ptr<ngraph::Function> f) { functions.push_back(write(*f)); });
    for (auto it = functions.rbegin(); it != functions.rend(); it++)
    {
        j.push_back(*it);
    }

    return j.dump();
}

shared_ptr<ngraph::Function> ngraph::deserialize(istream& in)
{
    json js = json::array();
    shared_ptr<Function> rc;
    in >> js;
    unordered_map<string, shared_ptr<Function>> function_map;
    for (json func : js)
    {
        shared_ptr<Function> f = read_function(func, function_map);
        if (rc == nullptr)
        {
            rc = f;
        }
    }

    return rc;
}

json write(const Function& f)
{
    json function;
    function["name"] = f.get_name();
    function["result_type"] = write_element_type(f.get_result_type()->get_element_type());
    function["result_shape"] = f.get_result_type()->get_shape();
    for (auto param : f.get_parameters())
    {
        function["parameters"].push_back(param->get_name());
    }
    function["result"].push_back(f.get_result()->get_name());

    list<shared_ptr<Node>> result_list;
    {
        deque<Node*> independent_nodes;
        unordered_map<const Node*, size_t> node_depencency_count;
        unordered_map<Node*, shared_ptr<Node>> node_map;

        traverse_nodes(const_cast<Function*>(&f), [&](shared_ptr<Node> node) {
            node_map[node.get()] = node;
            node_depencency_count[node.get()] = node->get_arguments().size();
            if (node->get_arguments().size() == 0)
            {
                independent_nodes.push_back(node.get());
            }
        });

        while (independent_nodes.size() > 0)
        {
            auto independent_node = independent_nodes.front();
            result_list.push_back(node_map[independent_node]);
            independent_nodes.pop_front();

            for (auto user : independent_node->users())
            {
                node_depencency_count[user] -= 1;
                size_t count = node_depencency_count[user];
                if (count == 0)
                {
                    independent_nodes.push_back(user);
                }
            }
        }
    }

    json nodes;
    for (shared_ptr<Node> node : result_list)
    {
        nodes.push_back(write(*node));
    }
    function["ops"] = nodes;
    return function;
}

shared_ptr<ngraph::Function>
    read_function(const json& func_js, unordered_map<string, shared_ptr<Function>>& function_map)
{
    shared_ptr<ngraph::Function> rc;

    string func_name = func_js.at("name").get<string>();
    vector<string> func_result = func_js.at("result").get<vector<string>>();
    vector<string> func_parameters = func_js.at("parameters").get<vector<string>>();
    const element::Type& result_type = read_element_type(func_js.at("result_type"));
    vector<size_t> result_shape = func_js.at("result_shape").get<vector<size_t>>();
    unordered_map<string, shared_ptr<Node>> node_map;
    for (json node_js : func_js.at("ops"))
    {
        string node_name = node_js.at("name").get<string>();
        string node_op = node_js.at("op").get<string>();
        const element::Type& node_etype = read_element_type(node_js.at("element_type"));
        vector<string> node_inputs = node_js.at("inputs").get<vector<string>>();
        vector<string> node_outputs = node_js.at("outputs").get<vector<string>>();
        shared_ptr<Node> node;
        shared_ptr<Function> function_ptr = nullptr;
        vector<shared_ptr<Node>> args;
        for (const string& name : node_inputs)
        {
            args.push_back(node_map.at(name));
        }

        vector<string> known_nodes;
        for (auto x : node_map)
        {
            known_nodes.push_back(x.first);
        }

        if (node_op == "Abs")
        {
            node = make_shared<op::Abs>(args[0]);
        }
        else if (node_op == "Acos")
        {
            node = make_shared<op::Acos>(args[0]);
        }
        else if (node_op == "Add")
        {
            node = make_shared<op::Add>(args[0], args[1]);
        }
        else if (node_op == "Asin")
        {
            node = make_shared<op::Asin>(args[0]);
        }
        else if (node_op == "Atan")
        {
            node = make_shared<op::Atan>(args[0]);
        }
        else if (node_op == "Broadcast")
        {
            auto shape = node_js.at("shape").get<vector<size_t>>();
            auto axes = node_js.at("axes").get<set<size_t>>();
            node = make_shared<op::Broadcast>(args[0], shape, axes);
        }
        else if (node_op == "Ceiling")
        {
            node = make_shared<op::Ceiling>(args[0]);
        }
        else if (node_op == "Concat")
        {
            auto axis = node_js.at("axis").get<size_t>();
            node = make_shared<op::Concat>(args, axis);
        }
        else if (node_op == "Constant")
        {
            auto shape = node_js.at("shape").get<vector<size_t>>();
            auto value = node_js.at("value").get<vector<string>>();
            node = make_shared<op::Constant>(node_etype, shape, value);
        }
        else if (node_op == "Convert")
        {
            auto target_type = read_element_type(node_js.at("target_type"));
            node = make_shared<op::Convert>(args[0], target_type);
        }
        else if (node_op == "Cos")
        {
            node = make_shared<op::Cos>(args[0]);
        }
        else if (node_op == "Cosh")
        {
            node = make_shared<op::Cosh>(args[0]);
        }
        else if (node_op == "Divide")
        {
            node = make_shared<op::Divide>(args[0], args[1]);
        }
        else if (node_op == "Dot")
        {
            node = make_shared<op::Dot>(args[0], args[1]);
        }
        else if (node_op == "Equal")
        {
            node = make_shared<op::Equal>(args[0], args[1]);
        }
        else if (node_op == "Exp")
        {
            node = make_shared<op::Exp>(args[0]);
        }
        else if (node_op == "Floor")
        {
            node = make_shared<op::Floor>(args[0]);
        }
        else if (node_op == "FunctionCall")
        {
            string function_name = node_js.at("function").get<string>();
            shared_ptr<Function> f_ptr = function_map.at(function_name);
            node = make_shared<op::FunctionCall>(f_ptr, args);
        }
        // else if (node_op == "GetTupleElement")
        // {
        //     node = make_shared<op::GetTupleElement>(args[0]);
        // }
        else if (node_op == "Greater")
        {
            node = make_shared<op::Greater>(args[0], args[1]);
        }
        else if (node_op == "GreaterEq")
        {
            node = make_shared<op::GreaterEq>(args[0], args[1]);
        }
        else if (node_op == "Less")
        {
            node = make_shared<op::Less>(args[0], args[1]);
        }
        else if (node_op == "LessEq")
        {
            node = make_shared<op::LessEq>(args[0], args[1]);
        }
        else if (node_op == "Log")
        {
            node = make_shared<op::Log>(args[0]);
        }
        else if (node_op == "Maximum")
        {
            node = make_shared<op::Maximum>(args[0], args[1]);
        }
        else if (node_op == "Minimum")
        {
            node = make_shared<op::Minimum>(args[0], args[1]);
        }
        else if (node_op == "Multiply")
        {
            node = make_shared<op::Multiply>(args[0], args[1]);
        }
        else if (node_op == "Negative")
        {
            node = make_shared<op::Negative>(args[0]);
        }
        else if (node_op == "NotEqual")
        {
            node = make_shared<op::NotEqual>(args[0], args[1]);
        }
        else if (node_op == "Parameter")
        {
            auto shape = node_js.at("shape");
            node = make_shared<op::Parameter>(node_etype, shape);
        }
        else if (node_op == "Power")
        {
            node = make_shared<op::Power>(args[0], args[1]);
        }
        else if (node_op == "Reduce")
        {
            auto reduction_axes = node_js.at("reduction_axes").get<set<size_t>>();
            node = make_shared<op::Reduce>(args[0], args[1], function_ptr, reduction_axes);
        }
        else if (node_op == "Remainder")
        {
            node = make_shared<op::Remainder>(args[0], args[1]);
        }
        else if (node_op == "Reshape")
        {
            auto input_order = node_js.at("input_order").get<vector<size_t>>();
            auto output_shape = node_js.at("output_shape").get<vector<size_t>>();
            node = make_shared<op::Reshape>(args[0], input_order, output_shape);
        }
        else if (node_op == "Select")
        {
            node = make_shared<op::Select>(args[0], args[1], args[2]);
        }
        else if (node_op == "Sign")
        {
            node = make_shared<op::Sign>(args[0]);
        }
        else if (node_op == "Sin")
        {
            node = make_shared<op::Sin>(args[0]);
        }
        else if (node_op == "Sinh")
        {
            node = make_shared<op::Sinh>(args[0]);
        }
        else if (node_op == "Slice")
        {
            auto lower_bounds = node_js.at("lower_bounds").get<vector<size_t>>();
            auto upper_bounds = node_js.at("upper_bounds").get<vector<size_t>>();
            auto strides = node_js.at("strides").get<vector<size_t>>();
            node = make_shared<op::Slice>(args[0], lower_bounds, upper_bounds, strides);
        }
        else if (node_op == "Subtract")
        {
            node = make_shared<op::Subtract>(args[0], args[1]);
        }
        else if (node_op == "Sum")
        {
            auto reduction_axes = node_js.at("reduction_axes").get<set<size_t>>();
            node = make_shared<op::Sum>(args[0], reduction_axes);
        }
        else if (node_op == "Tan")
        {
            node = make_shared<op::Tan>(args[0]);
        }
        else if (node_op == "Tanh")
        {
            node = make_shared<op::Tanh>(args[0]);
        }
        else if (node_op == "Tuple")
        {
            node = make_shared<op::Tuple>(args);
        }
        else
        {
            stringstream ss;
            ss << "unsupported op " << node_op;
            throw runtime_error(ss.str());
        }
        node_map[node_name] = node;
    }

    auto result = node_map.at(func_result[0]);
    std::vector<std::shared_ptr<op::Parameter>> params;
    for (auto param_name : func_parameters)
    {
        params.push_back(dynamic_pointer_cast<op::Parameter>(node_map.at(param_name)));
    }
    auto rt = make_shared<TensorViewType>(result_type, result_shape);
    rc = make_shared<Function>(result, rt, params, func_name);
    function_map[func_name] = rc;

    return rc;
}

json write(const Node& n)
{
    json node;
    node["name"] = n.get_name();
    node["op"] = n.description();
    node["element_type"] = write_element_type(n.get_element_type());
    json inputs = json::array();
    json outputs = json::array();
    for (const descriptor::Input& input : n.get_inputs())
    {
        inputs.push_back(input.get_output().get_node()->get_name());
    }
    for (const descriptor::Output& output : n.get_outputs())
    {
        outputs.push_back(output.get_node()->get_name());
    }
    node["inputs"] = inputs;
    node["outputs"] = outputs;

    string node_op = n.description();
    if (node_op == "Abs")
    {
    }
    else if (node_op == "Acos")
    {
    }
    else if (node_op == "Add")
    {
    }
    else if (node_op == "Asin")
    {
    }
    else if (node_op == "Atan")
    {
    }
    else if (node_op == "Broadcast")
    {
        auto tmp = dynamic_cast<const op::Broadcast*>(&n);
        node["axes"] = tmp->get_broadcast_axes();
        node["shape"] = tmp->get_broadcast_shape();
    }
    else if (node_op == "Ceiling")
    {
    }
    else if (node_op == "Concat")
    {
        auto tmp = dynamic_cast<const op::Concat*>(&n);
        node["axis"] = tmp->get_concatenation_axis();
    }
    else if (node_op == "Constant")
    {
        auto tmp = dynamic_cast<const op::Constant*>(&n);
        node["value"] = tmp->get_value_strings();
        node["shape"] = tmp->get_shape();
    }
    else if (node_op == "Convert")
    {
        auto tmp = dynamic_cast<const op::Convert*>(&n);
        node["target_type"] = write_element_type(tmp->get_convert_element_type());
    }
    else if (node_op == "Cos")
    {
    }
    else if (node_op == "Cosh")
    {
    }
    else if (node_op == "Divide")
    {
    }
    else if (node_op == "Dot")
    {
    }
    else if (node_op == "Equal")
    {
    }
    else if (node_op == "Exp")
    {
    }
    else if (node_op == "Floor")
    {
    }
    else if (node_op == "FunctionCall")
    {
        node["function"] = n.get_function()->get_name();
    }
    else if (node_op == "GetTupleElement")
    {
    }
    else if (node_op == "Greater")
    {
    }
    else if (node_op == "GreaterEq")
    {
    }
    else if (node_op == "Less")
    {
    }
    else if (node_op == "LessEq")
    {
    }
    else if (node_op == "Log")
    {
    }
    else if (node_op == "Maximum")
    {
    }
    else if (node_op == "Minimum")
    {
    }
    else if (node_op == "Multiply")
    {
    }
    else if (node_op == "Negative")
    {
    }
    else if (node_op == "NotEqual")
    {
    }
    else if (node_op == "Parameter")
    {
        auto tmp = dynamic_cast<const op::Parameter*>(&n);
        node["shape"] = tmp->get_shape();
    }
    else if (node_op == "Power")
    {
    }
    else if (node_op == "Reduce")
    {
        auto tmp = dynamic_cast<const op::Reduce*>(&n);
        node["function"] = tmp->get_function()->get_name();
        node["reduction_axes"] = tmp->get_reduction_axes();
    }
    else if (node_op == "Remainder")
    {
    }
    else if (node_op == "Reshape")
    {
        auto tmp = dynamic_cast<const op::Reshape*>(&n);
        node["input_order"] = tmp->get_input_order();
        node["output_shape"] = tmp->get_output_shape();
    }
    else if (node_op == "Select")
    {
    }
    else if (node_op == "Sign")
    {
    }
    else if (node_op == "Sin")
    {
    }
    else if (node_op == "Sinh")
    {
    }
    else if (node_op == "Slice")
    {
        auto tmp = dynamic_cast<const op::Slice*>(&n);
        node["lower_bounds"] = tmp->get_lower_bounds();
        node["upper_bounds"] = tmp->get_upper_bounds();
        node["strides"] = tmp->get_strides();
    }
    else if (node_op == "Subtract")
    {
    }
    else if (node_op == "Sum")
    {
        auto tmp = dynamic_cast<const op::Sum*>(&n);
        node["reduction_axes"] = tmp->get_reduction_axes();
    }
    else if (node_op == "Tan")
    {
    }
    else if (node_op == "Tanh")
    {
    }
    else if (node_op == "Tuple")
    {
    }

    return node;
}