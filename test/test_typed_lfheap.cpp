/**
 * @file test_limited_lf_arrayheap.cpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief
 * @version 0.1
 * @date 2025-07-19
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#include "typed_lfheap.hpp"

#include "test_comm.hpp"

#include <gtest/gtest.h>

TEST( RcItlHeapElementWithTrivialType, CanConstructDestruct )
{
	// Arrange

	// Act
	lfheap::heap_element<int> sut;

	// Assert
}

#if 0
TEST( RcItlHeapElementWithTrivialType, CanStoreAndLoadValue )
{
	// Arrange
	lfheap::heap_element<int> sut;

	// Act
	sut.store( 42 );

	// Assert
	EXPECT_EQ( sut.ref(), 42 );
}
#endif

TEST( RcItlHeapElementWithNonTrivialType, CanConstructDestruct )
{
	// Arrange

	// Act
	lfheap::heap_element<NonTrivialType> sut;

	// Assert
}
#if 0
TEST( RcItlHeapElementWithNonTrivialType, CanStoreAndLoadValue )
{
	// Arrange
	lfheap::heap_element<NonTrivialType> sut;

	// Act
	sut.store( NonTrivialType() );

	// Assert
	EXPECT_EQ( sut.ref().get_value(), 42 );

	// Clean up
	sut.destruct_value();   // Ensure proper cleanup
}
#endif

// =========================================================

class RcLimitedArrayHeapWithTrivialType : public ::testing::Test {
protected:
	void SetUp() override
	{
		// Clear the heap before each test
		lfheap::typed_pool_heap<int>::debug_destruction_and_regeneration();
	}
	void TearDown() override
	{
		// Optionally, you can clear the heap after each test
		lfheap::typed_pool_heap<int>::debug_destruction_and_regeneration();
	}
};

TEST_F( RcLimitedArrayHeapWithTrivialType, FreeListIsEmpty_CanAllocate_ThenReturnNonNullptr )
{
	// Arrange

	// Act
	auto* p_elem = lfheap::typed_pool_heap<int>::allocate();

	// Assert
	EXPECT_NE( p_elem, nullptr );
}

TEST_F( RcLimitedArrayHeapWithTrivialType, FreeListIsNotEmpty_CanAllocate_ThenReturnNonNullptr )
{
	// Arrange
	auto* p_elem = lfheap::typed_pool_heap<int>::allocate();
	ASSERT_NE( p_elem, nullptr );
	lfheap::typed_pool_heap<int>::retire( p_elem );

	// Act
	auto* p_sut = lfheap::typed_pool_heap<int>::allocate();

	// Assert
	EXPECT_NE( p_sut, nullptr );
}
