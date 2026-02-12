/**
 * @file yan_lf_queue.hpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief lock-free queue based on reference counter
 * @version 0.1
 * @date 2025-07-27
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#ifndef YAN_LF_QUEUE_HPP_
#define YAN_LF_QUEUE_HPP_

#include <atomic>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#ifdef YAN_LF_QUEUE_DEBUG_CODE
#include <string>

inline void my_runtime_assert_impl( bool expr, std::string expr_str, const char* file, int line )
{
	if ( !expr ) {
		std::string full_expr_str = std::string( "Assertion failed: " ) + expr_str + ", file " + file + ", line " + std::to_string( line );
		throw std::logic_error( full_expr_str );
	}
}
#define MY_RUNTIME_ASSERT( EXPR, ASSERT_STR ) my_runtime_assert_impl( EXPR, ASSERT_STR, __FILE__, __LINE__ )

#else
#define MY_RUNTIME_ASSERT( EXPR, ASSERT_STR ) ( (void)0 )
#endif

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

namespace yan2 {   // yet another

/**
 * @brief lock-free queue based on reference counter
 *
 * @tparam T
 */
template <typename T, typename Alloc = std::allocator<T>>
class rc_lf_queue {
public:
	using value_type     = T;
	using allocator_type = Alloc;

	// 他スレッドが参照していない状態で呼び出すこと。
	// Call this when no other threads are referencing this queue.
	~rc_lf_queue()
	{
		node* p_cur_node = ap_que_head_.exchange( nullptr );
		if ( p_cur_node == nullptr ) {
			// ap_que_head_がnullptrであることは不具合だが、他にどうしようもないので、そのまま終了する。
			return;
		}

		// 先頭の番兵ノードだけ、特別扱いする。
		// 番兵ノードが保持する値はすでに読み出されているので、関連するリソース解放の必要はないため。
		node* p_nxt_node = p_cur_node->ap_next_.load();
		p_cur_node->rc_.decrement_then_is_zero();   // 参照カウンタをデクリメントする。この操作はallocate_node()でインクリメントされた分を相殺するためのもの。
		retire_node( p_cur_node );

		// 順次、保持している値をデストラクトした後、ノードを開放する。
		while ( p_nxt_node != nullptr ) {
			p_cur_node = p_nxt_node;
			p_cur_node->destruct_value();
			p_nxt_node = p_cur_node->ap_next_.load();
			p_cur_node->rc_.decrement_then_is_zero();   // 参照カウンタをデクリメントする。この操作はallocate_node()でインクリメントされた分を相殺するためのもの。
			retire_node( p_cur_node );
		}
	}
	rc_lf_queue( void )
	  : ap_que_head_( allocate_node() )   // 番兵ノードを準備する
	  , ap_que_tail_( ap_que_head_.load() )
	{
		MY_RUNTIME_ASSERT( ap_que_head_.load()->rc_.read() > 0, "ap_que_head_ has zero reference count" );
	}
	rc_lf_queue( const rc_lf_queue& )            = delete;
	rc_lf_queue& operator=( const rc_lf_queue& ) = delete;
	rc_lf_queue( rc_lf_queue&& )                 = delete;
	rc_lf_queue& operator=( rc_lf_queue&& )      = delete;

