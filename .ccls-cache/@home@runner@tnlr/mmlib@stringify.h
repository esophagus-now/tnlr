#ifndef STRINGIFY_H
#define STRINGIFY_H 1

//Because of the bizarre C preprocessing rules, if
//you want to expand a macro before stringifying,
//you need to do this:

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x) //<- Use this one

#endif

