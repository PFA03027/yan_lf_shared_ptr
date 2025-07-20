/**
 * @file test_limited_lf_shared_ptr.cpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief
 * @version 0.1
 * @date 2025-07-20
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#include "limited_lf_shared_ptr.hpp"

#include "test_comm.hpp"

#include <gtest/gtest.h>

TEST( LimitedLfSharedPtr, CanConstructDestruct )
{
	// Arrange

	// Act
	rc::limited_lf_shared_ptr<int> sut;

	// Assert
	EXPECT_FALSE( sut.is_valid() );
}

TEST( LimitedLfSharedPtr, CanMakeLimitedLfSharedPtrWithTrivialType_ThenReturnValidPointer )
{
	// Arrange

	// Act
	auto p_sut = rc::make_limited_lf_shared_ptr<int>( 42 );

	// Assert
	EXPECT_TRUE( p_sut.is_valid() );
	EXPECT_EQ( *p_sut, 42 );
}

TEST( LimitedLfSharedPtr, CanMakeLimitedLfSharedPtrWithNonTrivialType_ThenReturnValidPointer )
{
	// Arrange

	// Act
	auto p_sut = rc::make_limited_lf_shared_ptr<NonTrivialType>( 42 );

	// Assert
	EXPECT_TRUE( p_sut.is_valid() );
	EXPECT_EQ( p_sut->get_value(), 42 );
}