	void push( const T& v )
	{
		node* p_pushed_new_tail = allocate_node();
		p_pushed_new_tail->emplace_value( v );

		push_impl( p_pushed_new_tail );
	}
	void push( T&& v )
	{
		node* p_pushed_new_tail = allocate_node();
		p_pushed_new_tail->emplace_value( std::move( v ) );

		push_impl( p_pushed_new_tail );
	}
	std::optional<T> try_pop( void )
	{
		while ( true ) {
			stickey_counter_decrement_guard expect_head_rc_g;
			node*                           p_expect_head_node = nullptr;
			while ( true ) {
				p_expect_head_node    = ap_que_head_.load( /*std::memory_order_acquire*/ );   // 番兵ノードが必ず存在する構造なので、nullptrチェックは不要
				bool get_head_node_rc = p_expect_head_node->rc_.increment_if_not_zero();
				if ( get_head_node_rc ) {
					expect_head_rc_g      = stickey_counter_decrement_guard( p_expect_head_node->rc_ );
					node* p_chk_head_node = ap_que_head_.load( /*std::memory_order_acquire*/ );
					if ( p_expect_head_node == p_chk_head_node ) {
						break;
					}
					// headノードが変化していたので、再度やり直す。
				} else {
					// headノードの参照カウンタが０に到達済みなので、他のスレッドが解放処理中かもしれない。最初からやり直す。
				}
			}

#ifdef YAN_LF_QUEUE_DEBUG_CODE
			// デバッグ用コード
			MY_RUNTIME_ASSERT( p_expect_head_node->rc_.read() > 0, "p_expect_head_node has zero reference count" );
#endif

			stickey_counter_decrement_guard expect_next_rc_g;
			node*                           p_expect_next_node                = nullptr;
			bool                            is_success_next_node_rc_increment = false;
			while ( true ) {
				p_expect_next_node = p_expect_head_node->ap_next_.load( /*std::memory_order_acquire*/ );
				if ( p_expect_next_node == nullptr ) {
					return std::nullopt;   // 本当に空っぽだったので、popを終了する。
				}

				bool get_next_node_rc = p_expect_next_node->rc_.increment_if_not_zero();
				if ( get_next_node_rc ) {
					expect_next_rc_g      = stickey_counter_decrement_guard( p_expect_next_node->rc_ );
					node* p_chk_next_node = p_expect_head_node->ap_next_.load( /*std::memory_order_acquire*/ );
					if ( p_expect_next_node == p_chk_next_node ) {
						is_success_next_node_rc_increment = true;
						break;
					}
					// nextノードが変化していたので、nextノードに対するリファレンスカウンタ取得をやり直す。
				} else {
					// nextノードの参照カウンタが０に到達済みなので、他のスレッドが解放処理中かもしれない。headの取得からやり直す。
					break;
				}
			}
			if ( !is_success_next_node_rc_increment ) {
				// nextノードの参照カウンタのインクリメントに失敗したので、最初からやり直す。
				continue;
			}

#ifdef YAN_LF_QUEUE_DEBUG_CODE
			// デバッグ用コード
			MY_RUNTIME_ASSERT( p_expect_next_node->rc_.read() > 0, "p_expect_next_node has zero reference count" );
#endif

			stickey_counter_decrement_guard expect_tail_rc_g;
			node*                           p_expect_tail_node = nullptr;
			while ( true ) {
				p_expect_tail_node    = ap_que_tail_.load( /*std::memory_order_acquire*/ );   // 番兵ノードが必ず存在する構造なので、nullptrチェックは不要
				bool get_tail_node_rc = p_expect_tail_node->rc_.increment_if_not_zero();
				if ( get_tail_node_rc ) {
					expect_tail_rc_g      = stickey_counter_decrement_guard( p_expect_tail_node->rc_ );
					node* p_chk_tail_node = ap_que_tail_.load( /*std::memory_order_acquire*/ );
					if ( p_expect_tail_node == p_chk_tail_node ) {
						break;
					}
					// tailノードが変化していたので、再度やり直す。
				} else {
					// tailノードの参照カウンタが０に到達済みなので、他のスレッドが解放処理中かもしれない。最初からやり直す。
				}
			}

#ifdef YAN_LF_QUEUE_DEBUG_CODE
			// デバッグ用コード
			MY_RUNTIME_ASSERT( p_expect_head_node->rc_.read() > 0, "p_expect_head_node has zero reference count" );
			MY_RUNTIME_ASSERT( p_expect_tail_node->rc_.read() > 0, "p_expect_tail_node has zero reference count" );
			MY_RUNTIME_ASSERT( p_expect_next_node->rc_.read() > 0, "p_expect_next_node has zero reference count" );
#endif

			// ここに到達した時点で、p_expect_head_node, p_expect_tail_node, p_expect_next_nodeはnullptrでないことが保証されている。
			if ( p_expect_head_node == p_expect_tail_node ) {
				// queueが空かもしれないが、tailの更新が遅れているだけかもしれない
				ap_que_tail_.compare_exchange_strong( p_expect_tail_node, p_expect_next_node );
				// tailの更新を試みる。成否は気にない。そのあと、最初からやり直す。
			} else {
				if ( ap_que_head_.compare_exchange_strong( p_expect_head_node, p_expect_next_node ) ) {
					// headが獲得できたので、nextの保存されている値情報を取り出す。
					// もともとのアルゴリズムでは、このv_の読み出しは、ap_que_head_.compare_exchange_strong()前で行っている。
					// これは、ABA問題を避けるために先行読み出しを行う必要があったから、そのように実装されている。
					// しかし、一方でその実装だと、Thread Sanitizerがレースコンディションのエラーを指摘してくる。
					// Thread Sanitizerの指摘を避けるためには、ABA問題を避けつつ、ap_que_head_.compare_exchange_strong()の後に読み出す必要がある。
					// これを実現するには、p_expect_next_nodeに対してのABA問題を避けるために、ハザードポインタを用いる方法が基本である。
					// この実装では、ハザードポインタの代わりにリファレンスカウンタを用いるので、p_expect_next_nodeに対してリファレンスカウンタを
					// 適用し、p_expect_next_nodeに対してのABA問題を避ける方策を採った。
					if constexpr ( std::is_move_constructible<T>::value ) {
						T ans( std::move( p_expect_next_node->v_ ) );
						p_expect_next_node->destruct_value();   // moveしたので、nodeが保持する値のデストラクトを行う。

						// pop処理が完了したので、headをnodeプールへ返却する。
						p_expect_head_node->rc_.decrement_then_is_zero();   // 参照カウンタをデクリメントする。この操作はallocate_node()でインクリメントされた分を相殺するためのもの。
						retire_node( p_expect_head_node );
						return ans;
					} else {
						T ans( p_expect_next_node->v_ );
						p_expect_next_node->destruct_value();   // copyしたので、nodeが保持する値のデストラクトを行う。

						// pop処理が完了したので、headをnodeプールへ返却する。
						p_expect_head_node->rc_.decrement_then_is_zero();   // 参照カウンタをデクリメントする。この操作はallocate_node()でインクリメントされた分を相殺するためのもの。
						retire_node( p_expect_head_node );
						return ans;
					}
				} else {
					// headの獲得に失敗したので、最初からやり直す。
				}
			}
		}

		return std::nullopt;
	}

