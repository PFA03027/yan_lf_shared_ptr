/**
 * @file test_limited_lf_shared_ptr_queue.cpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief
 * @version 0.1
 * @date 2025-07-21
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#include <array>
#include <atomic>
#include <future>
#include <optional>
#include <thread>
#include <vector>

#include "lf_typed_heap.hpp"
#include "limited_lf_shared_ptr.hpp"
#include "limited_lf_shared_ptr_queue.hpp"

#include "test_comm.hpp"

#include <gtest/gtest.h>

TEST( TestRcLimitedLfSharedPtrQueue, CanDefaultConstruct )
{
	// Arrange
	static constexpr size_t QUEUE_SIZE = 100;

	// Act
	rc::limited_lf_shared_ptr_queue<NonTrivialType, QUEUE_SIZE> sut;

	// Assert
}

TEST( TestRcLimitedLfSharedPtrQueue, Empty_CanPush )
{
	// Arrange
	rc::limited_lf_shared_ptr_queue<NonTrivialType> sut;
	auto                                            sp_data = rc::make_limited_lf_shared_ptr<NonTrivialType>( 42U );

	// Act
	auto ret = sut.push( sp_data );

	// Assert
	EXPECT_FALSE( ret.has_value() );
}

TEST( TestRcLimitedLfSharedPtrQueue, Empty_CanPop )
{
	// Arrange
	rc::limited_lf_shared_ptr_queue<NonTrivialType> sut;

	// Act
	auto sp_ret = sut.pop();

	// Assert
	EXPECT_FALSE( sp_ret.has_value() );
}

TEST( TestRcLimitedLfSharedPtrQueue, Empty_CanPushPop )
{
	// Arrange
	rc::limited_lf_shared_ptr_queue<NonTrivialType> sut;
	auto                                            sp_data = rc::make_limited_lf_shared_ptr<NonTrivialType>( 42U );
	auto                                            ret     = sut.push( sp_data );
	EXPECT_FALSE( ret.has_value() );

	// Act
	auto sp_ret = sut.pop();

	// Assert
	ASSERT_TRUE( sp_ret.has_value() );
	EXPECT_EQ( ( *sp_ret )->get_value(), 42 );
}

TEST( TestRcLimitedLfSharedPtrQueue, Empty_CanPushPushPopPop )
{
	// Arrange
	rc::limited_lf_shared_ptr_queue<NonTrivialType> sut;
	auto                                            sp_data = rc::make_limited_lf_shared_ptr<NonTrivialType>( 42U );
	auto                                            ret     = sut.push( sp_data );
	EXPECT_FALSE( ret.has_value() );
	sp_data = rc::make_limited_lf_shared_ptr<NonTrivialType>( 43U );
	ret     = sut.push( sp_data );
	EXPECT_FALSE( ret.has_value() );

	// Act
	auto sp_ret1 = sut.pop();
	auto sp_ret2 = sut.pop();

	// Assert
	ASSERT_TRUE( sp_ret1.has_value() );
	EXPECT_EQ( ( *sp_ret1 )->get_value(), 42 );
	ASSERT_TRUE( sp_ret2.has_value() );
	EXPECT_EQ( ( *sp_ret2 )->get_value(), 43 );
}

#if 1
TEST( TestRcLimitedLfSharedPtrQueue, Empty_CanPushPopHighload )
{
	// Arrange
	// NoTrivialTypeは非トリビアルな型なので、メモリリークを防ぐために適切に破棄される必要があります。
	// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果です。
	using que_type               = rc::limited_lf_shared_ptr_queue<NonTrivialType>;
	using que_contents_heap_type = que_type::que_contents_heap_type;
	using que_node_heap_type     = que_type::que_node_heap_type;

	que_type          sut;
	constexpr size_t  NUM_THREADS = 20;
	std::atomic<bool> done { false };

	// Act
	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [&done, &sut]() {
			size_t count = 0;
			while ( !done.load() ) {
				auto sp_elem = rc::make_limited_lf_shared_ptr<NonTrivialType>( count );   // Create shared pointer with value 42
				auto ret     = sut.push( std::move( sp_elem ) );
				if ( ret.has_value() ) {
					std::cerr << "Push failed unexpectedly, should not happen in high load test. count = " << count << std::endl;
					throw std::logic_error( "Push failed unexpectedly, should not happen in high load test. count = " + std::to_string( count ) );
				}
				ret = sut.pop();
				if ( !ret.has_value() ) {
					std::cerr << "Pop failed unexpectedly, should not happen in high load test. count = " << count << std::endl;
					throw std::logic_error( "Pop failed unexpectedly, should not happen in high load test. count = " + std::to_string( count ) );
				}
				count = ( *ret )->get_value() + 1;
			}
			return count;
		} );   // 非同期実行する関数を登録する
		results.emplace_back( task.get_future() );

		threads.emplace_back( std::move( task ) );
	}
	std::this_thread::sleep_for( std::chrono::seconds( 1 ) );   // 1秒間実行する
	done.store( true );                                         // 全スレッドに終了を通知する

	// Assert
	for ( auto& t : threads ) {
		t.join();
	}
	size_t total_count = 0;
	for ( auto& r : results ) {
		size_t ret_count;
		EXPECT_NO_THROW( ret_count = r.get() );
		EXPECT_GT( ret_count, 0 );   // 各スレッドが少なくとも1つの要素を処理したことを確認する
		total_count += ret_count;
	}
	std::cout << "Total elements processed: " << total_count << std::endl;
	std::cout << "Watermark of contents in Queue after high load: " << que_contents_heap_type::get_watermark() << std::endl;
	std::cout << "Watermark of node in Queue after high load: " << que_node_heap_type::get_watermark() << std::endl;
	EXPECT_LT( rc::limited_arrayheap<NonTrivialType>::get_watermark(), que_contents_heap_type::NUM );
	EXPECT_LT( rc::limited_arrayheap<NonTrivialType>::get_watermark(), que_node_heap_type::NUM );
}
#endif
