

#ifndef APEX_HASH_ERR 

#define APEX_HASH_ERR 
#include <cstdio>
#include <cstdlib>
/* This is a normal error */
constexpr int APEX_ENORM = -1;


__attribute__((noinline , cold)) void failure_setup(){
	std::fprintf(stderr ,"Failed to initialize hash map");
	exit(1);
	
}
#endif
