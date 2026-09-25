#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <orbis/libkernel.h>

#define TEST_PORT 19876

static void *server_thread_func(void *arg) {
    (void)arg;
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        printf("[Server] socket() failed: %d\n", errno);
        return (void *)-1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_len = sizeof(srv_addr);
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(TEST_PORT);
    srv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listen_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) != 0) {
        printf("[Server] bind() failed: %d\n", errno);
        close(listen_fd);
        return (void *)-1;
    }

    if (listen(listen_fd, 1) != 0) {
        printf("[Server] listen() failed: %d\n", errno);
        close(listen_fd);
        return (void *)-1;
    }

    printf("[Server] Listening on port %d...\n", TEST_PORT);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        printf("[Server] accept() failed: %d\n", errno);
        close(listen_fd);
        return (void *)-1;
    }

    printf("[Server] Accepted client connection!\n");

    char buf[64] = {0};
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        printf("[Server] Received: \"%s\"\n", buf);
        const char *reply = "PONG_FROM_RECOMP_SERVER";
        send(client_fd, reply, strlen(reply), 0);
    }

    close(client_fd);
    close(listen_fd);
    return NULL;
}

int main(void) {
    printf("[POSIX + libkernel Test] Starting validation suite...\n");

    // 1. Validate libkernel File I/O (sceKernelOpen, sceKernelWrite, sceKernelLseek, sceKernelRead, sceKernelClose)
    const char *test_file = "/tmp/ps4_test_io.txt";
    int fd = sceKernelOpen(test_file, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        printf("ERROR: sceKernelOpen failed: fd=%d\n", fd);
        return 1;
    }
    const char *test_data = "Hello from PS4 OpenOrbis recompiled binary!";
    size_t written = sceKernelWrite(fd, test_data, strlen(test_data));
    if (written != strlen(test_data)) {
        printf("ERROR: sceKernelWrite mismatch: written=%zu expected=%zu\n", written, strlen(test_data));
        return 1;
    }
    sceKernelClose(fd);

    fd = sceKernelOpen(test_file, O_RDONLY, 0);
    if (fd < 0) {
        printf("ERROR: sceKernelOpen for read failed: fd=%d\n", fd);
        return 1;
    }
    char read_buf[128] = {0};
    size_t bytes_read = sceKernelRead(fd, read_buf, sizeof(read_buf) - 1);
    if (bytes_read != strlen(test_data) || strcmp(read_buf, test_data) != 0) {
        printf("ERROR: sceKernelRead mismatch: got \"%s\"\n", read_buf);
        return 1;
    }
    sceKernelClose(fd);
    unlink(test_file);
    printf("[+] libkernel I/O test PASSED!\n");

    // 2. Validate pthread_rwlock_*
    OrbisPthreadRwlock rwlock;
    int rc = scePthreadRwlockInit(&rwlock, NULL, "test_rwlock");
    if (rc != 0) {
        printf("ERROR: scePthreadRwlockInit failed: %d\n", rc);
        return 1;
    }
    rc = scePthreadRwlockWrlock(&rwlock);
    if (rc != 0) {
        printf("ERROR: scePthreadRwlockWrlock failed: %d\n", rc);
        return 1;
    }
    rc = scePthreadRwlockUnlock(&rwlock);
    if (rc != 0) {
        printf("ERROR: scePthreadRwlockUnlock wrlock failed: %d\n", rc);
        return 1;
    }
    rc = scePthreadRwlockRdlock(&rwlock);
    if (rc != 0) {
        printf("ERROR: scePthreadRwlockRdlock failed: %d\n", rc);
        return 1;
    }
    rc = scePthreadRwlockUnlock(&rwlock);
    if (rc != 0) {
        printf("ERROR: scePthreadRwlockUnlock rdlock failed: %d\n", rc);
        return 1;
    }
    scePthreadRwlockDestroy(&rwlock);
    printf("[+] pthread_rwlock test PASSED!\n");

    // 3. Validate POSIX BSD Sockets over multi-threading (loopback TCP)
    OrbisPthread server_thread;
    rc = scePthreadCreate(&server_thread, NULL, server_thread_func, NULL, "server_th");
    if (rc != 0) {
        printf("ERROR: scePthreadCreate failed: %d\n", rc);
        return 1;
    }

    // Give server thread a moment to bind and listen
    sceKernelUsleep(50000); // 50ms

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        printf("ERROR: client socket() failed: %d\n", errno);
        return 1;
    }

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_len = sizeof(srv_addr);
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(TEST_PORT);
    inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr);

    if (connect(client_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) != 0) {
        printf("ERROR: client connect() failed: %d\n", errno);
        return 1;
    }

    const char *ping = "PING_FROM_CLIENT";
    send(client_fd, ping, strlen(ping), 0);

    char reply_buf[64] = {0};
    ssize_t reply_len = recv(client_fd, reply_buf, sizeof(reply_buf) - 1, 0);
    if (reply_len <= 0 || strcmp(reply_buf, "PONG_FROM_RECOMP_SERVER") != 0) {
        printf("ERROR: Unexpected server reply: got \"%s\"\n", reply_buf);
        return 1;
    }
    printf("[+] Client received reply: \"%s\"\n", reply_buf);
    close(client_fd);

    scePthreadJoin(server_thread, NULL);
    printf("[+] POSIX BSD Sockets test PASSED!\n");

    printf("\n[SUCCESS] ALL POSIX, BSD SOCKETS & LIBKERNEL SHIM TESTS PASSED!\n");
    return 0;
}
