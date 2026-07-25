#include <iostream>

#define HELP "Usage: %s <server|client>\n", argc[0]

int main(int argv, char** argc) {
    bool server; // True when in server mode

    // This command argument line parsing is temporary and is supposed
    // to be used while developing the program. It will be replaced
    // when the internals of the program are figured out.

    std::string i;
    if(argv <= 1) {
        printf(HELP);
        return 1;
    } else {
        i = argc[1];
    }

    if (i == "server") {
        printf("Running in server mode");
        server = true;
    } else if (i == "client") {
        printf("Running in client mode");
        server = false;
    } else {
        printf("Unknown argument '%s'\n", argc[1]);
        printf(HELP);
        return 1;
    }
}
