#ifdef MM_IMPLEMENT
	#ifndef VECTOR_H_IMPLEMENTED
		#define SHOULD_INCLUDE 1
		#define VECTOR_H_IMPLEMENTED 1
	#else
		#define SHOULD_INCLUDE 0
	#endif
#else
	#ifndef VECTOR_H
		#define SHOULD_INCLUDE 1
		#define VECTOR_H 1
	#else 
		#define SHOULD_INCLUDE 0
	#endif
#endif


#if SHOULD_INCLUDE
#undef SHOULD_INCLUDE


#ifdef MM_IMPLEMENT
#undef MM_IMPLEMENT
#include "vector.h"
#define MM_IMPLEMENT 1
#endif

#include <stdlib.h>
#include "fast_fail.h"


#ifdef MM_IMPLEMENT
#define VECTOR_INIT_SZ 16
#endif

#ifndef MM_IMPLEMENT

//This is meant to be used in struct declarations
#define VECTOR_DECL(type, name) \
    unsigned name##_len;        \
    unsigned name##_cap;        \
    type *name

//You should probably default to using this when just
//declaring vectors in code
#define VECTOR_INIT_DECL(type, name)     \
    unsigned name##_len = 0;             \
    unsigned name##_cap = 0;             \
    type *name = NULL


//TODO: there is no easy way to have a vector of vectors
//Currently, the only option is to define a struct that
//has a VECTOR_DECL in it, then make a vector of that
//type of struct

//Not really sure if this macro is necessary
#define VECTOR_LENGTH(v) (v##_len)

#define vector_extend_if_full(v)                                 \
    do {                                                         \
        if((v##_len) == (v##_cap)) {                             \
            vector_extend(sizeof(*v), &(v##_cap), (void**)&(v)); \
        }                                                        \
    } while(0)

#define vector_reserve(v, n) \
    __vector_reserve(sizeof(*(v)),&(v##_len),&(v##_cap),(void**)(&(v)),n)

//Do not pass a pointer to a vector. Just give the l-value.
#define vector_init(v) __vector_init(sizeof(*(v)), &(v##_len), &(v##_cap), (void**)&(v))

#define vector_clear(v) (v##_len) = 0;

//There are two ways to append an element to a vector:
//1. Call vector_lengthen, which gives you a pointer to
//   the new element. You can then do whatever you want
//   with this pointer.
//2. Use vector_push, which copies a given item to the
//   the end of the vector using the C assignment operator

//Extends vector length by one (resizing, if necessary) then 
//returns a pointer to the new free element.
#define vector_lengthen(v) \
    __vector_lengthen(sizeof(*(v)),&(v##_len),&(v##_cap),(void**)(&(v)))

#define vector_push(v, x)         \
    do {                          \
        vector_extend_if_full(v); \
        (v)[(v##_len)++] = x;     \
    } while (0)

#define vector_push_n(v, p, n)                    \
    do {                                          \
	vector_reserve(v, v##_len + n);           \
	memcpy(v + v##_len, p, (n)*sizeof(*(p))); \
	v##_len += n;                             \
    } while (0)

#define vector_pop(v) \
    do {              \
        (v##_len)--;  \
    } while (0)

//Pointer to last (filled) element
#define vector_back_ptr(v) ((v) + (v##_len) - 1)

//There was no cleaner way
//To pass a vector (by reference) to a function, use
//VECTOR_PTR_PARAM in the function argument list declaration,
//and use VECTOR_ARG at the call site.
//Example:
//
//void push_seven(VECTOR_PTR_PARAM(int, v)) {
//  vector_push(*v, 7); //Remember: v is a pointer to a vector
//}
//
//void main() {
//  VECTOR_DECL(int, my_vec);
//  vector_init(my_vec);
//
//  push_seven(VECTOR_ARG(my_vec));
//
//  printf("%d\n", my_vec[0]);
//  vector_free(my_vec);
//}
#define VECTOR_PTR_PARAM(type, v) unsigned *v##_len, unsigned *v##_cap, type **v
#define VECTOR_ARG(v) &(v##_len), &(v##_cap), &(v)

//Unlike the C++ assignment operator, DOES NOT desctruct the lhs
#define VECTOR_ASSIGN(lhs, rhs) \
    do {                        \
        lhs##_len = rhs##_len;  \
        lhs##_cap = rhs##_cap;  \
        lhs = rhs;              \
    } while (0)

#define vector_shrink_to_fit(v) \
    __vector_shrink_to_fit(sizeof(*v), &(v##_len), &(v##_cap), (void**)&(v))

#endif



void vector_extend(unsigned elem_sz, unsigned *cap, void **data) 
#ifdef MM_IMPLEMENT
{
    *data = realloc(*data, *cap*2*elem_sz);
    if (!*data) FAST_FAIL("out of memory");
    *cap *= 2;
}
#else
;
#endif


//Ensure the vector capacity is at least equal to the given size
void __vector_reserve(unsigned elem_sz, unsigned *len, unsigned *cap, void **data, unsigned n) 
#ifdef MM_IMPLEMENT
{
	if (*len + n >= *cap) {
		*data = realloc(*data, n*elem_sz);
		if (!*data) FAST_FAIL("out of memory");
		*cap = n;
	}
}
#else
;
#endif

void __vector_init(unsigned elem_sz, unsigned *len, unsigned *cap, void **data) 
#ifdef MM_IMPLEMENT
{
    *data = malloc(VECTOR_INIT_SZ*elem_sz);
    if (!*data) FAST_FAIL("out of memory");
    *len = 0;
    *cap = VECTOR_INIT_SZ;
}
#else
;
#endif

//Could just call free directly, but this is more consistent
void vector_free(void *v) 
#ifdef MM_IMPLEMENT
{
    free(v);
}
#else
;
#endif

void* __vector_lengthen(unsigned elem_sz, unsigned *len, unsigned *cap, void **data) 
#ifdef MM_IMPLEMENT
{
    if (*len == *cap) vector_extend(elem_sz, cap, data);

    return *data + elem_sz * (*len)++;
}
#else
;
#endif

void __vector_shrink_to_fit(unsigned elem_sz, unsigned *len, unsigned *cap, void **v) 
#ifdef MM_IMPLEMENT
{
    void *ret = realloc(*v, *len*elem_sz);
    if (!ret) FAST_FAIL("reallocation failed");
    *v = ret;
    *cap = *len;
}
#else
;
#endif



#endif

