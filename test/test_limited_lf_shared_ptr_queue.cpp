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

#include "limited_lf_arrayheap.hpp"
#include "limited_lf_shared_ptr.hpp"

#include "test_comm.hpp"

#include <gtest/gtest.h>

constexpr size_t QUEUE_SIZE = 100;

struct que_idxs {
	uint32_t in_idx_;
	uint32_t out_idx_;
};

enum class que_state : uintptr_t {
	unused         = 0,
	reserved_store = 1,
	stored         = 2,
	reserved_load  = 3
};

template <typename T, uint32_t QUENUM>
struct que_elem {
	std::atomic<typename rc::limited_arrayheap<rc::limited_lf_shared_ptr<T>, QUENUM>::element_type*> ap_;
	std::atomic<que_state>                                                                           status_;

	constexpr que_elem( void )
	  : ap_( nullptr )
	  , status_( que_state::unused )
	{
	}
};

template <typename T, uint32_t QUENUM = 100000>
class lf_sp_fifo {
public:
	constexpr lf_sp_fifo( void )
	  : que_idx_info_( que_idxs { 0, 0 } )
	  , queue_()
	{
	}

	~lf_sp_fifo()
	{
		for ( auto& e : queue_ ) {
			auto s = e.status_.load();
			if ( ( s == que_state::stored ) || ( s == que_state::reserved_load ) ) {
				auto p = e.ap_.load();
				p->destruct_value();
				queue_heap_t::retire( p );
			}
		}
	}

	std::optional<rc::limited_lf_shared_ptr<T>> push( rc::limited_lf_shared_ptr<T> pushed_sp )
	{
		auto p = queue_heap_t::allocate();
		if ( p == nullptr ) {
			// キューのエリアが確保できなかったので、push失敗
			return pushed_sp;
		}
		p->emplace( std::move( pushed_sp ) );

		if ( !push_ptr( p ) ) {
			pushed_sp = std::move( p->ref() );
			p->destruct_value();
			queue_heap_t::retire( p );
			return pushed_sp;
		}

		return std::nullopt;
	}

	std::optional<rc::limited_lf_shared_ptr<T>> pop( void )
	{
		rc::limited_lf_shared_ptr<T> pop_sp;
		while ( true ) {
			que_idxs cur_idxs   = que_idx_info_.load();
			uint32_t cur_in_idx = cur_idxs.in_idx_;
			uint32_t my_out_idx = cur_idxs.out_idx_;

			// キューにデータが入っているかどうかを確認する。
			if ( cur_in_idx == my_out_idx ) {
				return std::nullopt;
			}
			uint32_t updated_out_idx = my_out_idx + 1;
			if ( updated_out_idx >= QUENUM ) {
				updated_out_idx = 0;
			}

			que_state expect_state = que_state::stored;
			if ( !queue_[my_out_idx].status_.compare_exchange_strong( expect_state, que_state::reserved_load ) ) {
				// 別スレッドによって、先に場所を確保されてしまったので、最初からやり直す
				continue;
			}

			while ( !que_idx_info_.compare_exchange_strong( cur_idxs, que_idxs { cur_in_idx, updated_out_idx } ) ) {
#ifdef TEST_ENABLE_LOGICCHECKER
				if ( cur_idxs.out_idx_ != my_out_idx ) {
					std::cerr << "unexpected out index updated: " << cur_idxs.out_idx_ << " != " << my_out_idx << "    in_idx_ " << cur_idxs.in_idx_ << std::endl;
					throw std::logic_error( "unexpected out index updated." );
				}
#endif
				cur_in_idx = cur_idxs.in_idx_;
			}

			auto p = queue_[my_out_idx].ap_.load();
			queue_[my_out_idx].status_.store( que_state::unused );
			pop_sp = std::move( p->ref() );
			p->destruct_value();
			queue_heap_t::retire( p );
			break;
		}

		return pop_sp;
	}

private:
	using queue_heap_t = rc::limited_arrayheap<rc::limited_lf_shared_ptr<T>, QUENUM>;

