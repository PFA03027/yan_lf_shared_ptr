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

#include "limited_lf_arrayheap.hpp"
#include "rc_sticky_counter.hpp"

namespace rc {
namespace itl {

}

template <typename T>
class limited_lf_shared_ptr {
public:
	using element_type = itl::heap_element<T>;

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

	limited_lf_shared_ptr( limited_lf_shared_ptr&& other ) noexcept
	  : p_elem_( other.p_elem_ )
	  , rc_guard_( std::move( other.rc_guard_ ) )
	{
		other.p_elem_ = nullptr;   // reset other pointer to avoid double free
	}

	limited_lf_shared_ptr& operator=( const limited_lf_shared_ptr& other )
	{
		if ( this == &other ) {
			return *this;   // Handle self-assignment
		}

		limited_lf_shared_ptr( other ).swap( *this );   // Use copy-and-swap idiom for exception safety
		return *this;
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
		return &( p_elem_->ref() );
	}

	const T* get() const noexcept
	{
		if ( p_elem_ == nullptr ) {
			return nullptr;   // nothing to do
		}
		return &( p_elem_->ref() );
	}
	T* operator->() noexcept
	{
		return &p_elem_->ref();
	}
	const T* operator->() const noexcept
	{
		return &p_elem_->ref();
	}
	T& operator*() noexcept
	{
		return p_elem_->ref();
	}
	const T& operator*() const noexcept
	{
		return p_elem_->ref();
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
			limited_arrayheap<T>::retire( p_elem_ );
		}
		p_elem_ = nullptr;
	}

private:
	limited_lf_shared_ptr( element_type* p_elem_arg )
	  : p_elem_( p_elem_arg )
	  , rc_guard_( p_elem_->rc_ )   // acquire reference count
	{
#ifdef TEST_ENABLE_LOGICCHECKER
		if ( !rc_guard_.owns_count() ) {
			throw std::logic_error( "limited_lf_shared_ptr: failed to acquire reference count." );
		}
#endif
	}

	template <typename U, typename... Args>
	friend limited_lf_shared_ptr<U> make_limited_lf_shared_ptr( Args&&... args );

	element_type*                        p_elem_;     //!< pointer to the heap element that holds the value
	sticky_counter_guard<sticky_counter> rc_guard_;   //!< reference counter guard to manage the lifetime of the element
};

template <typename T, typename... Args>
limited_lf_shared_ptr<T> make_limited_lf_shared_ptr( Args&&... args )
{
	typename limited_lf_shared_ptr<T>::element_type* ptr = limited_arrayheap<T>::allocate();
	if ( ptr == nullptr ) {
		throw std::bad_alloc();   // allocation failed
	}
	ptr->emplace( std::forward<Args>( args )... );
	return limited_lf_shared_ptr<T>( ptr );
}

}   // namespace rc

#endif   // LIMITED_LF_SHARED_PTR_HPP_
