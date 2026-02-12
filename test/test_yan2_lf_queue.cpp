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
#include <latch>
#include <optional>
#include <thread>
#include <vector>

#include "typed_lfheap.hpp"
#include "yan_lf_queue.hpp"
#include "yan_lf_shared_ptr.hpp"

#include "test_comm.hpp"

#include <gtest/gtest.h>

TEST( TestRcLimitedLfSharedPtrQueue, CanDefaultConstruct )
{
	// Arrange

	// Act
	yan::shared_ptr_lf_queue<NonTrivialType> sut;

	// Assert
}

TEST( TestRcLimitedLfSharedPtrQueue, Empty_CanPush )
{
	// Arrange
	yan::shared_ptr_lf_queue<NonTrivialType> sut;
	auto                                     sp_data = yan::make_limited_lf_shared_ptr<NonTrivialType>( 42U );

	// Act
	auto ret = sut.push( sp_data );

	// Assert
	EXPECT_FALSE( ret.has_value() );
}

TEST( TestRcLimitedLfSharedPtrQueue, Empty_CanPop )
{
	// Arrange
	yan::shared_ptr_lf_queue<NonTrivialType> sut;

	// Act
	auto sp_ret = sut.try_pop();

	// Assert
	EXPECT_FALSE( sp_ret.has_value() );
}

TEST( TestRcLimitedLfSharedPtrQueue, Empty_CanPushPop )
{
	// Arrange
	yan::shared_ptr_lf_queue<NonTrivialType> sut;
	auto                                     sp_data = yan::make_limited_lf_shared_ptr<NonTrivialType>( 42U );
	auto                                     ret     = sut.push( sp_data );
	EXPECT_FALSE( ret.has_value() );

	// Act
	auto sp_ret = sut.try_pop();

	// Assert
	ASSERT_TRUE( sp_ret.has_value() );
	EXPECT_EQ( ( *sp_ret )->get_value(), 42 );
}

TEST( TestRcLimitedLfSharedPtrQueue, Empty_CanPushPushPopPop )
{
	// Arrange
	yan::shared_ptr_lf_queue<NonTrivialType> sut;
	auto                                     sp_data = yan::make_limited_lf_shared_ptr<NonTrivialType>( 42U );
	auto                                     ret     = sut.push( sp_data );
	EXPECT_FALSE( ret.has_value() );
	sp_data = yan::make_limited_lf_shared_ptr<NonTrivialType>( 43U );
	ret     = sut.push( sp_data );
	EXPECT_FALSE( ret.has_value() );

	// Act
	auto sp_ret1 = sut.try_pop();
	auto sp_ret2 = sut.try_pop();

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
	using que_type               = yan::shared_ptr_lf_queue<NonTrivialType>;
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
				auto sp_elem = yan::make_limited_lf_shared_ptr<NonTrivialType>( count );   // Create shared pointer with value 42
				auto ret     = sut.push( std::move( sp_elem ) );
				if ( ret.has_value() ) {
					std::cerr << "Push failed unexpectedly, should not happen in high load test. count = " << count << std::endl;
					throw std::logic_error( "Push failed unexpectedly, should not happen in high load test. count = " + std::to_string( count ) );
				}
				ret = sut.try_pop();
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
	EXPECT_LT( lfheap::typed_pool_heap<NonTrivialType>::get_watermark(), que_contents_heap_type::NUM );
	EXPECT_LT( lfheap::typed_pool_heap<NonTrivialType>::get_watermark(), que_node_heap_type::NUM );
}
#endif

// ===========================================

TEST( TestYanRcLfQueue, CanDefaultConstruct )
{
	// Arrange

	// Act
	yan2::rc_lf_queue<NonTrivialType> sut;

	// Assert
}

TEST( TestYanRcLfQueue, Empty_CanPush )
{
	// Arrange
	yan2::rc_lf_queue<NonTrivialType> sut;

	// Act
	sut.push( NonTrivialType( 41 ) );

	// Assert
}

TEST( TestYanRcLfQueue, Empty_CanPop )
{
	// Arrange
	yan2::rc_lf_queue<NonTrivialType> sut;

	// Act
	auto opt_ret = sut.try_pop();

	// Assert
	EXPECT_FALSE( opt_ret.has_value() );
}

