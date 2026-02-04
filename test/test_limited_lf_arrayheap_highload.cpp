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
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "lf_typed_heap.hpp"

#include <gtest/gtest.h>

TEST( LimitedLfArrayheapHighLoad, CanHandleHighLoad )
{
	// Arrange
	constexpr size_t                                       NUM_THREADS = 20;
	std::atomic<bool>                                      done { false };
	std::atomic<rc::limited_arrayheap<int>::element_type*> duplicate_bug_check { nullptr };

	// Act
	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [&done, &duplicate_bug_check]() {
			size_t count = 0;
			while ( !done.load() ) {
				count++;
				rc::limited_arrayheap<int>::element_type* p_elem = nullptr;
				try {
					p_elem = rc::limited_arrayheap<int>::allocate();
				} catch ( const std::exception& e ) {
					std::cerr << "Exception during allocation: count = " << count << std::endl;
					throw;
				}
				if ( p_elem == nullptr ) {
					// ヒープが枯渇してしまった場合を検出する。たいていは何らかのバグの副作用の結果として生じる。
					std::cerr << "Exception by Failed to allocate element from limited_arrayheap: count = " << count << std::endl;
					throw std::runtime_error( "Failed to allocate element from limited_arrayheap. current count = " + std::to_string( count ) );
				}
				if ( duplicate_bug_check.exchange( p_elem ) == p_elem ) {
					// 別のスレッドで同じポインタが取得されてしまった場合、バグを検出する。
					std::cerr << "Exception by Duplicate element detected in limited_arrayheap allocation: count = " << count << std::endl;
					throw std::logic_error( "Duplicate element detected in limited_arrayheap allocation. current count = " + std::to_string( count ) );
				}
				try {
					duplicate_bug_check.store( nullptr );   // Reset for next iteration
					rc::limited_arrayheap<int>::retire( p_elem );
				} catch ( const std::exception& e ) {
					std::cerr << "Exception during retire: count = " << count << std::endl;
					throw;
				}
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
		size_t ret_count = 0;
		EXPECT_NO_THROW( ret_count = r.get() );
		EXPECT_GT( ret_count, 0 );   // 各スレッドが少なくとも1つの要素を処理したことを確認する
		total_count += ret_count;
	}
	std::cout << "Total elements processed: " << total_count << std::endl;
	std::cout << "Watermark after high load: " << rc::limited_arrayheap<int>::get_watermark() << std::endl;
	EXPECT_LT( rc::limited_arrayheap<int>::get_watermark(), rc::limited_arrayheap<int>::NUM );
}
