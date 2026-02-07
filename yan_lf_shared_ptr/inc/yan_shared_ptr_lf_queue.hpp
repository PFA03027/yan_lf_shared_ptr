/**
 * @file shared_ptr_lf_queue.hpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief
 * @version 0.1
 * @date 2025-07-27
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#ifndef LIMITED_LF_SHARED_PTR_QUEUE_HPP_
#define LIMITED_LF_SHARED_PTR_QUEUE_HPP_

#include <atomic>
#include <optional>
#include <type_traits>

#include "rc_sticky_counter.hpp"
#include "typed_lfheap.hpp"
#include "yan_lf_shared_ptr.hpp"

namespace yan {   // yet another
namespace itl {

template <typename T>
struct queue_node {
	static_assert( std::is_trivially_copyable<T>::value, "T should be trivially copyable" );
	static_assert( std::is_default_constructible<T>::value, "T should be default constructible" );
	// static_assert( std::is_pointer<T>::value, "T should be pointer" );

	std::atomic<void*> ap_next_;   // リファレンスカウンタが利用可能なヒープ要素へのポインタを格納したいが、不完全型になってしまうので、void*で定義する。
	T                  v_;

	queue_node( std::nullptr_t, T v_arg )
	  : ap_next_( nullptr )
	  , v_( v_arg )
	{
	}
	queue_node( std::nullptr_t )
	  : ap_next_( nullptr )
	  , v_()
	{
	}
};

}   // namespace itl

template <typename T>
class shared_ptr_lf_queue {
	// static_assert( std::is_trivially_copyable<T>::value, "T should be trivially copyable" );
	// static_assert( std::is_default_constructible<T>::value, "T should be default constructible" );

public:
	using shared_ptr_type = yan::lf_shared_ptr<T>;

	using que_contents_heap_type          = lfheap::typed_pool_heap<shared_ptr_type>;
	using que_contents_heap_element_type  = typename que_contents_heap_type::element_type;
	using que_contents_heap_element_ptr_t = que_contents_heap_element_type*;

	using que_node_type                    = itl::queue_node<que_contents_heap_element_ptr_t>;
	using que_node_heap_type               = lfheap::typed_pool_heap<que_node_type>;
	using que_node_heap_element_type       = typename que_node_heap_type::element_type;
	using que_node_heap_element_ptr_t      = que_node_heap_element_type*;
	using que_node_heap_element_rc_guard_t = typename que_node_heap_type::counter_guard_t;

	~shared_ptr_lf_queue()
	{
		que_node_heap_element_ptr_t p_cur_node = ap_que_head_.exchange( nullptr );
		if ( p_cur_node == nullptr ) {
			// ap_que_head_がnullptrであることは不具合だが、他にどうしようもないので、そのまま終了する。
			return;
		}

		// 先頭の番兵ノードだけ、特別扱いする。
		// 番兵ノードが保持する値はすでに読み出されているので、関連するリソース解放の必要はないため。
		que_node_heap_element_ptr_t p_nxt_node = load_queue_node_next( p_cur_node );
		p_cur_node->destruct_value();
		que_node_heap_type::retire( p_cur_node );

		if ( p_cur_node == ap_que_tail_.load() ) {
			// 番兵ノードだけが残っていた。番兵ノードの開放は終了しているので、処理を終了。
			return;
		}

		// 順次開放する。
		p_cur_node = p_nxt_node;
		while ( p_cur_node != nullptr ) {
			p_nxt_node = load_queue_node_next( p_cur_node );

			// lf_shared_ptrをdestructして、pointerをヒープに返す。
			que_contents_heap_element_ptr_t p_cnt_elem = p_cur_node->ref().v_;
			p_cnt_elem->destruct_value();
			que_contents_heap_type::retire( p_cnt_elem );

			// queue_nodeをdestructして、pointerをヒープに返す
			p_cur_node->destruct_value();
			que_node_heap_type::retire( p_cur_node );

			p_cur_node = p_nxt_node;
		}
	}
	shared_ptr_lf_queue( void )
	  : ap_que_head_( que_node_heap_type::allocate() )
	  , ap_que_tail_( ap_que_head_.load() )
	{
		que_node_heap_element_ptr_t p_cur_sentinel_node = ap_que_head_.load();
		if ( p_cur_sentinel_node == nullptr ) {
			throw std::bad_alloc();
		}

		// 番兵ノードを準備する
		p_cur_sentinel_node->emplace( nullptr );
	}
	shared_ptr_lf_queue( const shared_ptr_lf_queue& )            = delete;
	shared_ptr_lf_queue& operator=( const shared_ptr_lf_queue& ) = delete;
	shared_ptr_lf_queue( shared_ptr_lf_queue&& )                 = delete;
	shared_ptr_lf_queue& operator=( shared_ptr_lf_queue&& )      = delete;

	std::optional<shared_ptr_type> push( const shared_ptr_type& push_sp_arg )
	{
		que_node_heap_element_ptr_t p_pushed_new_tail = que_node_heap_type::allocate();
		if ( p_pushed_new_tail == nullptr ) {
			return push_sp_arg;   // queue is full
		}
		que_contents_heap_element_ptr_t p_new_cnt = que_contents_heap_type::allocate();
		if ( p_new_cnt == nullptr ) {
			que_node_heap_type::retire( p_pushed_new_tail );
			throw std::bad_alloc();   // ヒープを他と共有しているかもしれないので、メモリ不足とする。
		}
		p_new_cnt->emplace( push_sp_arg );

		push_impl( p_new_cnt, p_pushed_new_tail );
		return std::nullopt;
	}
	std::optional<shared_ptr_type> push( shared_ptr_type&& push_sp_arg )
	{
		que_node_heap_element_ptr_t p_pushed_new_tail = que_node_heap_type::allocate();
		if ( p_pushed_new_tail == nullptr ) {
			return std::move( push_sp_arg );   // queue is full
		}
		que_contents_heap_element_ptr_t p_new_cnt = que_contents_heap_type::allocate();
		if ( p_new_cnt == nullptr ) {
			que_node_heap_type::retire( p_pushed_new_tail );
			throw std::bad_alloc();   //  ヒープを他と共有しているかもしれないので、メモリ不足とする。
		}
		p_new_cnt->emplace( std::move( push_sp_arg ) );

		push_impl( p_new_cnt, p_pushed_new_tail );
		return std::nullopt;
	}

	std::optional<shared_ptr_type> try_pop( void )
	{
		while ( true ) {
			que_node_heap_element_ptr_t      p_expect_head_node = ap_que_head_.load();
			que_node_heap_element_rc_guard_t expect_head_rc_g;
			while ( true ) {
				// ABA問題を回避するため、リファレンスカウンタの獲得がうまくいくまでループ。概念的にはハザードポインタ登録と同じことをやっている。
				expect_head_rc_g                            = que_node_heap_type::get_counter_guard( p_expect_head_node );
				que_node_heap_element_ptr_t p_chk_head_node = ap_que_head_.load();
				if ( p_expect_head_node == p_chk_head_node ) {
					break;
				}
				p_expect_head_node = p_chk_head_node;
			}

			que_node_heap_element_ptr_t      p_expect_tail_node = ap_que_tail_.load();
			que_node_heap_element_rc_guard_t expect_tail_rc_g;
			while ( true ) {
				// ABA問題を回避するため、リファレンスカウンタの獲得がうまくいくまでループ。概念的にはハザードポインタ登録と同じことをやっている。
				expect_tail_rc_g                            = que_node_heap_type::get_counter_guard( p_expect_tail_node );
				que_node_heap_element_ptr_t p_chk_tail_node = ap_que_tail_.load();
				if ( p_expect_tail_node == p_chk_tail_node ) {
					break;
				}
				p_expect_tail_node = p_chk_tail_node;
			}

			que_node_heap_element_ptr_t      p_expect_next_node = load_queue_node_next( p_expect_head_node );
			que_node_heap_element_rc_guard_t expect_next_rc_g;
			while ( p_expect_next_node != nullptr ) {
				// ABA問題を回避するため、リファレンスカウンタの獲得がうまくいくまでループ。概念的にはハザードポインタ登録と同じことをやっている。
				que_node_heap_element_rc_guard_t tmp_expect_next_rc_g = que_node_heap_type::get_counter_guard( p_expect_next_node );
				que_node_heap_element_ptr_t      p_chk_next_node      = load_queue_node_next( p_expect_head_node );
				if ( p_expect_next_node == p_chk_next_node ) {
					expect_next_rc_g = std::move( tmp_expect_next_rc_g );
					break;
				}
				p_expect_next_node = p_chk_next_node;
			}

			if ( p_expect_head_node == p_expect_tail_node ) {
				// queueが空かもしれないが、tailの更新が遅れているだけかもしれない
				if ( p_expect_next_node == nullptr ) {
					return std::nullopt;   // 本当に空っぽだったので、popを終了する。
				}
				ap_que_tail_.compare_exchange_strong( p_expect_tail_node, p_expect_next_node );
				// tailの更新を試みる。成否は気にない。そのあと、最初からやり直す。
			} else {
				if ( !ap_que_head_.compare_exchange_strong( p_expect_head_node, p_expect_next_node ) ) {
					// headの獲得に失敗したので、最初からやり直す。
					continue;
				}
				// headが獲得できたので、popできたポインタ情報を取り出す。
				que_contents_heap_element_ptr_t p_ans_ptr_candidate = p_expect_next_node->ref().v_;
				// もともとのアルゴリズムでは、このv_の読み出しは、ap_que_head_.compare_exchange_strong()前で行っている。
				// これは、ABA問題を避けるために先行読み出しを行う必要があったから、そのように実装されている。
				// しかし、一方でその実装だと、Thread Sanitizerがレースコンディションのエラーを指摘してくる。
				// Thread Sanitizerの指摘を避けるためには、ABA問題を避けつつ、ap_que_head_.compare_exchange_strong()の後に読み出す必要がある。
				// これを実現するには、p_expect_next_nodeに対してのABA問題を避けるために、ハザードポインタを用いる方法が基本である。
				// この実装では、ハザードポインタの代わりにリファレンスカウンタを用いるので、p_expect_next_nodeに対してリファレンスカウンタを
				// 適用し、p_expect_next_nodeに対してのABA問題を避ける方策を採った。

				shared_ptr_type ans = std::move( p_ans_ptr_candidate->ref() );
				// pop処理が完了したので、ヒープへ返却する。
				p_ans_ptr_candidate->destruct_value();
				que_contents_heap_type::retire( p_ans_ptr_candidate );
				p_expect_head_node->destruct_value();
				que_node_heap_type::retire( p_expect_head_node );
				return ans;
			}
		}
	}

private:
	void push_impl( que_contents_heap_element_ptr_t p_new_cnt, que_node_heap_element_ptr_t p_pushed_new_tail )
	{
		p_pushed_new_tail->emplace( nullptr, p_new_cnt );

		while ( true ) {
			que_node_heap_element_ptr_t      p_expect_tail_node = ap_que_tail_.load();
			que_node_heap_element_rc_guard_t expect_tail_rc_g;
			while ( true ) {
				expect_tail_rc_g                            = que_node_heap_type::get_counter_guard( p_expect_tail_node );
				que_node_heap_element_ptr_t p_chk_tail_node = ap_que_tail_.load();
				if ( p_expect_tail_node == p_chk_tail_node ) {
					break;
				}
				p_expect_tail_node = p_chk_tail_node;
			}

			que_node_heap_element_ptr_t p_next_node = load_queue_node_next( p_expect_tail_node );
			if ( p_next_node == nullptr ) {
				// 終端ノードのはず。
				if ( cas_queue_node_next( p_expect_tail_node, p_next_node, p_pushed_new_tail ) ) {
					// 終端ノードの後ろへの追加に成功。tailの更新を試みる。
					ap_que_tail_.compare_exchange_strong( p_expect_tail_node, p_pushed_new_tail );
					break;   // 更新処理完了、ループを抜ける。なお、更新に失敗しても、他のスレッドが頑張ってくれるから、気にしない。
				} else {
					// 終端ノードへの追加に失敗したので、最初からやり直す。
				}
			} else {
				// p_expect_tail_nodeが終端ノードを指していなかったので、tailを更新してから、最初からやり直す
				ap_que_tail_.compare_exchange_strong( p_expect_tail_node, p_next_node );
			}
		}

		return;
	}

	static inline que_node_heap_element_ptr_t load_queue_node_next( que_node_heap_element_ptr_t p )
	{
		return reinterpret_cast<que_node_heap_element_ptr_t>( p->ref().ap_next_.load() );
	}
	static inline bool cas_queue_node_next( que_node_heap_element_ptr_t p, que_node_heap_element_ptr_t& p_expect, que_node_heap_element_ptr_t p_new_value )
	{
		void* p_void_expect    = reinterpret_cast<void*>( p_expect );
		void* p_void_new_value = reinterpret_cast<void*>( p_new_value );
		bool  ans              = p->ref().ap_next_.compare_exchange_strong( p_void_expect, p_void_new_value );
		if ( !ans ) {
			p_expect = reinterpret_cast<que_node_heap_element_ptr_t>( p_void_expect );
		}
		return ans;
	}

	std::atomic<que_node_heap_element_ptr_t> ap_que_head_;
	std::atomic<que_node_heap_element_ptr_t> ap_que_tail_;
};

}   // namespace yan

#endif
