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

#include <memory>
#include <list>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include "gtest/gtest.h"
#include "ngraph/log.hpp"
#include "ngraph/ngraph.hpp"
#include "ngraph/pattern/matcher.hpp"
#include "ngraph/pattern/op/any.hpp"
#include "ngraph/pattern/op/label.hpp"
#include "ngraph/pass/graph_rewrite.hpp"
#include "ngraph/pass/manager.hpp"
#include "ngraph/pass/topological_sort.hpp"
#include "ngraph/pass/propagate_types.hpp"

using namespace ngraph;
using namespace std;


class TestGraphRewrite : public ngraph::pass::GraphRewrite
{
public:
    TestGraphRewrite() : GraphRewrite() 
    {
        auto shape = Shape{ 1 };
        //pattern #1 : a * 1 = a
        auto pattern = std::make_shared<pattern::op::Label>([](shared_ptr<Node> n) { return !dynamic_pointer_cast<op::ParameterizedConstant<element::Int32>>(n); });
        auto t = ngraph::runtime::make_tensor<element::Int32>(shape);
        (*t) = std::vector<int>{ 1 };
        auto iconst1_1 = make_shared<op::ParameterizedConstant<element::Int32>>(shape, t);

        NGRAPH_DEBUG << "IN TestGraphRewrite" << std::endl;

        ngraph::pattern::gr_callback_fn callback = [pattern](pattern::Matcher& m)
        {
            NGRAPH_DEBUG << "IN CALLBACK" << std::endl;
            assert(m.match_root()->get_arguments().size() == 2);

            size_t const_node_index = m.match_root()->get_arguments().at(0) == pattern->get_binded_node();
            auto const_node = dynamic_pointer_cast<op::ParameterizedConstant<element::Int32>>(m.match_root()->get_arguments().at(const_node_index));
            auto second_node = m.match_root()->get_arguments().at(const_node_index);
            NGRAPH_DEBUG << "second_node " << second_node->description() << " , " << second_node << std::endl;
            NGRAPH_DEBUG << "pattern " << pattern->get_binded_node()->description() << " , " << pattern->get_binded_node() << std::endl;
            assert(const_node);

            auto pattern_value_type = dynamic_pointer_cast<const TensorViewType>(pattern->get_binded_node()->get_value_type());
            auto const_node_value_type = dynamic_pointer_cast<const TensorViewType>(const_node->get_value_type());
            assert(pattern_value_type && const_node);


            if (pattern_value_type->get_element_type() != const_node_value_type->get_element_type() ||
                pattern_value_type->get_shape() != const_node_value_type->get_shape())
            {
                NGRAPH_DEBUG << "TYPE/SHAPE" << std::endl;
                return;
            }

            auto const_values = const_node->get_value()->get_vector();
            bool all_ones = std::all_of(begin(const_values), end(const_values), [](int e) {return e == 1; });

            if (!all_ones) 
            {
                NGRAPH_DEBUG << "ALL_ONES" << std::endl;
                return;
            }
            
            NGRAPH_DEBUG << "BEFORE REPLACE" << std::endl;
            ngraph::pass::GraphRewrite::replace_node(m.match_root(), pattern->get_binded_node());
        };

        auto m = make_shared<pattern::Matcher>(pattern * iconst1_1, callback);
        this->add_matcher(m);
    }

};


