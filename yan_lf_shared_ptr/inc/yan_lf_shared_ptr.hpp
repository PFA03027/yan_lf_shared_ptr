/**
 * @file yan_lf_shared_ptr.hpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief wait-free shared pointer implementation based on sticky counter
 * @version 0.1
 * @date 2025-07-20
 *
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 *
 */

#ifndef YAN_LF_SHARED_PTR_HPP_
#define YAN_LF_SHARED_PTR_HPP_

#include <atomic>
#include <cstddef>
#include <memory>

#include "rc_sticky_counter.hpp"
#include "typed_lfheap.hpp"

namespace yan2 {   // yet another
namespace itl {

struct lf_shared_value_carrier_base {
	virtual ~lf_shared_value_carrier_base()      = default;
	virtual void destruct_value( void ) noexcept = 0;
	virtual void discard_self( void ) noexcept   = 0;

	rc::sticky_counter carrier_rc_;   //!< carrier自身の寿命管理用reference counter
	rc::sticky_counter value_rc_;     //!< v_の有効性管理用reference counter
};

template <typename T, typename Alloc>
struct lf_shared_value_carrier_impl_in_place : public lf_shared_value_carrier_base {
	using value_type = T;

	Alloc allocator_;
	union {
		value_type v_;
		char       dummy_;
	};

	~lf_shared_value_carrier_impl_in_place() override {}   // unionメンバのため、デフォルトデストラクタは生成されないので、明示的に定義する。
	template <typename... Args, typename std::enable_if<std::is_constructible<T, Args&&...>::value>::type* = nullptr>
	lf_shared_value_carrier_impl_in_place( Alloc a, Args&&... args ) noexcept( std::is_nothrow_constructible<T>::value )
	  : lf_shared_value_carrier_base()
	  , allocator_( a )
	  , v_( std::forward<Args>( args )... )
	{
	}

	void destruct_value( void ) noexcept override
	{
		if constexpr ( !std::is_trivially_destructible<value_type>::value ) {
			v_.value_type::~value_type();
		}
	}

	void discard_self( void ) noexcept override
	{
		discard_my_class_carrier( this );   // 適切なAllocatorを使って自身を破棄するため、少々トリッキーなやり方で、破棄を行っている。
	}

	template <typename... Args>
	static lf_shared_value_carrier_impl_in_place* create_my_class_carrier( Alloc a, Args&&... args );

private:
	static void discard_my_class_carrier( lf_shared_value_carrier_impl_in_place* p_discard_target ) noexcept;
};

template <typename T, typename Alloc>
template <typename... Args>
lf_shared_value_carrier_impl_in_place<T, Alloc>* lf_shared_value_carrier_impl_in_place<T, Alloc>::create_my_class_carrier( Alloc a, Args&&... args )
{
	using allocator_type        = typename std::allocator_traits<Alloc>::template rebind_alloc<lf_shared_value_carrier_impl_in_place<T, Alloc>>;
	using allocator_traits_type = std::allocator_traits<allocator_type>;

	allocator_type tmp_alloc( a );

	lf_shared_value_carrier_impl_in_place<T, Alloc>* p_new = allocator_traits_type::allocate( tmp_alloc, 1 );
	try {
		allocator_traits_type::construct( tmp_alloc, p_new, a, std::forward<Args>( args )... );
	} catch ( ... ) {
		allocator_traits_type::deallocate( tmp_alloc, p_new, 1 );
		throw;
	}
	return p_new;
}

template <typename T, typename Alloc>
void lf_shared_value_carrier_impl_in_place<T, Alloc>::discard_my_class_carrier( lf_shared_value_carrier_impl_in_place<T, Alloc>* p_discard_target ) noexcept
{
	using allocator_type        = typename std::allocator_traits<Alloc>::template rebind_alloc<lf_shared_value_carrier_impl_in_place<T, Alloc>>;
	using allocator_traits_type = std::allocator_traits<allocator_type>;

	allocator_type tmp_alloc( p_discard_target->allocator_ );
	allocator_traits_type::destroy( tmp_alloc, p_discard_target );
	allocator_traits_type::deallocate( tmp_alloc, p_discard_target, 1 );
}

template <typename T, typename Deleter, typename Alloc>
struct lf_shared_value_carrier_impl_pointer_with_deleter : public lf_shared_value_carrier_base {
	using value_type   = T;
	using deleter_type = Deleter;

