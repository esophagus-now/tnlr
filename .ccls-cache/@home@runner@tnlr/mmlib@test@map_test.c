/*
Try to exercise all the advertised features of map.h

gcc -O0 -Wall -g -o map_test map_test.c -I../ &&
valgrind \
  --leak-check=full --track-origins=yes --show-leak-kinds=all \
  ./map_test 2>&1 | tee report.txt
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#define MM_IMPLEMENT 1
#include "../map.h"
#include "../vector.h"

typedef struct _some_struct {
    char name[32];
    float x,y,z;
    struct _some_struct *prev, *next;
} some_struct;

typedef struct {
    char *k;
    char *v;
} str2str;

typedef struct {
    uint32_t k;
    char *v;
} int2str;

typedef struct {
    char *k;
    uint32_t v;
} str2int;

char *mk_rand_str(int len) {
    char *ret = malloc(len+1);
    for (int i = 0; i < len; i++) {
        char r;
        do r = rand() & 0xFF; while (!isprint(r));
        ret[i] = r;
    }

    ret[len] = 0;

    return ret;
}

//This is slow as molasses
int str2str_equality(map *m, VECTOR_PTR_PARAM(str2str, golden)) {
    map_assert_type(m, char*, char*, STR2STR);
    
    //Check that all entries in the golden vector match the map
    for (int i = 0; i < *golden_len; i++) {
        char *k = (*golden)[i].k;
        char *v = (*golden)[i].v;

        //First search using the key
        char *found = map_search(m, k);
        if (found == NULL) {
            printf("ERROR: searched for [%s] but it was not found\n", k);
            return -1;
        } else if (strcmp(found,v)) {
            printf("ERROR: searched for [%s] but the data was wrong\n", k);
            printf("-> Expected [%s]\n", v);
            printf("-> Got      [%s]\n", found);
            return -1;
        }
    }
    
    //Check that all entries in the map match the golden vector
    map_iter cur = map_begin(m);
    while (cur != map_end(m)) {
        char *k, *v;
        map_iter_deref(m, cur, &k, &v);
        //Dumbass linear search in golden array
        char *found = NULL;
        for (int i = 0; i < *golden_len; i++) {
            if (!strcmp((*golden)[i].k, k)) {
                found = (*golden)[i].v;
                break;
            }
        }
        if (!found) {
            printf("ERROR: map contains key [%s] that is not in golden array\n", k);
            return -1;
        } else if (strcmp(found, v)) {
            printf("ERROR: map contains incorrect mapping for [%s]\n", k);
            printf("-> Expected [%s]\n", found);
            printf("-> Got      [%s]\n", v);
            return -1;
        }
        map_iter_step(cur);
    }

    return 0;
}

//This is slow as molasses
int int2str_equality(map *m, VECTOR_PTR_PARAM(int2str, golden)) {
    map_assert_type(m, uint32_t, char*, VAL2STR);
    
    //Check that all entries in the golden vector match the map
    for (int i = 0; i < *golden_len; i++) {
        uint32_t k = (*golden)[i].k;
        char *v = (*golden)[i].v;

        //First search using the key
        char *found = map_search(m, &k);
        if (found == NULL) {
            printf("ERROR: searched for [%#08x] but it was not found\n", k);
            return -1;
        } else if (strcmp(found,v)) {
            printf("ERROR: searched for [%#08x] but the data was wrong\n", k);
            printf("-> Expected [%s]\n", v);
            printf("-> Got      [%s]\n", found);
            return -1;
        }
    }
    
    //Check that all entries in the map match the golden vector
    map_iter cur = map_begin(m);
    while (cur != map_end(m)) {
        uint32_t k; 
        char *v;
        map_iter_deref(m, cur, &k, &v);
        //Dumbass linear search in golden array
        char *found = NULL;
        for (int i = 0; i < *golden_len; i++) {
            if ((*golden)[i].k == k) {
                found = (*golden)[i].v;
                break;
            }
        }
        if (!found) {
            printf("ERROR: map contains key [%#08x] that is not in golden array\n", k);
            return -1;
        } else if (strcmp(found, v)) {
            printf("ERROR: map contains incorrect mapping for [%#08x]\n", k);
            printf("-> Expected [%s]\n", found);
            printf("-> Got      [%s]\n", v);
            return -1;
        }
        map_iter_step(cur);
    }

    return 0;
}

//This is slow as molasses
int str2int_equality(map *m, VECTOR_PTR_PARAM(str2int, golden)) {
    map_assert_type(m, char*, uint32_t, STR2VAL);
    
    //Check that all entries in the golden vector match the map
    for (int i = 0; i < *golden_len; i++) {
        char *k = (*golden)[i].k;
        uint32_t v = (*golden)[i].v;

        //First search using the key
        uint32_t *found = map_search(m, k);
        if (found == NULL) {
            printf("ERROR: searched for [%s] but it was not found\n", k);
            return -1;
        } else if (*found != v) {
            printf("ERROR: searched for [%s] but the data was wrong\n", k);
            printf("-> Expected [%#08x]\n", v);
            printf("-> Got      [%#08x]\n", *found);
            return -1;
        }
    }
    
    //Check that all entries in the map match the golden vector
    map_iter cur = map_begin(m);
    while (cur != map_end(m)) {
        char *k;
        uint32_t v;
        map_iter_deref(m, cur, &k, &v);
        //Dumbass linear search in golden array
        uint32_t found_val = 0;
        int found = 0;
        for (int i = 0; i < *golden_len; i++) {
            if (!strcmp((*golden)[i].k, k)) {
                found_val = (*golden)[i].v;
                found = 1;
                break;
            }
        }
        if (!found) {
            printf("ERROR: map contains key [%s] that is not in golden array\n", k);
            return -1;
        } else if (found_val != v) {
            printf("ERROR: map contains incorrect mapping for [%s]\n", k);
            printf("-> Expected [%#08x]\n", found_val);
            printf("-> Got      [%#08x]\n", v);
            return -1;
        }
        map_iter_step(cur);
    }

    return 0;
}

int main() {
    srand(77); //TODO: let seed be overridden by cmd line?
    
    {
        puts("Test 1: string-to-string maps");
        map m;
        map_init(&m, char*, char*, STR2STR);

        VECTOR_DECL(str2str, golden);
        vector_init(golden);

        int const MINOR_ITERS = 128;
        int const MAJOR_ITERS = 64;

        int canary = 0;
        
        for (int i = 0; i < MAJOR_ITERS; i++) {
            //Because we have to use linear search in our correctness
            //checker, things or O(n^2), so for the sake of not needing
            //to wait forever we'll have a higher chance of deleting
            //elements if the map is larger
            for (int j = 0; j < MINOR_ITERS; j++) {
                int tmp = rand()%(golden_len+1); // Prevent divide-by-zero
                if (tmp < 250) {
                    //Insert an element. We try to keep the map
                    //size near 500
                    char *k = mk_rand_str(20);
                    char *v = mk_rand_str(20);
                    //Map will be the owner of the data
                    int rc = map_insert(&m, k, 1, v, 1);
                    assert((rc == 0) && "Something went wrong inserting");
                    str2str *fill_me = vector_lengthen(golden);
                    fill_me->k = k;
                    fill_me->v = v;
                } else {
                    //Delete an element. Impossible to get here
                    //if map is empty, but assert it anyway
                    assert(golden_len > 0);
                    int idx = rand() % golden_len;
                    int rc = map_search_delete(&m, golden[idx].k, golden[idx].v);
                    assert((rc == 0) && "Something went wrong deleting");
                    golden[idx] = golden[golden_len-1];
                    golden_len--;
                }
            }
            canary = str2str_equality(&m, VECTOR_ARG(golden));
            if (canary) {
                printf("Leaving str2str test on the %dth iteration\n", i);
                break;
            }
        }

        vector_free(golden);
        map_free(&m);

        if (canary) {
            puts("STRING-TO-STRING TEST FAILED");
        } else {
            puts("String-to-string test passed");
        }
    }

    {
        puts("Test 2: int-to-string test");
        map m;
        map_init(&m, uint32_t, char*, VAL2STR);
        VECTOR_DECL(int2str, golden);
        vector_init(golden);

        int const MINOR_ITERS = 128;
        int const MAJOR_ITERS = 64;

        int canary = 0;
        
        for (int i = 0; i < MAJOR_ITERS; i++) {
            //Because we have to use linear search in our correctness
            //checker, things or O(n^2), so for the sake of not needing
            //to wait forever we'll have a higher chance of deleting
            //elements if the map is larger
            for (int j = 0; j < MINOR_ITERS; j++) {
                int tmp = rand()%(golden_len+1); // Prevent divide-by-zero
                if (tmp < 250) {
                    //Insert an element. We try to keep the map
                    //size near 500
                    uint32_t k = rand() << 16 | rand();
                    char *v = mk_rand_str(20);
                    //Map will be the owner of the data
                    int rc = map_insert(&m, &k, 0, v, 1);
                    assert((rc == 0) && "Something went wrong inserting");
                    int2str *fill_me = vector_lengthen(golden);
                    fill_me->k = k;
                    fill_me->v = v;
                } else {
                    //Delete an element. Impossible to get here
                    //if map is empty, but assert it anyway
                    assert(golden_len > 0);
                    int idx = rand() % golden_len;
                    int rc = map_search_delete(&m, &golden[idx].k, golden[idx].v);
                    assert((rc == 0) && "Something went wrong deleting");
                    golden[idx] = golden[golden_len-1];
                    golden_len--;
                }
            }
            canary = int2str_equality(&m, VECTOR_ARG(golden));
            if (canary) {
                printf("Leaving int2str test on the %dth iteration\n", i);
                break;
            }
        }

        vector_free(golden);
        map_free(&m);

        if (canary) {
            puts("INT-TO-STRING TEST FAILED");
        } else {
            puts("Int-to-string test passed");
        }
    }

    {
        puts("Test 3: string-to-int maps");
        map m;
        map_init(&m, char*, uint32_t, STR2VAL);

        VECTOR_DECL(str2int, golden);
        vector_init(golden);

        int const MINOR_ITERS = 128;
        int const MAJOR_ITERS = 64;

        int canary = 0;
        
        for (int i = 0; i < MAJOR_ITERS; i++) {
            //Because we have to use linear search in our correctness
            //checker, things or O(n^2), so for the sake of not needing
            //to wait forever we'll have a higher chance of deleting
            //elements if the map is larger
            for (int j = 0; j < MINOR_ITERS; j++) {
                int tmp = rand()%(golden_len+1); // Prevent divide-by-zero
                if (tmp < 250) {
                    //Insert an element. We try to keep the map
                    //size near 500
                    char *k = mk_rand_str(20);
                    uint32_t v = rand() << 16 | rand();
                    //Map will be the owner of the data
                    int rc = map_insert(&m, k, 1, &v, 0);
                    assert((rc == 0) && "Something went wrong inserting");
                    str2int *fill_me = vector_lengthen(golden);
                    fill_me->k = k;
                    fill_me->v = v;
                } else {
                    //Delete an element. Impossible to get here
                    //if map is empty, but assert it anyway
                    assert(golden_len > 0);
                    int idx = rand() % golden_len;
                    int rc = map_search_delete(&m, golden[idx].k, &golden[idx].v);
                    assert((rc == 0) && "Something went wrong deleting");
                    golden[idx] = golden[golden_len-1];
                    golden_len--;
                }
            }
            canary = str2int_equality(&m, VECTOR_ARG(golden));
            if (canary) {
                printf("Leaving str2int test on the %dth iteration\n", i);
                break;
            }
        }

        vector_free(golden);
        map_free(&m);

        if (canary) {
            puts("STRING-TO-INT TEST FAILED");
        } else {
            puts("String-to-int test passed");
        }
    }
}