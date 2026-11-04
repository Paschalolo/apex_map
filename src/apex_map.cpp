
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
#include <emmintrin.h>
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
#include <type_traits>
#include <utility>
/* Concepts */
template <typename K , typename  H > 
concept Apex_check =  requires(H h , K k){
	{h(k)} -> std::same_as<size_t>;
};


/* Definitions*/ 
constexpr auto  HDI_SZ2 = std::hardware_destructive_interference_size * 2 ;
constexpr auto CACHE_LINE = std::hardware_destructive_interference_size ;
constexpr char m_shared_mem_name[] = "apex_hash_map_data";
constexpr size_t _4_KB_PAGE = 4ULL * 1024ULL;
constexpr size_t _2_MB_HUGE_PAGE  = 2ULL * 1024ULL * 1024ULL ;
constexpr size_t _1_GB_HUGE_PAGE= 1ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr size_t BACKOFF = 8ULL ;
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
template <typename Key , typename Value , apex_type  AT  ,typename Hash , 
	 size_t HGT = _4_KB_PAGE /* Default Page size */,
	 size_t CB /*COLLISION BUFFER : This si the amount of of extra payload we can store due to a hash collision
	 */ = 5ULL > 
requires Apex_check<Key,Hash >
class alignas(32) Apex_map{
	public :
		Apex_map(size_t ) noexcept;
		int write(Key key , Value v)noexcept ; 
		int write(Key key , Value* val)noexcept ; 
		int read(const Key key , Value* output_val) const noexcept;
		~Apex_map();
		size_t 	size() const {
			return m_size;
		}
	private : 
		[[no_unique_address]] Hash m_hash{};
		size_t m_size ; 
		int openfd;
		Payload<Key,  Value>* m_payload ;
};
template <typename Key , typename Value ,  apex_type AT , typename Hash ,size_t HGT, 
	 size_t CB > 
