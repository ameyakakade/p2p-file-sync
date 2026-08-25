#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>

struct Sp {
    const char* string;
    int loc;
};

char gc(Sp* sp) {
    return sp->string[sp->loc];
}

Sp createSp(const char* s) {
    return {s, 0};
}

void printSp(Sp* sp) {
    const char* sn = sp->string + sp->loc;
    printf("%s", sn);
}

void eatSpaces(Sp* sp) {
    while(gc(sp) == ' ' && gc(sp) != '\0') {
        sp->loc = sp->loc + 1;
    }
}

void charParser(Sp* sp, char c) {
    eatSpaces(sp);
    if(gc(sp) == c && gc(sp) != '\0') {
        sp->loc = sp->loc + 1;
    }
}

void keywordParser(Sp* sp, const char* keyword) {
    eatSpaces(sp);
    Sp kp = createSp(keyword);
    while(gc(sp) == gc(&kp) && gc(sp) != '\0' && gc(&kp) != '\0') {
        sp->loc = sp->loc + 1;
        kp.loc = kp.loc + 1;
    }
}

uint64_t natParser(Sp* sp) {
    eatSpaces(sp);
    uint64_t a = 0;
    while('0' <= gc(sp) && gc(sp) <= '9' && gc(sp) != '\0') {
        a *= 10;
        a += gc(sp) - '0';
        sp->loc = sp->loc + 1;
    }
    return a;
}

std::string stringParser(Sp* sp) {
    eatSpaces(sp);
    charParser(sp, '"');
    std::string a;
    while(gc(sp) != '"') {
        a += gc(sp);
        sp->loc = sp->loc + 1;
    }
    charParser(sp, '"');
    return a;
}

bool boolParser(Sp* sp) {
    eatSpaces(sp);
    std::string a;
    while('a' <= gc(sp) && gc(sp) <= 'z' && gc(sp) != '\0') {
        a += gc(sp);
        sp->loc = sp->loc + 1;
    }
    if(a == "false") return false;
    if(a == "true") return true;
    assert(false);
}

#define FIELD_PARSE(sp, keyword, parser)        \
    keywordParser(sp, keyword);                 \
    charParser(sp, ':');                        \
    parser                                      \
    charParser(sp, ',');

#define ARRAY_PARSE(sp, parser)                 \
    charParser(sp, '[');                        \
    while(gc(sp) != ']') {                      \
        parser                                  \
            charParser(sp, ',');                \
    }                                           \
    charParser(sp, ']');

#define OBJECT_PARSE(sp, parser)                \
    eatSpaces(sp);                              \
    charParser(sp, '{');                        \
    parser                                      \
    charParser(sp, '}');
