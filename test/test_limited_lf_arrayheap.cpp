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

#include "limited_lf_arrayheap.hpp"

#include <gtest/gtest.h>

class NonTrivialType {
public:
	~NonTrivialType()
	{
		delete p_value;   // Clean up dynamically allocated memory
	}
	NonTrivialType( int value = 42 )
	  : p_value( new int( value ) ) {}

	NonTrivialType( const NonTrivialType& other )
	  : p_value( new int( *other.p_value ) )   // Deep copy
	{
	}
	NonTrivialType( NonTrivialType&& other ) noexcept
	  : p_value( new int( *other.p_value ) )   // Deep copy
	{
	}

	NonTrivialType& operator=( const NonTrivialType& other )
	{
		if ( this == &other ) {
			return *this;   // Handle self-assignment
		}
		delete p_value;                        // Clean up existing resource
		p_value = new int( *other.p_value );   // Deep copy
		return *this;
	}
	NonTrivialType& operator=( NonTrivialType&& other ) noexcept
	{
		if ( this == &other ) {
			return *this;   // Handle self-assignment
		}
		delete p_value;                        // Clean up existing resource
		p_value = new int( *other.p_value );   // Deep copy
		return *this;
	}

	int get_value() const
	{
		return p_value ? *p_value : 0;   // Return value or 0 if p_value is null
	}

private:
	int* p_value;   //!< Pointer to an integer value, which is non-trivial due to dynamic memory allocation.
};

TEST( RcItlHeapElementWithTrivialType, CanConstructDestruct )
{
	// Arrange

	// Act
	rc::itl::heap_element<int> sut;

	// Assert
}

TEST( RcItlHeapElementWithTrivialType, CanStoreAndLoadValue )
{
	// Arrange
	rc::itl::heap_element<int> sut;

	// Act
	sut.store( 42 );

	// Assert
	EXPECT_EQ( sut.ref(), 42 );
}

TEST( RcItlHeapElementWithNonTrivialType, CanConstructDestruct )
{
	// Arrange

	// Act
	rc::itl::heap_element<NonTrivialType> sut;

	// Assert
}

TEST( RcItlHeapElementWithNonTrivialType, CanStoreAndLoadValue )
{
	// Arrange
	rc::itl::heap_element<NonTrivialType> sut;

	// Act
	sut.store( NonTrivialType() );

	// Assert
	EXPECT_EQ( sut.ref().get_value(), 42 );

	// Clean up
	sut.destruct_value();   // Ensure proper cleanup
}

// =========================================================

class RcLimitedArrayHeapWithTrivialType : public ::testing::Test {
protected:
	void SetUp() override
	{
		// Clear the heap before each test
		rc::limited_arrayheap<int>::debug_destruction_and_regeneration();
	}
	void TearDown() override
	{
		// Optionally, you can clear the heap after each test
		rc::limited_arrayheap<int>::debug_destruction_and_regeneration();
	}
};

TEST_F( RcLimitedArrayHeapWithTrivialType, FreeListIsEmpty_CanAllocate_ThenReturnNonNullptr )
{
	// Arrange

	// Act
	auto* p_elem = rc::limited_arrayheap<int>::allocate();

	// Assert
	EXPECT_NE( p_elem, nullptr );
}

TEST_F( RcLimitedArrayHeapWithTrivialType, FreeListIsNotEmpty_CanAllocate_ThenReturnNonNullptr )
{
	// Arrange
	auto* p_elem = rc::limited_arrayheap<int>::allocate();
	ASSERT_NE( p_elem, nullptr );
	rc::limited_arrayheap<int>::retire( p_elem );

	// Act
	auto* p_sut = rc::limited_arrayheap<int>::allocate();

	// Assert
	EXPECT_NE( p_sut, nullptr );
}