	Alloc        allocator_;
	value_type*  p_value_;
	deleter_type deleter_;

	~lf_shared_value_carrier_impl_pointer_with_deleter() override = default;
	lf_shared_value_carrier_impl_pointer_with_deleter( Alloc a, value_type* p_value_arg, deleter_type deleter_arg )
	  : lf_shared_value_carrier_base()
	  , allocator_( a )
	  , p_value_( p_value_arg )
	  , deleter_( deleter_arg )
	{
	}

	void destruct_value( void ) noexcept override
	{
		deleter_( p_value_ );
		p_value_ = nullptr;
	}

	void discard_self( void ) noexcept override
	{
		discard_my_class_carrier( this );   // 適切なAllocatorを使って自身を破棄するため、少々トリッキーなやり方で、破棄を行っている。
	}

	static lf_shared_value_carrier_impl_pointer_with_deleter* create_my_class_carrier( Alloc a, value_type* p_value_arg, deleter_type deleter_arg );

private:
	static void discard_my_class_carrier( lf_shared_value_carrier_impl_pointer_with_deleter* p_discard_target ) noexcept;
};

template <typename T, typename Deleter, typename Alloc>
lf_shared_value_carrier_impl_pointer_with_deleter<T, Deleter, Alloc>* lf_shared_value_carrier_impl_pointer_with_deleter<T, Deleter, Alloc>::create_my_class_carrier( Alloc a, value_type* p_value_arg, deleter_type deleter_arg )
{
	using allocator_type        = typename std::allocator_traits<Alloc>::template rebind_alloc<lf_shared_value_carrier_impl_pointer_with_deleter<T, Deleter, Alloc>>;
	using allocator_traits_type = std::allocator_traits<allocator_type>;

	allocator_type tmp_alloc( a );

	lf_shared_value_carrier_impl_pointer_with_deleter<T, Deleter, Alloc>* p_new = allocator_traits_type::allocate( tmp_alloc, 1 );
	try {
		allocator_traits_type::construct( tmp_alloc, p_new, a, p_value_arg, deleter_arg );
	} catch ( ... ) {
		allocator_traits_type::deallocate( tmp_alloc, p_new, 1 );
		throw;
	}
	return p_new;
}

template <typename T, typename Deleter, typename Alloc>
void lf_shared_value_carrier_impl_pointer_with_deleter<T, Deleter, Alloc>::discard_my_class_carrier( lf_shared_value_carrier_impl_pointer_with_deleter<T, Deleter, Alloc>* p_discard_target ) noexcept
{
	using allocator_type        = typename std::allocator_traits<Alloc>::template rebind_alloc<lf_shared_value_carrier_impl_pointer_with_deleter<T, Deleter, Alloc>>;
	using allocator_traits_type = std::allocator_traits<allocator_type>;

	allocator_type tmp_alloc( p_discard_target->allocator_ );
	allocator_traits_type::destroy( tmp_alloc, p_discard_target );
	allocator_traits_type::deallocate( tmp_alloc, p_discard_target, 1 );
}

}   // namespace itl

/**
 * @brief lock-free shared pointer
 *
 * This class provides a lock-free shared pointer implementation that manages the lifetime of an object through reference counting.
 * And reference counter is wait-free implemented by wait-free sticky counter.
 *
 * If deleter, Allocator and constructor/destructor of T are also lock-free, then lf_shared_ptr is lock-free.
 *
 * @tparam T type to reference
 */
template <typename T>
class lf_shared_ptr {
public:
	using element_type = T;
	// using weak_type    = lf_weak_ptr<T>;

	~lf_shared_ptr()
	{
		if ( p_carrier_ == nullptr ) {
			return;   // nothing to do
		}
		if ( p_carrier_->value_rc_.decrement_then_is_zero() ) {   // decrement the reference count
			p_carrier_->destruct_value();
		}
		if ( p_carrier_->carrier_rc_.decrement_then_is_zero() ) {   // decrement the reference count
			p_carrier_->discard_self();
		}
	}

	constexpr lf_shared_ptr( void )
	  : p_carrier_( nullptr )
	  , p_elem_( nullptr )
	{
	}

