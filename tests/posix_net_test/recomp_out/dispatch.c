#include "recomp_runtime.h"

void recomp_register_guest_modules(void);

// Forward declarations of chunk registration functions
void recomp_init_dispatch_chunk_0(void);

// Initialize dispatch table with all recompiled chunks and runtime shims
void recomp_init_dispatch_table(void) {
    recomp_init_dispatch_chunk_0();
    recomp_register_fn(0x4a70ULL, shim_open); // PLT sceKernelOpen
    recomp_register_fn(0x10018ULL, shim_open); // GOT sceKernelOpen
    recomp_register_fn(0x4a80ULL, shim_write); // PLT sceKernelWrite
    recomp_register_fn(0x10020ULL, shim_write); // GOT sceKernelWrite
    recomp_register_fn(0x4a90ULL, shim_close); // PLT sceKernelClose
    recomp_register_fn(0x10028ULL, shim_close); // GOT sceKernelClose
    recomp_register_fn(0x4aa0ULL, shim_read); // PLT sceKernelRead
    recomp_register_fn(0x10030ULL, shim_read); // GOT sceKernelRead
    recomp_register_fn(0x4ab0ULL, shim_unlink); // PLT unlink
    recomp_register_fn(0x10038ULL, shim_unlink); // GOT unlink
    recomp_register_fn(0x4ac0ULL, shim_pthread_rwlock_init); // PLT scePthreadRwlockInit
    recomp_register_fn(0x10040ULL, shim_pthread_rwlock_init); // GOT scePthreadRwlockInit
    recomp_register_fn(0x4ad0ULL, shim_pthread_rwlock_wrlock); // PLT scePthreadRwlockWrlock
    recomp_register_fn(0x10048ULL, shim_pthread_rwlock_wrlock); // GOT scePthreadRwlockWrlock
    recomp_register_fn(0x4ae0ULL, shim_pthread_rwlock_unlock); // PLT scePthreadRwlockUnlock
    recomp_register_fn(0x10050ULL, shim_pthread_rwlock_unlock); // GOT scePthreadRwlockUnlock
    recomp_register_fn(0x4af0ULL, shim_pthread_rwlock_rdlock); // PLT scePthreadRwlockRdlock
    recomp_register_fn(0x10058ULL, shim_pthread_rwlock_rdlock); // GOT scePthreadRwlockRdlock
    recomp_register_fn(0x4b00ULL, shim_pthread_rwlock_destroy); // PLT scePthreadRwlockDestroy
    recomp_register_fn(0x10060ULL, shim_pthread_rwlock_destroy); // GOT scePthreadRwlockDestroy
    recomp_register_fn(0x4b10ULL, shim_scePthreadCreate); // PLT scePthreadCreate
    recomp_register_fn(0x10068ULL, shim_scePthreadCreate); // GOT scePthreadCreate
    recomp_register_fn(0x4b20ULL, shim_sceKernelUsleep); // PLT sceKernelUsleep
    recomp_register_fn(0x10070ULL, shim_sceKernelUsleep); // GOT sceKernelUsleep
    recomp_register_fn(0x4b30ULL, shim_connect); // PLT connect
    recomp_register_fn(0x10078ULL, shim_connect); // GOT connect
    recomp_register_fn(0x4b40ULL, shim_close); // PLT close
    recomp_register_fn(0x10080ULL, shim_close); // GOT close
    recomp_register_fn(0x4b50ULL, shim_scePthreadJoin); // PLT scePthreadJoin
    recomp_register_fn(0x10088ULL, shim_scePthreadJoin); // GOT scePthreadJoin
    recomp_register_fn(0x4b60ULL, shim_setsockopt); // PLT setsockopt
    recomp_register_fn(0x10090ULL, shim_setsockopt); // GOT setsockopt
    recomp_register_fn(0x4b70ULL, shim_bind); // PLT bind
    recomp_register_fn(0x10098ULL, shim_bind); // GOT bind
    recomp_register_fn(0x4b80ULL, shim_listen); // PLT listen
    recomp_register_fn(0x100a0ULL, shim_listen); // GOT listen
    recomp_register_fn(0x4b90ULL, shim_accept); // PLT accept
    recomp_register_fn(0x100a8ULL, shim_accept); // GOT accept
    recomp_register_fn(0x4ba0ULL, shim_error); // PLT __error
    recomp_register_fn(0x100b0ULL, shim_error); // GOT __error
    recomp_register_fn(0x4bb0ULL, shim_open); // PLT open
    recomp_register_fn(0x100b8ULL, shim_open); // GOT open
    recomp_register_fn(0x4bc0ULL, shim_recvfrom); // PLT recvfrom
    recomp_register_fn(0x100c0ULL, shim_recvfrom); // GOT recvfrom
    recomp_register_fn(0x4bd0ULL, shim_sendto); // PLT sendto
    recomp_register_fn(0x100c8ULL, shim_sendto); // GOT sendto
    recomp_register_fn(0x4be0ULL, shim_socket); // PLT __sys_socketex
    recomp_register_fn(0x100d0ULL, shim_socket); // GOT __sys_socketex
    recomp_register_fn(0x4bf0ULL, shim_fcntl); // PLT _fcntl
    recomp_register_fn(0x100d8ULL, shim_fcntl); // GOT _fcntl
    recomp_register_fn(0x4c00ULL, shim_writev); // PLT _writev
    recomp_register_fn(0x100e0ULL, shim_writev); // GOT _writev
    recomp_register_fn(0x4c10ULL, shim_write); // PLT _write
    recomp_register_fn(0x100e8ULL, shim_write); // GOT _write
    recomp_register_fn(0x4c20ULL, shim_ioctl); // PLT _ioctl
    recomp_register_fn(0x100f0ULL, shim_ioctl); // GOT _ioctl
    recomp_register_fn(0x4c30ULL, shim_lseek); // PLT lseek
    recomp_register_fn(0x100f8ULL, shim_lseek); // GOT lseek
    recomp_register_fn(0x4c40ULL, shim_exit); // PLT _exit
    recomp_register_fn(0x10100ULL, shim_exit); // GOT _exit
    recomp_register_fn(0x4c50ULL, shim_poll); // PLT poll
    recomp_register_fn(0x10108ULL, shim_poll); // GOT poll
    recomp_register_fn(0x4400ULL, shim_strlen); // Symbol strlen
    recomp_register_fn(0x42e0ULL, shim_memset); // Symbol memset
    recomp_register_fn(0x43c0ULL, shim_strcmp); // Symbol strcmp
    recomp_register_fn(0xd6cULL, shim_socket); // Symbol socket
    recomp_register_fn(0x800ULL, shim_inet_pton); // Symbol inet_pton
    recomp_register_fn(0xd60ULL, shim_send); // Symbol send
    recomp_register_fn(0xd54ULL, shim_recv); // Symbol recv
    recomp_register_fn(0x4a2bULL, shim_exit); // Symbol exit
    recomp_register_fn(0xe10ULL, shim_fcntl); // Symbol fcntl
    recomp_register_fn(0x1220ULL, shim_ioctl); // Symbol ioctl
    recomp_register_fn(0x3c10ULL, shim_memmove); // Symbol memmove
    recomp_register_fn(0x38e0ULL, shim_memcpy); // Symbol memcpy
    recomp_register_fn(0x477cULL, shim___tls_get_addr); // Symbol __tls_get_addr
    recomp_register_guest_modules();
}
