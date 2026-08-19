// Build "script" for the project using nob.h
// Run "cc -o nob nob.c" using the available compiler,
// then run "nob" executable to rebuild.

#define NOB_IMPLEMENTATION
// #define NOB_EXPERIMENTAL_DELETE_OLD -- Enable if works on windows
#define BUILD_DIR "build/"

#include "thirdparty/nob.h"
#include <string.h>

#ifdef _WIN32
#define EXT ".exe"
#else
#define EXT ""
#endif /* win32 */

#define RUN do {                                    \
    if (cmd.count) {                                \
        if (!nob_cmd_run(&cmd)) return 1;           \
        cmd = (Nob_Cmd){0};                         \
    } else {                                        \
        nob_log(NOB_INFO, "No commands to run.");   \
    }                                               \
    if (singleTarget) goto end;                     \
} while(0)

#define IMGUI "thirdparty/imgui/"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    nob_mkdir_if_not_exists(BUILD_DIR);
    Nob_Cmd cmd = {0};
    int a = 0;
    bool singleTarget = false;
    if(argc > 1) {
        a = argv[1][0] - '0';
        singleTarget = true;
        switch(a) {
        case 0: goto zero;
        case 1: goto one;
        }
    }

////// building main.cpp
zero:;
#ifdef _WIN32
    // do nothing for now
#else
    nob_cmd_append(&cmd, "c++", "-std=c++17", "-Wall", "-Wextra", "-o", BUILD_DIR"main");
    nob_cmd_append(&cmd, "src/main.cpp");
    nob_cmd_append(&cmd, "-ggdb");
#endif /* win32 */
    RUN;
////// finished building

////// building gui
one:;
    char* sources[] = { IMGUI"imgui.cpp"
                      , IMGUI"imgui_demo.cpp"
                      , IMGUI"imgui_draw.cpp"
                      , IMGUI"imgui_tables.cpp"
                      , IMGUI"imgui_widgets.cpp"
                      , IMGUI"backends/imgui_impl_sdl2.cpp"
                      , IMGUI"backends/imgui_impl_sdlrenderer2.cpp"
                      , "src/gui.cpp"
                      };
#ifdef _WIN32
    // nothing
#elif __APPLE__
    nob_cmd_append(&cmd, "c++", "-std=c++17", "-Wall", "-Wextra", "-o", BUILD_DIR"gui");
    for(int i=0; i<sizeof(sources)/sizeof(char*); i++) {
        nob_cmd_append(&cmd, sources[i]);
    }
    nob_cmd_append(&cmd, "-ggdb");
    nob_cmd_append(&cmd, "-Ithirdparty/imgui");
    nob_cmd_append(&cmd, "-Ithirdparty/imgui/backends/");
    // hardcoded sdl2 flags
    nob_cmd_append(&cmd, "-I/opt/homebrew/include/SDL2");
    nob_cmd_append(&cmd, "-D_THREAD_SAFE");
    nob_cmd_append(&cmd, "-L/opt/homebrew/lib");
    nob_cmd_append(&cmd, "-lSDL2main");
    nob_cmd_append(&cmd, "-lSDL2");
    nob_cmd_append(&cmd, "-Wl,-framework,Cocoa");
    // flags end
#else
    printf("linux peenux");
#endif
    RUN;
////// finished building

end:;
    return 0;
}
