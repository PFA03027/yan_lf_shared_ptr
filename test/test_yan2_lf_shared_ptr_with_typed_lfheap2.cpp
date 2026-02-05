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

#include "typed_lfheap.hpp"
#include "yan_lf_shared_ptr.hpp"

#include "test_comm.hpp"

#include <gtest/gtest.h>

TEST( YanLFSharedPtrWithTypedPoolHeap, CanAllocateShared )
{
	// Arrange
	using AllocType = lfheap::typed_pool_heap2<NonTrivialType>;
	AllocType alloc;

	// Act
	auto sp_sut = yan2::allocate_lf_shared<NonTrivialType>( alloc, 42U );

	// Assert
	EXPECT_TRUE( sp_sut );
	EXPECT_EQ( sp_sut->get_value(), 42 );
}

TEST( YanLFSharedPtrWithTypedPoolHeap, CanConstruct )
{
	// Arrange
	using AllocType = lfheap::typed_pool_heap2<NonTrivialType>;
	AllocType alloc;
	using DeleterType = lfheap::deleter_via_typed_pool_heap2<NonTrivialType>;
	DeleterType deleter;

	using allocator_traits_type = std::allocator_traits<AllocType>;

	AllocType tmp_alloc;

	NonTrivialType* p_new = allocator_traits_type::allocate( tmp_alloc, 1 );
	try {
		allocator_traits_type::construct( tmp_alloc, p_new, 42U );
	} catch ( ... ) {
		allocator_traits_type::deallocate( tmp_alloc, p_new, 1 );
		throw;
	}

	// Act
	yan2::lf_shared_ptr<NonTrivialType> sp_sut( p_new, deleter, alloc );

	// Assert
	EXPECT_TRUE( sp_sut );
	EXPECT_EQ( sp_sut->get_value(), 42 );

	sp_sut.reset();
}
