
/**
 * APEX MAP 
 * 
 * Copyright (c) 2026 Paschal Ahanmisi 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <apex_hs_error.h>
#include <apex_hs_hash.h>
#include <concepts>
#include <atomic>
#include <new>
#include <type_traits>
/* Concepts */
template <typename K , typename  H > 
concept Apex_check =  requires(H h , K k){
	{h(k)} -> std::same_as<size_t>;
};


/* Definitions*/ 
constexpr auto  HDI_SZ2 = std::hardware_destructive_interference_size * 2 ;
constexpr auto CACHE_LINE = std::hardware_destructive_interference_size ;

enum class apex_type  {
	reader , writer 
};
/* Payload */
template <typename Key , typename Value > 
struct alignas(HDI_SZ2) Payload{
	std::atomic<Key> m_id ;
	std::atomic<size_t>	seq ;
	alignas(CACHE_LINE) Value m_data ;	
};


/* APEX MAP */
template <typename Key , typename Value , apex_type  AT ,typename Hash , 
	 size_t CB /*COLLISION BUFFER : This si the amount of of extra payload we can store due to a hash collision
	 */ = 5ULL > 
requires Apex_check<Key,Hash >
class Apex_map{
	public :
		Apex_map() noexcept;
		int write()noexcept ; 
		int read() noexcept;
		~Apex_map();
	private : 
		size_t m_size ; 
		Payload<Key,  Value>* m_payload ;
};
template <typename Key , typename Value ,  apex_type AT ,typename Hash , 
	 size_t CB > 
requires Apex_check<Key,Hash >
Apex_map<Key , Value , AT , Hash , CB>::Apex_map() noexcept {
	if constexpr (apex_type::reader == AT) {
	
	}else if constexpr(apex_type::writer == AT){
	}else {
		static_assert(false  , "Dont know the type" );
	}
}
int main(){

	return 0;
}
