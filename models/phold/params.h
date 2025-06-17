#define M 10 
#define CHUNKS_IN_LIST (8000)  
#define REALLOCATION (CHUNKS_IN_LIST/1000) 
#define P (CHUNKS_IN_LIST >> 5) 
//#define TA (LOOKAHEAD*1)
#define TA 1.0

#ifndef H_LOAD
#define H_LOAD 2.0
#endif

#ifndef HOTSPOTS
#define HOTSPOTS 0.1
#endif
