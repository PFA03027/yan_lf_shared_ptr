/**
 * @file limited_lf_arrayheap.hpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief
 * @version 0.1
 * @date 2025-07-19
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#ifndef LIMITED_LF_ARRAYHEAP_TYPE_T_HPP_
#define LIMITED_LF_ARRAYHEAP_TYPE_T_HPP_

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "rc_sticky_counter.hpp"

namespace rc {
namespace itl {

/**
 * @brief heap element for limited_arrayheap
 *
 * @tparam T value type
 *
 * @note
 * This heap element is used in limited_arrayheap.
 * It has a reference counter to manage the lifetime of the value.
 */
template <typename T>
struct heap_element {
	using value_type = T;

	std::atomic<heap_element*> ap_next_;              //!< freeリスト用に使われるnextポインタ
	heap_element*              p_retire_keep_next_;   //!< retire内で一時的に保持するリスト用のnextポインタ

	int debug_info_;   //!< dummy member to ensure the size of heap_element is same as value_type

	sticky_counter rc_;   //!< v_の寿命管理用reference counter
	union {
		value_type v_;
		uint8_t    dummy_;
	};

	~heap_element()
	{
	}

	constexpr heap_element( void )
	  : ap_next_ { nullptr }
	  , p_retire_keep_next_ { nullptr }
	  , debug_info_( 0 )
	  , rc_()
	  , dummy_() {}

	template <typename U = T, typename std::enable_if<std::is_copy_constructible<U>::value>::type* = nullptr>
	value_type& store( const value_type& v_arg ) noexcept( std::is_nothrow_copy_constructible<T>::value )
	{
		return *( new ( &v_ ) value_type( v_arg ) );
	}

	template <typename U = T, typename std::enable_if<std::is_move_constructible<U>::value>::type* = nullptr>
	value_type& store( value_type&& v_arg ) noexcept( std::is_nothrow_move_constructible<T>::value )
	{
		return *( new ( &v_ ) value_type( std::move( v_arg ) ) );
	}

	template <typename... Args, typename std::enable_if<std::is_constructible<T, Args&&...>::value>::type* = nullptr>
	value_type& emplace( Args&&... args ) noexcept( std::is_nothrow_constructible<T>::value )
	{
		return *( new ( &v_ ) value_type( std::forward<Args>( args )... ) );
	}

	value_type& ref( void ) noexcept
	{
		return v_;
	}

	const value_type& ref( void ) const noexcept
	{
		return v_;
	}

	void destruct_value( void ) noexcept
	{
		if constexpr ( !std::is_trivially_destructible<value_type>::value ) {
			v_.value_type::~value_type();
		}
	}
};

}   // namespace itl

template <typename T>
struct limited_arrayheap {
	using element_type          = itl::heap_element<T>;
	static constexpr size_t NUM = 10000;

	static element_type* allocate( void )
	{
		return try_pop_from_free();
	}

	/**
	 * @brief retire element
	 *
	 * @pre p_elem->v_の値構築を行った場合は、destruct_value()で値を破棄していること。
	 *
	 * @param p_elem
	 */
	static void retire( element_type* p_elem )
	{
		if ( auto [p_confirmed_free, idx] = retired_elem_list_.check_and_pop(); p_confirmed_free != nullptr ) {
			// freeリストにpushする
			push_to_free_list( p_confirmed_free, idx );
		}

		if ( p_elem == nullptr ) {
			return;
		}
		if ( !p_elem->rc_.is_sticky_or_recycled_zero() ) {
			// rcがゼロになるまで待っても良いが、とりあえず、例外をスローする実装を行う。
			throw std::logic_error( "limited_arrayheap::retire() is called, but reference count is not zero." );
		}
		p_elem->rc_.recycle();   // recycle the sticky counter

		{
			size_t idx = elem_pointer_to_index( p_elem );
			if ( array_rc_[idx].load( /*std::memory_order_acquire*/ ) == 0 ) {
				// 参照カウンタがすでにゼロに到着しているので、freeリストに戻す
				push_to_free_list( p_elem, idx );
			} else {
				// 参照カウンタがまだゼロに到着していないので、スレッドローカルretireリストにpushする。
				retired_elem_list_.push( p_elem );
			}
		}
	}

