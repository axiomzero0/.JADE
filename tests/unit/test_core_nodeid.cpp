// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_core_nodeid.cpp

#include <gtest/gtest.h>
#include "jade/core/NodeId.hpp"

using namespace jade;

TEST(NodeIdTest, DefaultConstructorIsInvalid) {
    NodeId id;
    EXPECT_FALSE(id.valid());
}

TEST(NodeIdTest, ExplicitConstructorSetsValue) {
    NodeId id{42};
    EXPECT_TRUE(id.valid());
    EXPECT_EQ(id.value, 42u);
}

TEST(NodeIdTest, EqualityIsByValue) {
    EXPECT_EQ(NodeId{1}, NodeId{1});
    EXPECT_NE(NodeId{1}, NodeId{2});
}

TEST(NodeIdTest, OrderingIsByValue) {
    EXPECT_LT(NodeId{1}, NodeId{2});
    EXPECT_LE(NodeId{1}, NodeId{1});
    EXPECT_GT(NodeId{3}, NodeId{2});
}

TEST(NodeIdTest, InvalidSentinelIsZero) {
    EXPECT_EQ(NodeId::invalid().value, 0u);
    EXPECT_FALSE(NodeId::invalid().valid());
}

TEST(NodeIdTest, StartSentinelIsOne) {
    EXPECT_EQ(NodeId::start().value, 1u);
    EXPECT_TRUE(NodeId::start().valid());
}

TEST(NodeIdTest, BoolConversionMatchesValid) {
    EXPECT_FALSE(static_cast<bool>(NodeId{}));
    EXPECT_TRUE(static_cast<bool>(NodeId{1}));
}

TEST(NodeIdTest, RawAccessorReturnsValue) {
    NodeId id{123};
    EXPECT_EQ(id.raw(), 123u);
}

TEST(EdgeSliceTest, DefaultIsEmpty) {
    EdgeSlice s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
}

TEST(EdgeSliceTest, NonEmptyAfterSet) {
    EdgeSlice s{5, 3};
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s.size(), 3u);
    EXPECT_EQ(s.first_edge, 5u);
}
