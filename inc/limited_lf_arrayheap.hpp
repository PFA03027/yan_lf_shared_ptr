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
		if ( auto [p_confirmed_free, idx] = try_pop_from_retired_list(); p_confirmed_free != nullptr ) {
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
			if ( array_rc_[idx].load( std::memory_order_acquire ) == 0 ) {
				// 参照カウンタがすでにゼロに到着しているので、freeリストに戻す
				push_to_free_list( p_elem, idx );
			} else {
				// 参照カウンタがまだゼロに到着していないので、スレッドローカルretireリストにpushする。
				p_elem->p_retire_keep_next_ = p_retired_elem_head_;
				p_retired_elem_head_        = p_elem;
			}
		}
	}

	static size_t get_watermark( void )
	{
		return watermark_of_array_.load();
	}

	static void debug_destruction_and_regeneration( void );

private:
	static element_type* try_pop_from_free( void )
	{
		auto [p_ans, idx] = try_pop_from_retired_list();
		if ( p_ans != nullptr ) {
			array_rc_[idx].store( 0, std::memory_order_release );   // reset reference counter
			return p_ans;
		}

		p_ans = try_pop_from_free_list();
		if ( p_ans != nullptr ) {
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
				// ここで、reference countの確保完了。

				// p_ansがまだ有効かどうかを検証する。
				// TODO: 先にフリーリストのヘッドを戻してから、reference count をrecycleするという順序なら、下のcompare_exchange_strong処理は不要のはず。
				if ( ap_free_elem_head_.compare_exchange_strong( p_ans, p_ans ) ) {
					// p_ansが有効であるこの検証完了
					my_rc_g.swap( tmp_rc_g );   // リファレンスカウンタを保持しているガード変数をループの外の変数に移動し、ループを抜ける
					break;
				}

				// p_ansが別の値に置き換わっていたので、最初からやり直す。
			}

			element_type* p_new_head = p_ans->ap_next_.load();
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
		array_rc_[idx].store( 0, std::memory_order_release );   // reset reference counter

		element_type* p_new_next = ap_free_elem_head_.load();
		do {
			p_elem->ap_next_.store( p_new_next, std::memory_order_release );
		} while ( !ap_free_elem_head_.compare_exchange_strong( p_new_next, p_elem, std::memory_order_acq_rel ) );
	}

	static std::pair<element_type*, size_t> try_pop_from_retired_list( void )
	{
		// スレッドローカルretireリストをチェックする
		if ( p_retired_elem_head_ == nullptr ) {
			return std::pair<element_type*, size_t>( nullptr, 0 );
		}

		size_t idx = elem_pointer_to_index( p_retired_elem_head_ );
		if ( array_rc_[idx].load( std::memory_order_acquire ) != 0 ) {
			// まだ参照しているスレッドがいるので、取り出せない。
			return std::pair<element_type*, size_t>( nullptr, 0 );
		}

		// 参照カウンタがゼロに到着しているので、retiredリストから取り出す。
		element_type* p_confirmed_free = p_retired_elem_head_;
		element_type* p_retired_next   = p_retired_elem_head_->p_retire_keep_next_;
		p_retired_elem_head_           = p_retired_next;

		// nextをクリア
		p_confirmed_free->p_retire_keep_next_ = nullptr;
		return std::pair<element_type*, size_t>( p_confirmed_free, idx );
	}

	static element_type* try_pop_from_unallocated( void )
	{
		size_t idx = watermark_of_array_.fetch_add( 1, std::memory_order_acq_rel );
		if ( idx >= NUM ) {
			// free要素が枯渇していることを示しているため、nullptrをreturnする。
			// ただし、watermark_of_array_がオーバーシュートしてしまっているので、補正してからreturnする。
			// この処理の効果によって、オーバーシュートはNUM+CPUコア数までに抑えられる。
			watermark_of_array_.exchange( NUM, std::memory_order_release );
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

	static std::array<std::atomic<size_t>, NUM>  array_rc_;              //!< reference counter for each element in array_heap_
	static std::array<itl::heap_element<T>, NUM> array_heap_;            //!< array_heap_ for each element
	static std::atomic<size_t>                   watermark_of_array_;    //<! watermark of array_heap_ for each element 初期化時にリスト構築を不要にする役割も担う。
	static std::atomic<element_type*>            ap_free_elem_head_;     //!< free list head pointer
	static thread_local element_type*            p_retired_elem_head_;   //!< thread-local variable to hold retired elements TODO: thread終了時のクリーニングは後で追加する
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
constinit thread_local limited_arrayheap<T>::element_type* limited_arrayheap<T>::p_retired_elem_head_ { nullptr };

template <typename T>
void limited_arrayheap<T>::debug_destruction_and_regeneration( void )
{
	for ( size_t i = 0; i < NUM; i++ ) {
		array_rc_[i].store( 0, std::memory_order_release );   // reset reference counter
	}
	array_heap_.~array();
	new ( &array_heap_ ) std::array<itl::heap_element<T>, NUM>();
	watermark_of_array_.store( 0, std::memory_order_release );
	ap_free_elem_head_.store( nullptr, std::memory_order_release );
	p_retired_elem_head_ = nullptr;
}

}   // namespace rc

#endif
