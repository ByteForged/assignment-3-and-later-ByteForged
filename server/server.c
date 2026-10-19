#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>

#define DEBUG_LOG(__FMT__, ...) syslog(LOG_DEBUG, __FMT__, ##__VA_ARGS__)
#define ERROR_LOG(__FMT__, ...) syslog(LOG_ERR, __FMT__, ##__VA_ARGS__)

static void * getAddrIpV4(struct sockaddr * addr) {
    if (addr) {
        return &((struct sockaddr_in*)addr)->sin_addr;
    }
    return NULL;
}

static void * getAddrIpV6(struct sockaddr * addr) {
    if (addr) {
        return &((struct sockaddr_in6*)addr)->sin6_addr;
    }
    return NULL;
}

static void * getAddr(struct addrinfo* info) {
    if (info) {
        if (info->ai_family == AF_INET) {
            return getAddrIpV4(info->ai_addr);
        } else if (info->ai_family == AF_INET6) {
            return getAddrIpV6(info->ai_addr);
        }
    }
    return NULL;
}

static socklen_t getSockLen(struct addrinfo* info) {
    if (info) {
        if (info->ai_family == AF_INET) {
            return sizeof(struct sockaddr_in);
        } else if (info->ai_family == AF_INET6) {
            return sizeof(struct sockaddr_in6);
        }
    }
    return 0;
}

static int getPort(struct addrinfo* info) {
    if (info) {
        if (info->ai_family == AF_INET) {
            return ntohs(((struct sockaddr_in*)info->ai_addr)->sin_port);
        } else if (info->ai_family == AF_INET6) {
            return ntohs(((struct sockaddr_in6*)info->ai_addr)->sin6_port);
        }
    }
    return -1;
}

#define FILE_DIR "/var/tmp/"
static const char* filePath = FILE_DIR "aesdsocketdata";
static volatile sig_atomic_t signalFlag = 0;

struct main_ctx_t {
    int sock;
    int client;
    int file;
    bool fileDeleteFlag;
    struct addrinfo* addrInfo;
};

static struct main_ctx_t ctx = {
    .sock           = -1,
    .client         = -1,
    .file           = -1,
    .fileDeleteFlag = true,
    .addrInfo       = NULL
};

static void signalHandler(int sig) {
    DEBUG_LOG("Caught signal, exiting");
    signalFlag = 1;
}

static void teardown(int ec, int err) {
    if (ec == EXIT_FAILURE) {
        if (signalFlag && (err == EINTR)) {
            // Graceful termination via signal
            ec = EXIT_SUCCESS;
        }
    }
    exit(ec);
}

