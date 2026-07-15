// Build "script" for the project using nob.h
// Run "cc -o nob nob.c" using the available compiler,
// then run "nob" executable to rebuild.

#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"

#define BUILD_DIR "build/"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    nob_mkdir_if_not_exists(BUILD_DIR);
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "c++", "-Wall", "-Wextra", "-o", BUILD_DIR"main");
    nob_cmd_append(&cmd, "src/main.cpp");
    if (!nob_cmd_run(&cmd)) return 1;
    return 0;
}
