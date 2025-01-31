#include <clickhouse/columns/array.h>
#include <clickhouse/columns/tuple.h>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/enum.h>
#include <clickhouse/columns/lowcardinality.h>
#include <clickhouse/columns/nullable.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <clickhouse/columns/uuid.h>
#include <clickhouse/columns/ip4.h>
#include <clickhouse/columns/ip6.h>
#include <clickhouse/base/input.h>
#include <clickhouse/base/output.h>
#include <clickhouse/base/socket.h> // for ipv4-ipv6 platform-specific stuff

#include <gtest/gtest.h>

#include "utils.h"
#include "value_generators.h"

namespace {
using namespace clickhouse;
}


// Generic tests for a Column subclass against basic API:
// 1. Constructor: Create, ensure that it is empty
// 2. Append: Create, add some data one by one via Append, make sure that values inserted match extracted with At() and operator[]
// 3. Slice: Create, add some data via Append, do Slice()
// 4. CloneEmpty Create, invoke CloneEmplty, ensure that clone is Empty
// 5. Clear: Create, add some data, invoke Clear(), make sure column is empty
// 6. Swap: create two instances, populate one with data, swap with second, make sure has data was transferred
// 7. Load/Save: create, append some data, save to buffer, load from same buffer into new column, make sure columns match.

template <typename T>
class GenericColumnTest : public testing::Test {
public:
    using ColumnType = std::decay_t<T>;

    static auto MakeColumn() {
        if constexpr (std::is_same_v<ColumnType, ColumnFixedString>) {
            return std::make_shared<ColumnFixedString>(12);
        } else if constexpr (std::is_same_v<ColumnType, ColumnDateTime64>) {
                return std::make_shared<ColumnDateTime64>(3);
        } else if constexpr (std::is_same_v<ColumnType, ColumnDecimal>) {
                return std::make_shared<ColumnDecimal>(10, 5);
        } else {
            return std::make_shared<ColumnType>();
        }
    }

    static auto GenerateValues(size_t values_size) {
        if constexpr (std::is_same_v<ColumnType, ColumnString>) {
            return GenerateVector(values_size, FooBarGenerator);
        } else if constexpr (std::is_same_v<ColumnType, ColumnFixedString>) {
            return GenerateVector(values_size, FromVectorGenerator{MakeFixedStrings(12)});
        } else if constexpr (std::is_same_v<ColumnType, ColumnDate>) {
            return GenerateVector(values_size, FromVectorGenerator{MakeDates()});
        } else if constexpr (std::is_same_v<ColumnType, ColumnDateTime>) {
            return GenerateVector(values_size, FromVectorGenerator{MakeDateTimes()});
        } else if constexpr (std::is_same_v<ColumnType, ColumnDateTime64>) {
            return MakeDateTime64s(3u, values_size);
        } else if constexpr (std::is_same_v<ColumnType, ColumnIPv4>) {
            return GenerateVector(values_size, FromVectorGenerator{MakeIPv4s()});
        } else if constexpr (std::is_same_v<ColumnType, ColumnIPv6>) {
            return GenerateVector(values_size, FromVectorGenerator{MakeIPv6s()});
        } else if constexpr (std::is_same_v<ColumnType, ColumnInt128>) {
            return GenerateVector(values_size, FromVectorGenerator{MakeInt128s()});
        } else if constexpr (std::is_same_v<ColumnType, ColumnDecimal>) {
            return GenerateVector(values_size, FromVectorGenerator{MakeDecimals(3, 10)});
        } else if constexpr (std::is_same_v<ColumnType, ColumnUUID>) {
            return GenerateVector(values_size, FromVectorGenerator{MakeUUIDs()});
        } else if constexpr (std::is_integral_v<typename ColumnType::ValueType>) {
            // ColumnUIntX and ColumnIntX
            return GenerateVector<typename ColumnType::ValueType>(values_size, RandomGenerator<int>());
        } else if constexpr (std::is_floating_point_v<typename ColumnType::ValueType>) {
            // OR ColumnFloatX
            return GenerateVector<typename ColumnType::ValueType>(values_size, RandomGenerator<typename ColumnType::ValueType>());
        }
    }

    template <typename Values>
    static void AppendValues(std::shared_ptr<ColumnType> column, const Values& values) {
        for (const auto & v : values) {
            column->Append(v);
        }
    }