static void startDaemon(void) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process

        // Inherit umask from parent
        //umask(0x2);

        if (setsid() == -1) {
            ERROR_LOG("Failed to create session: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }
        if (chdir("/") == -1 ) {
            ERROR_LOG("Failed to set working dir: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }

        // Close STDIO related files.
        // Only other files open should be data file and socket
        for (int i = 0; i < 3; i++) {
            (void)close(i);
        }

        int fd = open("/dev/null", O_RDWR);
        if (fd == -1) {
            ERROR_LOG("Failed to open /dev/null: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }
        if (dup2(fd, STDIN_FILENO) == -1) {
            ERROR_LOG("Failed to redirect stdin: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
            ERROR_LOG("Failed to redirect stdout: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }
        if (dup2(fd, STDERR_FILENO) == -1) {
            ERROR_LOG("Failed to redirect stderr: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }
        if (fd > STDERR_FILENO) {
            (void)close(fd);
        }
    } else if (pid < 0) {
        // Failed to fork
        ERROR_LOG("Failed to fork: %s", strerror(errno));
        teardown(EXIT_FAILURE, errno);
    }
    else {
        // Parent process
        DEBUG_LOG("Spawned child. Exit.");
        ctx.fileDeleteFlag = false;
        exit(EXIT_SUCCESS);
    }
}

void onExit(void) {
    DEBUG_LOG("onExit()");
    if (ctx.sock > 0) {
        (void)close(ctx.sock);
    }
    if (ctx.client > 0) {
        (void)close(ctx.client);
    }
    if (ctx.file > 0) {
        (void)close(ctx.file);
    }
    if (ctx.addrInfo) {
        free(ctx.addrInfo);
    }
    if (ctx.fileDeleteFlag) {
        (void)remove(filePath); 
    }
}

int main(int argc, const char* argv[]) {
    int rc;
    char srvAddrBuf[64];
    char clientAddrBuf[64];
    struct addrinfo hints;
    struct sigaction sigAction = { 0 };
    bool daemon = false;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (argc == 2) {
        if (strcmp("-d", argv[1]) == 0) {
            daemon = true;
        } else {
            ERROR_LOG("Arg should be \"-d\"");
            teardown(EXIT_FAILURE, 0);
        }
    } else if (argc != 1) {
        ERROR_LOG("Invalid args: Only \"-d\" supported");
        teardown(EXIT_FAILURE, 0);
    }

    if (atexit(&onExit) != 0) {
        ERROR_LOG("Failed to register exit handler");
        teardown(EXIT_FAILURE, 0);
    }

    sigAction.sa_handler = &signalHandler;
    if (sigaction(SIGINT, &sigAction, NULL) == -1) {
        ERROR_LOG("Failed to set SIGINT handler: %s", strerror(errno));
        teardown(EXIT_FAILURE, errno);
    }

    if (sigaction(SIGTERM, &sigAction, NULL) == -1) {
        ERROR_LOG("Failed to set SIGTERM handler: %s", strerror(errno));
        teardown(EXIT_FAILURE, errno);
    }

    if (system("mkdir -p " FILE_DIR) != 0) {
        ERROR_LOG("Failed to mkdir %s: %s", FILE_DIR, strerror(errno));
        teardown(EXIT_FAILURE, errno);
    }

    ctx.file = open(filePath, O_CREAT|O_TRUNC|O_RDWR|O_APPEND, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (ctx.file == -1) {
        ERROR_LOG("Failed to open %s: %s", filePath, strerror(errno));
        teardown(EXIT_FAILURE, errno);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(NULL, "9000", &hints, &ctx.addrInfo);
    if (rc == 0) {
        const char* serverAddrStr = inet_ntop(ctx.addrInfo->ai_family, getAddr(ctx.addrInfo), srvAddrBuf, sizeof(srvAddrBuf));
        ctx.sock = socket(ctx.addrInfo->ai_family, ctx.addrInfo->ai_socktype, ctx.addrInfo->ai_protocol);
        if (ctx.sock == -1) {
            ERROR_LOG("Failed to open socket: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }
        DEBUG_LOG("Server socket opened");

        int optVal = 1;
        if (setsockopt(ctx.sock, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal)) == -1) {
            ERROR_LOG("Failed to set socket option: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }
        DEBUG_LOG("Socket option SO_REUSEADDR set");

        if (setsockopt(ctx.sock, SOL_SOCKET, SO_REUSEPORT, &optVal, sizeof(optVal)) == -1) {
            ERROR_LOG("Failed to set socket option: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }
        DEBUG_LOG("Socket option SO_REUSEPORT set");

        if (bind(ctx.sock, ctx.addrInfo->ai_addr, getSockLen(ctx.addrInfo)) == -1) {
            ERROR_LOG("Failed to bind: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }
        DEBUG_LOG("Server socket bound to %s:%d", (serverAddrStr ? serverAddrStr : "-"), getPort(ctx.addrInfo));

        if (daemon) {
            startDaemon();
        }

        if (listen(ctx.sock, 10) == -1) {
            ERROR_LOG("Failed to listen: %s", strerror(errno));
            teardown(EXIT_FAILURE, errno);
        }
        DEBUG_LOG("Server socket listen");

        while (!signalFlag) {
            struct sockaddr_in addr;
            socklen_t sockLen = sizeof(struct sockaddr_in);
            DEBUG_LOG("Server accepting new client...");
            ctx.client = accept(ctx.sock, (struct sockaddr*)&addr, &sockLen);
            if (ctx.client != -1) {
                char buf[256];
                const char* clientAddrStr = inet_ntop(AF_INET, getAddrIpV4((struct sockaddr*)&addr), clientAddrBuf, sizeof(clientAddrBuf));
                DEBUG_LOG("Accepted connection from %s", (clientAddrStr ? clientAddrStr: "-"));
                while (!signalFlag) {
                    // Receive packet from client
                    memset(buf, 0, sizeof(buf));
                    DEBUG_LOG("Receiving up to %ld bytes from client", sizeof(buf)-1);
                    ssize_t numRead = recv(ctx.client, buf, sizeof(buf)-1, 0);
                    if (numRead > 0) {
                        DEBUG_LOG("Recv'd %ld bytes from client", numRead);
                        const char* newLine = strchr(buf, '\n');
                        size_t numToWrite = numRead;
                        if (newLine) {
                            numToWrite = (size_t)newLine - (size_t)&buf[0];
                            numToWrite += 1; // include newline
                        }
                        do {
                            ssize_t wrote = write(ctx.file, buf, numToWrite);
                            if (wrote >= 0) {
                                DEBUG_LOG("Wrote %ld bytes to %s", wrote, filePath);
                                numToWrite -= wrote;
                            } else {
                                ERROR_LOG("Failed to write: %s", strerror(errno));
                                teardown(EXIT_FAILURE, errno);
                            }
                        } while (!signalFlag && numToWrite > 0);

                        if (newLine) {
                            // Write back all contents to client
                            if (lseek(ctx.file, 0, SEEK_SET) == -1) {
                                ERROR_LOG("Failed to seek: %s", strerror(errno));
                                teardown(EXIT_FAILURE, errno);
                            }
                            do {
                                numRead = read(ctx.file, buf, sizeof(buf));
                                if (numRead == -1) {
                                    ERROR_LOG("Failed to read: %s", strerror(errno));
                                    teardown(EXIT_FAILURE, errno);
                                }
                                if (numRead > 0) {
                                    if (send(ctx.client, buf, numRead, 0) == -1) {
                                        ERROR_LOG("Failed to send: %s", strerror(errno));
                                        teardown(EXIT_FAILURE, errno);
                                    }
                                }
                            } while (!signalFlag && numRead > 0);

                            close(ctx.client);
                            DEBUG_LOG("End of packet reached.");
                            DEBUG_LOG("Closed connection from %s", clientAddrStr);
                            break;
                        } else {
                            DEBUG_LOG("Not end of packet. Waiting for data...");
                        }
                    } else if (numRead == 0) {
                        close(ctx.client);
                        DEBUG_LOG("Client from %s closed connection", clientAddrStr);
                        break;
                    } else {
                        ERROR_LOG("Failed to recv: %s", strerror(errno));
                        teardown(EXIT_FAILURE, errno);
                    }
                }
            } else {
                ERROR_LOG("Failed to accept: %s", strerror(errno));
                teardown(EXIT_FAILURE, errno);
            }
        }
    } else {
        ERROR_LOG("Failed to build address: %s", gai_strerror(rc));
        teardown(EXIT_FAILURE, errno);
    }

    teardown(EXIT_SUCCESS, 0);
    return 0;
}