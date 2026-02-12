/**
 * @file rc_sticky_counter.hpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief sticky counter
 * @version 0.1
 * @date 2025-07-19
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 * CppCon 2024で紹介されたwait-free sticky counterの再実装
 *
 * @note
 * This wait-free algorithm and implementation introduced in below CppCon 2024 by Daniel Anderson
 *
 * https://www.youtube.com/watch?v=kPh8pod0-gk
 *
 * If the platform is x86, this algorithm may be wait-free truely. Because x86 has the instruction lock xadd.
 * If the platform is ARM64, this algorithm is lock-free, but not wait-free. Because ARM64 maybe be used CAS loop by linked-store for fetchadd/fetchsub.
 * Caution: above check is depended on compiler type and/or compiler version. please check by yourself.
 */

#ifndef RC_STICKY_COUNTER_HPP_
#define RC_STICKY_COUNTER_HPP_

#include <cstdlib>

#include <atomic>
#include <cstdint>
#include <limits>

namespace rc {

/**
 * @brief sticky counter that could be recycle
 *
 * CppCon 2024で紹介されたwait-free sticky counterをもとに、再利用できる機能を追加したwait-free sticky counter
 *
 * @note
 * Wait-freeとなるのは、fetch_add()がatomicに実行可能な命令を持つx86の場合。
 * fetch_add()の実装がCAS loopで実現される(と思われる)ARM系の場合、厳密にはWait-freeとはならない。
 *
 */
template <typename T, bool ApplyOptimizedMemoryOrder = false>
struct basic_sticky_counter {
	static_assert( std::atomic<T>::is_always_lock_free, "T should support atomic operation" );
	static_assert( std::is_unsigned<T>::value, "T should be unsigned" );
	using rc_type = T;

	/**
	 * @brief increment counter if it is not zero
	 *
	 * @return true counter value before increment is initial value or not zero, then success to increment
	 * @return false counter value reaches to zero sticky, therefore fail to increment
	 */
	bool increment_if_not_zero( void ) noexcept
	{
		rc_type pre_value = counter_.fetch_add( 1, ApplyOptimizedMemoryOrder ? std::memory_order_acq_rel : std::memory_order_seq_cst );
#ifdef ENABLE_STICKY_COUNTER_OVERFLOW_CHECK
		if ( is_overflow( pre_value ) ) {
			exit( 1 );
			// オーバーフローが発生した場合、sticky counterとしては回復不能な状態となる。異常検出可能なように、exit(1)を呼び出す。
			// 補足：
			// CAS loopを使うことで加算前にチェック可能だが、wait-freeではなくなってしまう。
			// さらに、チェックでオーバーフロー発生前に加算を回避したとしても、呼び出し側で、結局は整合性が取れなくなってしまう。
			// そのため、事前回避は無意味である。
			// 呼び出し側の使い方を変えるしか改善方法はないので、ここでexit(1)することで、呼び出し側に異常を知らせるだけで十分である。
		}
#endif
		bool ans = ( ( pre_value & is_zero_ ) == 0 );
		if ( ans ) {
			// ゼロフラグが立っていないので、incrementに成功
		} else {
			// ゼロフラグが立っていたので、incrementには失敗。
			// ただ、加算自体は実行されてしまっていて、これを放置すると、オーバーフローが発生するかもしれないので、加算を差し戻す。
			// ただし、他スレッドで、recycle()が呼び出されていて、counter_が1にリサイクルされている可能性もあるため、単純にfetch_sub()を呼び出すのではなく、
			// compare_exchange_strong()で、recycle()が呼び出されていないことを確認した上で、差し戻しを行う。
			// recycle()が呼び出されていた場合、counter_は1になっているはずなので、差し戻しは不要であり、その場合、compare_exchange_strong()は失敗するので、
			// comppare_exchange_strong()の成否にかかわらす、結果は意図通りとなる。
			rc_type e = pre_value + 1;
			counter_.compare_exchange_strong( e, is_zero_, ApplyOptimizedMemoryOrder ? std::memory_order_acq_rel : std::memory_order_seq_cst );
			// compare_exchange_strong()の戻り値は不要なので、無視する
		}
		return ans;
	}

