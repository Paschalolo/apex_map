
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
#define _DEFAULT_SOURCE

#if defined (__x86_64__)
#include <xmmintrin.h>
#endif

#include <sys/mman.h>
#include <cstdint>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/mman.h>
#include <apex_hs_error.h>
#include <apex_hs_hash.h>
#include <cstring>
#include <concepts>
#include <atomic>
#include <utility>
#include <type_traits>
#include <cstdio>
/* Concepts */


template <typename K , typename  H , typename Value > 
concept Apex_check =  requires(H h , K k){
	{h(k)} -> std::same_as<size_t>;
	requires std::is_trivially_destructible_v<Value>; 
};


/* Definitions*/ 
constexpr auto  HDI_SZ2 = std::hardware_destructive_interference_size * 2 ;
constexpr auto CACHE_LINE = std::hardware_destructive_interference_size ;
constexpr char m_shared_mem_name[] = "apex_hash_map_data";

/* HUGE PAGE SIZES */
constexpr size_t _4_KB_PAGE = 4ULL * 1024ULL;
constexpr size_t _2_MB_HUGE_PAGE  = 2ULL * 1024ULL * 1024ULL ;
constexpr size_t _1_GB_HUGE_PAGE= 1ULL * 1024ULL * 1024ULL * 1024ULL;


constexpr size_t BACKOFF = 8ULL ;


/* This functions takes a size and rounds up to  the nearest ceiling of the alignment value */
template <size_t ALIGN>
static inline size_t get_aligned_size(size_t sz ){
	const size_t align_mask = !(sz & (ALIGN - 1)) -1  ;
	auto align_sz = (sz+ ALIGN) & (~(ALIGN - 1));
	return (align_sz & align_mask) | (sz & ~align_mask);
}

enum class apex_type  {
	reader , writer 
};

enum class payload_mask : uint8_t {
	FILLED =0xFF , REMOVED=0x01 	
};
/* Payload */
template <typename Key , typename Value > 
struct alignas(HDI_SZ2) Payload{
	std::atomic<Key> m_id ;
	std::atomic<size_t>	seq ;
	std::atomic<std::uint8_t> m_mask ;
	alignas(CACHE_LINE) Value m_data ;	
};

template <typename Key , typename Value , size_t CB = 5ULL> 
struct alignas(HDI_SZ2) Bucket {
	Payload<Key , Value>  m_payloads[CB];
};
/* APEX MAP */
template <typename Key , typename Value , apex_type  AT  ,typename Hash = default_hash<Key> , 
	 size_t HGT = _4_KB_PAGE /* Default Page size */,
	 size_t CB /*COLLISION BUFFER : This si the amount of of extra payload we can store due to a hash collision
	 */ = 5ULL > 
requires Apex_check<Key,Hash  , Value>
class alignas(64) Apex_map{
	public :
		Apex_map(size_t ) noexcept;
		int write(Key key , Value v)noexcept ; 
		int write(Key key , Value* val)noexcept ; 
		int read(const Key key , Value* output_val)  noexcept;

		template <typename U , typename Func>
		requires std::is_same_v<std::invoke_result_t<Func, Payload<Key, Value>*>, void* >
		int read_sub_value(const Key key ,Func func , U* output_val)  noexcept;
		~Apex_map();
		size_t 	size() const {
			return m_size;
		}
	private : 
		[[no_unique_address]] Hash m_hash{};
		size_t m_size ;
		size_t m_no ;
		int openfd;
		Payload<Key,  Value>* m_payload ;
};
template <typename Key , typename Value ,  apex_type AT , typename Hash ,size_t HGT, 
	 size_t CB > 