	// 値のdestruct_value()が行われていること、およびすべてのスレッドが参照しないことを前提として、すべてのフリーノードを開放する。
	static size_t deallocate_all_free_nodes( void ) noexcept
	{
		size_t ans = tl_retire_node_list_.deallocate_all();
		ans += free_node_list_.deallocate_all();
		ans += primary_retired_node_list_.deallocate_all();
		return ans;
	}

private:
	struct stickey_counter_decrement_guard {
		~stickey_counter_decrement_guard( void ) noexcept
		{
			if ( p_rc_ != nullptr ) {
				p_rc_->decrement_then_is_zero();
			}
		}
		stickey_counter_decrement_guard( void ) noexcept
		  : p_rc_( nullptr )
		{
		}
		stickey_counter_decrement_guard( const stickey_counter_decrement_guard& )            = delete;
		stickey_counter_decrement_guard& operator=( const stickey_counter_decrement_guard& ) = delete;
		stickey_counter_decrement_guard( stickey_counter_decrement_guard&& other ) noexcept
		  : p_rc_( other.p_rc_ )
		{
			other.p_rc_ = nullptr;
		}
		stickey_counter_decrement_guard& operator=( stickey_counter_decrement_guard&& other ) noexcept
		{
			if ( this == &other ) {
				return *this;
			}

			if ( p_rc_ != nullptr ) {
				p_rc_->decrement_then_is_zero();
			}
			p_rc_       = other.p_rc_;
			other.p_rc_ = nullptr;
			return *this;
		}

		explicit stickey_counter_decrement_guard( rc::sticky_counter& rc_arg ) noexcept
		  : p_rc_( &rc_arg )
		{
		}

		auto read_count( void ) const noexcept
		{
			decltype( p_rc_->read() ) ans = 0;
			if ( p_rc_ != nullptr ) {
				ans = p_rc_->read();
			}
			return ans;
		}

		rc::sticky_counter* p_rc_;
	};

