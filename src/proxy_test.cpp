#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

int main() {

    // Website we want to connect to
    const char* hostname = "example.com";
    const char* port = "80";

    // -----------------------------
    // Step 1: Resolve hostname
    // -----------------------------

    addrinfo hints{};
    addrinfo* result;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(hostname, port, &hints, &result);

    if (status != 0) {
        std::cerr << "DNS lookup failed\n";
        return 1;
    }

    // -----------------------------
    // Step 2: Create socket
    // -----------------------------

    int serverSocket = socket(
        result->ai_family,
        result->ai_socktype,
        result->ai_protocol
    );

    if (serverSocket < 0) {
        std::cerr << "Socket creation failed\n";
        freeaddrinfo(result);
        return 1;
    }

    // -----------------------------
    // Step 3: Connect to website
    // -----------------------------

    if (connect(
        serverSocket,
        result->ai_addr,
        result->ai_addrlen
    ) < 0) {

        std::cerr << "Connection failed\n";

        close(serverSocket);
        freeaddrinfo(result);

        return 1;
    }

    std::cout << "Connected to example.com!\n";

    // -----------------------------
    // Step 4: Send HTTP request
    // -----------------------------

    const char* request =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    send(
        serverSocket,
        request,
        strlen(request),
        0
    );

    // -----------------------------
    // Step 5: Receive response
    // -----------------------------

    char buffer[4096];

    int bytesReceived;

    while ((bytesReceived = recv(
        serverSocket,
        buffer,
        sizeof(buffer) - 1,
        0
    )) > 0) {

        buffer[bytesReceived] = '\0';

        std::cout << buffer;
    }

    // -----------------------------
    // Step 6: Close connection
    // -----------------------------

    close(serverSocket);
    freeaddrinfo(result);

    return 0;
}