requires Apex_check<Key,Hash , Value >
Apex_map<Key , Value , AT , Hash , HGT  , CB>::Apex_map(size_t sz ) noexcept  {
	if constexpr (apex_type::reader == AT) {

			int ret ;

			m_size = get_aligned_size<HGT>((sz*CB * sizeof(Payload<Key, Value>)));
			m_no = sz;
			int fd = shm_open(m_shared_mem_name, O_RDWR | O_CREAT , S_IRWXU );
			openfd = fd ;
			if(fd == -1 ) {
				failure_setup();	
			}
			ret =ftruncate(fd , static_cast<off_t>(m_size));
			if(ret == -1 ) {
				failure_setup();
			}
			void* addr  = nullptr;
			if(HGT == _4_KB_PAGE){
				addr = mmap(nullptr , m_size , PROT_READ| PROT_WRITE , MAP_SHARED , fd , 0);
				if(addr == MAP_FAILED) {
					failure_setup();
				}
			}else if (HGT == _2_MB_HUGE_PAGE){
				addr = mmap(nullptr , m_size , PROT_READ| PROT_WRITE , MAP_SHARED | MAP_HUGETLB| MAP_HUGE_2MB , fd , 0);
				if(addr == MAP_FAILED) {
					failure_setup();
				}
			}else if(HGT == _1_GB_HUGE_PAGE){
				addr = mmap(nullptr , m_size , PROT_READ| PROT_WRITE , MAP_SHARED| MAP_HUGETLB| MAP_HUGE_1GB , fd , 0);
				if(addr == MAP_FAILED) {
					failure_setup();
				}
				
			}else {
				failure_setup();
			}
			ret = mlock(addr , m_size);
			if(ret == -1 ) {
				failure_setup();
			}
			ret =madvise(addr , m_size , MADV_RANDOM | MADV_WILLNEED);

			m_payload = reinterpret_cast<Payload<Key, Value>*>(addr);
			
			
	}else if constexpr(apex_type::writer == AT){
			int ret ;
			m_size = get_aligned_size<HGT>((sz*CB * sizeof(Payload<Key, Value>)));
			int fd = shm_open(m_shared_mem_name, O_RDWR | O_CREAT , S_IRWXU );
			if(fd == -1 ) {
				failure_setup();	
			}
			ret =ftruncate(fd , static_cast<off_t>(m_size));
			if(ret == -1 ) {
				failure_setup();
			}
			void* addr  = nullptr;
			if(HGT == _4_KB_PAGE){
				addr = mmap(nullptr , m_size , PROT_READ| PROT_WRITE , MAP_SHARED , fd , 0);
				if(addr == MAP_FAILED) {
					failure_setup();
				}
			}else if (HGT == _2_MB_HUGE_PAGE){
				addr = mmap(nullptr , m_size , PROT_READ| PROT_WRITE , MAP_SHARED | MAP_HUGETLB| MAP_HUGE_2MB , fd , 0);
				if(addr == MAP_FAILED) {
					failure_setup();
				}
			}else if(HGT == _1_GB_HUGE_PAGE){
				addr = mmap(nullptr , m_size , PROT_READ| PROT_WRITE , MAP_SHARED| MAP_HUGETLB| MAP_HUGE_1GB , fd , 0);
				if(addr == MAP_FAILED) {
					failure_setup();
				}
				
			}else {
				failure_setup();
			}
			ret = mlock(addr , m_size);
			if(ret == -1 ) {
				failure_setup();
			}
			ret =madvise(addr , m_size , MADV_RANDOM | MADV_WILLNEED);

			m_payload = reinterpret_cast<Payload<Key, Value>*>(addr);
	}else {
		static_assert(false  , "Dont know the type" );
	}
}


template <typename Key , typename Value ,  apex_type AT , typename Hash ,size_t HGT, 
	 size_t CB > 
