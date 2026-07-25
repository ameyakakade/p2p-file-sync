#include <iostream>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "openssl/bio.h"
#include "openssl/err.h"
#include "openssl/ssl.h"

#define HELP "Usage: %s <server|client>\n", argc[0]
#define CAN_FAIL(exp) do {                                              \
        int a = exp;                                                    \
        if(a) {                                                         \
            printf("Failed with code %d at %s:%d\n",                    \
                   a, __FILE__, __LINE__);                              \
            exit(1);                                                    \
        }                                                               \
    } while(0);

int client() {
    SSL_CTX *ctx;
    SSL *ssl;

    SSL_load_error_strings();
    SSL_library_init();

    ctx = SSL_CTX_new(TLS_client_method());
    if(! ctx) {
        fprintf(stderr, "Error creating SSL context\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }

    int sfd;

    struct sockaddr_in addr = {0};
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	addr.sin_family=AF_INET;
	addr.sin_port = htons(6600);

    CAN_FAIL(!(sfd = socket(AF_INET, SOCK_STREAM, 0)));
    CAN_FAIL(connect(sfd, (struct sockaddr*)&addr, sizeof(addr)));

    char arr[256];
    char* req = "play\n";

    write(sfd, req, sizeof(req));
    read(sfd, &arr, sizeof(arr));
    printf("Socket fd: %d\n", sfd);
    printf("Response: %s", arr);
            
    return 0;
}

int server() {
    return 69;
}

int main(int argv, char** argc) {
    bool isServer; // True when in server mode

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
        printf("Running in server mode\n");
        isServer = true;
    } else if (i == "client") {
        printf("Running in client mode\n");
        isServer = false;
    } else {
        printf("Unknown argument '%s'\n", argc[1]);
        printf(HELP);
        return 1;
    }

    SSL_load_error_strings();
    SSL_library_init();

    // all work happens inside the server/client fns
    if(isServer) return server();
    else         return client();

}
