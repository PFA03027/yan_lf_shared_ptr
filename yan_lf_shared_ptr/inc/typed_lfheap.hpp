/**
 * @file typed_lfheap.hpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief
 * @version 0.1
 * @date 2025-07-19
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#ifndef LF_TYPED_HEAP_HPP_
#define LF_TYPED_HEAP_HPP_

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "rc_sticky_counter.hpp"

// ========================================================================

namespace lfheap {

namespace itl {

/**
 * @brief counter guard for integral type atomic variable.
 *
 * @tparam IAV integral type atomic variable
 */
template <typename IAV>
class counter_guard {
public:
	~counter_guard()
	{
		if ( p_counter_ == nullptr ) {
			return;   // nothing to do
		}
		p_counter_->fetch_sub( 1 /* , std::memory_order_acq_rel */ );
	}
	constexpr counter_guard( void ) noexcept
	  : p_counter_( nullptr )
	{
	}

	counter_guard( const counter_guard& src ) noexcept
	  : p_counter_( src.p_counter_ )
	{
		if ( p_counter_ == nullptr ) {
			return;   // nothing to do
		}
		p_counter_->fetch_add( 1 /* , std::memory_order_acq_rel */ );
	}

	counter_guard( counter_guard&& src ) noexcept
	  : p_counter_( src.p_counter_ )
	{
		src.p_counter_ = nullptr;   // reset source pointer to avoid double decrement
	}

	counter_guard& operator=( const counter_guard& src ) noexcept
	{
		if ( this == &src ) {
			return *this;   // Handle self-assignment
		}

		counter_guard( src ).swap( *this );   // Use copy-and-swap idiom for exception safety
		return *this;
	}
	counter_guard& operator=( counter_guard&& src ) noexcept
	{
		if ( this == &src ) {
			return *this;   // Handle self-assignment
		}

		counter_guard( std::move( src ) ).swap( *this );   // Use move-and-swap idiom for exception safety
		return *this;
	}

	explicit counter_guard( IAV& iav_ref ) noexcept
	  : p_counter_( &iav_ref )
	{
		p_counter_->fetch_add( 1 /* , std::memory_order_acq_rel */ );
	}

	void swap( counter_guard& other ) noexcept
	{
		std::swap( p_counter_, other.p_counter_ );
	}

private:
	IAV* p_counter_;   //!< pointer to integral type atomic variable
};

template <typename T>
struct heap_element {
	using value_type = T;

	union {
		value_type v_;
		uint8_t    dummy_;
	};

	~heap_element()
	{
	}

	constexpr heap_element( void )
	  : dummy_() {}
};

struct heap_element_mgrinfo {
	std::atomic<size_t>                rc_;                   //!< heap_element_mgrinfoを参照中を示すreference counter
	std::atomic<heap_element_mgrinfo*> ap_free_stack_next_;   //!< freeリスト用に使われるnextポインタ
	heap_element_mgrinfo*              p_retire_keep_next_;   //!< retire内で一時的に保持するリスト用のnextポインタ

	constexpr heap_element_mgrinfo( void )
	  : rc_( 0 )
	  , ap_free_stack_next_( nullptr )
	  , p_retire_keep_next_( nullptr )
	{
	}
};
}   // namespace itl

constexpr size_t ELEMNUM = 10000;

class typed_pool_bad_alloc : public std::bad_alloc {
public:
	const char* what( void ) const noexcept override
	{
		// 内部状態を調べて、より詳細なエラーメッセージを返すことができるようにする予定。
		return "typed_pool_heap: memory allocation failed";
	}
};

template <typename T>
struct typed_pool_heap {
	// Allocator要件対応用。状態を持たないので、propagate系の定義は不要=std::false_type相当。
	using value_type                             = T;
	using propagate_on_container_move_assignment = std::true_type;
	using size_type                              = size_t;
	using difference_type                        = ptrdiff_t;

	// 機能実装用
	static constexpr size_t NUM = ELEMNUM;

	~typed_pool_heap()                                 = default;
	typed_pool_heap( void ) noexcept                   = default;
	typed_pool_heap( const typed_pool_heap& ) noexcept = default;
	// typed_pool_heap( typed_pool_heap&& ) noexcept        = default;
	typed_pool_heap& operator=( const typed_pool_heap& ) = default;
	// typed_pool_heap& operator=( typed_pool_heap&& )      = default;

