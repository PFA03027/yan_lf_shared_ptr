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

	heap_element( void )
	  : ap_next_ { nullptr }
	  , p_retire_keep_next_ { nullptr }
	  , rc_()
	  , dummy_() {}

	template <typename U = T, typename std::enable_if<std::is_copy_constructible<U>::value>::type* = nullptr>
	value_type& store( const value_type& v_arg )
	{
		return *( new ( &v_ ) value_type( v_arg ) );
	}

	template <typename U = T, typename std::enable_if<std::is_move_constructible<U>::value>::type* = nullptr>
	value_type& store( value_type&& v_arg )
	{
		return *( new ( &v_ ) value_type( std::move( v_arg ) ) );
	}

	template <typename... Args, typename std::enable_if<std::is_constructible<value_type, Args&&...>::value>::type* = nullptr>
	value_type& emplace( Args&&... args )
	{
		return *( new ( &v_ ) value_type( std::forward<Args>( args )... ) );
	}

	value_type load( void )
	{
		if constexpr ( std::is_nothrow_move_constructible<value_type>::value ) {
			value_type ans = std::move( v_ );
			if constexpr ( !std::is_trivially_destructible<value_type>::value ) {
				v_.value_type::~value_type();
			}
			return ans;
		} else {
			value_type ans = v_;
			if constexpr ( !std::is_trivially_destructible<value_type>::value ) {
				v_.value_type::~value_type();
			}
			return ans;
		}
	}
};

}   // namespace itl

template <typename T>
struct queue_element_heap {
	using element_type          = itl::heap_element<T>;
	static constexpr size_t NUM = 100;

	template <typename U = T, typename std::enable_if<std::is_default_constructible<U>::value>::type* = nullptr>
	static element_type* allocate( void )
	{
		element_type* p_ans = try_pop_from_free_list();
		p_ans->store( T {} );
		return p_ans;
	}

	template <typename U = T, typename std::enable_if<std::is_copy_constructible<U>::value>::type* = nullptr>
	static element_type* allocate( const T& v_arg )
	{
		element_type* p_ans = try_pop_from_free_list();
		if ( p_ans == nullptr ) {
			return p_ans;   // free要素が枯渇していることを示しているため、nullptrをreturnする。
		}
		p_ans->store( v_arg );
		return p_ans;
	}

	template <typename U = T, typename std::enable_if<std::is_move_constructible<U>::value>::type* = nullptr>
	static element_type* allocate( T&& v_arg )
	{
		element_type* p_ans = try_pop_from_free_list();
		if ( p_ans == nullptr ) {
			return p_ans;   // free要素が枯渇していることを示しているため、nullptrをreturnする。
		}
		p_ans->store( std::move( v_arg ) );
		return p_ans;
	}

	template <typename... Args, typename std::enable_if<std::is_constructible<T, Args&&...>::value>::type* = nullptr>
	static element_type* emplace( Args&&... args )
	{
		element_type* p_ans = try_pop_from_free_list();
		if ( p_ans == nullptr ) {
			return p_ans;   // free要素が枯渇していることを示しているため、nullptrをreturnする。
		}
		p_ans->emplace( std::forward<Args>( args )... );
		return p_ans;
	}