TEST(graph_rewrite, basic)
{
    auto shape = Shape{1};
    auto a = make_shared<op::Parameter>(element::Int32::element_type(), shape);
    auto b = make_shared<op::Parameter>(element::Int32::element_type(), shape);
    auto c = make_shared<op::Parameter>(element::Int32::element_type(), shape);
    auto d = make_shared<op::Parameter>(element::Int32::element_type(), shape);

    auto t = ngraph::runtime::make_tensor<element::Int32>(shape);
    (*t) = std::vector<int>{ 1 };
    
    pass::Manager pass_manager;

    pass_manager.register_pass<pass::TopologicalSort>();
    pass_manager.register_pass<pass::PropagateTypes>();
    pass_manager.register_pass<TestGraphRewrite>();

    {
        auto a = make_shared<op::Parameter>(element::Int32::element_type(), shape);
        auto b = make_shared<op::Parameter>(element::Int32::element_type(), shape);
        auto iconst1_0 = make_shared<op::ParameterizedConstant<element::Int32>>(shape, t);
        auto mul = (a * iconst1_0);
        auto graph = b + mul;
        auto rt = make_shared<TensorViewType>(element::Int32::element_type(), shape);
        auto func = make_shared<Function>(graph, rt, op::Parameters{ a, b });
        pass_manager.run_passes(func);
        auto graph_args = graph->get_arguments();
        ASSERT_TRUE(graph->get_arguments().at(1) == a);
    }

    {
        auto a = make_shared<op::Parameter>(element::Int32::element_type(), shape);
        auto b = make_shared<op::Parameter>(element::Int32::element_type(), shape);
        auto iconst1_0 = make_shared<op::ParameterizedConstant<element::Int32>>(shape, t);
        auto graph = ((((a * iconst1_0) * iconst1_0) * iconst1_0) * iconst1_0) + b;
        auto rt = make_shared<TensorViewType>(element::Int32::element_type(), shape);
        auto func = make_shared<Function>(graph, rt, op::Parameters{ a, b });
        pass_manager.run_passes(func);
        ASSERT_TRUE(graph->get_arguments().at(0) == a);
    }

    {
        NGRAPH_DEBUG << "CASCADE " << std::endl;
        auto a = make_shared<op::Parameter>(element::Int32::element_type(), shape);
        auto b = make_shared<op::Parameter>(element::Int32::element_type(), shape);
        auto iconst1_0 = make_shared<op::ParameterizedConstant<element::Int32>>(shape, t);
        //auto graph = b + (iconst1_0 * (iconst1_0 * (iconst1_0 * (iconst1_0 * a))));
        auto graph = b + (iconst1_0 * (iconst1_0 * a));
        auto rt = make_shared<TensorViewType>(element::Int32::element_type(), shape);
        auto func = make_shared<Function>(graph, rt, op::Parameters{ a, b });
        pass_manager.run_passes(func);
        ASSERT_TRUE(graph->get_arguments().at(1) == a);
    }


    /*
    auto pattern = std::make_shared<pattern::op::Label>();
    auto sum = b + a;
    auto pattern_mul = pattern_sum * a;
    auto mul = sum * a;

    ngraph::pattern::gr_callback_fn callback = [pattern](pattern::Matcher& m)
    {
        pattern->get_binded_node()
        ngraph::pass::GraphRewrite::replace_node(m.match_root(), m.pattern_node());
    };
    a * 1 = a;
    auto m = make_shared<pattern::Matcher>(pattern + a);
    auto pattern = std::make_shared<pattern::op::Pattern>(); //marker
    ASSERT_TRUE(m->match(pattern_mul, mul));

    ngraph::pass::GraphRewrite gr;

    std::list<std::shared_ptr<Node>> nodes{ b, a, sum, mul };
    gr.run_on_call_graph(nodes);
    ASSERT_EQ(mul->get_arguments().at(0), pattern);
    auto& sum_users = sum->users();
    ASSERT_TRUE(sum_users.find(mul.get()) == sum_users.end());
    */
}



TEST(pattern, op_op)
{
    auto shape = Shape{1};

    auto a = make_shared<op::Parameter>(element::Int32::element_type(), shape);
    pattern::Matcher n(nullptr);
    ASSERT_TRUE(n.match(a, a));

    auto abs = make_shared<op::Abs>(a);
    auto any = std::make_shared<pattern::op::Any>(a); 
    ASSERT_TRUE(n.match(any, abs));

    auto any_false = std::make_shared<pattern::op::Any>(a, [](std::shared_ptr<Node> n) {return false;});
    ASSERT_TRUE(n.match(any_false, a));

    auto pattern = std::make_shared<pattern::op::Label>();
    ASSERT_TRUE(n.match(pattern, a));
    ASSERT_EQ(pattern->get_binded_node(), a);
    
    auto pattern_false = std::make_shared<pattern::op::Label>([](std::shared_ptr<Node> n) {return false;});
    ASSERT_FALSE(n.match(pattern_false, a));
    
    auto b = make_shared<op::Parameter>(element::Int32::element_type(), shape);
    ASSERT_FALSE(n.match(a, b));
    ASSERT_FALSE(n.match(abs + b, b + b));
    ASSERT_TRUE(n.match(any + b, abs + b));

    ASSERT_TRUE(n.match(pattern + b, abs + b));
    ASSERT_EQ(pattern->get_binded_node(), abs);

    ASSERT_TRUE(n.match(b + pattern, abs + b));
    ASSERT_EQ(pattern->get_binded_node(), abs);

    auto c = make_shared<op::Parameter>(element::Int32::element_type(), shape);
    ASSERT_TRUE(n.match(c * (b + pattern), c * (abs + b)));
    ASSERT_EQ(pattern->get_binded_node(), abs);

    ASSERT_TRUE(n.match(c * (any + b), c * (abs + b))); //nested any
    ASSERT_TRUE(n.match(c * (any + b), (b + abs) * c)); //permutations w/ any
    ASSERT_TRUE(n.match(c * (any_false + b), c * (a + b))); //nested any
    ASSERT_TRUE(n.match(c * (any_false + b), (b + a) * c)); //permutations w/ any_false 

    auto t = ngraph::runtime::make_tensor<element::Int32>(shape);
    auto f = ngraph::runtime::make_tensor<element::Float32>(shape);
    (*t) = std::vector<int>{ 1 };
    (*f) = std::vector<float>{ 1 };
    auto iconst1_0 = make_shared<op::ParameterizedConstant<element::Int32>>(shape, t);
    auto iconst1_1 = make_shared<op::ParameterizedConstant<element::Int32>>(shape, t);
    ASSERT_TRUE(n.match(pattern * iconst1_0, a * iconst1_1)); //different iconst 
    ASSERT_EQ(pattern->get_binded_node(), a);

    auto fconst1_0 = make_shared<op::ParameterizedConstant<element::Float32>>(shape, f);
    ASSERT_FALSE(n.match(pattern * fconst1_0, a * iconst1_1)); //different iconst 

}