	bool push_ptr( typename rc::limited_arrayheap<rc::limited_lf_shared_ptr<T>, QUENUM>::element_type* p )
	{
		while ( true ) {
			que_idxs  cur_idxs     = que_idx_info_.load();
			uint32_t  my_in_idx    = cur_idxs.in_idx_;
			uint32_t  cur_out_idx  = cur_idxs.out_idx_;
			que_state expect_state = que_state::unused;

			// 場所の確保にトライする。
			if ( !queue_[my_in_idx].status_.compare_exchange_strong( expect_state, que_state::reserved_store ) ) {
				// 別スレッドによって、先に場所を確保されてしまったので、最初からやり直す
				continue;
			}

			// キューに空きがあるかを確認する。
			// ヒープとキューサイズが同じなので、キューより多いサイズのallocateが要求された場合は、allocateの時点で失敗しているはず。
			// そのため、キューがあふれることはない(はず)だが、チェックする。
			uint32_t updated_in_idx = my_in_idx + 1;
			if ( updated_in_idx >= QUENUM ) {
				updated_in_idx = 0;
			}
			if ( updated_in_idx == cur_out_idx ) {
				// キューがあふれたので、push失敗
				queue_[my_in_idx].status_.store( que_state::unused );   // 状態を元に戻す
				return false;
			}

			while ( !que_idx_info_.compare_exchange_strong( cur_idxs, que_idxs { updated_in_idx, cur_out_idx } ) ) {
				if ( cur_idxs.in_idx_ != my_in_idx ) {
					// 別スレッドによって、先に処理を追い越されてしまったため、最初からやり直す。
					queue_[my_in_idx].status_.store( que_state::unused );   // 状態を元に戻す
#ifdef TEST_ENABLE_LOGICCHECKER
					// std::cout << "in index is takeovered by other thread " << cur_idxs.in_idx_ << " != " << my_in_idx << "   updated_in_idx = " << updated_in_idx << "    out_idx_ " << cur_idxs.out_idx_ << std::endl;
					// std::cout << "                             queue_[my_in_idx].status_ =        " << static_cast<uintptr_t>( queue_[my_in_idx].status_.load() ) << std::endl;
					// std::cout << "                             queue_[cur_idxs.in_idx_].status_ =  " << static_cast<uintptr_t>( queue_[cur_idxs.in_idx_].status_.load() ) << std::endl;
					// std::cout << "                             queue_[cur_idxs.out_idx_].status_ = " << static_cast<uintptr_t>( queue_[cur_idxs.out_idx_].status_.load() ) << std::endl;
#endif
					continue;   // 再度、キューの状態の確認からやり直す。
				}
				cur_out_idx = cur_idxs.out_idx_;
			}

			queue_[my_in_idx].ap_.store( p );
			queue_[my_in_idx].status_.store( que_state::stored );
			break;
		}

		return true;
	}

	std::atomic<que_idxs>                   que_idx_info_;
	std::array<que_elem<T, QUENUM>, QUENUM> queue_;

	static_assert( std::atomic<que_idxs>::is_always_lock_free, "std::atomic<que_idxs> should be lock-free" );
};

TEST( RcLfSpFifo, Empty_CanPush )
{
	// Arrange
	lf_sp_fifo<NonTrivialType, QUEUE_SIZE> sut;
	auto                                   sp_data = rc::make_limited_lf_shared_ptr<NonTrivialType>( 42U );

	// Act
	auto ret = sut.push( sp_data );

	// Assert
	EXPECT_FALSE( ret.has_value() );
}

TEST( RcLfSpFifo, Empty_CanPop )
{
	// Arrange
	lf_sp_fifo<NonTrivialType, QUEUE_SIZE> sut;

	// Act
	auto sp_ret = sut.pop();

	// Assert
	EXPECT_FALSE( sp_ret.has_value() );
}

TEST( RcLfSpFifo, Empty_CanPushPop )
{
	// Arrange
	lf_sp_fifo<NonTrivialType, QUEUE_SIZE> sut;
	auto                                   sp_data = rc::make_limited_lf_shared_ptr<NonTrivialType>( 42U );
	auto                                   ret     = sut.push( sp_data );
	EXPECT_FALSE( ret.has_value() );

	// Act
	auto sp_ret = sut.pop();

	// Assert
	ASSERT_TRUE( sp_ret.has_value() );
	EXPECT_EQ( ( *sp_ret )->get_value(), 42 );
}

TEST( RcLfSpFifo, Empty_CanPushPushPopPop )
{
	// Arrange
	lf_sp_fifo<NonTrivialType, QUEUE_SIZE> sut;
	auto                                   sp_data = rc::make_limited_lf_shared_ptr<NonTrivialType>( 42U );
	auto                                   ret     = sut.push( sp_data );
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

#if 0
TEST( RcLfSpFifo, Empty_CanPushPopHighload )
{
	// Arrange
	// NoTrivialTypeは非トリビアルな型なので、メモリリークを防ぐために適切に破棄される必要があります。
	// 不具合があれば、ここでメモリリークが発生します。そのメモリリークをLeakサニタイザーで検出するのが、このテストの効果です。
	lf_sp_fifo<NonTrivialType> sut;
	constexpr size_t           NUM_THREADS = 2;
	std::atomic<bool>          done { false };

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
					std::this_thread::yield();   // Yield to allow other threads to process
					ret = sut.pop();
					if ( !ret.has_value() ) {
						std::cerr << "Pop failed unexpectedly, should not happen in high load test. count = " << count << std::endl;
						throw std::logic_error( "Pop failed unexpectedly, should not happen in high load test. count = " + std::to_string( count ) );
					}
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
	std::cout << "Watermark after high load: " << rc::limited_arrayheap<NonTrivialType>::get_watermark() << std::endl;
	EXPECT_LT( rc::limited_arrayheap<NonTrivialType>::get_watermark(), rc::limited_arrayheap<NonTrivialType>::NUM );
}
#endif