	lf_shared_ptr( const lf_shared_ptr& other )
	  : p_carrier_( other.p_carrier_ )
	  , p_elem_( other.p_elem_ )
	{
		if ( p_carrier_ == nullptr ) {
			return;   // nothing to do
		}

		p_carrier_->carrier_rc_.increment_if_not_zero();
		p_carrier_->value_rc_.increment_if_not_zero();
	}

	lf_shared_ptr& operator=( const lf_shared_ptr& other )
	{
		if ( this == &other ) {
			return *this;   // Handle self-assignment
		}

		lf_shared_ptr( other ).swap( *this );   // Use copy-and-swap idiom for exception safety
		return *this;
	}

	lf_shared_ptr( lf_shared_ptr&& other ) noexcept
	  : p_carrier_( other.p_carrier_ )
	  , p_elem_( other.p_elem_ )
	{
		other.p_carrier_ = nullptr;
		other.p_elem_    = nullptr;   // reset other pointer to avoid double free
	}

	lf_shared_ptr& operator=( lf_shared_ptr&& other ) noexcept
	{
		if ( this == &other ) {
			return *this;   // Handle self-assignment
		}

		lf_shared_ptr( std::move( other ) ).swap( *this );   // Use move-and-swap idiom for exception safety
		return *this;
	}

	void swap( lf_shared_ptr& other ) noexcept
	{
		std::swap( p_carrier_, other.p_carrier_ );
		std::swap( p_elem_, other.p_elem_ );
	}

	template <typename Y>
	explicit lf_shared_ptr( Y* p )
	  : p_carrier_( itl::lf_shared_value_carrier_impl_pointer_with_deleter<Y, std::default_delete<Y>, std::allocator<Y>>::create_my_class_carrier( std::allocator<Y>(), p, std::default_delete<Y>() ) )
	  , p_elem_( p )
	{
	}

	template <typename Y, typename Deleter>
	lf_shared_ptr( Y* p, Deleter deleter )
	  : p_carrier_( itl::lf_shared_value_carrier_impl_pointer_with_deleter<Y, Deleter, std::allocator<Y>>::create_my_class_carrier( std::allocator<Y>(), p, deleter ) )
	  , p_elem_( p )
	{
	}

	template <typename Y, typename Deleter, typename Alloc>
	lf_shared_ptr( Y* p, Deleter deleter, Alloc alloc )
	  : p_carrier_( itl::lf_shared_value_carrier_impl_pointer_with_deleter<Y, Deleter, Alloc>::create_my_class_carrier( alloc, p, deleter ) )
	  , p_elem_( p )
	{
	}

	lf_shared_ptr( std::nullptr_t p )
	  : p_carrier_( itl::lf_shared_value_carrier_impl_pointer_with_deleter<T, std::default_delete<T>, std::allocator<T>>::create_my_class_carrier( std::allocator<T>(), nullptr, std::default_delete<T>() ) )
	  , p_elem_( nullptr )
	{
	}

	template <typename Deleter>
	lf_shared_ptr( std::nullptr_t p, Deleter deleter )
	  : p_carrier_( itl::lf_shared_value_carrier_impl_pointer_with_deleter<T, Deleter, std::allocator<T>>::create_my_class_carrier( std::allocator<T>(), nullptr, deleter ) )
	  , p_elem_( nullptr )
	{
	}

	template <typename Deleter, typename Alloc>
	lf_shared_ptr( std::nullptr_t p, Deleter deleter, Alloc alloc )
	  : p_carrier_( itl::lf_shared_value_carrier_impl_pointer_with_deleter<T, Deleter, Alloc>::create_my_class_carrier( alloc, nullptr, deleter ) )
	  , p_elem_( nullptr )
	{
	}

	template <class Y>
	lf_shared_ptr( const lf_shared_ptr<Y>& r, element_type* p ) noexcept
	  : p_carrier_( r.p_carrier_ )
	  , p_elem_( p )
	{
		if ( p_carrier_ == nullptr ) {
			return;   // nothing to do
		}

		p_carrier_->carrier_rc_.increment_if_not_zero();
		p_carrier_->value_rc_.increment_if_not_zero();
	}

	template <class Y>
	lf_shared_ptr( lf_shared_ptr<Y>&& r, element_type* p ) noexcept
	  : p_carrier_( r.p_carrier_ )
	  , p_elem_( p )
	{
		r.p_carrier_ = nullptr;
		r.p_elem_    = nullptr;   // reset other pointer to avoid double free
	}

