#include <stdio.h>
#include <filesystem>
#include <iostream>

const std::string tempDirs[]  = {"dir1", "dir2", "dir3"};
const std::string tempDirs2[] = {"nd1" , "nd2" , "nd3"};

void traverseDirectory(std::filesystem::path directoryPath, int depth) {
    for(auto dir : std::filesystem::directory_iterator(directoryPath)) {
        for(int i=0; i<depth*4; i++) putchar(' ');
        printf("'%s'\n", dir.path().c_str());
        if(dir.is_directory()) {
            traverseDirectory(dir.path(), depth+1);
        }
    }
}

int main(int argv, char** argc) {
    bool removeTempDir  = false;
    bool createTempDirs = true;
    if(argv > 1) {
        std::string i = argc[1];
        if(i == "rmd") removeTempDir = true;
    }

    const std::filesystem::path tempDir{"temp"};
    std::filesystem::create_directory(tempDir); // may error

    if(createTempDirs) {
        printf("Creating temporary directories.\n");
        for(auto it : tempDirs) {
            for(auto it2 : tempDirs2) {
                std::filesystem::create_directories(tempDir/it/it2); // may error
            }
        }
    }

    printf("Traversing directory tree\n");
    traverseDirectory(tempDir, 0);

    if(removeTempDir) {
        printf("Deleting temporary directories.\n");
        std::filesystem::remove_all(tempDir);
    }
    return 0;
}
