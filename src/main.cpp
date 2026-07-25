#include <iostream>

int main() {
    bool shouldQuit = false;
    while(!shouldQuit) {
        printf("# ");
        std::string inp;
        std::cin >> inp;
        if (inp == "quit") {
            shouldQuit = true;
        }
    }
}