	template <class U>
	typed_pool_heap( const typed_pool_heap<U>& ) noexcept
	{
	}

	value_type* allocate( size_type n )
	{
		if ( n != 1 ) {
			throw typed_pool_bad_alloc();   // TODO: typed_pool_heap2は1要素ずつしか割り当てできないので、とりあえず、例外を投げる。
		}
		auto el_info = try_pop_from_free();
		if ( el_info.first == nullptr ) {
			throw typed_pool_bad_alloc();
		}
		return &( array_heap_[el_info.second].v_ );
	}

	void deallocate( value_type* p, size_type n )
	{
		if ( n != 1 ) {
			return;   // TODO: typed_pool_heap2は1要素ずつしか解放できないので、とりあえず、何もしない。
		}
		auto idx = value_pointer_to_index( p );
		retire( &( array_mgr_info_[idx] ) );
	}

	static size_t get_watermark( void )
	{
		return watermark_of_array_.load();
	}

	static void debug_destruction_and_regeneration( void );

private:
	using index_t         = size_t;
	using element_type    = itl::heap_element<T>;
	using mgr_info_type   = itl::heap_element_mgrinfo;
	using counter_guard_t = itl::counter_guard<std::atomic<size_t>>;

	static index_t value_pointer_to_index( value_type* p_value )
	{
		uintptr_t addr_p_value = reinterpret_cast<uintptr_t>( p_value );
		uintptr_t addr_top     = reinterpret_cast<uintptr_t>( &( array_heap_[0] ) );
		if ( addr_p_value < addr_top ) {
			throw std::logic_error( "argument p_value does not belong to array_heap_" );
		}

		uintptr_t addr_diff = addr_p_value - addr_top;
		index_t   ans       = static_cast<index_t>( addr_diff / sizeof( array_heap_[0] ) );
		if ( ans >= NUM ) {
			throw std::logic_error( "argument p_value does not belong to array_heap_" );
		}
		return ans;
	}

	static index_t mgr_info_pointer_to_index( mgr_info_type* p_mgrinfo )
	{
		if ( p_mgrinfo < &( array_mgr_info_[0] ) ) {
			throw std::logic_error( "argument p_mgrinfo does not belong to array_mgr_info_" );
		}

		index_t ans = static_cast<index_t>( p_mgrinfo - &( array_mgr_info_[0] ) );
		if ( ans >= NUM ) {
			throw std::logic_error( "argument p_mgrinfo does not belong to array_mgr_info_" );
		}
		return ans;
	}

	class retired_fifo_list {
	public:
		constexpr retired_fifo_list( void )
		  : p_head_( nullptr )
		  , p_tail_( nullptr )
		{
		}

		void push_back( mgr_info_type* p_mgr ) noexcept
		{
			if ( p_tail_ == nullptr ) {
				p_head_ = p_tail_          = p_mgr;
				p_mgr->p_retire_keep_next_ = nullptr;
			} else {
				p_mgr->p_retire_keep_next_   = nullptr;
				p_tail_->p_retire_keep_next_ = p_mgr;
				p_tail_                      = p_mgr;
			}
		}
		void push_front( mgr_info_type* p_mgr ) noexcept
		{
			p_mgr->p_retire_keep_next_ = p_head_;
			p_head_                    = p_mgr;
			if ( p_tail_ == nullptr ) {
				p_tail_ = p_mgr;
			}
		}

		// リストの先頭の要素のリファレンスカウンタの値をチェックして、ゼロであれば取り出す。
		// 戻り値のリファレンスカウンタ値はゼロのままで返す。
		mgr_info_type* check_and_pop( void ) noexcept
		{
			mgr_info_type* p_poped = pop();
			if ( p_poped == nullptr ) {
				return nullptr;
			}

			if ( p_poped->rc_.load( /* std::memory_order_acquire */ ) != 0 ) {
				// まだ参照しているスレッドがいるので、取り出せない。再度すぐにチェックするのは無駄なので、FIFOキューの後ろに回す。
				push_back( p_poped );
				return nullptr;
			}

			return p_poped;
		}

