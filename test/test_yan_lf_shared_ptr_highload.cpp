/**
 * @file test_limited_lf_arrayheap_highload.cpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief
 * @version 0.1
 * @date 2025-07-20
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#include <atomic>
#include <future>
#include <thread>
#include <vector>

#include "test_comm.hpp"
#include "yan_lf_shared_ptr.hpp"

#include <gtest/gtest.h>

#if 1
constexpr size_t NUM_THREADS = 10;

TEST( LimitedLfSharedPtrHighLoad, CanHandleHighLoad )
{
	// Arrange
	std::atomic<bool> done { false };

	// Act
	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [&done]() {
			size_t count = 0;
			while ( !done.load() ) {
				auto sp_elem = yan::make_limited_lf_shared_ptr<NonTrivialType>( 42U );   // Create shared pointer with value 42
				// NoTrivialTypeは非トリビアルな型なので、メモリリークを防ぐために適切に破棄される必要があります。
				// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果です。
				count++;
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
	std::cout << "Watermark after high load: " << lfheap::typed_pool_heap<NonTrivialType>::get_watermark() << std::endl;
	EXPECT_LT( lfheap::typed_pool_heap<NonTrivialType>::get_watermark(), lfheap::typed_pool_heap<NonTrivialType>::NUM );
}

TEST( LimitedLfSharedPtrHighLoad, CanComparePerformanceWithStdSharedPtr )
{
	// Arrange
	std::atomic<bool> done { false };

	// Act
	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [&done]() {
			size_t count = 0;
			while ( !done.load() ) {
				auto sp_elem = std::make_shared<uint32_t>( 42U );   // Create shared pointer with value 42
				count++;
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
}
TEST( LimitedLfSharedPtrHighLoad, CanComparePerformanceWithRcSharedPtr )
{
	// Arrange
	std::atomic<bool> done { false };

	// Act
	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [&done]() {
			size_t count = 0;
			while ( !done.load() ) {
				auto sp_elem = yan::make_limited_lf_shared_ptr<uint32_t>( 42U );   // Create shared pointer with value 42
				count++;
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
}
#endif
