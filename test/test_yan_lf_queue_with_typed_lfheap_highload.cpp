/**
 * @file test_yan_lf_queue_with_typed_lfheap_highload.cpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief
 * @version 0.1
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#include <atomic>
#include <future>
#include <latch>
#include <list>
#include <mutex>
#include <optional>
#include <thread>

#include "typed_lfheap.hpp"
#include "yan_lf_queue.hpp"
#include "yan_lf_shared_ptr.hpp"

#include "test_comm.hpp"

#include <gtest/gtest.h>

template <typename T, typename Alloc = std::allocator<T>>
class MutexQueue {
public:
	constexpr MutexQueue( void )
	  : mtx_()
	  , que_()
	{
	}

	void push( const T& v )
	{
		std::lock_guard lk( mtx_ );
		que_.emplace_back( v );
	}
	void push( T&& v )
	{
		std::lock_guard lk( mtx_ );
		que_.emplace_back( std::move( v ) );
	}

	std::optional<T> try_pop( void )
	{
		std::lock_guard lk( mtx_ );

		if ( que_.empty() ) {
			return std::nullopt;
		}

		if constexpr ( std::is_move_constructible<T>::value ) {
			T ans( std::move( que_.front() ) );
			que_.pop_front();
			return ans;
		} else {
			T ans( que_.front() );
			que_.pop_front();
			return ans;
		}
	}

private:
	std::mutex          mtx_;
	std::list<T, Alloc> que_;
};

// ===========================================
template <typename QUE_TYPE>
class TestYanRcLfQueueWithLFAllocHighload : public ::testing::Test {
protected:
	void SetUp() override
	{
	}

	void TearDown() override
	{
	}
};

typedef ::testing::Types<
	MutexQueue<size_t>,
	MutexQueue<size_t, lfheap::typed_pool_heap<size_t>>,
	yan::rc_lf_queue<size_t, std::allocator<size_t>, false>,
	yan::rc_lf_queue<size_t, std::allocator<size_t>, true>,
	yan::rc_lf_queue<size_t, lfheap::typed_pool_heap<size_t>, false>,
	yan::rc_lf_queue<size_t, lfheap::typed_pool_heap<size_t>, true>>
	MyQueTypes;
TYPED_TEST_CASE( TestYanRcLfQueueWithLFAllocHighload, MyQueTypes );

#if 1
TYPED_TEST( TestYanRcLfQueueWithLFAllocHighload, Empty_CanPushPopHighload )
{
	// Arrange
	// NoTrivialTypeは非トリビアルな型なので、メモリリークを防ぐために適切に破棄される必要があります。
	// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果の一つです。
	TypeParam sut;

	constexpr size_t  NUM_THREADS = 8;
	std::atomic<bool> done { false };
	std::latch        start_latch( 1 + NUM_THREADS );

	std::vector<std::thread>                            threads;
	std::vector<std::future<std::pair<size_t, size_t>>> results;
	for ( size_t i = 0; i < NUM_THREADS; ++i ) {
		std::packaged_task<std::pair<size_t, size_t>()> task( [i, &done, &sut, &start_latch]() {
			size_t count      = 0;
			size_t loop_count = 0;
			// std::cout << "Thread " << i << " started processing." << std::endl;
			start_latch.arrive_and_wait();   // すべてのスレッドが準備完了するまで待機する
			while ( !done.load() ) {
				sut.push( count );
				auto opt_ret = sut.try_pop();
				if ( !opt_ret.has_value() ) {
					std::cerr << "Pop failed unexpectedly, should not happen in high load test. count = " << count << std::endl;
					throw std::logic_error( "Pop failed unexpectedly, should not happen in high load test. count = " + std::to_string( count ) );
				}
				loop_count++;
				count = ( *opt_ret ) + 1;
			}

			// std::cout << "Thread " << i << " finished processing " << count << " elements." << std::endl;
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
		// std::cout << "Thread finished processing " << ret_count << " elements." << std::endl;
	}
	std::cout << "Total elements processed: " << total_count << std::endl;
	std::cout << "Total loop count: " << total_loop_count << std::endl;
	EXPECT_EQ( total_count, total_loop_count );
	auto opt_ret = sut.try_pop();
	EXPECT_FALSE( opt_ret.has_value() );   // 最終的にキューが空であることを確認する
}

#endif