		void clear( void ) noexcept
		{
			p_head_ = p_tail_ = nullptr;   // Clear the list
		}

		void merge( retired_fifo_list& other ) noexcept
		{
			if ( this == &other ) {
				return;   // nothing to merge with itself
			}
			if ( other.p_head_ == nullptr ) {
				return;   // nothing to merge
			}
			if ( p_tail_ == nullptr ) {
				p_head_ = other.p_head_;
				p_tail_ = other.p_tail_;
			} else {
				p_tail_->p_retire_keep_next_ = other.p_head_;
				p_tail_                      = other.p_tail_;
			}
			other.clear();   // Clear the other list after merging
		}

	private:
		mgr_info_type* pop( void )
		{
			mgr_info_type* p_mgr = p_head_;
			if ( p_mgr == nullptr ) {
				return nullptr;
			}
			p_head_ = p_mgr->p_retire_keep_next_;
			if ( p_head_ == nullptr ) {
				p_tail_ = nullptr;   // list is now empty
			}
			return p_mgr;
		}

		mgr_info_type* p_head_;   //!< pointer to head of retired list
		mgr_info_type* p_tail_;   //!< pointer to tail of retired list
	};

	class mutex_retired_fifo_list {
	public:
		~mutex_retired_fifo_list( void )          = default;
		constexpr mutex_retired_fifo_list( void ) = default;

		void push_back( mgr_info_type* p_mgr ) noexcept
		{
			std::lock_guard<std::mutex> lock( mtx_ );
			retired_list_.push_back( p_mgr );
		}
		void push_front( mgr_info_type* p_mgr ) noexcept
		{
			std::lock_guard<std::mutex> lock( mtx_ );
			retired_list_.push_front( p_mgr );
		}

		// ロックが確保できることを確認してから、
		// リストの先頭の要素のリファレンスカウンタの値をチェックして、ゼロであれば取り出す。
		// 戻り値のリファレンスカウンタ値はゼロのままで返す。
		mgr_info_type* try_check_and_pop( void ) noexcept
		{
			std::unique_lock<std::mutex> lock( mtx_, std::try_to_lock );
			if ( !lock.owns_lock() ) {
				return nullptr;
			}
			return retired_list_.check_and_pop();
		}

		void clear( void ) noexcept
		{
			std::lock_guard<std::mutex> lock( mtx_ );
			retired_list_.clear();
		}

		void merge( retired_fifo_list& other ) noexcept
		{
			std::lock_guard<std::mutex> lock( mtx_ );
			retired_list_.merge( other );
		}

	private:
		std::mutex        mtx_;
		retired_fifo_list retired_list_;
	};

	class thread_local_retired_fifo_list_cleaner {
	public:
		thread_local_retired_fifo_list_cleaner( void ) = default;

		~thread_local_retired_fifo_list_cleaner( void )
		{
			primary_retired_mgrinfo_list_.merge( retired_mgrinfo_list_ );   // Merge the thread-local retired elements list into the primary list
		}
	};

	static void push_to_free_stack( mgr_info_type* p_mgrinfo )
	{
		mgr_info_type* p_pre_free_list_head = ap_free_mgrinfo_head_.load();
		do {
#ifdef TEST_ENABLE_LOGICCHECKER
			if ( p_pre_free_list_head == p_mgrinfo ) {
				// 構造がリープしてしまっていることを示す。
				throw std::logic_error( "p_mgrinfo to ap_free_mgrinfo_head_ already, which is not expected." );
			}
#endif
			p_mgrinfo->ap_free_stack_next_.store( p_pre_free_list_head /* , std::memory_order_release */ );
		} while ( !ap_free_mgrinfo_head_.compare_exchange_strong( p_pre_free_list_head, p_mgrinfo /* , std::memory_order_acq_rel */ ) );
#ifdef TEST_ENABLE_LOGICCHECKER
		if ( p_mgrinfo == p_mgrinfo->ap_free_stack_next_.load() ) {
			// 構造がリープしてしまっていることを示す。
			throw std::logic_error( "ap_free_stack_next_ of p_mgrinfo is p_mgrinfo itself, which is not expected." );
		}
#endif
	}

