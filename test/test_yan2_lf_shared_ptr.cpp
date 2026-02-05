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

#include "yan_lf_shared_ptr.hpp"

#include "test_comm.hpp"

#include <gtest/gtest.h>

TEST( YanLFSharedPtr, CanConstructDestruct )
{
	// Arrange

	// Act
	yan2::lf_shared_ptr<int> sut;

	// Assert
	EXPECT_FALSE( sut );
}

TEST( YanLFSharedPtr, CanMakeLimitedLfSharedPtrWithTrivialType_ThenReturnValidPointer )
{
	// Arrange

	// Act
	auto sp_sut = yan2::make_lf_shared_ptr<int>( 42 );

	// Assert
	EXPECT_TRUE( sp_sut );
	EXPECT_EQ( *sp_sut, 42 );
}

TEST( YanLFSharedPtr, CanMakeLimitedLfSharedPtrWithNonTrivialType_ThenReturnValidPointer )
{
	// Arrange

	// Act
	auto sp_sut = yan2::make_lf_shared_ptr<NonTrivialType>( 42U );

	// Assert
	EXPECT_TRUE( sp_sut );
	EXPECT_EQ( sp_sut->get_value(), 42 );
}

TEST( YanLFSharedPtr, CanMakeLimitedLfSharedPtr_ThenReturnValidPointer )
{
	// Arrange
	{
		auto sp_dummy = yan2::make_lf_shared_ptr<int>( 41 );
	}

	// Act
	auto sp_sut = yan2::make_lf_shared_ptr<int>( 42 );

	// Assert
	EXPECT_TRUE( sp_sut );
	EXPECT_EQ( *sp_sut, 42 );
}

TEST( YanLFSharedPtrWithStdAlloc, CanAllocateShared )
{
	// Arrange
	using AllocType = std::allocator<NonTrivialType>;
	AllocType alloc;

	// Act
	auto sp_sut = yan2::allocate_lf_shared_ptr<NonTrivialType>( alloc, 42U );

	// Assert
	EXPECT_TRUE( sp_sut );
	EXPECT_EQ( sp_sut->get_value(), 42 );
}

TEST( YanLFSharedPtrWithStdAlloc, CanConstruct )
{
	// Arrange
	using DeleterType = std::default_delete<NonTrivialType>;
	DeleterType deleter;
	using AllocType = std::allocator<NonTrivialType>;
	AllocType alloc;

	// Act
	yan2::lf_shared_ptr<NonTrivialType> sp_sut( new NonTrivialType( 42U ), deleter, alloc );

	// Assert
	EXPECT_TRUE( sp_sut );
	EXPECT_EQ( sp_sut->get_value(), 42 );
}
