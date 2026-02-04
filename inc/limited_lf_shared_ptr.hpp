/**
 * @file limited_lf_shared_ptr.hpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief
 * @version 0.1
 * @date 2025-07-20
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#ifndef LIMITED_LF_SHARED_PTR_HPP_
#define LIMITED_LF_SHARED_PTR_HPP_

#include "rc_sticky_counter.hpp"
#include "typed_lfheap.hpp"

namespace yan {
namespace itl {

struct lf_shared_value_carrier_base {
	virtual ~lf_shared_value_carrier_base() = default;
	rc::sticky_counter rc_;   //!< v_の寿命管理用reference counter
};

template <typename T>
struct lf_shared_value_carrier : public lf_shared_value_carrier_base {
	using value_type = T;
	value_type v_;

	template <typename... Args, typename std::enable_if<std::is_constructible<T, Args&&...>::value>::type* = nullptr>
	lf_shared_value_carrier( Args&&... args ) noexcept( std::is_nothrow_constructible<T>::value )
	  : lf_shared_value_carrier_base()
	  , v_( std::forward<Args>( args )... )
	{
	}
};

}   // namespace itl

template <typename T>
class limited_lf_shared_ptr {
public:
	~limited_lf_shared_ptr()
	{
		reset();
	}

	constexpr limited_lf_shared_ptr( void )
	  : p_elem_( nullptr )
	  , rc_guard_() {}

	limited_lf_shared_ptr( const limited_lf_shared_ptr& other )
	  : p_elem_( other.p_elem_ )
	  , rc_guard_( other.rc_guard_ ) {}

	limited_lf_shared_ptr& operator=( const limited_lf_shared_ptr& other )
	{
		if ( this == &other ) {
			return *this;   // Handle self-assignment
		}

		limited_lf_shared_ptr( other ).swap( *this );   // Use copy-and-swap idiom for exception safety
		return *this;
	}

	limited_lf_shared_ptr( limited_lf_shared_ptr&& other ) noexcept
	  : p_elem_( other.p_elem_ )
	  , rc_guard_( std::move( other.rc_guard_ ) )
	{
		other.p_elem_ = nullptr;   // reset other pointer to avoid double free
	}

	limited_lf_shared_ptr& operator=( limited_lf_shared_ptr&& other ) noexcept
	{
		if ( this == &other ) {
			return *this;   // Handle self-assignment
		}

		limited_lf_shared_ptr( std::move( other ) ).swap( *this );   // Use move-and-swap idiom for exception safety
		return *this;
	}

	void swap( limited_lf_shared_ptr& other ) noexcept
	{
		std::swap( p_elem_, other.p_elem_ );
		rc_guard_.swap( other.rc_guard_ );
	}

	T* get() noexcept
	{
		if ( p_elem_ == nullptr ) {
			return nullptr;   // nothing to do
		}
		return &( p_elem_->ref().v_ );
	}

	const T* get() const noexcept
	{
		if ( p_elem_ == nullptr ) {
			return nullptr;   // nothing to do
		}
		return &( p_elem_->ref().v_ );
	}
	T* operator->() noexcept
	{
		return &( p_elem_->ref().v_ );
	}
	const T* operator->() const noexcept
	{
		return &( p_elem_->ref().v_ );
	}
	T& operator*() noexcept
	{
		return p_elem_->ref().v_;
	}
	const T& operator*() const noexcept
	{
		return p_elem_->ref().v_;
	}
	bool is_valid() const noexcept
	{
		return p_elem_ != nullptr;
	}
	void reset()
	{
		if ( p_elem_ == nullptr ) {
			return;   // nothing to do
		}
		if ( rc_guard_.decrement_then_is_zero() ) {   // decrement the reference count
			p_elem_->destruct_value();
			heap_type::retire( p_elem_ );
		}
		p_elem_ = nullptr;
	}

private:
	using carrier_type = itl::lf_shared_value_carrier<T>;
	using heap_type    = lfheap::fixedarray_heap<carrier_type>;
	using element_type = lfheap::heap_element<carrier_type>;

	limited_lf_shared_ptr( element_type* p_elem_arg )
	  : p_elem_( p_elem_arg )
	  , rc_guard_( p_elem_->ref().rc_ )   // acquire reference count
	{
#ifdef TEST_ENABLE_LOGICCHECKER
		if ( !rc_guard_.owns_count() ) {
			throw std::logic_error( "limited_lf_shared_ptr: failed to acquire reference count." );
		}
#endif
	}

	template <typename U, typename... Args>
	friend limited_lf_shared_ptr<U> make_limited_lf_shared_ptr( Args&&... args );

	element_type*                                p_elem_;     //!< pointer to the heap element that holds the value
	rc::sticky_counter_guard<rc::sticky_counter> rc_guard_;   //!< reference counter guard to manage the lifetime of the element
};

template <typename T, typename... Args>
limited_lf_shared_ptr<T> make_limited_lf_shared_ptr( Args&&... args )
{
	typename limited_lf_shared_ptr<T>::element_type* ptr = limited_lf_shared_ptr<T>::heap_type::allocate();
	if ( ptr == nullptr ) {
		throw std::bad_alloc();   // allocation failed
	}
	ptr->emplace( std::forward<Args>( args )... );
	return limited_lf_shared_ptr<T>( ptr );
}

}   // namespace yan

#endif   // LIMITED_LF_SHARED_PTR_HPP_