TEST( TestYanRcLfQueue, Empty_CanPushPop )
{
	// Arrange
	yan2::rc_lf_queue<NonTrivialType> sut;
	sut.push( NonTrivialType( 41 ) );

	// Act
	auto opt_ret = sut.try_pop();

	// Assert
	ASSERT_TRUE( opt_ret.has_value() );
	EXPECT_EQ( opt_ret->get_value(), 41 );
}

TEST( TestYanRcLfQueue, Empty_CanPushPushPopPop )
{
	// Arrange
	yan2::rc_lf_queue<NonTrivialType> sut;
	sut.push( NonTrivialType( 41 ) );
	sut.push( NonTrivialType( 43 ) );

	// Act
	auto opt_ret1 = sut.try_pop();
	auto opt_ret2 = sut.try_pop();

	// Assert
	ASSERT_TRUE( opt_ret1.has_value() );
	EXPECT_EQ( opt_ret1->get_value(), 41 );
	ASSERT_TRUE( opt_ret2.has_value() );
	EXPECT_EQ( opt_ret2->get_value(), 43 );
}

#if 1
TEST( TestYanRcLfQueue, Empty_CanPushPushHighload )
{
	// Arrange
	// NoTrivialTypeは非トリビアルな型なので、メモリリークを防ぐために適切に破棄される必要があります。
	// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果です。
	yan2::rc_lf_queue<NonTrivialType> sut;

	constexpr size_t  NUM_THREADS = 2;
	std::atomic<bool> done { false };
	std::latch        start_latch( 1 + NUM_THREADS );

	// Act
	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [i, &done, &sut, &start_latch]() {
			size_t loop_count = 0;
			std::cout << "Thread " << i << " started processing." << std::endl;
			start_latch.arrive_and_wait();   // すべてのスレッドが準備完了するまで待機する
			while ( !done.load() ) {
				sut.push( NonTrivialType( loop_count ) );
				loop_count++;
			}

			return loop_count;
		} );   // 非同期実行する関数を登録する
		results.emplace_back( task.get_future() );

		threads.emplace_back( std::move( task ) );
	}

	start_latch.arrive_and_wait();                              // メインスレッドも含めて、すべてのスレッドが準備完了するまで待機する
	std::this_thread::sleep_for( std::chrono::seconds( 1 ) );   // 1秒間実行する
	done.store( true );                                         // 全スレッドに終了を通知する

	// Assert
	for ( auto& t : threads ) {
		t.join();
	}
	size_t total_count = 0;
	for ( auto& r : results ) {
		size_t result_count;
		EXPECT_NO_THROW( result_count = r.get() );
		EXPECT_GT( result_count, 0 );   // 各スレッドが少なくとも1つの要素を処理したことを確認する
		total_count += result_count;
	}
	for ( size_t i = 0; i < total_count; ++i ) {
		auto opt_ret = sut.try_pop();
		EXPECT_TRUE( opt_ret.has_value() );   // キューから要素を取り出せることを確認する
	}
	std::cout << "Total elements processed: " << total_count << std::endl;

	auto opt_ret = sut.try_pop();
	EXPECT_FALSE( opt_ret.has_value() );   // 最終的にキューが空であることを確認する
}

#endif

