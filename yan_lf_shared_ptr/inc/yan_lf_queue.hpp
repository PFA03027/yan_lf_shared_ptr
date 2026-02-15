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

#include "rc_sticky_counter.hpp"

#include "typed_lfheap.hpp"
#include "yan_lf_shared_ptr.hpp"

namespace yan {   // yet another

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
		retire_node( p_cur_node );

		// 順次、保持している値をデストラクトした後、ノードを開放する。
		while ( p_nxt_node != nullptr ) {
			p_cur_node = p_nxt_node;
			p_cur_node->destruct_value();
			p_nxt_node = p_cur_node->ap_next_.load();
			retire_node( p_cur_node );
		}
	}
	rc_lf_queue( void )
	  : ap_que_head_( allocate_node() )   // 番兵ノードを準備する
	  , ap_que_tail_( ap_que_head_.load() )
	{
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
		std::optional<T> ans             = std::nullopt;
		node*            p_detached_node = try_pop_impl( ans );

		retire_node( p_detached_node );
		return ans;
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
	struct node {
		rc::basic_sticky_counter<uint64_t, true> rc_;                 // このノードを参照しているスレッド数を示すreference counter
		std::atomic<node*>                       ap_next_;            // 次のノードへのポインタ
		node*                                    p_next_in_retire_;   // retire内での次のノードへのポインタ

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

#if 1
			// リファレンスカウンタ値がゼロに到達したことをチェックするのに、is_sticky_zero()を使うこちらのコードは、
			// ABA問題が生じない。。。
			if ( p_poped->rc_.is_sticky_zero() ) {
				return p_poped;
			}

			// まだ参照しているスレッドがいるので、取り出せない。再度すぐにチェックするのは無駄なので、FIFOキューの後ろに回す。
			ndl_.push_back( p_poped );
			return nullptr;
#else
			// リファレンスカウンタ値がゼロに到達したことをチェックするのに、read()を使うこちらのコードは、
			// なぜかABA問題が発生する。。。
			if ( p_poped->rc_.read() == 0 ) {
				return p_poped;
			}

			// まだ参照しているスレッドがいるので、取り出せない。再度すぐにチェックするのは無駄なので、FIFOキューの後ろに回す。
			ndl_.push_back( p_poped );
			return nullptr;
#endif
		}

		void merge( node_list& other ) noexcept
		{
			ndl_.merge( other );
		}

		void merge( retired_node_list& other ) noexcept
		{
			ndl_.merge( other.ndl_ );
		}

		// すべてのノードで、値のdestruct_value()が行われていること、およびすべてのスレッドが参照しないことを前提として、すべてのノードを開放する。
		size_t deallocate_all( void ) noexcept
		{
			return ndl_.deallocate_all();
		}

	private:
		node_list ndl_;

		friend class mutex_retired_node_list;
		friend class tl_retired_node_list;
	};

	class tl_retired_node_list {
	public:
		~tl_retired_node_list( void )
		{
			primary_retired_node_list_.merge( ndl_ );
		}
		constexpr tl_retired_node_list( void ) = default;

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
			return ndl_.check_and_pop();
		}

		// すべてのノードで、値のdestruct_value()が行われていること、およびすべてのスレッドが参照しないことを前提として、すべてのノードを開放する。
		size_t deallocate_all( void ) noexcept
		{
			return ndl_.deallocate_all();
		}

	private:
		retired_node_list ndl_;
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
			ndl_.merge( other );
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

	void push_impl( node* p_pushed_new_tail )
	{
		while ( true ) {
			node*                                   p_expect_tail_node = ap_que_tail_.load( /*std::memory_order_acquire*/ );   // 番兵ノードが必ず存在する構造なので、nullptrチェックは不要
			rc::stickey_counter_try_increment_guard expect_tail_rc_g( p_expect_tail_node->rc_ );
			if ( !expect_tail_rc_g.owns_rc() ) {
				continue;   // tailノードの参照カウンタが０に到達済みなので、すでに、pop済みで他のスレッドが解放処理中かもしれない。最初からやり直す。
			}
			if ( p_expect_tail_node != ap_que_tail_.load( /*std::memory_order_acquire*/ ) ) {
				continue;   // tailノードが変化していたので、再度やり直す。
			}

			node* p_next_node = p_expect_tail_node->ap_next_.load( /*std::memory_order_acquire*/ );
			if ( p_next_node != nullptr ) {
				// p_expect_tail_nodeが終端ノードを指していなかったので、tailを更新してから、最初からやり直す
				ap_que_tail_.compare_exchange_strong( p_expect_tail_node, p_next_node );
				continue;
			}

			// 終端ノードのはず。終端ノードへの追加を試みる
			bool ret = p_expect_tail_node->ap_next_.compare_exchange_strong( p_next_node, p_pushed_new_tail );
			if ( !ret ) {
				continue;   // 終端ノードへの追加に失敗したので、最初からやり直す。
			}

			// 終端ノードの後ろへの追加に成功。tailの更新を試みる。
			ap_que_tail_.compare_exchange_strong( p_expect_tail_node, p_pushed_new_tail );
			break;   // 更新処理完了、ループを抜ける。なお、更新に失敗しても、他のスレッドが頑張ってくれるから、気にしない。
		}

		return;
	}

	node* try_pop_impl( std::optional<T>& popped_value )
	{
		while ( true ) {
			node*                                   p_expect_head_node = ap_que_head_.load( /*std::memory_order_acquire*/ );   // 番兵ノードが必ず存在する構造なので、nullptrチェックは不要
			rc::stickey_counter_try_increment_guard expect_head_rc_g( p_expect_head_node->rc_ );
			if ( !expect_head_rc_g.owns_rc() ) {
				continue;   // headノードの参照カウンタが０に到達済みなので、他のスレッドが解放処理中かもしれない。最初からやり直す。
			}
			if ( p_expect_head_node != ap_que_head_.load( /*std::memory_order_acquire*/ ) ) {
				continue;   // headノードが変化していたので、再度やり直す。
			}

			node*                                   p_expect_tail_node = ap_que_tail_.load( /*std::memory_order_acquire*/ );   // 番兵ノードが必ず存在する構造なので、nullptrチェックは不要
			rc::stickey_counter_try_increment_guard expect_tail_rc_g( p_expect_tail_node->rc_ );
			if ( !expect_tail_rc_g.owns_rc() ) {
				continue;   // tailノードの参照カウンタが０に到達済みなので、他のスレッドが解放処理中かもしれない。headの取得からやり直す。
			}
			if ( p_expect_tail_node != ap_que_tail_.load( /*std::memory_order_acquire*/ ) ) {
				continue;   // tailノードが変化していたので、再度やり直す。
			}

			node* p_expect_next_node = p_expect_head_node->ap_next_.load( /*std::memory_order_acquire*/ );
			if ( p_expect_next_node == nullptr ) {
				return nullptr;   // 本当に空っぽだったので、popを終了する。
			}
			rc::stickey_counter_try_increment_guard expect_next_rc_g( p_expect_next_node->rc_ );
			if ( !expect_next_rc_g.owns_rc() ) {
				continue;   // nextノードの参照カウンタが０に到達済みなので、他のスレッドが解放処理中かもしれない。headの取得からやり直す。
			}
			if ( p_expect_next_node != p_expect_head_node->ap_next_.load( /*std::memory_order_acquire*/ ) ) {
				continue;   // nextノードが変化していたので、nextノードに対するリファレンスカウンタ取得をやり直す。
			}

			// ここに到達した時点で、p_expect_head_node, p_expect_tail_node, p_expect_next_nodeはnullptrでないことが保証されている。
			if ( p_expect_head_node == p_expect_tail_node ) {
				// queueが空かもしれないが、tailの更新が遅れているだけかもしれない
				ap_que_tail_.compare_exchange_strong( p_expect_tail_node, p_expect_next_node );
				continue;   // tailの更新を試みる。成否は気にない。そのあと、最初からやり直す。
			}
			if ( !ap_que_head_.compare_exchange_strong( p_expect_head_node, p_expect_next_node ) ) {
				continue;   // headの獲得に失敗したので、最初からやり直す。
			}

			// headが獲得できたので、nextの保存されている値情報を取り出す。
			// もともとのアルゴリズムでは、このv_の読み出しは、ap_que_head_.compare_exchange_strong()前で行っている。
			// これは、ABA問題を避けるために先行読み出しを行う必要があったから、そのように実装されている。
			// しかし、一方でその実装だと、Thread Sanitizerがレースコンディションのエラーを指摘してくる。
			// Thread Sanitizerの指摘を避けるためには、ABA問題を避けつつ、ap_que_head_.compare_exchange_strong()の後に読み出す必要がある。
			// これを実現するには、p_expect_next_nodeに対してのABA問題を避けるために、ハザードポインタを用いる方法が基本である。
			// この実装では、ハザードポインタの代わりにリファレンスカウンタを用いるので、p_expect_next_nodeに対してリファレンスカウンタを
			// 適用し、p_expect_next_nodeに対してのABA問題を避ける方策を採った。
			if constexpr ( std::is_move_constructible<T>::value ) {
				popped_value = std::optional<T>( std::move( p_expect_next_node->v_ ) );
			} else {
				popped_value = std::optional<T>( p_expect_next_node->v_ );
			}
			p_expect_next_node->destruct_value();   // moveしたので、nodeが保持する値のデストラクトを行う。
			return p_expect_head_node;
		}
	}

	// 事後条件： 戻り値のnodeのrc_は１で初期化されている
	static node* allocate_node( void )
	{
		node* p_reused = free_node_list_.try_pop();
		if ( p_reused != nullptr ) {
			// 再利用可能なノードがあったので、それを返す。
			p_reused->recycle();
			return p_reused;
		}
		p_reused = tl_retire_node_list_.check_and_pop();
		if ( p_reused != nullptr ) {
			// 再利用可能なノードがあったので、それを返す。
			p_reused->recycle();
			return p_reused;
		}
		p_reused = primary_retired_node_list_.try_check_and_pop();
		if ( p_reused != nullptr ) {
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
	// また、ここでは、ノード自身のdeallocateは行わない。これは、個々のリファレンスカウンタをいつでも参照できる状態に保つためである。
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
		bool  dec_ret   = p->rc_.decrement_then_is_zero();   // 参照カウンタをデクリメントする。この操作はallocate_node()で初期値として割り当てられているカウンタ１を相殺するためのもの。
		if ( dec_ret ) {
			// pはだれも参照していないので、そのままフリーノードリストへの登録に回す。
			p_fc_node = p;
		} else {
			// pはまだ参照しているスレッドがいるので、スレッドローカルなretireリストに回す。
			p_fc_node = tl_retire_node_list_.check_and_pop();
			tl_retire_node_list_.push_back( p );
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

	static mutex_retired_node_list           primary_retired_node_list_;   //!< primary retired elements list
	static mutex_free_node_list              free_node_list_;              //!< mutex-free list to hold free nodes
	static thread_local tl_retired_node_list tl_retire_node_list_;         //!< thread-local variable to hold retired mgr_info_type elements
};

template <typename T, typename Alloc>
constinit rc_lf_queue<T, Alloc>::mutex_retired_node_list rc_lf_queue<T, Alloc>::primary_retired_node_list_;   //!< primary retired elements list
template <typename T, typename Alloc>
constinit rc_lf_queue<T, Alloc>::mutex_free_node_list rc_lf_queue<T, Alloc>::free_node_list_;   //!< mutex-free list to hold free nodes
template <typename T, typename Alloc>
constinit thread_local rc_lf_queue<T, Alloc>::tl_retired_node_list rc_lf_queue<T, Alloc>::tl_retire_node_list_;   //!< thread-local variable to hold retired mgr_info_type elements

}   // namespace yan

#endif
