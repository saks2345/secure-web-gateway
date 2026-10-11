#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

int main() {

    const char* hostname = "example.com";
    const char* port = "80";

    // Look up the server address
    addrinfo hints{};
    addrinfo* result;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(hostname, port, &hints, &result);

    if (status != 0) {
        std::cerr << "DNS lookup failed: "
                  << gai_strerror(status) << "\n";
        return 1;
    }

    // Create socket
    int sock = socket(
        result->ai_family,
        result->ai_socktype,
        result->ai_protocol
    );

    if (sock < 0) {
        std::cerr << "Socket creation failed\n";
        freeaddrinfo(result);
        return 1;
    }

    // Connect to server
    if (connect(
            sock,
            result->ai_addr,
            result->ai_addrlen
        ) < 0) {

        std::cerr << "Connection failed\n";

        close(sock);
        freeaddrinfo(result);

        return 1;
    }

    std::cout << "Connected to example.com!\n";

    close(sock);
    freeaddrinfo(result);

    return 0;
}