	static void retire( element_type* p_elem )
	{
		if ( p_elem == nullptr ) {
			return;
		}
		if ( p_elem->rc_.read() != 0 ) {
			// rcがゼロになるまで待っても良いが、とりあえず、例外をスローする実装を行う。
			throw std::logic_error( "queue_element_heap::retire() is called, but reference count is not zero." );
		}

		if ( auto [p_confirmed_free, idx] = try_pop_from_retired_list(); p_confirmed_free != nullptr ) {
			// freeリストにpushする
			push_to_free_list( p_confirmed_free );
			array_rc_[idx].recycle();
		}

		{
			size_t idx = elem_pointer_to_index( p_elem );
			if ( array_rc_[idx].is_sticky_zero() ) {
				// 参照カウンタがすでにゼロに到着しているので、freeリストに戻す
				push_to_free_list( p_elem );
				array_rc_[idx].recycle();
			} else {
				// 参照カウンタがまだゼロに到着していないので、スレッドローカルretireリストにpushする。
				p_elem->p_retire_keep_next_ = p_retired_elem_head_;
				p_retired_elem_head_        = p_elem;
			}
		}
	}

private:
	static element_type* try_pop_from_free_list( void )
	{
		auto [p_ans, idx] = try_pop_from_retired_list();
		if ( p_ans != nullptr ) {
			// freeリストにpushする
			array_rc_[idx].recycle();
			return p_ans;
		}

		while ( true ) {
			counter_guard<sticky_counter> my_rc_g;
			while ( true ) {
				p_ans = ap_free_elem_head_.load();
				if ( p_ans == nullptr ) return p_ans;   // free要素が枯渇していることを示しているため、nullptrをreturnする。

				// ここで、タスクスイッチして、p_ansがretireまで行ってしまう可能性がある。
				// そのため、reference countを獲得する。
				size_t        idx = elem_pointer_to_index( p_ans );
				counter_guard tmp_rc_g( &( array_rc_[idx] ) );
				if ( !tmp_rc_g.owns_count() ) {
					continue;   // タスクスイッチして、p_ansがretireまで行っていたので、やり直す。
				}
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

	static void push_to_free_list( element_type* p_elem )
	{
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
		if ( !array_rc_[idx].is_sticky_zero() ) {
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

	static size_t elem_pointer_to_index( element_type* p_elem )
	{
		size_t ans = p_elem - &( array_heap_[0] );
		if ( ans >= NUM ) {
			// 本当はエラーログを出して無視すべきだが、いったん例外で実装する
			throw std::logic_error( "argument p_elem does not belong to array_heap_" );
		}
		return ans;
	}

	static std::array<sticky_counter, NUM>       array_rc_;
	static std::array<itl::heap_element<T>, NUM> array_heap_;
	static std::atomic<element_type*>            ap_free_elem_head_;
	static thread_local element_type*            p_retired_elem_head_;   // TODO: thread終了時のクリーニングは後で追加する
};

template <typename T>
constexpr std::array<itl::heap_element<T>, queue_element_heap<T>::NUM> initialize_array_heap( void )
{
	std::array<itl::heap_element<T>, queue_element_heap<T>::NUM> ans;
	for ( size_t i = 0; i < queue_element_heap<T>::NUM - 1; i++ ) {
		ans[i].ap_next_.store( &( ans[i + 1] ) );
	}
	// 最後の要素のap_next_はすでにnullptrが設定されている。

	return ans;
}

template <typename T>
constinit std::array<sticky_counter, queue_element_heap<T>::NUM> queue_element_heap<T>::array_rc_;
template <typename T>
constinit std::array<itl::heap_element<T>, queue_element_heap<T>::NUM> queue_element_heap<T>::array_heap_ = initialize_array_heap<T>();
template <typename T>
constinit std::atomic<typename queue_element_heap<T>::element_type*> queue_element_heap<T>::ap_free_elem_head_ { &( array_heap_[0] ) };
template <typename T>
constinit thread_local queue_element_heap<T>::element_type* queue_element_heap<T>::p_retired_elem_head_ { nullptr };

}   // namespace rc

#endif

#if 0
int main( void )
{
	auto p_sut = new sticky_counter;
	auto ret   = p_sut->increment_if_not_zero();
	std::cout << std::boolalpha << ret << std::endl;
	std::cout << p_sut->read() << std::endl;
	ret = p_sut->decrement_then_is_zero();
	std::cout << std::boolalpha << ret << std::endl;
	std::cout << p_sut->read() << std::endl;
	std::cout << std::boolalpha << p_sut->is_sticky_zero() << std::endl;
	delete p_sut;

	queue_element<int> sut_qe;

	return 0;
}
#endif
