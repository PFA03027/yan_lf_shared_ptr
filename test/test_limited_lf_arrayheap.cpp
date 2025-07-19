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

	NonTrivialType( const NonTrivialType& other ) = delete;   // Disable copy constructor
	NonTrivialType( NonTrivialType&& other ) noexcept
	  : p_value( other.p_value )
	{
		other.p_value = nullptr;   // Transfer ownership
	}

	NonTrivialType& operator=( const NonTrivialType& other ) = delete;
	NonTrivialType& operator=( NonTrivialType&& other ) noexcept
	{
		if ( this == &other ) {
			return *this;   // Handle self-assignment
		}
		delete p_value;                  // Clean up existing resource
		p_value       = other.p_value;   // Transfer ownership
		other.p_value = nullptr;         // Leave other in a valid state
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
	EXPECT_EQ( sut.load(), 42 );
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
	EXPECT_EQ( sut.load().get_value(), 42 );
}
