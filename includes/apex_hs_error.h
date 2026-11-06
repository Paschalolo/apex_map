

#ifndef APEX_HASH_ERR 

#define APEX_HASH_ERR 
#include <cstdio>
#include <cstdlib>
/* This is a normal error */
constexpr int APEX_ENORM = -1;
constexpr int APEX_ENO_VALUE = -2 ; /* Error expresses nothing written to the key */ 
constexpr int APEX_TIMEOUT = -3;
__attribute__((noinline , cold)) void failure_setup(){
	std::fprintf(stderr ,"Failed to initialize hash map");
	exit(1);
	
}
#endif