	/**
	 * @brief decrement counter
	 *
	 * @pre decrement_then_is_zero()の呼び出し回数が、increment_if_not_zero()の戻り値がtrueである回数+1以下(*)であること。
	 * (*)このカウンタの初期値を1としているため、コンストラクタ呼び出し直後にdecrement_then_is_zero()を1回呼び出すことが可能である。
	 * この事前条件は、increment_if_not_zero()の戻り値がtrueであった呼び出しとdecrement_then_is_zero()の呼び出しが対称関係であることの保証を要求している。
	 *
	 * @return true counter value reatched to zero after decrement of caller thread
	 * @return false counter value does not reached to zero, even if decrement of caller thread
	 *
	 * @warning if caller side violate the pre-condtion, this sticky counter is broken
	 */
	bool decrement_then_is_zero( void ) noexcept
	{
#ifdef ENABLE_STICKY_COUNTER_LOGIC_CHECK
		rc_type tmp = counter_.load( /* std::memory_order_acquire */ );
		if ( tmp == 0 ) {
			// 呼び出し側が、すくなくともincrement_if_not_zero()を一度も呼び出さずに、decrement_then_is_zero()を呼び出した状況。
			// pre conditionの違反なので、abort()を呼ぶ。
			abort();
		}
#endif

		if ( counter_.fetch_sub( 1, ApplyOptimizedMemoryOrder ? std::memory_order_acq_rel : std::memory_order_seq_cst ) == 1 ) {
			rc_type e = 0;
			if ( counter_.compare_exchange_strong( e, is_zero_, ApplyOptimizedMemoryOrder ? std::memory_order_acq_rel : std::memory_order_seq_cst ) ) {
				// fetch_sub(1)で、counter_がゼロに到達したあと、ゼロ確定に成功した。よって、trueを返す。
				return true;
			}

			// fetch_sub(1)で、counter_がゼロに到達したあと、別スレッドがゼロ確定を行ったか、incrementされた場合にここに到達する。
			if ( ( e & helped_ ) != 0 ) {
				if ( ( counter_.exchange( is_zero_, ApplyOptimizedMemoryOrder ? std::memory_order_acq_rel : std::memory_order_seq_cst ) & helped_ ) != 0 ) {
					// this thread get helped flag. this thread takes credit that decrement operation reached to zero.
					return true;
				} else {
					// other thread get helped flag. other thread took credit that decrement operation reached to zero.
				}
			} else {
				// does not reach zero by below possibilies;
				// * other threads increment counter_ b/w fetch_sub and compare_exchange_strong. So, counter become 1 or greater than 1
				// * other thread set zero flag. so, other thread took credit that decrement operation reached to zero.
			}
		}
		return false;
	}

	/**
	 * @brief read counter value
	 *
	 * @return rc_type counter value
	 */
	rc_type read( void ) const noexcept
	{
		rc_type val = counter_.load( ApplyOptimizedMemoryOrder ? std::memory_order_acquire : std::memory_order_seq_cst );
		if ( val == 0 ) {
			// 1->0への変更処理中と思われる状況でゼロが読み出せてしまったので、ゼロになったことを確定する必要がある。
			// そのため、ゼロフラグを立てることが出来ることを再検査する。
			// また、1->0への変更に成功したスレッドの再判定ができるように、helped_フラグも立てておく。
			// 補足： recycle直後の状況では、val == 1 であるため、val != 0となり、ここには到達しない。
			if ( counter_.compare_exchange_strong( val, is_zero_ | helped_, ApplyOptimizedMemoryOrder ? std::memory_order_acq_rel : std::memory_order_seq_cst ) ) {
				return 0;
			}
		}
		return ( ( val & is_zero_ ) != 0 ) ? 0 : val;
	}

	/**
	 * @brief check if counter value is zero
	 *
	 * @retval true counter value is zero
	 * @retval false counter value is not zero
	 */
	bool is_sticky_zero( void ) const noexcept
	{
		rc_type val = counter_.load( ApplyOptimizedMemoryOrder ? std::memory_order_acquire : std::memory_order_seq_cst );
		return ( val & is_zero_ ) != 0;
	}

	/**
	 * @brief 再利用するために、カウンタの状態を初期化しなおす。
	 *
	 * @pre
	 * このAPIを利用できるのは、decrement_then_is_zero()がtrueで帰ってきたスレッドであること。
	 * そうでない場合、何らかの方法で、このカウンターがすでに参照されていないことを外部ロジックで保証されていること。
	 *
	 * @warning
	 * 事前条件が守られない場合、stickyではなくなってしまい、異常なリファレンスカウンタとして動作する。結果として、メモリ破壊等の異常状態に至る。
	 */
	void recycle( void ) noexcept
	{
		return counter_.store( 1, ApplyOptimizedMemoryOrder ? std::memory_order_release : std::memory_order_seq_cst );
	}

	static constexpr rc_type max( void )
	{
		return std::numeric_limits<rc_type>::max() >> 3;
	}

private:
	static constexpr rc_type is_zero_ = ~( std::numeric_limits<rc_type>::max() >> 1 );                  //<! 最上位ビットのみが立った値
	static constexpr rc_type helped_  = ~( ( std::numeric_limits<rc_type>::max() >> 2 ) | is_zero_ );   //<! 最上位から2番目のみのビットが立った値

	static constexpr bool is_overflow( rc_type pre_value )
	{
		return ( pre_value & ( ~( is_zero_ | helped_ ) ) ) >= max();
	}

	mutable std::atomic<rc_type> counter_ { 1 };   //!< reference counter. initial value is 1. mutable attribute is for read() member function
};

using sticky_counter = basic_sticky_counter<uint64_t>;

}   // namespace rc

#endif
