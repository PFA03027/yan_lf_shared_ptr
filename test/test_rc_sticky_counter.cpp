/**
 * @file test_rc_sticky_counter.cpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief Test for the sticky counter in reference counting.
 * @version 0.1
 * @date 2025-07-19
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#include "rc_sticky_counter.hpp"

#include <gtest/gtest.h>

TEST( RcStickyCounter, CanConstruct )
{
	// Arrange

	// Act
	rc::sticky_counter sut;

	// Assert
	EXPECT_FALSE( sut.is_sticky_zero() );
}

TEST( RcStickyCounter, CanIncrementIfNotZero_ThenReturnTrue )
{
	// Arrange
	rc::sticky_counter sut;

	// Act
	bool result1 = sut.increment_if_not_zero();

	// Assert
	EXPECT_TRUE( result1 );
	EXPECT_FALSE( sut.is_sticky_zero() );
	EXPECT_EQ( sut.read(), 1 );
}

TEST( RcStickyCounter, CanDecrementThenIsZero_ThenReturnTrue )
{
	// Arrange
	rc::sticky_counter sut;
	sut.increment_if_not_zero();   // Increment to make sure counter is not zero

	// Act
	bool result = sut.decrement_then_is_zero();

	// Assert
	EXPECT_TRUE( result );
	EXPECT_TRUE( sut.is_sticky_zero() );
	EXPECT_EQ( sut.read(), 0 );
}

TEST( RcStickyCounter, CanIncrementMultipleTimes )
{
	// Arrange
	rc::sticky_counter sut;

	// Act
	for ( int i = 0; i < 5; ++i ) {
		EXPECT_TRUE( sut.increment_if_not_zero() );
	}

	// Assert
	EXPECT_FALSE( sut.is_sticky_zero() );
	EXPECT_EQ( sut.read(), 5 );
}

TEST( RcStickyCounter, CanDecrementMultipleTimes )
{
	// Arrange
	rc::sticky_counter sut;
	for ( int i = 0; i < 5; ++i ) {
		EXPECT_TRUE( sut.increment_if_not_zero() );
	}

	// Act
	for ( int i = 0; i < 4; ++i ) {
		EXPECT_FALSE( sut.decrement_then_is_zero() );
	}
	EXPECT_TRUE( sut.decrement_then_is_zero() );

	// Assert
	EXPECT_TRUE( sut.is_sticky_zero() );
	EXPECT_EQ( sut.read(), 0 );
}

TEST( RcStickyCounter, StickyZero_CanIncrementIfNotZero_ThenReturnFalse )
{
	// Arrange
	rc::sticky_counter sut;
	sut.increment_if_not_zero();    // Increment to make sure counter is not zero
	sut.decrement_then_is_zero();   // Set sticky zero

	// Act
	bool result = sut.increment_if_not_zero();

	// Assert
	EXPECT_FALSE( result );
	EXPECT_TRUE( sut.is_sticky_zero() );
	EXPECT_EQ( sut.read(), 0 );
}
