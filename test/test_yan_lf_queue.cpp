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

// ===========================================
TEST( TestYanRcLfQueueCounterGuard, CanDefaultConstruct )
{
	// Arrange

	// Act
	rc::stickey_counter_try_increment_guard sut;

	// Assert
	EXPECT_FALSE( sut.owns_rc() );
}

TEST( TestYanRcLfQueueCounterGuard, CanConstructWithRc )
{
	// Arrange
	rc::sticky_counter rc;
	EXPECT_EQ( rc.read(), 1 );

	// Act
	rc::stickey_counter_try_increment_guard sut( rc );

	// Assert
	EXPECT_EQ( rc.read(), 2 );
	EXPECT_TRUE( sut.owns_rc() );
}

TEST( TestYanRcLfQueueCounterGuard, CanDestruct_Then_RcIsDecremented )
{
	// Arrange
	rc::sticky_counter rc;
	EXPECT_EQ( rc.read(), 1 );

	{
		// Act
		rc::stickey_counter_try_increment_guard sut( rc );

		// Assert
		EXPECT_EQ( rc.read(), 2 );
		EXPECT_TRUE( sut.owns_rc() );
	}

	EXPECT_EQ( rc.read(), 1 );
}

TEST( TestYanRcLfQueueCounterGuard, CanMoveConstruct )
{
	// Arrange
	rc::sticky_counter rc;
	EXPECT_EQ( rc.read(), 1 );

	{
		rc::stickey_counter_try_increment_guard sut1( rc );
		EXPECT_EQ( rc.read(), 2 );
		EXPECT_TRUE( sut1.owns_rc() );

		// Act
		rc::stickey_counter_try_increment_guard sut2( std::move( sut1 ) );

		// Assert
		EXPECT_EQ( rc.read(), 2 );
		EXPECT_TRUE( sut2.owns_rc() );
		EXPECT_FALSE( sut1.owns_rc() );
	}

	EXPECT_EQ( rc.read(), 1 );   // sut2 が rc を decrement したので、rc の値は 1 になる
}

TEST( TestYanRcLfQueueCounterGuard, CanMoveAssignment )
{
	// Arrange
	rc::sticky_counter rc;
	EXPECT_EQ( rc.read(), 1 );

	{
		rc::stickey_counter_try_increment_guard sut1( rc );
		rc::stickey_counter_try_increment_guard sut2( rc );
		EXPECT_EQ( rc.read(), 3 );

		// Act
		sut2 = std::move( sut1 );

		// Assert
		EXPECT_EQ( rc.read(), 2 );   // sut2 が rc を decrement したので、rc の値は 2 になる
		EXPECT_TRUE( sut2.owns_rc() );
		EXPECT_FALSE( sut1.owns_rc() );
	}

	EXPECT_EQ( rc.read(), 1 );   // sut1 が rc を decrement したので、rc の値は 1 になる
}

// ===========================================

TEST( TestYanRcLfQueue, CanDefaultConstruct )
{
	// Arrange

	// Act
	yan::rc_lf_queue<NonTrivialType> sut;

	// Assert
}

TEST( TestYanRcLfQueue, Empty_CanPush )
{
	// Arrange
	yan::rc_lf_queue<NonTrivialType> sut;

	// Act
	sut.push( NonTrivialType( 41 ) );

	// Assert
}

TEST( TestYanRcLfQueue, Empty_CanPop )
{
	// Arrange
	yan::rc_lf_queue<NonTrivialType> sut;

	// Act
	auto opt_ret = sut.try_pop();

	// Assert
	EXPECT_FALSE( opt_ret.has_value() );
}

TEST( TestYanRcLfQueue, Empty_CanPushPop )
{
	// Arrange
	yan::rc_lf_queue<NonTrivialType> sut;
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
	yan::rc_lf_queue<NonTrivialType> sut;
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
	// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果の一つです。
	yan::rc_lf_queue<NonTrivialType>::deallocate_all_free_nodes();
	yan::rc_lf_queue<NonTrivialType> sut;

	constexpr size_t  NUM_THREADS = 10;
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

	// Cleanup
	size_t deallocated_count = yan::rc_lf_queue<NonTrivialType>::deallocate_all_free_nodes();
	std::cout << "Total nodes deallocated in free: " << deallocated_count << std::endl;
}

#endif

#if 1
TEST( TestYanRcLfQueue, Empty_CanPopPopHighload )
{
	// Arrange
	// NoTrivialTypeは非トリビアルな型なので、メモリリークを防ぐために適切に破棄される必要があります。
	// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果の一つです。
	yan::rc_lf_queue<NonTrivialType>::deallocate_all_free_nodes();
	yan::rc_lf_queue<NonTrivialType> sut;

	constexpr size_t  NUM_THREADS = 10;
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

	// Cleanup
	size_t deallocated_count = yan::rc_lf_queue<NonTrivialType>::deallocate_all_free_nodes();
	std::cout << "Total nodes deallocated in free: " << deallocated_count << std::endl;
}

#endif

#if 1
TEST( TestYanRcLfQueue, Empty_CanPushPopHighload )
{
	// Arrange
	// NoTrivialTypeは非トリビアルな型なので、メモリリークを防ぐために適切に破棄される必要があります。
	// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果の一つです。
	yan::rc_lf_queue<NonTrivialType>::deallocate_all_free_nodes();
	yan::rc_lf_queue<NonTrivialType> sut;

	constexpr size_t  NUM_THREADS = 8;
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
		std::cout << "Thread finished processing " << ret_count << " elements." << std::endl;
	}
	std::cout << "Total elements processed: " << total_count << std::endl;
	std::cout << "Total loop count: " << total_loop_count << std::endl;
	EXPECT_EQ( total_count, total_loop_count );
	auto opt_ret = sut.try_pop();
	EXPECT_FALSE( opt_ret.has_value() );   // 最終的にキューが空であることを確認する

	// // Cleanup
	size_t deallocated_count = yan::rc_lf_queue<NonTrivialType>::deallocate_all_free_nodes();
	std::cout << "Total nodes deallocated in free: " << deallocated_count << std::endl;
}

#endif
