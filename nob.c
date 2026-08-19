// Build "script" for the project using nob.h
// Run "cc -o nob nob.c" using the available compiler,
// then run "nob" executable to rebuild.

#define NOB_IMPLEMENTATION
// #define NOB_EXPERIMENTAL_DELETE_OLD -- Enable if works on windows
#define BUILD_DIR "build/"

#include "thirdparty/nob.h"
#include <string.h>

#define RUN do {                                                        \
        if (cmd.count) {                                                \
            if (!nob_cmd_run(&cmd)) {                                   \
                nob_log(NOB_ERROR, "Failed to run command");            \
                ret = 1;                                                \
            }                                                           \
            cmd = (Nob_Cmd){0};                                         \
        } else {                                                        \
            nob_log(NOB_INFO, "No commands to run.");                   \
        }                                                               \
    } while(0)

#ifdef _WIN32
#define EXT ".exe"
#else
#define EXT ""
#endif /* win32 */

int main(int argc, char **argv)
{
    bool autorun = false;
    bool rebuild = false;
    int ret = 0;
    for (int i=0; i<argc; i++) {
        if (!strcmp(argv[i], "run")) autorun = true;
        if (!strcmp(argv[i], "rebuild")) rebuild = true;
    }

    NOB_GO_REBUILD_URSELF(argc, argv);
    nob_mkdir_if_not_exists(BUILD_DIR);
    Nob_Cmd cmd = {0};

    // building main.cpp
    if (nob_needs_rebuild1(BUILD_DIR"main"EXT, "src/main.cpp") || rebuild) {
#ifdef _WIN32
        // do nothing for now
#else
        nob_cmd_append(&cmd, "c++", "-Wall", "-Wextra", "-o", BUILD_DIR"main");
        nob_cmd_append(&cmd, "src/main.cpp");
        nob_cmd_append(&cmd, "-Ithirdparty/openssl-4.0.1/include/");
        nob_cmd_append(&cmd, "thirdparty/openssl-4.0.1/libssl.a");
        nob_cmd_append(&cmd, "thirdparty/openssl-4.0.1/libcrypto.a");
#endif /* win32 */
    }

    RUN;

    //building server
    if (nob_needs_rebuild1(BUILD_DIR"server"EXT, "server/main.cpp") || rebuild) {
#ifdef _WIN32
        nob_cmd_append(&cmd, "cl", "-o", BUILD_DIR"server");
        nob_cmd_append(&cmd, "server/main.cpp");
        #else
        nob_cmd_append(&cmd, "c++", "-Wall", "-Wextra", "-o", BUILD_DIR"server");
        nob_cmd_append(&cmd, "server/main.cpp");
        #endif /* win32 */
    }

    RUN;

    //building client
    if (nob_needs_rebuild1(BUILD_DIR"client"EXT, "client/main.cpp") || rebuild) {
        #ifdef _WIN32
        nob_cmd_append(&cmd, "cl", "-o", BUILD_DIR"client");
        nob_cmd_append(&cmd, "client/main.cpp");
        #else
        nob_cmd_append(&cmd, "c++", "-Wall", "-Wextra", "-o", BUILD_DIR"client");
        nob_cmd_append(&cmd, "client/main.cpp");
        #endif /* win32 */
    }

    RUN;

    if (autorun) {
        cmd = (Nob_Cmd){0};
        nob_cmd_append(&cmd, "./build/main");
        if (!nob_cmd_run(&cmd)) return 1;
    }
 
    return ret;
}