requires Apex_check<Key,Hash , Value >
int Apex_map<Key ,  Value,  AT,  Hash , HGT , CB>::write(Key key , Value val) noexcept {
	static_assert(sizeof(Value) <= 8, "Size of value must be less than or equal to 8 bytes unless pass by pointer " );
	auto idx = m_hash(key) % m_no;
	Payload<Key, Value>* payload_ptr = reinterpret_cast<Payload<Key, Value>*>((&(reinterpret_cast<Bucket<Key,Value , CB>*>(m_payload)[idx])));

	// prefetching using non temporal streaming 
	for(size_t i {0uz} ;i < CB ; i++){
	// preetcjhing using non temporaal streaming
		Key ky = payload_ptr[i].m_id.load(std::memory_order_relaxed);
		auto msk = payload_ptr[i].m_mask.load(std::memory_order_acquire);
		if(key == ky){
			payload_ptr[i].seq.fetch_add(1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			payload_ptr[i].m_data = val ;
			payload_ptr[i].seq.fetch_add(1, std::memory_order_release);
			goto proper_finish;	
		}
		if(msk != std::to_underlying(payload_mask::FILLED)){
			/* new write create */
			payload_ptr[i].seq.fetch_add(1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			payload_ptr[i].m_mask.store(std::to_underlying(payload_mask::FILLED), std::memory_order_relaxed);
			payload_ptr[i].m_id.store(key, std::memory_order_relaxed);
			payload_ptr[i].m_data = val ;
			payload_ptr[i].seq.fetch_add(1, std::memory_order_release);
			goto proper_finish;	
		}
	}
	return -1;
proper_finish : 
		return 0;
}



template <typename Key , typename Value ,  apex_type AT , typename Hash ,size_t HGT, 
	 size_t CB > 
requires Apex_check<Key,Hash , Value >
int Apex_map<Key ,  Value,  AT,  Hash , HGT , CB>::write(Key key , Value* val) noexcept {
	auto idx = m_hash(key) %m_no ;
	Payload<Key, Value>* payload_ptr = reinterpret_cast<Payload<Key, Value>*>((&(reinterpret_cast<Bucket<Key,Value, CB>*>(m_payload)[idx])));
	// prefetching using non temporal streaming 
	for(size_t i {0uz} ;i < CB ; i++){
	// preetcjhing using non temporaal streaming
		Key ky = payload_ptr[i].m_id.load(std::memory_order_relaxed);
		auto msk = payload_ptr[i].m_mask.load(std::memory_order_acquire);
		if(key == ky){
			payload_ptr[i].seq.fetch_add(1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			std::memcpy(static_cast<void*>(&(payload_ptr[i].m_data)) , static_cast<void*>(val) , sizeof(Value) );
			payload_ptr[i].seq.fetch_add(1, std::memory_order_release);
			goto proper_finish;	
		}
		if(msk != std::to_underlying(payload_mask::FILLED)){
			/* new write create */
			payload_ptr[i].seq.fetch_add(1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			payload_ptr[i].m_id.store(key, std::memory_order_relaxed);
			payload_ptr[i].m_mask.store( std::to_underlying(payload_mask::FILLED), std::memory_order_relaxed);
			std::memcpy(static_cast<void*>(&(payload_ptr[i].m_data)) , static_cast<void*>(val) , sizeof(Value) );
			payload_ptr[i].seq.fetch_add(1, std::memory_order_release);
			goto proper_finish;	
		}
	}
	return -1;


proper_finish :
	return 0;
}

template <typename Key , typename Value ,  apex_type AT , typename Hash ,size_t HGT, 
	 size_t CB > 
requires Apex_check<Key,Hash  , Value>
int Apex_map<Key ,  Value,  AT,  Hash , HGT , CB>::read(const Key key , Value* output_val)  noexcept {
	Key id ;	
	const auto idx = m_hash(key) % m_no;
	Payload<Key, Value>* payload_ptr = reinterpret_cast<Payload<Key, Value>*>(&(reinterpret_cast<Bucket<Key , Value , CB>*>(m_payload)[idx]));
	// preeftch and utilize streaming 
	for(size_t i{0}; i < CB ; i++){
	// prefect h and utilize streaming
	 	id  = payload_ptr[i].m_id.load(std::memory_order_acquire);
	 	if(id == key){
			size_t j =BACKOFF ;
			while(j--){
				auto seq1 = payload_ptr[i].seq.load(std::memory_order_acquire);
				if(seq1 & 0x01){
					#if defined (__x86_64__)
					_mm_pause();
					#elif defined(__aarch64__)
    					__asm__ volatile("yield" ::: "memory");
					#endif
					continue;	
				}
				std::memcpy(static_cast<void*>(output_val), static_cast<void*>(&(payload_ptr[i].m_data)) , sizeof(Value));
				std::atomic_thread_fence(std::memory_order_acquire);
				auto seq2 = payload_ptr[i].seq.load(std::memory_order_relaxed);	
				if(seq1 != seq2){
					#if defined (__x86_64__)
					_mm_pause();
					#elif defined(__aarch64__)
    					__asm__ volatile("yield" ::: "memory");
					#endif
					continue;	
				}
				goto real_end;		
			}
		return APEX_TIMEOUT;	
	 	}	

		if(payload_ptr[i].m_mask.load(std::memory_order_relaxed) != std::to_underlying(payload_mask::FILLED)){
			return APEX_ENO_VALUE ; /* nothing filled in array yet*/
	 	}
	}
	return -1;/* Key not found */
real_end: 
	return 0;
}


template <typename Key , typename Value ,  apex_type AT , typename Hash ,size_t HGT, 
	 size_t CB > 
requires Apex_check<Key,Hash ,Value  >
template <typename U , typename Func >
requires std::is_same_v<std::invoke_result_t<Func, Payload<Key, Value>*>, void* >
int Apex_map<Key ,  Value,  AT,  Hash , HGT , CB>::read_sub_value(const Key key ,Func func  , U* output_val)  noexcept{	
	const auto idx = m_hash(key) % m_no;
	Payload<Key, Value>* payload_ptr = reinterpret_cast<Payload<Key, Value>*>(&(reinterpret_cast<Bucket<Key , Value , CB>*>(m_payload)[idx]));
	Key id ;
	// preeftch and utilize streaming
	for(auto i{0uz} ; i < CB ; i++){
		id = payload_ptr[i].m_id.load(std::memory_order_acquire);
		if(key == id){
			auto j = BACKOFF;
			while(j--){
				auto seq1 = payload_ptr[i].seq.load(std::memory_order_acquire);
				if(seq1 & 0x01) {	
					#if defined (__x86_64__)
					_mm_pause();
					#elif defined(__aarch64__)
    					__asm__ volatile("yield" ::: "memory");
					#endif
					continue; ;
				}

				std::memcpy(static_cast<void*>(output_val), func(&(payload_ptr[i])) , sizeof(U));
				std::atomic_thread_fence(std::memory_order_acquire);
				auto seq2 = payload_ptr[i].seq.load(std::memory_order_relaxed);
				if(seq1 != seq2){
					#if defined (__x86_64__)
					_mm_pause();
					#elif defined(__aarch64__)
    					__asm__ volatile("yield" ::: "memory");
					#endif
					continue; ;
				}
			goto real_end;	
			}
			return APEX_TIMEOUT;
		}
		if(payload_ptr[i].m_mask.load(std::memory_order_relaxed) != std::to_underlying(payload_mask::FILLED)){
			return APEX_ENO_VALUE ; /* nothing filled in array yet*/
	 	}
	}
	return APEX_ENORM ;
real_end :
	return 0 ;
}



template <typename Key , typename Value ,  apex_type AT , typename Hash ,size_t HGT, 
	 size_t CB > 
requires Apex_check<Key,Hash ,Value  >
Apex_map<Key ,  Value,  AT,  Hash , HGT , CB>::~Apex_map(){
	munlock(static_cast<void*>(m_payload) , m_size);
	munmap(static_cast<void*>(m_payload), m_size);
	shm_unlink(m_shared_mem_name);
	close(openfd);

}

/* Testing */
struct toy{
	size_t data ; 
	size_t count ; 
	size_t food;
};
/* ask if i kill writer reader will die too*/

/* Hunt down uncessufukl bug inn reader and writer  some of the writes are failign */ 
int main(){
#ifdef WRITER
	Apex_map<int,toy,apex_type::writer> my_hashmap{6000};
	int ret; 
	for(size_t i = 0 ; i < 100 ; i++){
		toy tt{i* 7 , i * 99 , 100-i};
		ret = my_hashmap.write(static_cast<int>(i) ,&tt );
		if(ret < 0 ) std::printf("unsuccesful ");
		if(ret == 0 ) std::printf("wrote %lu " , i);
	}
	std::printf("\n");
#endif 
#ifdef READER 
	Apex_map<int,toy,apex_type::reader> my_hashmap{6000};
	alignas(32) toy dt ;
	int ret ;
	for(size_t i = 0 ; i < 100 ; i++){
		ret = my_hashmap.read(static_cast<int>(i) , &dt);
		if(ret == -1) {
			std::printf("failure");
		}
		if(ret == 0 ){
			std::printf("dta\n");
		std::printf("data : %lu count : %lu food : %lu\n" , dt.data , dt.count , dt.food);}
	}
#endif 
	while(true){}


	return 0;
}
