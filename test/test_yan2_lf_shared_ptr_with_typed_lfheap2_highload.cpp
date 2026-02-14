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
#include <latch>
#include <thread>
#include <vector>

#include "test_comm.hpp"
#include "yan_lf_shared_ptr.hpp"

#include <gtest/gtest.h>

#if 1
constexpr size_t NUM_THREADS = 10;

template <typename T, typename Alloc>
size_t test_get_lf_shared_ptr_watermark( void ) noexcept
{
	using carrier_impl_allocator_type = typename std::allocator_traits<Alloc>::template rebind_alloc<yan2::itl::lf_shared_value_carrier_impl_in_place<T, Alloc>>;
	return carrier_impl_allocator_type::get_watermark();
}

TEST( YanLFSharedPtrWithTypedPoolHeapHighLoad, CanHandleHighLoad )
{
	// Arrange
	std::atomic<bool> done { false };
	std::latch        start_latch( 1 + NUM_THREADS );
	using AllocType = lfheap::typed_pool_heap<NonTrivialType>;
	AllocType::debug_destruction_and_regeneration();
	AllocType alloc;

	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [&done, &start_latch, alloc]() {
			start_latch.arrive_and_wait();
			size_t count = 0;
			while ( !done.load() ) {
				auto sp_elem = yan2::allocate_lf_shared<NonTrivialType>( alloc, 42U );   // Create shared pointer with value 42
				// NoTrivialTypeは非トリビアルな型なので、メモリリークを防ぐために適切に破棄される必要があります。
				// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果です。
				count++;
			}
			return count;
		} );   // 非同期実行する関数を登録する
		results.emplace_back( task.get_future() );

		threads.emplace_back( std::move( task ) );
	}

	// Act
	start_latch.arrive_and_wait();
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
	std::cout << "Watermark after high load: " << test_get_lf_shared_ptr_watermark<NonTrivialType, AllocType>() << std::endl;
}

TEST( YanLFSharedPtrWithTypedPoolHeapHighLoad, CanComparePerformanceWithStdSharedPtr )
{
	// Arrange
	std::atomic<bool> done { false };
	std::latch        start_latch( 1 + NUM_THREADS );

	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [&done, &start_latch]() {
			start_latch.arrive_and_wait();
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

	// Act
	start_latch.arrive_and_wait();
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
TEST( YanLFSharedPtrWithTypedPoolHeapHighLoad, CanComparePerformanceWithRcSharedPtr )
{
	// Arrange
	std::atomic<bool> done { false };
	std::latch        start_latch( 1 + NUM_THREADS );

	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [&done, &start_latch]() {
			start_latch.arrive_and_wait();
			size_t count = 0;
			while ( !done.load() ) {
				auto sp_elem = yan2::make_lf_shared<uint32_t>( 42U );   // Create shared pointer with value 42
				count++;
			}
			return count;
		} );   // 非同期実行する関数を登録する
		results.emplace_back( task.get_future() );

		threads.emplace_back( std::move( task ) );
	}

	// Act
	start_latch.arrive_and_wait();
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

TEST( YanLFSharedPtrWithTypedPoolHeapHighLoad, CanComparePerformanceWithRcSharedPtrWithAlloc )
{
	// Arrange
	std::atomic<bool> done { false };
	std::latch        start_latch( 1 + NUM_THREADS );
	using AllocType = lfheap::typed_pool_heap<uint32_t>;
	AllocType::debug_destruction_and_regeneration();
	AllocType alloc;

	std::vector<std::thread>         threads;
	std::vector<std::future<size_t>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<size_t()> task( [&done, &start_latch, alloc]() {
			start_latch.arrive_and_wait();
			size_t count = 0;
			while ( !done.load() ) {
				auto sp_elem = yan2::allocate_lf_shared<uint32_t>( alloc, 42U );   // Create shared pointer with value 42
				count++;
			}
			return count;
		} );   // 非同期実行する関数を登録する
		results.emplace_back( task.get_future() );

		threads.emplace_back( std::move( task ) );
	}

	// Act
	start_latch.arrive_and_wait();
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
	std::cout << "Watermark after high load: " << test_get_lf_shared_ptr_watermark<uint32_t, AllocType>() << std::endl;
}
#endif
