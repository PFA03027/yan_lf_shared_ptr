/**
 * @file test_comm.hpp
 * @author Teruaki Ata (PFA03027@nifty.com)
 * @brief 
 * @version 0.1
 * @date 2025-07-20
 * 
 * @copyright Copyright (c) 2025, Teruaki Ata (PFA03027@nifty.com)
 * 
 */

#ifndef TEST_COMM_HPP_
#define TEST_COMM_HPP_


class NonTrivialType {
public:
	~NonTrivialType()
	{
		delete p_value;   // Clean up dynamically allocated memory
	}
	NonTrivialType( int value = 42 )
	  : p_value( new int( value ) ) {}

	NonTrivialType( const NonTrivialType& other )
	  : p_value( new int( *other.p_value ) )   // Deep copy
	{
	}
	NonTrivialType( NonTrivialType&& other ) noexcept
	  : p_value( new int( *other.p_value ) )   // Deep copy
	{
	}

	NonTrivialType& operator=( const NonTrivialType& other )
	{
		if ( this == &other ) {
			return *this;   // Handle self-assignment
		}
		delete p_value;                        // Clean up existing resource
		p_value = new int( *other.p_value );   // Deep copy
		return *this;
	}
	NonTrivialType& operator=( NonTrivialType&& other ) noexcept
	{
		if ( this == &other ) {
			return *this;   // Handle self-assignment
		}
		delete p_value;                        // Clean up existing resource
		p_value = new int( *other.p_value );   // Deep copy
		return *this;
	}

	int get_value() const
	{
		return p_value ? *p_value : 0;   // Return value or 0 if p_value is null
	}

private:
	int* p_value;   //!< Pointer to an integer value, which is non-trivial due to dynamic memory allocation.
};

#endif   // TEST_COMM_HPP_
