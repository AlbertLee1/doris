// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#pragma once

#include <faiss/impl/platform_macros.h>
#include <faiss/utils/distances.h>
#include <gen_cpp/Types_types.h>

#include "common/exception.h"
#include "common/status.h"
#include "runtime/primitive_type.h"
#include "vec/columns/column.h"
#include "vec/columns/column_array.h"
#include "vec/columns/column_nullable.h"
#include "vec/common/assert_cast.h"
#include "vec/core/types.h"
#include "vec/data_types/data_type.h"
#include "vec/data_types/data_type_array.h"
#include "vec/data_types/data_type_nullable.h"
#include "vec/data_types/data_type_number.h"
#include "vec/functions/array/function_array_utils.h"
#include "vec/functions/function.h"
#include "vec/utils/util.hpp"

namespace doris::vectorized {

class L1Distance {
public:
    static constexpr auto name = "l1_distance";
    static float distance(const float* x, const float* y, size_t d) {
        return faiss::fvec_L1(x, y, d);
    }
};

class L2Distance {
public:
    static constexpr auto name = "l2_distance";
    static float distance(const float* x, const float* y, size_t d) {
        return std::sqrt(faiss::fvec_L2sqr(x, y, d));
    }
};

class InnerProduct {
public:
    static constexpr auto name = "inner_product";
    static float distance(const float* x, const float* y, size_t d) {
        return faiss::fvec_inner_product(x, y, d);
    }
};

class CosineDistance {
public:
    static constexpr auto name = "cosine_distance";
    static float distance(const float* x, const float* y, size_t d);
};

class L2DistanceApproximate : public L2Distance {
public:
    static constexpr auto name = "l2_distance_approximate";
};

class InnerProductApproximate : public InnerProduct {
public:
    static constexpr auto name = "inner_product_approximate";
};

template <typename DistanceImpl>
class FunctionArrayDistance : public IFunction {
public:
    using DataType = PrimitiveTypeTraits<TYPE_FLOAT>::DataType;
    using ColumnType = PrimitiveTypeTraits<TYPE_FLOAT>::ColumnType;

    static constexpr auto name = DistanceImpl::name;
    String get_name() const override { return name; }
    static FunctionPtr create() { return std::make_shared<FunctionArrayDistance<DistanceImpl>>(); }
    size_t get_number_of_arguments() const override { return 2; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        if (arguments.size() != 2) {
            throw doris::Exception(ErrorCode::INVALID_ARGUMENT, "Invalid number of arguments");
        }

        // primitive_type of Nullable is its nested type.
        if (arguments[0]->get_primitive_type() != TYPE_ARRAY ||
            arguments[1]->get_primitive_type() != TYPE_ARRAY) {
            throw doris::Exception(ErrorCode::INVALID_ARGUMENT,
                                   "Arguments for function {} must be arrays", get_name());
        }

        return std::make_shared<DataTypeFloat32>();
    }

    // All array distance functions has always not nullable return type.
    // We want to make sure throw exception if input columns contain NULL.
    bool use_default_implementation_for_nulls() const override { return false; }

private:
    // Helper function to extract numeric data as float array
    template <typename T>
    static void extract_to_float(const IColumn* col, size_t start, size_t size, std::vector<float>& output) {
        const auto* numeric_col = assert_cast<const ColumnVector<T>*>(col);
        const auto& data = numeric_col->get_data();
        output.resize(size);
        for (size_t i = 0; i < size; ++i) {
            output[i] = static_cast<float>(data[start + i]);
        }
    }