#if 1
TEST( TestYanRcLfQueue, Empty_CanPopPopHighload )
{
	// Arrange
	// NoTrivialTypeは非トリビアルな型なので、メモリリークを防ぐために適切に破棄される必要があります。
	// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果です。
	yan2::rc_lf_queue<NonTrivialType> sut;

	constexpr size_t  NUM_THREADS = 2;
	std::atomic<bool> done { false };
	std::latch        start_latch( 1 + NUM_THREADS );

	constexpr size_t total_count = 100000;
	for ( size_t i = 0; i < total_count; ++i ) {
		sut.push( NonTrivialType( i ) );
	}

	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [i, &done, &sut, &start_latch]() {
			bool   loop_flag  = true;
			size_t loop_count = 0;
			std::cout << "Thread " << i << " started processing." << std::endl;
			start_latch.arrive_and_wait();   // すべてのスレッドが準備完了するまで待機する
			do {
				loop_count++;
				auto opt_ret = sut.try_pop();
				loop_flag    = opt_ret.has_value();
			} while ( loop_flag );
			loop_count--;   // 最後のループは要素が取得できなかった分を減算する

			return loop_count;
		} );   // 非同期実行する関数を登録する
		results.emplace_back( task.get_future() );

		threads.emplace_back( std::move( task ) );
	}

	// Act
	start_latch.arrive_and_wait();                              // メインスレッドも含めて、すべてのスレッドが準備完了するまで待機する
	std::this_thread::sleep_for( std::chrono::seconds( 1 ) );   // 1秒間実行する
	done.store( true );                                         // 全スレッドに終了を通知する

	// Assert
	for ( auto& t : threads ) {
		t.join();
	}
	size_t pop_total_count = 0;
	for ( auto& r : results ) {
		size_t result_count;
		EXPECT_NO_THROW( result_count = r.get() );
		EXPECT_GT( result_count, 0 );   // 各スレッドが少なくとも1つの要素を処理したことを確認する
		pop_total_count += result_count;
	}
	std::cout << "Total elements processed: " << pop_total_count << std::endl;
	EXPECT_EQ( pop_total_count, total_count );

	auto opt_ret = sut.try_pop();
	EXPECT_FALSE( opt_ret.has_value() );   // 最終的にキューが空であることを確認する
}

#endif

#if 1
TEST( TestYanRcLfQueue, Empty_CanPushPopHighload )
{
	// Arrange
	// NoTrivialTypeは非トリビアルな型なので、メモリリークを防ぐために適切に破棄される必要があります。
	// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果です。
	yan2::rc_lf_queue<NonTrivialType> sut;

	constexpr size_t  NUM_THREADS = 2;
	std::atomic<bool> done { false };
	std::latch        start_latch( 1 + NUM_THREADS );

	std::vector<std::thread>                            threads;
	std::vector<std::future<std::pair<size_t, size_t>>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<std::pair<size_t, size_t>()> task( [i, &done, &sut, &start_latch]() {
			size_t count      = 0;
			size_t loop_count = 0;
			std::cout << "Thread " << i << " started processing." << std::endl;
			start_latch.arrive_and_wait();   // すべてのスレッドが準備完了するまで待機する
			while ( !done.load() ) {
				sut.push( NonTrivialType( count ) );
				auto opt_ret = sut.try_pop();
				if ( !opt_ret.has_value() ) {
					std::cerr << "Pop failed unexpectedly, should not happen in high load test. count = " << count << std::endl;
					throw std::logic_error( "Pop failed unexpectedly, should not happen in high load test. count = " + std::to_string( count ) );
				}
				loop_count++;
				count = opt_ret->get_value() + 1;
			}

			std::cout << "Thread " << i << " finished processing " << count << " elements." << std::endl;
			return std::make_pair( count, loop_count );
		} );   // 非同期実行する関数を登録する
		results.emplace_back( task.get_future() );

		threads.emplace_back( std::move( task ) );
	}

	// Act
	start_latch.arrive_and_wait();                              // メインスレッドも含めて、すべてのスレッドが準備完了するまで待機する
	std::this_thread::sleep_for( std::chrono::seconds( 1 ) );   // 1秒間実行する
	done.store( true );                                         // 全スレッドに終了を通知する

	// Assert
	for ( auto& t : threads ) {
		t.join();
	}
	size_t total_count      = 0;
	size_t total_loop_count = 0;
	for ( auto& r : results ) {
		std::pair<size_t, size_t> result_counts;
		EXPECT_NO_THROW( result_counts = r.get() );
		size_t ret_count      = result_counts.first;
		size_t ret_loop_count = result_counts.second;
		EXPECT_GT( ret_count, 0 );   // 各スレッドが少なくとも1つの要素を処理したことを確認する
		total_count += ret_count;
		total_loop_count += ret_loop_count;
	}
	std::cout << "Total elements processed: " << total_count << std::endl;
	std::cout << "Total loop count: " << total_loop_count << std::endl;
	EXPECT_EQ( total_count, total_loop_count );
	auto opt_ret = sut.try_pop();
	EXPECT_FALSE( opt_ret.has_value() );   // 最終的にキューが空であることを確認する
}

#endif
