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

TEST( Lfheap2HeapElementWithTrivialType, CanConstructDestruct )
{
	// Arrange

	// Act
	lfheap2::heap_element<int> sut;

	// Assert
}

TEST( Lfheap2HeapElementWithNonTrivialType, CanConstructDestruct )
{
	// Arrange

	// Act
	lfheap2::heap_element<NonTrivialType> sut;

	// Assert
}

// =========================================================

class TypedPoolHeapWithTrivialType : public ::testing::Test {
protected:
	void SetUp() override
	{
		// Clear the heap before each test
		lfheap2::typed_pool_heap<int>::debug_destruction_and_regeneration();
	}
	void TearDown() override
	{
		// Optionally, you can clear the heap after each test
		lfheap2::typed_pool_heap<int>::debug_destruction_and_regeneration();
	}
};

TEST_F( TypedPoolHeapWithTrivialType, FreeListIsEmpty_CanAllocate_ThenReturnNonNullptr )
{
	// Arrange
	lfheap2::typed_pool_heap<int> sut_allocator;

	// Act
	auto* p_elem = sut_allocator.allocate( 1 );

	// Assert
	EXPECT_NE( p_elem, nullptr );
}

TEST_F( TypedPoolHeapWithTrivialType, FreeListIsNotEmpty_CanAllocate_ThenReturnNonNullptr )
{
	// Arrange
	lfheap2::typed_pool_heap<int> sut_allocator;
	auto*                         p_elem = sut_allocator.allocate( 1 );
	ASSERT_NE( p_elem, nullptr );
	sut_allocator.deallocate( p_elem, 1 );

	// Act
	auto* p_sut = sut_allocator.allocate( 1 );

	// Assert
	EXPECT_NE( p_sut, nullptr );
}