    static auto MakeColumnWithValues(size_t values_size) {
        auto column = MakeColumn();
        auto values = GenerateValues(values_size);
        AppendValues(column, values);

        return std::tuple{column, values};
    }
};

using ValueColumns = ::testing::Types<
    ColumnUInt8, ColumnUInt16, ColumnUInt32, ColumnUInt64
    , ColumnInt8, ColumnInt16, ColumnInt32, ColumnInt64
    , ColumnFloat32, ColumnFloat64
    , ColumnString, ColumnFixedString
    , ColumnDate, ColumnDateTime, ColumnDateTime64
    , ColumnIPv4, ColumnIPv6
    , ColumnInt128
    , ColumnDecimal
    , ColumnUUID
>;
TYPED_TEST_SUITE(GenericColumnTest, ValueColumns);

TYPED_TEST(GenericColumnTest, Construct) {
    auto column = this->MakeColumn();
    ASSERT_EQ(0u, column->Size());
}

TYPED_TEST(GenericColumnTest, EmptyColumn) {
    auto column = this->MakeColumn();
    ASSERT_EQ(0u, column->Size());

    // verify that Column methods work as expected on empty column:
    // some throw exceptions, some return poper values (like CloneEmpty)

    // Shouldn't be able to get items on empty column.
    ASSERT_ANY_THROW(column->At(0));

    {
        auto slice = column->Slice(0, 0);
        ASSERT_NO_THROW(slice->template AsStrict<typename TestFixture::ColumnType>());
        ASSERT_EQ(0u, slice->Size());
    }

    {
        auto clone = column->CloneEmpty();
        ASSERT_NO_THROW(clone->template AsStrict<typename TestFixture::ColumnType>());
        ASSERT_EQ(0u, clone->Size());
    }

    ASSERT_NO_THROW(column->Clear());
    ASSERT_NO_THROW(column->Swap(*this->MakeColumn()));
}

TYPED_TEST(GenericColumnTest, Append) {
    auto column = this->MakeColumn();
    const auto values = this->GenerateValues(100);

    for (const auto & v : values) {
        EXPECT_NO_THROW(column->Append(v));
    }

    EXPECT_TRUE(CompareRecursive(values, *column));
}

TYPED_TEST(GenericColumnTest, Slice) {
    auto [column, values] = this->MakeColumnWithValues(100);

    auto untyped_slice = column->Slice(0, column->Size());
    auto slice = untyped_slice->template AsStrict<typename TestFixture::ColumnType>();
    EXPECT_EQ(column->GetType(), slice->GetType());

    EXPECT_TRUE(CompareRecursive(values, *slice));

    // TODO: slices of different sizes
}

TYPED_TEST(GenericColumnTest, CloneEmpty) {
    auto [column, values] = this->MakeColumnWithValues(100);
    EXPECT_EQ(values.size(), column->Size());

    auto clone_untyped = column->CloneEmpty();
    // Check that type matches
    auto clone = clone_untyped->template AsStrict<typename TestFixture::ColumnType>();
    EXPECT_EQ(0u, clone->Size());

    EXPECT_EQ(column->GetType(), clone->GetType());
}

TYPED_TEST(GenericColumnTest, Clear) {
    auto [column, values] = this->MakeColumnWithValues(100);
    EXPECT_EQ(values.size(), column->Size());

    column->Clear();
    EXPECT_EQ(0u, column->Size());
}

TYPED_TEST(GenericColumnTest, Swap) {
    auto [column_A, values] = this->MakeColumnWithValues(100);
    auto column_B = this->MakeColumn();

    column_A->Swap(*column_B);

    EXPECT_EQ(0u, column_A->Size());
    EXPECT_TRUE(CompareRecursive(values, *column_B));
}

TYPED_TEST(GenericColumnTest, LoadAndSave) {
    auto [column_A, values] = this->MakeColumnWithValues(100);

    char buffer[4096] = {'\0'};
    {
        ArrayOutput output(buffer, sizeof(buffer));
        // Save
        EXPECT_NO_THROW(column_A->Save(&output));
    }

    auto column_B = this->MakeColumn();
    {
        ArrayInput input(buffer, sizeof(buffer));
        // Load
        EXPECT_TRUE(column_B->Load(&input, values.size()));
    }

    EXPECT_TRUE(CompareRecursive(*column_A, *column_B));
}
