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

#include "limited_lf_arrayheap.hpp"

#include <gtest/gtest.h>

#if 0
TEST( LimitedLfArrayheapHighLoad, CanHandleHighLoad )
{
	// Arrange
	constexpr size_t  NUM_THREADS = 2;
	std::atomic<bool> done { false };

	// Act
	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [&done]() {
			size_t count = 0;
			while ( !done.load() ) {
				auto p_elem = rc::limited_arrayheap<int>::allocate();
				if ( p_elem == nullptr ) {
					throw std::runtime_error( "Failed to allocate element from limited_arrayheap" );
				}
				rc::limited_arrayheap<int>::retire( p_elem );
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
	for ( auto& r : results ) {
		size_t ret_count;
		EXPECT_NO_THROW( ret_count = r.get() );
		EXPECT_GT( ret_count, 0 );   // 各スレッドが少なくとも1つの要素を処理したことを確認する
	}
	EXPECT_LT( rc::limited_arrayheap<int>::get_watermark(), rc::limited_arrayheap<int>::NUM );
}
#endif