	struct node {
		rc::sticky_counter rc_;                 // このノードを参照しているスレッド数を示すreference counter
		std::atomic<node*> ap_next_;            // 次のノードへのポインタ
		node*              p_next_in_retire_;   // retire内での次のノードへのポインタ

		union {
			T       v_;
			uint8_t dummy_;
		};

		~node() {}
		node( void ) noexcept
		  : rc_()
		  , ap_next_( nullptr )
		  , p_next_in_retire_( nullptr )
		  , dummy_()
		{
		}

		void emplace_value( const T& v_arg )
		{
			new ( &v_ ) T( v_arg );
		}
		void emplace_value( T&& v_arg )
		{
			new ( &v_ ) T( std::move( v_arg ) );
		}
		void destruct_value( void ) noexcept
		{
			if constexpr ( !std::is_trivially_destructible<T>::value ) {
				v_.T::~T();
			}
		}

		void recycle( void ) noexcept
		{
			ap_next_.store( nullptr, std::memory_order_release );
			p_next_in_retire_ = nullptr;
			rc_.recycle();
		}
	};

	class node_list {
	public:
		constexpr node_list( void )
		  : p_head_( nullptr )
		  , p_tail_( nullptr )
		{
		}

		void push_back( node* p_node ) noexcept
		{
			if ( p_tail_ == nullptr ) {
				p_head_ = p_tail_         = p_node;
				p_node->p_next_in_retire_ = nullptr;
			} else {
				p_node->p_next_in_retire_  = nullptr;
				p_tail_->p_next_in_retire_ = p_node;
				p_tail_                    = p_node;
			}
		}
		void push_front( node* p_node ) noexcept
		{
			p_node->p_next_in_retire_ = p_head_;
			p_head_                   = p_node;
			if ( p_tail_ == nullptr ) {
				p_tail_ = p_node;
			}
		}

		node* pop_front( void ) noexcept
		{
			node* p_node = p_head_;
			if ( p_node == nullptr ) {
				return nullptr;
			}
			p_head_ = p_node->p_next_in_retire_;
			if ( p_head_ == nullptr ) {
				p_tail_ = nullptr;   // list is now empty
			}
			return p_node;
		}

		void merge( node_list& other ) noexcept
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
				p_tail_->p_next_in_retire_ = other.p_head_;
				p_tail_                    = other.p_tail_;
			}

