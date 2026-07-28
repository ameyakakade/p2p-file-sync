// Build "script" for the project using nob.h
// Run "cc -o nob nob.c" using the available compiler,
// then run "nob" executable to rebuild.

#define NOB_IMPLEMENTATION
// #define NOB_EXPERIMENTAL_DELETE_OLD -- Enable if works on windows
#define BUILD_DIR "build/"

#include "thirdparty/nob.h"
#include <string.h>

int main(int argc, char **argv)
{
    bool autorun;
    for (int i=0; i<argc; i++) {
        if (!strcmp(argv[i], "run")) autorun = true;
    }

    NOB_GO_REBUILD_URSELF(argc, argv);
    nob_mkdir_if_not_exists(BUILD_DIR);
    Nob_Cmd cmd = {0};

    if (nob_needs_rebuild1("build/main", "src/main.cpp")) {
        nob_cmd_append(&cmd, "c++", "-Wall", "-Wextra", "-o", BUILD_DIR"main");
        nob_cmd_append(&cmd, "src/main.cpp");
    }

    if (cmd.count) {
        if (!nob_cmd_run(&cmd)) return 1;
    } else {
        nob_log(NOB_INFO, "Everything up to date.");
    }

    if (autorun) {
        cmd = {0};
        nob_cmd_append(&cmd, "./build/main");
        if (!nob_cmd_run(&cmd)) return 1;
    }
 
    return 0;
}