	template <class Y, typename std::enable_if<std::is_convertible<Y*, T*>::value>::type* = nullptr>
	lf_shared_ptr( const lf_shared_ptr<Y>& r ) noexcept
	  : p_carrier_( r.p_carrier_ )
	  , p_elem_( r.p_elem_ )
	{
		if ( p_carrier_ == nullptr ) {
			return;   // nothing to do
		}

		p_carrier_->carrier_rc_.increment_if_not_zero();
		p_carrier_->value_rc_.increment_if_not_zero();
	}

	template <class Y, typename std::enable_if<std::is_convertible<Y*, T*>::value>::type* = nullptr>
	lf_shared_ptr( lf_shared_ptr<Y>&& r ) noexcept
	  : p_carrier_( r.p_carrier_ )
	  , p_elem_( r.p_elem_ )
	{
		r.p_carrier_ = nullptr;
		r.p_elem_    = nullptr;   // reset other pointer to avoid double free
	}

	template <class Y, typename Deleter, typename std::enable_if<std::is_convertible<typename std::unique_ptr<Y, Deleter>::pointer, T*>::value>::type* = nullptr>
	lf_shared_ptr( std::unique_ptr<Y, Deleter>&& r ) noexcept
	  : p_carrier_( nullptr )
	  , p_elem_( nullptr )
	{
		p_elem_    = r.get();
		p_carrier_ = itl::lf_shared_value_carrier_impl_pointer_with_deleter<Y, Deleter, std::allocator<Y>>::create_my_class_carrier( std::allocator<Y>(), p_elem_, r.get_deleter() );
		r.release();   // 構築が成功したので、unique_ptrからポインタを解放する。例外安全のために、最後に行う。
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
		return p_elem_;
	}
	T* operator->() noexcept
	{
		return p_elem_;
	}
	const T* operator->() const noexcept
	{
		return p_elem_;
	}
	T& operator*() noexcept
	{
		return *p_elem_;
	}
	const T& operator*() const noexcept
	{
		return *p_elem_;
	}

	void reset()
	{
		lf_shared_ptr().swap( *this );
	}
	template <typename Y>
	void reset( Y* p )
	{
		lf_shared_ptr( p ).swap( *this );
	}
	template <typename Y, typename Deleter>
	void reset( Y* p, Deleter deleter )
	{
		lf_shared_ptr( p, deleter ).swap( *this );
	}

	operator bool() const noexcept
	{
		return p_elem_ != nullptr;
	}

private:
	struct inner_constructor_tag {};

	lf_shared_ptr( inner_constructor_tag, itl::lf_shared_value_carrier_base* p_carrier_arg, T* p_elem_arg )
	  : p_carrier_( p_carrier_arg )
	  , p_elem_( p_elem_arg )
	{
	}

	template <typename U, typename... Args>
	friend lf_shared_ptr<U> make_lf_shared( Args&&... args );
	template <typename U, typename Alloc, typename... Args>
	friend lf_shared_ptr<U> allocate_lf_shared( Alloc alloc, Args&&... args );

	itl::lf_shared_value_carrier_base* p_carrier_;   //!< pointer to the value carrier
	element_type*                      p_elem_;      //!< pointer to the heap element that holds the value
};

template <typename T, typename... Args>
lf_shared_ptr<T> make_lf_shared( Args&&... args )
{
	auto                                             p_impl_carrier = itl::lf_shared_value_carrier_impl_in_place<T, std::allocator<T>>::create_my_class_carrier( std::allocator<T>(), std::forward<Args>( args )... );
	T*                                               p_value        = &( p_impl_carrier->v_ );
	typename lf_shared_ptr<T>::inner_constructor_tag tag;
	return lf_shared_ptr<T>( tag, p_impl_carrier, p_value );
}

template <typename T, typename Alloc, typename... Args>
lf_shared_ptr<T> allocate_lf_shared( Alloc alloc, Args&&... args )
{
	auto                                             p_impl_carrier = itl::lf_shared_value_carrier_impl_in_place<T, Alloc>::create_my_class_carrier( alloc, std::forward<Args>( args )... );
	T*                                               p_value        = &( p_impl_carrier->v_ );
	typename lf_shared_ptr<T>::inner_constructor_tag tag;
	return lf_shared_ptr<T>( tag, p_impl_carrier, p_value );
}

}   // namespace yan2

#endif   // YAN_LF_SHARED_PTR_HPP_
