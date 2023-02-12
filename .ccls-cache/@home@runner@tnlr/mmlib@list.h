#ifndef LIST_H
#define LIST_H 1

#include <stddef.h>

//Stolen idea from Linux kernel code. By far this is 
//the most elegant implementation of linked lists I 
//have ever seen. In fact, I think it is the perfect 
//implementation in C. (In C++, objects can inherit 
//from list_head and get slightly better type checking,
//at the cost of whatever RTTI is needed to make it work)

typedef struct list_head{
    struct list_head *prev, *next;
} list_head;

#define LIST_HEAD_INIT(name) {&(name), &(name)}

//I always get a little freaked out when making inline 
//functions, because they can lead to very bizarre
//behaviour at link time
static inline void init_list_head(list_head *node) {
    node->prev = node;
    node->next = node;
}

static inline void list_add(list_head *before, list_head *node) {
    list_head *after = before->next;
    before->next = node;
    node->prev = before;
    node->next = after;
    after->prev = node;
}

static inline void list_add_before(list_head *after, list_head *node) {
    list_head *before = after->prev;
    before->next = node;
    node->prev = before;
    node->next = after;
    after->prev = node;
}

static inline void list_del(list_head *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

#define container_of(node, type, member) \
    (type*)(((void*)(node)) - offsetof(type,member))

#define list_empty(node) ((node)->next == (node))

#endif