	static mgr_info_type* try_pop_from_free_stack( void )
	{
		mgr_info_type*  p_ans = nullptr;
		counter_guard_t my_rc_g;
		while ( true ) {
			while ( true ) {
				p_ans = ap_free_mgrinfo_head_.load();
				if ( p_ans == nullptr ) return nullptr;   // free要素が枯渇していることを示しているため、nullptrをreturnする。

				// ここで、タスクスイッチして、p_ansがretireまで行ってしまう可能性がある。
				// そのため、reference countを獲得する。
				counter_guard_t tmp_rc_g( p_ans->rc_ );
				if ( ap_free_mgrinfo_head_.load() == p_ans ) {
					// カウンタ確保後も、p_ansが有効だったので、リファレンスカウンタの獲得は有効。
					my_rc_g.swap( tmp_rc_g );   // ループを抜けた後もp_ansを参照するため、リファレンスカウンタを保持しているガード変数をループの外の変数に移動してから、ループを抜ける
					break;
				}

				// p_ansが別の値に置き換わっていたので、最初からやり直す。
			}

			mgr_info_type* p_new_head = p_ans->ap_free_stack_next_.load();
#ifdef TEST_ENABLE_LOGICCHECKER
			if ( p_new_head == p_ans ) {
				// 構造がリープしてしまっていることを示す。
				throw std::logic_error( "p_ans->ap_free_stack_next_ points to itself, which is not expected." );
			}
#endif
			if ( ap_free_mgrinfo_head_.compare_exchange_strong( p_ans, p_new_head ) ) {
				// スタック構造の先頭の置き換えに成功したので、先頭のノードの所有権の確保完了。ループを抜ける。
				break;
			}

			// p_ansが別の値に置き換わっていたので、最初からやり直す。
		}

		return p_ans;
	}

	// スレッドローカルretireリストから要素を取り出すことを試みる。
	static mgr_info_type* try_pop_from_tl_retired_list( void )
	{
		return retired_mgrinfo_list_.check_and_pop();
	}

	static mgr_info_type* try_pop_from_primary_retired_list( void )
	{
		return primary_retired_mgrinfo_list_.try_check_and_pop();
	}

	static std::pair<mgr_info_type*, index_t> try_pop_from_unallocated( void )
	{
		index_t idx = watermark_of_array_.fetch_add( 1 );
		if ( idx >= NUM ) {
			// free要素が枯渇していることを示しているため、nullptrをreturnする。
			// ただし、watermark_of_array_がオーバーシュートしてしまっているので、補正してからreturnする。
			// この処理の効果によって、オーバーシュートはNUM+CPUコア数までに抑えられる。
			watermark_of_array_.exchange( NUM );
			return std::pair<mgr_info_type*, index_t>( nullptr, idx );
		}
		return std::pair<mgr_info_type*, index_t>( &( array_mgr_info_[idx] ), idx );
	}

	static std::pair<mgr_info_type*, index_t> try_pop_from_free( void )
	{
		mgr_info_type* p_ans = try_pop_from_tl_retired_list();
		if ( p_ans != nullptr ) {
			return std::pair<mgr_info_type*, index_t>( p_ans, mgr_info_pointer_to_index( p_ans ) );
		}

		p_ans = try_pop_from_free_stack();
		if ( p_ans != nullptr ) {
			return std::pair<mgr_info_type*, index_t>( p_ans, mgr_info_pointer_to_index( p_ans ) );
		}

		p_ans = try_pop_from_primary_retired_list();
		if ( p_ans != nullptr ) {
			return std::pair<mgr_info_type*, index_t>( p_ans, mgr_info_pointer_to_index( p_ans ) );
		}

		// freeリストが枯渇しているので、array_mgr_info_の未使用エリアから取得する。
		return try_pop_from_unallocated();
	}