			other.p_head_ = other.p_tail_ = nullptr;   // Clear the other list after merging
		}

		// すべてのノードで、値のdestruct_value()が行われていること、およびすべてのスレッドが参照しないことを前提として、すべてのノードを開放する。
		size_t deallocate_all( void ) noexcept
		{
			size_t ans        = 0;
			node*  p_cur_node = p_head_;
			while ( p_cur_node != nullptr ) {
				node* p_nxt_node = p_cur_node->p_next_in_retire_;
				deallocate_node( p_cur_node );
				p_cur_node = p_nxt_node;
				++ans;
			}
			p_head_ = p_tail_ = nullptr;
			return ans;
		}

	private:
		node* p_head_;   //!< pointer to head of retired list
		node* p_tail_;   //!< pointer to tail of retired list
	};

	class retired_node_list {
	public:
		constexpr retired_node_list( void )
		  : ndl_()
		{
		}

		void push_back( node* p_node ) noexcept
		{
			ndl_.push_back( p_node );
		}
		void push_front( node* p_node ) noexcept
		{
			ndl_.push_front( p_node );
		}

		// リストの先頭の要素のリファレンスカウンタの値をチェックして、ゼロであれば取り出す。
		// 戻り値のリファレンスカウンタ値はゼロのままで返す。
		node* check_and_pop( void ) noexcept
		{
			node* p_poped = ndl_.pop_front();
			if ( p_poped == nullptr ) {
				return nullptr;
			}

			if ( p_poped->rc_.read() != 0 ) {
				// まだ参照しているスレッドがいるので、取り出せない。再度すぐにチェックするのは無駄なので、FIFOキューの後ろに回す。
				ndl_.push_back( p_poped );
				return nullptr;
			}

			return p_poped;
		}

		void merge( node_list& other ) noexcept
		{
			ndl_.merge( other );
		}

		void merge( retired_node_list& other ) noexcept
		{
			ndl_.merge( other );
		}

		// すべてのノードで、値のdestruct_value()が行われていること、およびすべてのスレッドが参照しないことを前提として、すべてのノードを開放する。
		size_t deallocate_all( void ) noexcept
		{
			return ndl_.deallocate_all();
		}

	private:
		node_list ndl_;

		friend class mutex_retired_node_list;
	};

	class mutex_retired_node_list {
	public:
		~mutex_retired_node_list( void )
		{
			ndl_.deallocate_all();
		}
		constexpr mutex_retired_node_list( void ) = default;

		bool try_push_back( node* p_node ) noexcept
		{
			std::unique_lock<std::mutex> lock( mtx_, std::try_to_lock );
			if ( !lock.owns_lock() ) {
				return false;
			}
			ndl_.push_back( p_node );
			return true;
		}
		bool try_push_front( node* p_node ) noexcept
		{
			std::unique_lock<std::mutex> lock( mtx_, std::try_to_lock );
			if ( !lock.owns_lock() ) {
				return false;
			}
			ndl_.push_front( p_node );
			return true;
		}

		// ロックが確保できることを確認してから、
		// リストの先頭の要素のリファレンスカウンタの値をチェックして、ゼロであれば取り出す。
		// 戻り値のリファレンスカウンタ値はゼロのままで返す。
		node* try_check_and_pop( void ) noexcept
		{
			std::unique_lock<std::mutex> lock( mtx_, std::try_to_lock );
			if ( !lock.owns_lock() ) {
				return nullptr;
			}
			return ndl_.check_and_pop();
		}

		void merge( retired_node_list& other ) noexcept
		{
			std::lock_guard<std::mutex> lock( mtx_ );
			ndl_.merge( other.ndl_ );
		}

		// すべてのノードで、値のdestruct_value()が行われていること、およびすべてのスレッドが参照しないことを前提として、すべてのノードを開放する。
		size_t deallocate_all( void ) noexcept
		{
			std::lock_guard<std::mutex> lock( mtx_ );
			return ndl_.deallocate_all();
		}

	private:
		std::mutex        mtx_;
		retired_node_list ndl_;
	};

	class mutex_free_node_list {
	public:
		~mutex_free_node_list( void )
		{
			ndl_.deallocate_all();
		}

		constexpr mutex_free_node_list( void ) = default;

		void push_front( node* p_node ) noexcept
		{
			std::lock_guard<std::mutex> lock( mtx_ );
			ndl_.push_front( p_node );
		}

		bool try_push_front( node* p_node ) noexcept
		{
			std::unique_lock<std::mutex> lock( mtx_, std::try_to_lock );
			if ( !lock.owns_lock() ) {
				return false;
			}
			ndl_.push_front( p_node );
			return true;
		}

		// ロックが確保できることを確認してから、
		// リストの先頭の要素のリファレンスカウンタの値をチェックして、ゼロであれば取り出す。
		// 戻り値のリファレンスカウンタ値はゼロのままで返す。
		node* try_pop( void ) noexcept
		{
			std::unique_lock<std::mutex> lock( mtx_, std::try_to_lock );
			if ( !lock.owns_lock() ) {
				return nullptr;
			}
			return ndl_.pop_front();
		}

		// すべてのノードで、値のdestruct_value()が行われていること、およびすべてのスレッドが参照しないことを前提として、すべてのノードを開放する。
		size_t deallocate_all( void ) noexcept
		{
			std::lock_guard<std::mutex> lock( mtx_ );
			return ndl_.deallocate_all();
		}

	private:
		std::mutex mtx_;
		node_list  ndl_;
	};

	class thread_local_retired_node_list_cleaner {
	public:
		thread_local_retired_node_list_cleaner( void ) = default;

		~thread_local_retired_node_list_cleaner( void )
		{
			primary_retired_node_list_.merge( tl_retire_node_list_ );   // Merge the thread-local retired elements list into the primary list
		}
	};

	void push_impl( node* p_pushed_new_tail )
	{
#ifdef YAN_LF_QUEUE_DEBUG_CODE
		auto rc_val = p_pushed_new_tail->rc_.read();
		MY_RUNTIME_ASSERT( rc_val >= 1, "p_pushed_new_tail has invalid reference count(=" + std::to_string( rc_val ) + ")" );
#endif
		while ( true ) {
			stickey_counter_decrement_guard expect_tail_rc_g;
			node*                           p_expect_tail_node = nullptr;
			while ( true ) {
				p_expect_tail_node    = ap_que_tail_.load( /*std::memory_order_acquire*/ );   // 番兵ノードが必ず存在する構造なので、nullptrチェックは不要
				bool get_tail_node_rc = p_expect_tail_node->rc_.increment_if_not_zero();
				if ( get_tail_node_rc ) {
					expect_tail_rc_g      = stickey_counter_decrement_guard( p_expect_tail_node->rc_ );
					node* p_chk_tail_node = ap_que_tail_.load( /*std::memory_order_acquire*/ );
					if ( p_expect_tail_node == p_chk_tail_node ) {
						break;
					}
					// tailノードが変化していたので、再度やり直す。
				} else {
					// tailノードの参照カウンタが０に到達済みなので、他のスレッドが解放処理中かもしれない。最初からやり直す。
				}
			}
#ifdef YAN_LF_QUEUE_DEBUG_CODE
			// デバッグ用コード
			MY_RUNTIME_ASSERT( p_expect_tail_node->rc_.read() > 0, "p_expect_tail_node has zero reference count in push_impl" );
#endif

			node* p_next_node = p_expect_tail_node->ap_next_.load( /*std::memory_order_acquire*/ );
			if ( p_next_node == nullptr ) {
				// 終端ノードのはず。
#ifdef YAN_LF_QUEUE_DEBUG_CODE
				// デバッグ用コード
				MY_RUNTIME_ASSERT( p_pushed_new_tail->ap_next_.load() == nullptr, "p_pushed_new_tail has non-null next pointer in push_impl" );
#endif
				bool ret = p_expect_tail_node->ap_next_.compare_exchange_strong( p_next_node, p_pushed_new_tail );
				if ( ret ) {
					// 終端ノードの後ろへの追加に成功。tailの更新を試みる。
#ifdef YAN_LF_QUEUE_DEBUG_CODE
					// デバッグ用コード
					MY_RUNTIME_ASSERT( expect_tail_rc_g.read_count() > 0, "p_expect_tail_node has zero reference count in push_impl" );
#endif
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

	// 事後条件： 戻り値のnodeのrc_は１で初期化されている
	static node* allocate_node( void )
	{
		node* p_reused = free_node_list_.try_pop();
		if ( p_reused != nullptr ) {
#ifdef YAN_LF_QUEUE_DEBUG_CODE
			// デバッグ用コード
			MY_RUNTIME_ASSERT( p_reused->rc_.read() == 0, "p_reused has non-zero reference count" );
#endif
			// 再利用可能なノードがあったので、それを返す。
			p_reused->recycle();
			return p_reused;
		}
		p_reused = tl_retire_node_list_.check_and_pop();
		if ( p_reused != nullptr ) {
#ifdef YAN_LF_QUEUE_DEBUG_CODE
			// デバッグ用コード
			MY_RUNTIME_ASSERT( p_reused->rc_.read() == 0, "p_reused has non-zero reference count" );
#endif
			// 再利用可能なノードがあったので、それを返す。
			p_reused->recycle();
			return p_reused;
		}
		p_reused = primary_retired_node_list_.try_check_and_pop();
		if ( p_reused != nullptr ) {
#ifdef YAN_LF_QUEUE_DEBUG_CODE
			// デバッグ用コード
			MY_RUNTIME_ASSERT( p_reused->rc_.read() == 0, "p_reused has non-zero reference count" );
#endif
			// 再利用可能なノードがあったので、それを返す。
			p_reused->recycle();
			return p_reused;
		}

		// 再利用可能なノードがないので、新規に確保する。
		using node_allocator_type        = typename std::allocator_traits<Alloc>::template rebind_alloc<node>;
		using node_allocator_traits_type = std::allocator_traits<node_allocator_type>;

		node_allocator_type node_allocator;

		node* p_new = node_allocator_traits_type::allocate( node_allocator, 1 );
		try {
			node_allocator_traits_type::construct( node_allocator, p_new );
		} catch ( ... ) {
			node_allocator_traits_type::deallocate( node_allocator, p_new, 1 );
			throw;
		}
		return p_new;
	}

	// 事前条件： p->v_はdestruct_value()されていること。
	// Precondition: p->v_ has been destructed by destruct_value().
	// フリーノードリストか、リタイアドノードリストに登録する。
	// また、ここでは、ノード自身のdeallocateは行わない。これは、個々のリファレンスカウンタが参照できる状態を保つためである。
	// グローバルに管理されているリタイアドノードリストやフリーノードリストからノードをdeallocate_node()する操作は、別途行う。
	// Register to free node list or retired node list.
	// Also, do not deallocate the node itself here. This is to keep the state where each reference counter can be referenced.
	// Deallocate_node() from globally managed retired node list or free node list is done separately.
	static void retire_node( node* p ) noexcept
	{
		if ( p == nullptr ) {
			return;
		}

		node* p_fc_node = nullptr;
		if ( p->rc_.read() != 0 ) {
			// まだ参照しているスレッドがいるので、スレッドローカルなretireリストに回す。
			p_fc_node = tl_retire_node_list_.check_and_pop();
			tl_retire_node_list_.push_back( p );
		} else {
			p_fc_node = p;
		}
		if ( p_fc_node == nullptr ) {
			return;
		}

		// 参照しているスレッドがいないので、フリーノードに登録を試みる。
		bool ret = free_node_list_.try_push_front( p_fc_node );
		if ( !ret ) {
			// フリーノードリストへの登録に失敗したので、primary_retired_node_listへの登録を試みる。
			ret = primary_retired_node_list_.try_push_front( p_fc_node );
			if ( !ret ) {
				// primary_retired_node_listへの登録にも失敗したので、スレッドローカルなretireリストに回す。
				tl_retire_node_list_.push_front( p_fc_node );
			}
		}
	}

	// 事前条件： p->v_はdestruct_value()されていること。
	// 事前条件： pのリファレンスカウンタが０で、だれも参照していないこと。
	// Precondition: p->v_ has been destructed by destruct_value().
	// Precondition: p's reference counter is zero and no one is referencing it.
	static void deallocate_node( node* p ) noexcept
	{
		using node_allocator_type        = typename std::allocator_traits<Alloc>::template rebind_alloc<node>;
		using node_allocator_traits_type = std::allocator_traits<node_allocator_type>;
		node_allocator_type node_allocator;
		node_allocator_traits_type::destroy( node_allocator, p );
		node_allocator_traits_type::deallocate( node_allocator, p, 1 );
	}

	std::atomic<node*> ap_que_head_;
	std::atomic<node*> ap_que_tail_;

	static mutex_retired_node_list                             primary_retired_node_list_;     //!< primary retired elements list
	static mutex_free_node_list                                free_node_list_;                //!< mutex-free list to hold free nodes
	static thread_local retired_node_list                      tl_retire_node_list_;           //!< thread-local variable to hold retired mgr_info_type elements
	static thread_local thread_local_retired_node_list_cleaner tl_retire_node_list_cleaner_;   //!< thread-local cleaner for retired elements list
};

template <typename T, typename Alloc>
constinit rc_lf_queue<T, Alloc>::mutex_retired_node_list rc_lf_queue<T, Alloc>::primary_retired_node_list_;   //!< primary retired elements list
template <typename T, typename Alloc>
constinit rc_lf_queue<T, Alloc>::mutex_free_node_list rc_lf_queue<T, Alloc>::free_node_list_;   //!< mutex-free list to hold free nodes
template <typename T, typename Alloc>
constinit thread_local rc_lf_queue<T, Alloc>::retired_node_list rc_lf_queue<T, Alloc>::tl_retire_node_list_;   //!< thread-local variable to hold retired mgr_info_type elements
template <typename T, typename Alloc>
constinit thread_local rc_lf_queue<T, Alloc>::thread_local_retired_node_list_cleaner rc_lf_queue<T, Alloc>::tl_retire_node_list_cleaner_;   //!< thread-local cleaner for retired elements list

}   // namespace yan2

#endif
