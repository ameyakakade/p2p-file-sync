#include <iostream>
#include <vector>
#include <string>
#include <cstdint>

struct Sp {
    const char* string;
    int loc;
};

char gc(Sp* sp) {
    return sp->string[sp->loc];
}

Sp createSp(const char* s) {
    return {.string = s, .loc = 0};
}

void printSp(Sp* sp) {
    const char* sn = sp->string + sp->loc;
    printf(sn);
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
    while(gc(sp) != ',') {
        a += gc(sp);
        sp->loc = sp->loc + 1;
    }
    if(a == "false") return false;
    if(a == "true") return true;
}

#define FIELD_PARSE(sp, keyword, parser)        \
    keywordParser(sp, keyword);                \
    charParser(sp, ':');                       \
    parser                                      \
    charParser(sp, ',');

#define ARRAY_PARSE(sp, parser)                 \
    charParser(sp, '[');                       \
    while(gc(sp) != ']') {                     \
        parser                                  \
        charParser(sp, ',');                   \
    }                                           \
    charParser(sp, ']');

#define OBJECT_PARSE(sp, parser)                \
    eatSpaces(sp);                             \
    charParser(sp, '{');                       \
    parser                                      \
    charParser(sp, '}');

int main() {
    const char* a =
        "    {"
        "        index       : 34,"
        "        nodePath    : \"dir/wow\","
        "        isDirectory : false,"
        "        hash        : 10103411384967783403,"
        "        children    : [2,4,5]"
        "    }"
        "    {"
        "        index       : 8,"
        "        nodePath    : \"dir/wow/this is very cool\","
        "        isDirectory : true,"
        "        hash        : 10777771384967783403,"
        "        children    : []"
        "    }"
        "    {"
        "        index       : 34,"
        "        nodePath    : \"dfdsir/wow\","
        "        isDirectory : false,"
        "        hash        : 10103411384967783403,"
        "        children    : [2,4,5]"
        "    }"
        "    {"
        "        index       : 34,"
        "        nodePath    : \"dir/wow\","
        "        isDirectory : false,"
        "        hash        : 10103411384967783403,"
        "        children    : [2,4,5]"
        "    }";
    Sp sp = createSp(a);
    
    while(gc(&sp) != '\0') {
         OBJECT_PARSE(&sp,
                     FIELD_PARSE(&sp, "index",      std::cout << natParser(&sp) << "\n";)
                     FIELD_PARSE(&sp, "nodePath",   std::cout << stringParser(&sp) << "\n";)
                     FIELD_PARSE(&sp, "isDirectory", std::cout << boolParser(&sp) << "\n";)
                     FIELD_PARSE(&sp, "hash", std::cout << natParser(&sp) << "\n";)
                     FIELD_PARSE(&sp, "children",
                                 ARRAY_PARSE(&sp, std::cout << natParser(&sp) << " ";)
                                 std::cout << "\n";
                         )
            )
             printf("------\n");
    }
    // printSp(&sp);
}