	static void retire( mgr_info_type* p_mgrinfo )
	{
		if ( p_mgrinfo == nullptr ) {
			return;
		}

		// スレッドローカルretireリストにたまりすぎないように、１つだけ、free stack側への移動を試みる。
		// そのたと、スレッドローカルretireリストにpushする。
		{
			auto p_confirmed_free = try_pop_from_tl_retired_list();
			if ( p_confirmed_free != nullptr ) {
				// freeスタックにpushする
				push_to_free_stack( p_confirmed_free );
			}
		}

		{
			if ( p_mgrinfo->rc_.load( /* std::memory_order_acquire */ ) == 0 ) {
				// 参照カウンタがすでにゼロに到着しているので、freeスタックにpushする
				push_to_free_stack( p_mgrinfo );
			} else {
				// 参照カウンタがまだゼロに到着していないので、スレッドローカルretireリストにpushする。
				retired_mgrinfo_list_.push_back( p_mgrinfo );
			}
		}
		return;
	}

	static std::array<mgr_info_type, NUM>                      array_mgr_info_;                 //!< reference counter for each element in array_heap_
	static std::array<element_type, NUM>                       array_heap_;                     //!< array_heap_ for each element
	static std::atomic<index_t>                                watermark_of_array_;             //<! watermark of array_heap_ for each element 初期化時にリスト構築を不要にする役割も担う。
	static std::atomic<mgr_info_type*>                         ap_free_mgrinfo_head_;           //!< free list head pointer
	static mutex_retired_fifo_list                             primary_retired_mgrinfo_list_;   //!< primary retired mgr_info_type elements
	static thread_local retired_fifo_list                      retired_mgrinfo_list_;           //!< thread-local variable to hold retired mgr_info_type elements
	static thread_local thread_local_retired_fifo_list_cleaner tl_retired_fifo_list_cleaner_;   //!< thread-local cleaner for retired elements list
};

template <typename T>
constinit std::array<typename typed_pool_heap<T>::mgr_info_type, typed_pool_heap<T>::NUM> typed_pool_heap<T>::array_mgr_info_;   //!< reference counter for each element in array_heap_
template <typename T>
constinit std::array<typename typed_pool_heap<T>::element_type, typed_pool_heap<T>::NUM> typed_pool_heap<T>::array_heap_;   //!< array_heap_ for each element
template <typename T>
constinit std::atomic<typename typed_pool_heap<T>::index_t> typed_pool_heap<T>::watermark_of_array_;   //<! watermark of array_heap_ for each element 初期化時にリスト構築を不要にする役割も担う。
template <typename T>
constinit std::atomic<typename typed_pool_heap<T>::mgr_info_type*> typed_pool_heap<T>::ap_free_mgrinfo_head_;   //!< free list head pointer
template <typename T>
constinit typed_pool_heap<T>::mutex_retired_fifo_list typed_pool_heap<T>::primary_retired_mgrinfo_list_;   //!< primary retired mgr_info_type elements
template <typename T>
constinit thread_local typed_pool_heap<T>::retired_fifo_list typed_pool_heap<T>::retired_mgrinfo_list_;   //!< thread-local variable to hold retired mgr_info_type elements
template <typename T>
constinit thread_local typed_pool_heap<T>::thread_local_retired_fifo_list_cleaner typed_pool_heap<T>::tl_retired_fifo_list_cleaner_;   //!< thread-local cleaner for retired elements list

template <typename T>
void typed_pool_heap<T>::debug_destruction_and_regeneration( void )
{
	array_mgr_info_.~array();
	new ( &array_mgr_info_ ) std::array<typename typed_pool_heap<T>::mgr_info_type, typed_pool_heap<T>::NUM>();
	array_heap_.~array();
	new ( &array_heap_ ) std::array<typename typed_pool_heap<T>::element_type, typed_pool_heap<T>::NUM>();
	watermark_of_array_.store( 0 /* , std::memory_order_release */ );
	ap_free_mgrinfo_head_.store( nullptr /* , std::memory_order_release */ );
	primary_retired_mgrinfo_list_.clear();
	retired_mgrinfo_list_.clear();
}

template <typename T>
struct deleter_via_typed_pool_heap {
	void operator()( T* p ) noexcept
	{
		if ( p == nullptr ) {
			return;
		}

		using allocator_type        = typed_pool_heap<T>;
		using allocator_traits_type = std::allocator_traits<allocator_type>;

		allocator_type tmp_alloc;
		allocator_traits_type::destroy( tmp_alloc, p );
		allocator_traits_type::deallocate( tmp_alloc, p, 1 );
	}
};

}   // namespace lfheap

#endif   // LF_TYPED_HEAP_HPP_