	static size_t get_watermark( void )
	{
		return watermark_of_array_.load();
	}

	static void debug_destruction_and_regeneration( void );

private:
	class retired_fifo_list {
	public:
		constexpr retired_fifo_list( void )
		  : p_head_( nullptr )
		  , p_tail_( nullptr )
		{
		}

		void push( element_type* p_elem ) noexcept
		{
			if ( p_tail_ == nullptr ) {
				p_head_ = p_tail_           = p_elem;
				p_elem->p_retire_keep_next_ = nullptr;
			} else {
				p_elem->p_retire_keep_next_  = nullptr;
				p_tail_->p_retire_keep_next_ = p_elem;
				p_tail_                      = p_elem;
			}
		}

		std::pair<element_type*, size_t> check_and_pop( void ) noexcept
		{
			if ( p_head_ == nullptr ) {
				return std::pair<element_type*, size_t>( nullptr, 0 );
			}

			size_t idx = elem_pointer_to_index( p_head_ );
			if ( array_rc_[idx].load( /* std::memory_order_acquire */ ) != 0 ) {
				// まだ参照しているスレッドがいるので、取り出せない。
				return std::pair<element_type*, size_t>( nullptr, 0 );
			}

			element_type* p_elem = p_head_;
			p_head_              = p_elem->p_retire_keep_next_;
			if ( p_head_ == nullptr ) {
				p_tail_ = nullptr;   // list is now empty
			}
			return std::pair<element_type*, size_t>( p_elem, idx );
		}

		void clear( void ) noexcept
		{
			p_head_ = p_tail_ = nullptr;   // Clear the list
		}

	private:
		element_type* p_head_;   //!< pointer to head of retired list
		element_type* p_tail_;   //!< pointer to tail of retired list
	};

	static element_type* try_pop_from_free( void )
	{
		auto [p_ans, idx] = retired_elem_list_.check_and_pop();
		if ( p_ans != nullptr ) {
			p_ans->debug_info_ = 2;   // reset dummy member to ensure the size of heap_element is same as value_type
			return p_ans;
		}
		p_ans = try_pop_from_free_list();
		if ( p_ans != nullptr ) {
			p_ans->debug_info_ = 1;   // reset dummy member to ensure the size of heap_element is same as value_type
			return p_ans;
		}

		// freeリストが枯渇しているので、array_heap_の未使用エリアから取得する。
		p_ans = try_pop_from_unallocated();
		return p_ans;
	}

	static element_type* try_pop_from_free_list( void )
	{
		element_type* p_ans = nullptr;
		while ( true ) {
			counter_guard<std::atomic<size_t>> my_rc_g;
			while ( true ) {
				p_ans = ap_free_elem_head_.load();
				if ( p_ans == nullptr ) return p_ans;   // free要素が枯渇していることを示しているため、nullptrをreturnする。

				// ここで、タスクスイッチして、p_ansがretireまで行ってしまう可能性がある。
				// そのため、reference countを獲得する。
				size_t        idx = elem_pointer_to_index( p_ans );
				counter_guard tmp_rc_g( array_rc_[idx] );
				if ( ap_free_elem_head_.load() == p_ans ) {
					// カウンタ確保後も、p_ansが有効だったので、リファレンスカウンタの獲得は有効。
					my_rc_g.swap( tmp_rc_g );   // ループを抜けた後もp_ansを参照するため、リファレンスカウンタを保持しているガード変数をループの外の変数に移動してから、ループを抜ける
					break;
				}

				// p_ansが別の値に置き換わっていたので、最初からやり直す。
			}

			element_type* p_new_head = p_ans->ap_next_.load();
#ifdef TEST_ENABLE_LOGICCHECKER
			if ( p_new_head == p_ans ) {
				// 構造がリープしてしまっていることを示す。
				throw std::logic_error( "p_ans->ap_next_ points to itself, which is not expected." );
			}
#endif
			if ( ap_free_elem_head_.compare_exchange_strong( p_ans, p_new_head ) ) {
				// スタック構造の先頭の置き換えに成功したので、先頭のノードの所有権の確保完了。ループを抜ける。
				break;
			}

			// p_ansが別の値に置き換わっていたので、最初からやり直す。
		}

		return p_ans;
	}

