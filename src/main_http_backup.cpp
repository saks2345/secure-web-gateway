#include <netdb.h>
#include <iostream>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main() {

    // Create server socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    // Server address
    sockaddr_in serverAddress{};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(8080);

    // Bind socket
    if (bind(
        serverSocket,
        (sockaddr*)&serverAddress,
        sizeof(serverAddress)
    ) < 0) {

        std::cerr << "Bind failed\n";
        close(serverSocket);
        return 1;
    }

    // Listen
    if (listen(serverSocket, 10) < 0) {
        std::cerr << "Listen failed\n";
        close(serverSocket);
        return 1;
    }

    std::cout << "Proxy listening on port 8080...\n";

    // Accept one client
    int clientSocket = accept(
        serverSocket,
        nullptr,
        nullptr
    );

    if (clientSocket < 0) {
        std::cerr << "Accept failed\n";
        close(serverSocket);
        return 1;
    }

    std::cout << "Client connected!\n";

    // Receive HTTP request
    char buffer[4096];

    int bytesReceived = recv(
        clientSocket,
        buffer,
        sizeof(buffer) - 1,
        0
    );

    if (bytesReceived > 0) {

    buffer[bytesReceived] = '\0';

    std::cout << "\nReceived request:\n";
    std::cout << buffer << "\n";

    // Convert request to a C++ string
    std::string request(buffer);

    // Find the first space
    size_t firstSpace = request.find(' ');

    // Find the second space
    size_t secondSpace = request.find(' ', firstSpace + 1);

    // Extract the URL
    std::string url = request.substr(
        firstSpace + 1,
        secondSpace - firstSpace - 1
    );

    std::cout << "Requested URL: " << url << "\n";
    // Find the beginning of the hostname
size_t hostStart = url.find("://");

if (hostStart != std::string::npos) {
    hostStart += 3;
}

// Find the end of the hostname
size_t hostEnd = url.find('/', hostStart);

// Extract hostname
std::string hostname = url.substr(
    hostStart,
    hostEnd - hostStart
);

std::cout << "Hostname: " << hostname << "\n";
// Resolve hostname
addrinfo hints{};
addrinfo* result;

hints.ai_family = AF_INET;
hints.ai_socktype = SOCK_STREAM;

int status = getaddrinfo(
    hostname.c_str(),
    "80",
    &hints,
    &result
);

if (status != 0) {
    std::cerr << "DNS lookup failed\n";
    close(clientSocket);
    close(serverSocket);
    return 1;
}

// Create socket for destination server
int destinationSocket = socket(
    result->ai_family,
    result->ai_socktype,
    result->ai_protocol
);

if (destinationSocket < 0) {
    std::cerr << "Destination socket creation failed\n";
    freeaddrinfo(result);
    close(clientSocket);
    close(serverSocket);
    return 1;
}

// Connect to destination server
if (connect(
    destinationSocket,
    result->ai_addr,
    result->ai_addrlen
) < 0) {

    std::cerr << "Connection to destination failed\n";

    close(destinationSocket);
    freeaddrinfo(result);
    close(clientSocket);
    close(serverSocket);

    return 1;
}

std::cout << "Connected to destination server!\n";
// Build request for destination server
std::string path = url.substr(
    url.find(hostname) + hostname.length()
);

if (path.empty()) {
    path = "/";
}

// Find the Host header
size_t hostHeaderStart = request.find("Host:");

std::string hostHeader;

if (hostHeaderStart != std::string::npos) {

    size_t hostHeaderEnd = request.find(
        "\r\n",
        hostHeaderStart
    );

    hostHeader = request.substr(
        hostHeaderStart,
        hostHeaderEnd - hostHeaderStart
    );
}

// Create HTTP request for destination
std::string forwardRequest =
    "GET " + path + " HTTP/1.1\r\n" +
    hostHeader + "\r\n" +
    "Connection: close\r\n"
    "\r\n";

std::cout << "\nForwarding request:\n";
std::cout << forwardRequest;

// Send request to destination
send(
    destinationSocket,
    forwardRequest.c_str(),
    forwardRequest.length(),
    0
);

std::cout << "Request sent to destination!\n";
// Receive response from destination server
char responseBuffer[4096];

int bytesReceivedFromServer;

while ((bytesReceivedFromServer = recv(
    destinationSocket,
    responseBuffer,
    sizeof(responseBuffer),
    0
)) > 0) {

    // Send the received data back to the client
    send(
        clientSocket,
        responseBuffer,
        bytesReceivedFromServer,
        0
    );

    std::cout << "Forwarded "
              << bytesReceivedFromServer
              << " bytes\n";
}

std::cout << "Finished forwarding response.\n";
close(destinationSocket);
freeaddrinfo(result);
}
    close(clientSocket);
    close(serverSocket);

    return 0;
}