    // Extract array data to float vector based on column type
    static void extract_array_data(const ColumnArray* arr, size_t start, size_t size,
                                   std::vector<float>& output, const std::string& func_name) {
        const IColumn* data_col = arr->get_data_ptr().get();

        // Handle nullable data column
        if (data_col->is_nullable()) {
            if (data_col->has_null()) {
                throw doris::Exception(ErrorCode::INVALID_ARGUMENT,
                                     "Argument for function {} cannot have null elements", func_name);
            }
            auto nullable_col = assert_cast<const ColumnNullable*>(data_col);
            data_col = nullable_col->get_nested_column_ptr().get();
        }

        // Extract based on actual data type
        PrimitiveType type = data_col->get_primitive_type();
        switch (type) {
            case TYPE_FLOAT:
                extract_to_float<TYPE_FLOAT>(data_col, start, size, output);
                break;
            case TYPE_DOUBLE:
                extract_to_float<TYPE_DOUBLE>(data_col, start, size, output);
                break;
            case TYPE_TINYINT:
                extract_to_float<TYPE_TINYINT>(data_col, start, size, output);
                break;
            case TYPE_SMALLINT:
                extract_to_float<TYPE_SMALLINT>(data_col, start, size, output);
                break;
            case TYPE_INT:
                extract_to_float<TYPE_INT>(data_col, start, size, output);
                break;
            case TYPE_BIGINT:
                extract_to_float<TYPE_BIGINT>(data_col, start, size, output);
                break;
            default:
                throw doris::Exception(ErrorCode::INVALID_ARGUMENT,
                                     "Unsupported array element type for function {}: {}",
                                     func_name, type);
        }
    }

public:
    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        const auto& arg1 = block.get_by_position(arguments[0]);
        const auto& arg2 = block.get_by_position(arguments[1]);

        auto col1 = arg1.column->convert_to_full_column_if_const();
        auto col2 = arg2.column->convert_to_full_column_if_const();
        if (col1->size() != col2->size()) {
            return Status::RuntimeError(
                    fmt::format("function {} have different input array sizes: {} and {}",
                                get_name(), col1->size(), col2->size()));
        }

        const ColumnArray* arr1 = nullptr;
        const ColumnArray* arr2 = nullptr;

        if (col1->is_nullable()) {
            if (col1->has_null()) {
                throw doris::Exception(ErrorCode::INVALID_ARGUMENT,
                                       "First argument for function {} cannot be null", get_name());
            }
            auto nullable1 = assert_cast<const ColumnNullable*>(col1.get());
            arr1 = assert_cast<const ColumnArray*>(nullable1->get_nested_column_ptr().get());
        } else {
            arr1 = assert_cast<const ColumnArray*>(col1.get());
        }

        if (col2->is_nullable()) {
            if (col2->has_null()) {
                throw doris::Exception(ErrorCode::INVALID_ARGUMENT,
                                       "Second argument for function {} cannot be null",
                                       get_name());
            }
            auto nullable2 = assert_cast<const ColumnNullable*>(col2.get());
            arr2 = assert_cast<const ColumnArray*>(nullable2->get_nested_column_ptr().get());
        } else {
            arr2 = assert_cast<const ColumnArray*>(col2.get());
        }

        const ColumnOffset64* offset1 =
                assert_cast<const ColumnArray::ColumnOffsets*>(arr1->get_offsets_ptr().get());
        const ColumnOffset64* offset2 =
                assert_cast<const ColumnArray::ColumnOffsets*>(arr2->get_offsets_ptr().get());

        // prepare return data
        auto dst = ColumnType::create(input_rows_count);
        auto& dst_data = dst->get_data();

        // Temporary buffers for converting integer arrays to float
        std::vector<float> float_buffer1;
        std::vector<float> float_buffer2;

        size_t elemt_cnt = offset1->size();
        for (ssize_t row = 0; row < elemt_cnt; ++row) {
            // Calculate actual array sizes for current row.
            ssize_t size1 = offset1->get_data()[row] - offset1->get_data()[row - 1];
            ssize_t size2 = offset2->get_data()[row] - offset2->get_data()[row - 1];

            if (size1 != size2) [[unlikely]] {
                return Status::InvalidArgument(
                        "function {} have different input element sizes of array: {} and {}",
                        get_name(), size1, size2);
            }

            size_t start1 = offset1->get_data()[row - 1];
            size_t start2 = offset2->get_data()[row - 1];

            // Extract data to float arrays
            extract_array_data(arr1, start1, size1, float_buffer1, get_name());
            extract_array_data(arr2, start2, size2, float_buffer2, get_name());

            // Calculate distance using float data
            dst_data[row] = DistanceImpl::distance(float_buffer1.data(), float_buffer2.data(), size1);
        }

        block.replace_by_position(result, std::move(dst));
        return Status::OK();
    }
};

} // namespace doris::vectorized