	static void push_to_free_list( element_type* p_elem, size_t idx )
	{
		element_type* p_pre_free_list_head = ap_free_elem_head_.load();
		do {
#ifdef TEST_ENABLE_LOGICCHECKER
			if ( p_pre_free_list_head == p_elem ) {
				// 構造がリープしてしまっていることを示す。
				throw std::logic_error( "p_elem to ap_free_elem_head_ already, which is not expected." );
			}
#endif
			p_elem->ap_next_.store( p_pre_free_list_head /* , std::memory_order_release */ );
		} while ( !ap_free_elem_head_.compare_exchange_strong( p_pre_free_list_head, p_elem /* , std::memory_order_acq_rel */ ) );
#ifdef TEST_ENABLE_LOGICCHECKER
		if ( p_elem == p_elem->ap_next_.load() ) {
			// 構造がリープしてしまっていることを示す。
			throw std::logic_error( "ap_next_ of p_elem is p_elem itself, which is not expected." );
		}
#endif
	}

	static element_type* try_pop_from_unallocated( void )
	{
		size_t idx = watermark_of_array_.fetch_add( 1 );
		if ( idx >= NUM ) {
			// free要素が枯渇していることを示しているため、nullptrをreturnする。
			// ただし、watermark_of_array_がオーバーシュートしてしまっているので、補正してからreturnする。
			// この処理の効果によって、オーバーシュートはNUM+CPUコア数までに抑えられる。
			watermark_of_array_.exchange( NUM );
			return nullptr;
		}
		return &( array_heap_[idx] );
	}

	static size_t elem_pointer_to_index( element_type* p_elem )
	{
		if ( p_elem < &( array_heap_[0] ) ) {
			throw std::logic_error( "argument p_elem does not belong to array_heap_" );
		}

		size_t ans = static_cast<size_t>( p_elem - &( array_heap_[0] ) );
		if ( ans >= NUM ) {
			throw std::logic_error( "argument p_elem does not belong to array_heap_" );
		}
		return ans;
	}

	static std::array<std::atomic<size_t>, NUM>  array_rc_;             //!< reference counter for each element in array_heap_
	static std::array<itl::heap_element<T>, NUM> array_heap_;           //!< array_heap_ for each element
	static std::atomic<size_t>                   watermark_of_array_;   //<! watermark of array_heap_ for each element 初期化時にリスト構築を不要にする役割も担う。
	static std::atomic<element_type*>            ap_free_elem_head_;    //!< free list head pointer
	static thread_local retired_fifo_list        retired_elem_list_;    //!< thread-local variable to hold retired elements TODO: thread終了時のクリーニングは後で追加する
};

template <typename T>
constinit std::array<std::atomic<size_t>, limited_arrayheap<T>::NUM> limited_arrayheap<T>::array_rc_;
template <typename T>
constinit std::array<itl::heap_element<T>, limited_arrayheap<T>::NUM> limited_arrayheap<T>::array_heap_;
template <typename T>
std::atomic<size_t> limited_arrayheap<T>::watermark_of_array_ { 0 };   //<! watermark of array_heap_ for each element
template <typename T>
constinit std::atomic<typename limited_arrayheap<T>::element_type*> limited_arrayheap<T>::ap_free_elem_head_ { nullptr };
template <typename T>
constinit thread_local limited_arrayheap<T>::retired_fifo_list limited_arrayheap<T>::retired_elem_list_;

template <typename T>
void limited_arrayheap<T>::debug_destruction_and_regeneration( void )
{
	for ( size_t i = 0; i < NUM; i++ ) {
		array_rc_[i].store( 0 /* , std::memory_order_release */ );   // reset reference counter
	}
	array_heap_.~array();
	new ( &array_heap_ ) std::array<itl::heap_element<T>, NUM>();
	watermark_of_array_.store( 0 /* , std::memory_order_release */ );
	ap_free_elem_head_.store( nullptr /* , std::memory_order_release */ );
	retired_elem_list_.clear();
}

}   // namespace rc

#endif
