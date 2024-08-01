#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <mutex>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>

#define BUFLEN 4096 * 4
#define TIMEOUT 60
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 109
#define RESPONSE "HTTP/1.1 101 <b><i><font color=\"blue\">Assalamualaikum Kawann</font></b>\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: foo\r\n\r\n"

std::mutex logMutex;

void printLog(const std::string& log) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << log << std::endl;
}

class ProxyServer {
public:
    ProxyServer(const std::string& host, int port) : host(host), port(port), running(false) {}

    void start() {
        struct sockaddr_in serverAddr;
        serverSock = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSock == -1) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        int opt = 1;
        setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr(host.c_str());
        serverAddr.sin_port = htons(port);

        if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            perror("Bind failed");
            exit(EXIT_FAILURE);
        }

        if (listen(serverSock, 3) < 0) {
            perror("Listen failed");
            exit(EXIT_FAILURE);
        }

        running = true;
        while (running) {
            struct sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            int clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &addrLen);
            if (clientSock < 0) {
                perror("Accept failed");
                continue;
            }
            std::thread(&ProxyServer::handleClient, this, clientSock, clientAddr).detach();
        }
    }

    void stop() {
        running = false;
        close(serverSock);
    }

private:
    void handleClient(int clientSock, struct sockaddr_in clientAddr) {
        char buffer[BUFLEN] = {0};
        int bytesRead = read(clientSock, buffer, BUFLEN);
        if (bytesRead <= 0) {
            close(clientSock);
            return;
        }

        std::string request(buffer, bytesRead);
        std::string hostPort = findHeader(request, "X-Real-Host");
        if (hostPort.empty()) {
            hostPort = DEFAULT_HOST;
        }

        std::string response = RESPONSE;
        if (connectToTarget(clientSock, hostPort)) {
            send(clientSock, response.c_str(), response.size(), 0);
            proxyData(clientSock);
        } else {
            std::string errorResponse = "HTTP/1.1 400 Bad Request\r\n\r\n";
            send(clientSock, errorResponse.c_str(), errorResponse.size(), 0);
        }

        close(clientSock);
    }

    bool connectToTarget(int clientSock, const std::string& hostPort) {
        size_t colonPos = hostPort.find(':');
        std::string host = hostPort.substr(0, colonPos);
        int port = (colonPos != std::string::npos) ? std::stoi(hostPort.substr(colonPos + 1)) : DEFAULT_PORT;

        struct sockaddr_in targetAddr;
        targetSock = socket(AF_INET, SOCK_STREAM, 0);
        if (targetSock == -1) {
            perror("Target socket creation failed");
            return false;
        }

        targetAddr.sin_family = AF_INET;
        targetAddr.sin_addr.s_addr = inet_addr(host.c_str());
        targetAddr.sin_port = htons(port);

        if (connect(targetSock, (struct sockaddr*)&targetAddr, sizeof(targetAddr)) < 0) {
            perror("Connect to target failed");
            return false;
        }

        return true;
    }

    void proxyData(int clientSock) {
        struct pollfd fds[2];
        fds[0].fd = clientSock;
        fds[0].events = POLLIN;
        fds[1].fd = targetSock;
        fds[1].events = POLLIN;

        char buffer[BUFLEN];
        int timeout = TIMEOUT * 1000;
        while (true) {
            int ret = poll(fds, 2, timeout);
            if (ret <= 0) {
                break;
            }

            if (fds[0].revents & POLLIN) {
                int bytesRead = read(clientSock, buffer, BUFLEN);
                if (bytesRead <= 0) {
                    break;
                }
                send(targetSock, buffer, bytesRead, 0);
            }

            if (fds[1].revents & POLLIN) {
                int bytesRead = read(targetSock, buffer, BUFLEN);
                if (bytesRead <= 0) {
                    break;
                }
                send(clientSock, buffer, bytesRead, 0);
            }
        }

        close(targetSock);
    }

    std::string findHeader(const std::string& request, const std::string& header) {
        size_t start = request.find(header + ": ");
        if (start == std::string::npos) {
            return "";
        }
        start += header.size() + 2;
        size_t end = request.find("\r\n", start);
        return request.substr(start, end - start);
    }

    std::string host;
    int port;
    int serverSock;
    int targetSock;
    bool running;
};

void print_usage() {
    std::cout << "Usage: proxy -p <port>\n";
    std::cout << "       proxy -b <bindAddr> -p <port>\n";
    std::cout << "       proxy -b 0.0.0.0 -p 80\n";
}

void parse_args(int argc, char* argv[], std::string& bindAddr, int& port) {
    int opt;
    while ((opt = getopt(argc, argv, "hb:p:")) != -1) {
        switch (opt) {
            case 'h':
                print_usage();
                exit(EXIT_SUCCESS);
            case 'b':
                bindAddr = optarg;
                break;
            case 'p':
                port = std::stoi(optarg);
                break;
            default:
                print_usage();
                exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char* argv[]) {
    std::string bindAddr = "127.0.0.1";
    int port = 700;

    parse_args(argc, argv, bindAddr, port);

    std::cout << "\n:-------Socks Proxy-------:\n";
    std::cout << "Listening addr: " << bindAddr << "\n";
    std::cout << "Listening port: " << port << "\n";
    std::cout << ":-----------------------:\n";

    ProxyServer server(bindAddr, port);
    server.start();

    return 0;
}