requires Apex_check<Key,Hash >
Apex_map<Key , Value , AT , Hash , HGT  , CB>::Apex_map(size_t sz ) noexcept  {
	if constexpr (apex_type::reader == AT) {

			int ret ;
			m_size = get_aligned_size<HGT>((sz*CB));
			int fd = shm_open(m_shared_mem_name, O_RDWR | O_CREAT , S_IRWXU );
			openfd = fd ;
			if(fd == -1 ) {
				failure_setup();	
			}
			ret =ftruncate(fd , m_size);
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
			m_size = get_aligned_size<HGT>((sz*CB));
			int fd = shm_open(m_shared_mem_name, O_RDWR | O_CREAT , S_IRWXU );
			if(fd == -1 ) {
				failure_setup();	
			}
			ret =ftruncate(fd , m_size);
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
requires Apex_check<Key,Hash >
int Apex_map<Key ,  Value,  AT,  Hash , HGT , CB>::write(Key key , Value val) noexcept {
	auto idx = m_hash(key);
	Payload<Key, Value>* payload_ptr = reinterpret_cast<Payload<Key, Value>*>((&(reinterpret_cast<Bucket<Key,Value>*>(m_payload)[idx])));

	// prefetching using non temporal streaming 
	for(size_t i {0uz} ;i < CB ; i++){
	// preetcjhing using non temporaal streaming
		Key ky = payload_ptr[i].m_id.load(std::memory_order_relaxed);
		auto msk = payload_ptr[i].m_mask.load(std::memory_order_relaxed);
		if(key == ky){
			payload_ptr[i].seq.fetch_add(1, std::memory_order_release);
			payload_ptr[i].m_data = val ;
			payload_ptr[i].seq.fetch_add(1, std::memory_order_release);
			goto proper_finish;	
		}
		if(msk != std::to_underlying(payload_mask::FILLED)){
			/* new write create */
			payload_ptr[i].seq.fetch_add(1, std::memory_order_release);
			payload_ptr[i].m_mask.store(std::to_underlying(payload_mask::FILLED), std::memory_order_relaxed);
			payload_ptr[i].m_id.store(key, std::memory_order_relaxed);
			payload_ptr[i].m_data = val ;
			payload_ptr[i].seq.fetch_add(1, std::memory_order_release);
			goto proper_finish;	
		}
		if(payload_ptr[i].m_mask == std::to_underlying(payload_mask::FILLED) && (ky != key)){
			continue;	
		}	
	}
	return -1;
proper_finish : 
		return 0;
}



template <typename Key , typename Value ,  apex_type AT , typename Hash ,size_t HGT, 
	 size_t CB > 
requires Apex_check<Key,Hash >
int Apex_map<Key ,  Value,  AT,  Hash , HGT , CB>::write(Key key , Value* val) noexcept {
	auto idx = m_hash(key);
	Payload<Key, Value>* payload_ptr = reinterpret_cast<Payload<Key, Value>*>((&(reinterpret_cast<Bucket<Key,Value>*>(m_payload)[idx])));
	// prefetching using non temporal streaming 
	for(size_t i {0uz} ;i < CB ; i++){
	// preetcjhing using non temporaal streaming
		Key ky = payload_ptr[i].m_id.load(std::memory_order_relaxed);
		auto msk = payload_ptr[i].m_mask;
		if(key == ky){
			payload_ptr[i].seq.fetch_add(1, std::memory_order_release);
			std::memcpy(static_cast<void*>(&(payload_ptr[i].m_data)) , static_cast<void*>(val) , sizeof(Value) );
			std::atomic_thread_fence(std::memory_order_release);
			payload_ptr[i].seq.fetch_add(1, std::memory_order_relaxed);
			goto proper_finish;	
		}
		if(msk != std::to_underlying(payload_mask::FILLED)){
			/* new write create */
			payload_ptr[i].m_id.store(key, std::memory_order_relaxed);
			payload_ptr[i].seq.fetch_add(1, std::memory_order_release);
			payload_ptr[i].m_mask.store( std::to_underlying(payload_mask::FILLED), std::memory_order_relaxed);
			std::memcpy(static_cast<void*>(&(payload_ptr[i].m_data)) , static_cast<void*>(val) , sizeof(Value) );
			std::atomic_thread_fence(std::memory_order_release);
			payload_ptr[i].seq.fetch_add(1, std::memory_order_relaxed);
			goto proper_finish;	
		}
		if(payload_ptr[i].m_mask == std::to_underlying(payload_mask::FILLED) && (ky != key)){
			continue;	
		}	
	}
	return -1;


proper_finish :
	return 0;
}

template <typename Key , typename Value ,  apex_type AT , typename Hash ,size_t HGT, 
	 size_t CB > 
requires Apex_check<Key,Hash >
int Apex_map<Key ,  Value,  AT,  Hash , HGT , CB>::read(const Key key , Value* output_val) const noexcept {
	Key id ;	
	const auto idx = m_hash(key);
	Payload<Key, Value>* payload_ptr = reinterpret_cast<Payload<Key, Value>*>(&(reinterpret_cast<Bucket<Key , Value>*>(m_payload)[idx]));
	// preeftch and utilize streaming 
	for(size_t i{0}; i < CB ; i++){
	// prefect h and utilize streaming
	 	id  = payload_ptr[i].m_id.load(std::memory_order_acquire);
	 	if(id == key){
			size_t j =BACKOFF ;
			while(j--){
				auto seq1 = payload_ptr[i].seq.load(std::memory_order_acquire);
				if(seq1 & 0x01){
					_mm_pause();
					continue;	
				}
				std::memcpy(static_cast<void*>(output_val), static_cast<void*>(&(payload_ptr[i].m_data)) , sizeof(Value));
				std::atomic_thread_fence(std::memory_order_acquire);
				auto seq2 = payload_ptr[i].seq.load(std::memory_order_relaxed);	
				if(seq1 != seq2){
					_mm_pause();
					continue;	
				}
				break ;
			}
		goto real_end;		
	 	}	
		if(payload_ptr[i].m_mask.load(std::memory_order_relaxed) != std::to_underlying(payload_mask::FILLED)){
			return -2 ; /* nothing filled in array yet*/
	 	}
	}
	return -1;/* Key not found */
real_end: 
	return 0;
}


template <typename Key , typename Value ,  apex_type AT , typename Hash ,size_t HGT, 
	 size_t CB > 
requires Apex_check<Key,Hash >
Apex_map<Key ,  Value,  AT,  Hash , HGT , CB>::~Apex_map(){
	munlock(static_cast<void*>(m_payload) , m_size);
	munmap(static_cast<void*>(m_payload), m_size);
	shm_unlink(m_shared_mem_name);
	close(openfd);

}
int main(){
	std::printf("%lu" , get_aligned_size<64ULL>(129ULL));
	return 0;
}
