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

#include <memory>

#include "mkldnn_emitter.hpp"

#include "ngraph/runtime/cpu/cpu_layout_descriptor.hpp"
#include "ngraph/runtime/cpu/cpu_tensor_view_wrapper.hpp"
#include "ngraph/runtime/cpu/mkldnn_utils.hpp"

using namespace ngraph::runtime::cpu;

const std::vector<mkldnn::primitive*>& MKLDNNEmitter::get_mkldnn_primitives() const
{
    return mkldnn_primitives;
}

size_t MKLDNNEmitter::insert_primitive(mkldnn::primitive* primitive)
{
    mkldnn_primitives.emplace_back(primitive);
    return (mkldnn_primitives.size() - 1);
}

const std::vector<size_t>& MKLDNNEmitter::get_primitive_deps(size_t index) const
{
    return primitive_deps.at(index);
}

mkldnn::memory::desc MKLDNNEmitter::build_memory_descriptor(const TensorViewWrapper& tvw,
                                                            mkldnn::memory::format fmt) const
{
    return mkldnn::memory::desc(
        mkldnn::memory::dims(tvw.get_shape().begin(), tvw.get_shape().end()),
        mkldnn_utils::GetDataType(tvw.get_element_type()),
        fmt);
}

mkldnn::memory::desc MKLDNNEmitter::build_memory_descriptor(const TensorViewWrapper& tvw) const
{
    auto layout =
        std::static_pointer_cast<LayoutDescriptor>(tvw.get_tensor_view()->get_tensor_view_layout());

    return build_memory_descriptor(tvw, layout->get_mkldnn_format());
}

mkldnn::memory MKLDNNEmitter::build_memory_primitive(const TensorViewWrapper& tvw) const
{
    return mkldnn::memory({build_memory_descriptor(tvw), mkldnn_utils::global_cpu_engine}, nullptr);
}

size_t MKLDNNEmitter::build_memory_primitive(const mkldnn::memory::desc& desc)
{
    // The MKL-DNN C++ API forces proper initialization of a memory primitive
    // with a non-null pointer (unlike the C API)
    // Primitives are initialized at runtime so we use a known-invalid address here
    // to bypass this check
    return insert_primitive(
        new mkldnn::memory({desc, mkldnn_utils::global_cpu_engine}, reinterpret_cast<void*>(0x42)));
}

size_t MKLDNNEmitter::build_convolution_forward(const mkldnn::memory::desc& input_data_desc,
                                                const mkldnn::memory::desc& weights_desc,
                                                const mkldnn::memory::desc& result_desc,
                                                const ngraph::Strides& strides,
                                                const ngraph::CoordinateDiff& padding_below,
                                                const ngraph::CoordinateDiff& padding_above)

{
    size_t input_data_index = build_memory_primitive(input_data_desc);
    size_t weights_index = build_memory_primitive(weights_desc);
    size_t result_index = build_memory_primitive(result_desc);

    size_t conv_index = insert_primitive(new mkldnn::convolution_forward(
        {{mkldnn::prop_kind::forward,
          mkldnn::algorithm::convolution_direct,
          input_data_desc,
          weights_desc,
          result_desc,
          mkldnn::memory::dims(strides.begin(), strides.end()),
          mkldnn::memory::dims(padding_below.begin(), padding_below.end()),
          mkldnn::memory::dims(padding_above.begin(), padding_above.end()),
          mkldnn::padding_kind::zero},
         mkldnn_utils::global_cpu_engine},
        *mkldnn_primitives[input_data_index],
        *mkldnn_primitives[weights_index],
        *mkldnn_primitives[result_index]));

    primitive_deps[conv_index] = {input_data_index, weights_index, result_index};
    return conv_index;
}

size_t MKLDNNEmitter::build_convolution_forward(const mkldnn::memory::desc& input_data_desc,
                                                const mkldnn::memory::desc& weights_desc,
                                                const mkldnn::memory::desc& result_desc,
                                                const ngraph::Strides& strides,
                                                const ngraph::Strides& dilation_strides,
                                                const ngraph::CoordinateDiff& padding_below,
                                                const ngraph::CoordinateDiff& padding_above)

{
    size_t input_data_index = build_memory_primitive(input_data_desc);
    size_t weights_index = build_memory_primitive(weights_desc);
    size_t result_index = build_memory_primitive(result_desc);

    size_t conv_index = insert_primitive(new mkldnn::convolution_forward(
        {{mkldnn::prop_kind::forward,
          mkldnn::algorithm::convolution_direct,
          input_data_desc,
          weights_desc,
          result_desc,
          mkldnn::memory::dims(strides.begin(), strides.end()),
          mkldnn::memory::dims(dilation_strides.begin(), dilation_strides.end()),
          mkldnn::memory::dims(padding_below.begin(), padding_below.end()),
          mkldnn::memory::dims(padding_above.begin(), padding_above.end()),
          mkldnn::padding_kind::zero},
         mkldnn_utils::global_cpu_engine},
        *mkldnn_primitives[input_data_index],
        *mkldnn_primitives[weights_index],
        *mkldnn_primitives[result_index]));

    primitive_deps[conv_index] = {input_data_index, weights_index, result_index};
    return conv_index;
}