/**
 * @file rc_sticky_counter.hpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief sticky counter that could be recycle
 * @version 0.1
 * @date 2025-07-19
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 * CppCon 2024で紹介されたwait-free sticky counterをもとに、再利用できる機能を追加したwait-free sticky counter
 * 再利用可能なように、制御ビットを一つ追加している。
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

#include <atomic>
#include <cstdint>
#include <limits>

namespace rc {

/**
 * @brief sticky counter that could be recycle
 *
 * CppCon 2024で紹介されたwait-free sticky counterをもとに、再利用できる機能を追加したwait-free sticky counter
 */
template <typename T>
struct basic_sticky_counter {
	static_assert( std::atomic<T>::is_always_lock_free, "T should support atomic operation" );
	static_assert( std::is_unsigned<T>::value, "T should be unsigned" );
	using rc_type = T;

	/**
	 * @brief increment counter if it is not zero
	 *
	 * @return true counter is not zero, then success to increment
	 * @return false counter is zero, therefore fail to increment
	 */
	bool increment_if_not_zero( void ) noexcept
	{
		rc_type pre_value = counter_.fetch_add( 1, std::memory_order_acq_rel );
		bool    ans       = ( ( pre_value & is_zero_ ) == 0 );
		if ( ans && ( ( pre_value & recycled_zero_ ) != 0 ) ) {
			pre_value++;   // recycled_zero_フラグが立っている場合は、0->1への変化した状況のため、recycled_zero_フラグを落とす準備をする。
			// recycled_zero_ -> recycled_zero_ + 1への変化した状況のなので、recycled_zero_フラグを落とす。
			counter_.compare_exchange_strong( pre_value, pre_value & ( ~recycled_zero_ ), std::memory_order_acq_rel );
		}
		return ans;
	}

	/**
	 * @brief decrement counter
	 *
	 * @pre increment_if_not_zero()の戻り値がtrueであること。decrement_then_is_zero()の呼び出し回数が、increment_if_not_zero()の戻り値がtrueである回数以下であること。
	 * この事前条件は、increment_if_not_zero()の戻り値がtrueであった呼び出しとdecrement_then_is_zero()の呼び出しが対称関係であることの保証を要求している。
	 *
	 * @return true counter reatched to zero after decrement of caller thread
	 * @return false counter is not zero, even if decrement of caller thread
	 *
	 * @warning if caller side violate the pre-condtion, this sticky counter is broken
	 */
	bool decrement_then_is_zero( void ) noexcept
	{
		// ここに来る時点でrecycled_zero_のフラグは落ちているので、
		// 1 -> 0の変化を検出するのに、recycled_zero_のフラグの状態考慮は不要で、1との比較で十分となっている。
		if ( counter_.fetch_sub( 1, std::memory_order_acq_rel ) == 1 ) {
			rc_type e = 0;
			if ( counter_.compare_exchange_strong( e, is_zero_, std::memory_order_acq_rel ) ) {
				return true;
			}
			if ( ( e & helped_ ) != 0 ) {
				if ( ( counter_.exchange( is_zero_, std::memory_order_release ) & helped_ ) != 0 ) {
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
		rc_type val = counter_.load( std::memory_order_acquire );
		if ( val == 0 ) {
			// 1->0への変更処理中と思われる状況でゼロが読み出せてしまったので、ゼロになったことを確定する必要がある。
			// そのため、ゼロフラグを立てられることを再検査する。
			// また、1->0への変更に成功したスレッドの再判定ができるように、helped_フラグも立てておく。
			// 補足： recycle直後の状況では、val == recycled_zero_であるため、val != 0となり、ここには到達しない。
			if ( counter_.compare_exchange_strong( val, is_zero_ | helped_, std::memory_order_acq_rel ) ) {
				return 0;
			}
		}
		return ( val & is_zero_ ) ? 0 : ( val & ( ~recycled_zero_ ) );
	}

	/**
	 * @brief read counter value
	 *
	 * @return rc_type counter value
	 */
	bool is_sticky_zero( void ) const noexcept
	{
		rc_type val = counter_.load( std::memory_order_acquire );
		return ( val & is_zero_ ) != 0;
	}

	/**
	 * @brief 再利用するために、カウンタの状態を初期化しなおす。
	 *
	 * @pre
	 * このAPIを利用できるのは、decrement_then_is_zero()がtrueで帰ってきたスレッドに制約する必要がある。
	 * そうでない場合、何らかの方法で、このカウンターがすでに参照されていないことを外部ロジックで保証されている必要がある。
	 *
	 * @warning
	 * 事前条件が守られない場合、stickyではなくなってしまう。
	 * 例えば、
	 */
	void recycle( void ) noexcept
	{
		counter_.store( recycled_zero_ );
	}

	static constexpr rc_type max( void )
	{
		return std::numeric_limits<rc_type>::max() >> 3;
	}

private:
	static constexpr rc_type is_zero_       = ~( std::numeric_limits<rc_type>::max() >> 1 );                            //<! 最上位ビットのみが立った値
	static constexpr rc_type helped_        = ~( ( std::numeric_limits<rc_type>::max() >> 2 ) | is_zero_ );             //<! 最上位から2番目のみのビットが立った値
	static constexpr rc_type recycled_zero_ = ~( ( std::numeric_limits<rc_type>::max() >> 3 ) | is_zero_ | helped_ );   //<! 最上位から3番目のみのビットが立った値

	mutable std::atomic<rc_type> counter_ { recycled_zero_ };   //!< reference counter. mutable attribute is for read() member function
};

/**
 * @brief basic_sticky_counterのincrementとdecrementの対称性を保証する必要がある制約を守るためのサポートクラス
 *
 * @tparam SC
 */
template <typename SC>
struct counter_guard {
	~counter_guard()
	{
		decrement_then_is_zero();
	}
	counter_guard( void ) noexcept
	  : p_sc_( nullptr ), is_owns_count_( false )
	{
	}
	counter_guard( const counter_guard& src ) noexcept
	  : p_sc_( src.p_sc_ ), is_owns_count_( ( p_sc_ != nullptr ) ? p_sc_->increment_if_not_zero() : false )
	{
	}
	counter_guard( counter_guard&& src ) noexcept
	  : p_sc_( src.p_sc_ ), is_owns_count_( src.is_owns_count_ )
	{
		src.p_sc_          = nullptr;
		src.is_owns_count_ = false;
	}

	explicit counter_guard( SC& sc_ref ) noexcept
	  : p_sc_( &sc_ref ), is_owns_count_( p_sc_->increment_if_not_zero() )
	{
	}

	void swap( counter_guard& other ) noexcept
	{
		std::swap( p_sc_, other.p_sc_ );
		std::swap( is_owns_count_, other.is_owns_count_ );
	}

	bool decrement_then_is_zero( void ) noexcept   // ゼロに到達したスレッドがそれを認識できるように結果を返す
	{
		if ( p_sc_ == nullptr ) {
			return false;
		}
		if ( !is_owns_count_ ) {
			return false;
		}
		is_owns_count_ = false;
		return p_sc_->decrement_then_is_zero();
	}

	bool owns_count( void ) const noexcept   // count upに成功し、1以上になっていると真
	{
		return is_owns_count_;
	}

private:
	SC*  p_sc_;            //<! pointer to sticky_counter
	bool is_owns_count_;   //!< is success to increment ? true: success to increment
};

using sticky_counter = basic_sticky_counter<uint64_t>;

}   // namespace rc

#endif
