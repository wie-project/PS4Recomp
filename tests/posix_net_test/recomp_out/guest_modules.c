#include "recomp_runtime.h"
#include "ps4_sysmodule.h"

static const RecompModuleExport recomp_host_shim_exports[] = {
    { "FT_Get_Char_Index", 0x15000ULL },
    { "FT_Init_FreeType", 0x15010ULL },
    { "FT_Load_Glyph", 0x15020ULL },
    { "FT_New_Face", 0x15030ULL },
    { "FT_Render_Glyph", 0x15040ULL },
    { "FT_Set_Pixel_Sizes", 0x15050ULL },
    { "__error", 0x100b0ULL },
    { "__stack_chk_fail", 0x15060ULL },
    { "__sys_socketex", 0x4be0ULL },
    { "__tls_get_addr", 0x477cULL },
    { "_exit", 0x4a2bULL },
    { "_fcntl", 0x100d8ULL },
    { "_ioctl", 0x100f0ULL },
    { "_open", 0x4bb0ULL },
    { "_read", 0x4aa0ULL },
    { "_readv", 0x15070ULL },
    { "_write", 0x100e8ULL },
    { "_writev", 0x4c00ULL },
    { "accept", 0x4b90ULL },
    { "bind", 0x10098ULL },
    { "clock_gettime", 0x15080ULL },
    { "close", 0x4a90ULL },
    { "connect", 0x10078ULL },
    { "cpuset_getaffinity", 0x15090ULL },
    { "exit", 0x4a2bULL },
    { "fcntl", 0x100d8ULL },
    { "fstat", 0x150a0ULL },
    { "getpeername", 0x150b0ULL },
    { "getpid", 0x150c0ULL },
    { "getrlimit", 0x150d0ULL },
    { "getrusage", 0x150e0ULL },
    { "getsockname", 0x150f0ULL },
    { "getsockopt", 0x15100ULL },
    { "gettimeofday", 0x15110ULL },
    { "inet_pton", 0x800ULL },
    { "ioctl", 0x100f0ULL },
    { "listen", 0x100a0ULL },
    { "lseek", 0x4c30ULL },
    { "madvise", 0x15120ULL },
    { "memcpy", 0x38e0ULL },
    { "memmove", 0x3c10ULL },
    { "memset", 0x42e0ULL },
    { "mmap", 0x15130ULL },
    { "munmap", 0x15140ULL },
    { "nanosleep", 0x15150ULL },
    { "open", 0x4bb0ULL },
    { "poll", 0x10108ULL },
    { "pthread_attr_destroy", 0x15160ULL },
    { "pthread_attr_init", 0x15170ULL },
    { "pthread_attr_setdetachstate", 0x15180ULL },
    { "pthread_attr_setstacksize", 0x15190ULL },
    { "pthread_cond_broadcast", 0x151a0ULL },
    { "pthread_cond_destroy", 0x151b0ULL },
    { "pthread_cond_init", 0x151c0ULL },
    { "pthread_cond_signal", 0x151d0ULL },
    { "pthread_cond_timedwait", 0x151e0ULL },
    { "pthread_cond_wait", 0x151f0ULL },
    { "pthread_create", 0x15200ULL },
    { "pthread_create_name_np", 0x15210ULL },
    { "pthread_detach", 0x15220ULL },
    { "pthread_equal", 0x15230ULL },
    { "pthread_exit", 0x15240ULL },
    { "pthread_getschedparam", 0x15250ULL },
    { "pthread_getspecific", 0x15260ULL },
    { "pthread_join", 0x15270ULL },
    { "pthread_key_create", 0x15280ULL },
    { "pthread_key_delete", 0x15290ULL },
    { "pthread_mutex_destroy", 0x152a0ULL },
    { "pthread_mutex_init", 0x152b0ULL },
    { "pthread_mutex_lock", 0x152c0ULL },
    { "pthread_mutex_trylock", 0x152d0ULL },
    { "pthread_mutex_unlock", 0x152e0ULL },
    { "pthread_mutexattr_destroy", 0x152f0ULL },
    { "pthread_mutexattr_init", 0x15300ULL },
    { "pthread_mutexattr_settype", 0x15310ULL },
    { "pthread_once", 0x15320ULL },
    { "pthread_rename_np", 0x15330ULL },
    { "pthread_rwlock_destroy", 0x10060ULL },
    { "pthread_rwlock_init", 0x4ac0ULL },
    { "pthread_rwlock_rdlock", 0x4af0ULL },
    { "pthread_rwlock_unlock", 0x4ae0ULL },
    { "pthread_rwlock_wrlock", 0x4ad0ULL },
    { "pthread_self", 0x15340ULL },
    { "pthread_setcanceltype", 0x15350ULL },
    { "pthread_setschedparam", 0x15360ULL },
    { "pthread_setspecific", 0x15370ULL },
    { "pthread_sigmask", 0x15380ULL },
    { "raise", 0x15390ULL },
    { "read", 0x4aa0ULL },
    { "readv", 0x153a0ULL },
    { "recv", 0xd54ULL },
    { "recvfrom", 0x4bc0ULL },
    { "sceAudioOutClose", 0x153b0ULL },
    { "sceAudioOutInit", 0x153c0ULL },
    { "sceAudioOutOpen", 0x153d0ULL },
    { "sceAudioOutOutput", 0x153e0ULL },
    { "sceCommonDialogInitialize", 0x153f0ULL },
    { "sceCommonDialogIsUsed", 0x15400ULL },
    { "sceKernelAddUserEventEdge", 0x15410ULL },
    { "sceKernelAllocateDirectMemory", 0x15420ULL },
    { "sceKernelAllocateMainDirectMemory", 0x15430ULL },
    { "sceKernelAvailableDirectMemorySize", 0x15440ULL },
    { "sceKernelAvailableFlexibleMemorySize", 0x15450ULL },
    { "sceKernelChmod", 0x15460ULL },
    { "sceKernelClearEventFlag", 0x15470ULL },
    { "sceKernelClose", 0x4a90ULL },
    { "sceKernelCreateEqueue", 0x15480ULL },
    { "sceKernelCreateEventFlag", 0x15490ULL },
    { "sceKernelDeleteEqueue", 0x154a0ULL },
    { "sceKernelDeleteEventFlag", 0x154b0ULL },
    { "sceKernelDlsym", 0x154c0ULL },
    { "sceKernelFstat", 0x154d0ULL },
    { "sceKernelGetCpumode", 0x154e0ULL },
    { "sceKernelGetCurrentCpu", 0x154f0ULL },
    { "sceKernelGetDirectMemorySize", 0x15500ULL },
    { "sceKernelGetProcessTime", 0x15510ULL },
    { "sceKernelGetProcessTimeCounter", 0x15520ULL },
    { "sceKernelGetProcessTimeCounterFrequency", 0x15530ULL },
    { "sceKernelGetPrtAperture", 0x15540ULL },
    { "sceKernelGetSystemSwVersion", 0x15550ULL },
    { "sceKernelGetTscFrequency", 0x15560ULL },
    { "sceKernelGetdents", 0x15570ULL },
    { "sceKernelGettimeofday", 0x15580ULL },
    { "sceKernelHasNeoMode", 0x15590ULL },
    { "sceKernelIsAuthenticNeo", 0x155a0ULL },
    { "sceKernelIsNeoMode", 0x155b0ULL },
    { "sceKernelLoadStartModule", 0x155c0ULL },
    { "sceKernelLseek", 0x4c30ULL },
    { "sceKernelMapDirectMemory", 0x155d0ULL },
    { "sceKernelMapDirectMemory2", 0x155e0ULL },
    { "sceKernelMapFlexibleMemory", 0x155f0ULL },
    { "sceKernelMapNamedDirectMemory", 0x15600ULL },
    { "sceKernelMapNamedFlexibleMemory", 0x15610ULL },
    { "sceKernelMemoryPoolCommit", 0x15620ULL },
    { "sceKernelMemoryPoolDecommit", 0x15630ULL },
    { "sceKernelMemoryPoolExpand", 0x15640ULL },
    { "sceKernelMemoryPoolReserve", 0x15650ULL },
    { "sceKernelMunmap", 0x15660ULL },
    { "sceKernelOpen", 0x4bb0ULL },
    { "sceKernelPollEventFlag", 0x15670ULL },
    { "sceKernelQueryMemoryProtection", 0x15680ULL },
    { "sceKernelRead", 0x4aa0ULL },
    { "sceKernelReadTsc", 0x15690ULL },
    { "sceKernelReleaseDirectMemory", 0x156a0ULL },
    { "sceKernelSetEventFlag", 0x156b0ULL },
    { "sceKernelStat", 0x156c0ULL },
    { "sceKernelStopUnloadModule", 0x156d0ULL },
    { "sceKernelTriggerUserEvent", 0x156e0ULL },
    { "sceKernelUsleep", 0x4b20ULL },
    { "sceKernelUtimes", 0x156f0ULL },
    { "sceKernelVirtualQuery", 0x15700ULL },
    { "sceKernelWaitEqueue", 0x15710ULL },
    { "sceKernelWaitEventFlag", 0x15720ULL },
    { "sceKernelWrite", 0x100e8ULL },
    { "sceKeyboardClose", 0x15730ULL },
    { "sceKeyboardGetHandle", 0x15740ULL },
    { "sceKeyboardGetKey2Char", 0x15750ULL },
    { "sceKeyboardInit", 0x15760ULL },
    { "sceKeyboardOpen", 0x15770ULL },
    { "sceKeyboardReadState", 0x15780ULL },
    { "sceMsgDialogClose", 0x15790ULL },
    { "sceMsgDialogGetResult", 0x157a0ULL },
    { "sceMsgDialogGetStatus", 0x157b0ULL },
    { "sceMsgDialogInitialize", 0x157c0ULL },
    { "sceMsgDialogOpen", 0x157d0ULL },
    { "sceMsgDialogProgressBarInc", 0x157e0ULL },
    { "sceMsgDialogProgressBarSetMsg", 0x157f0ULL },
    { "sceMsgDialogProgressBarSetValue", 0x15800ULL },
    { "sceMsgDialogTerminate", 0x15810ULL },
    { "sceMsgDialogUpdateStatus", 0x15820ULL },
    { "sceNetAccept", 0x15830ULL },
    { "sceNetBind", 0x15840ULL },
    { "sceNetConnect", 0x15850ULL },
    { "sceNetEpollControl", 0x15860ULL },
    { "sceNetEpollCreate", 0x15870ULL },
    { "sceNetEpollDestroy", 0x15880ULL },
    { "sceNetEpollWait", 0x15890ULL },
    { "sceNetErrnoLoc", 0x158a0ULL },
    { "sceNetGetMacAddress", 0x158b0ULL },
    { "sceNetGetSockInfo", 0x158c0ULL },
    { "sceNetGetpeername", 0x158d0ULL },
    { "sceNetGetsockname", 0x158e0ULL },
    { "sceNetGetsockopt", 0x158f0ULL },
    { "sceNetHtonl", 0x15900ULL },
    { "sceNetHtonll", 0x15910ULL },
    { "sceNetHtons", 0x15920ULL },
    { "sceNetInetNtop", 0x15930ULL },
    { "sceNetInetPton", 0x15940ULL },
    { "sceNetInit", 0x15950ULL },
    { "sceNetListen", 0x15960ULL },
    { "sceNetNtohl", 0x15970ULL },
    { "sceNetNtohll", 0x15980ULL },
    { "sceNetNtohs", 0x15990ULL },
    { "sceNetPoolCreate", 0x159a0ULL },
    { "sceNetPoolDestroy", 0x159b0ULL },
    { "sceNetRecv", 0x159c0ULL },
    { "sceNetRecvfrom", 0x159d0ULL },
    { "sceNetResolverCreate", 0x159e0ULL },
    { "sceNetResolverDestroy", 0x159f0ULL },
    { "sceNetResolverStartAton", 0x15a00ULL },
    { "sceNetResolverStartNtoa", 0x15a10ULL },
    { "sceNetResolverStartNtoaMultipleRecords", 0x15a20ULL },
    { "sceNetSend", 0x15a30ULL },
    { "sceNetSendto", 0x15a40ULL },
    { "sceNetSetsockopt", 0x15a50ULL },
    { "sceNetShutdown", 0x15a60ULL },
    { "sceNetSocket", 0x15a70ULL },
    { "sceNetSocketClose", 0x15a80ULL },
    { "sceNetTerm", 0x15a90ULL },
    { "sceNpTrophyCreateContext", 0x15aa0ULL },
    { "sceNpTrophyCreateHandle", 0x15ab0ULL },
    { "sceNpTrophyDestroyContext", 0x15ac0ULL },
    { "sceNpTrophyDestroyHandle", 0x15ad0ULL },
    { "sceNpTrophyInit", 0x15ae0ULL },
    { "sceNpTrophyRegisterContext", 0x15af0ULL },
    { "sceNpTrophyShowTrophyList", 0x15b00ULL },
    { "sceNpTrophyTerm", 0x15b10ULL },
    { "sceNpTrophyUnlockTrophy", 0x15b20ULL },
    { "scePadClose", 0x15b30ULL },
    { "scePadGetControllerInformation", 0x15b40ULL },
    { "scePadGetHandle", 0x15b50ULL },
    { "scePadInit", 0x15b60ULL },
    { "scePadOpen", 0x15b70ULL },
    { "scePadRead", 0x15b80ULL },
    { "scePadReadState", 0x15b90ULL },
    { "scePadResetLightBar", 0x15ba0ULL },
    { "scePadResetOrientation", 0x15bb0ULL },
    { "scePadSetLightBar", 0x15bc0ULL },
    { "scePadSetVibration", 0x15bd0ULL },
    { "scePthreadAttrDestroy", 0x15be0ULL },
    { "scePthreadAttrGetschedparam", 0x15bf0ULL },
    { "scePthreadAttrInit", 0x15c00ULL },
    { "scePthreadAttrSetaffinity", 0x15c10ULL },
    { "scePthreadAttrSetdetachstate", 0x15c20ULL },
    { "scePthreadAttrSetinheritsched", 0x15c30ULL },
    { "scePthreadAttrSetschedparam", 0x15c40ULL },
    { "scePthreadAttrSetschedpolicy", 0x15c50ULL },
    { "scePthreadAttrSetstacksize", 0x15c60ULL },
    { "scePthreadCondBroadcast", 0x15c70ULL },
    { "scePthreadCondDestroy", 0x15c80ULL },
    { "scePthreadCondInit", 0x15c90ULL },
    { "scePthreadCondSignal", 0x15ca0ULL },
    { "scePthreadCondTimedwait", 0x15cb0ULL },
    { "scePthreadCondWait", 0x15cc0ULL },
    { "scePthreadCondattrDestroy", 0x15cd0ULL },
    { "scePthreadCondattrInit", 0x15ce0ULL },
    { "scePthreadCreate", 0x4b10ULL },
    { "scePthreadDetach", 0x15cf0ULL },
    { "scePthreadEqual", 0x15d00ULL },
    { "scePthreadExit", 0x15d10ULL },
    { "scePthreadGetaffinity", 0x15d20ULL },
    { "scePthreadGetprio", 0x15d30ULL },
    { "scePthreadGetspecific", 0x15d40ULL },
    { "scePthreadGetthreadid", 0x15d50ULL },
    { "scePthreadJoin", 0x4b50ULL },
    { "scePthreadKeyCreate", 0x15d60ULL },
    { "scePthreadKeyDelete", 0x15d70ULL },
    { "scePthreadMutexDestroy", 0x15d80ULL },
    { "scePthreadMutexInit", 0x15d90ULL },
    { "scePthreadMutexLock", 0x15da0ULL },
    { "scePthreadMutexTrylock", 0x15db0ULL },
    { "scePthreadMutexUnlock", 0x15dc0ULL },
    { "scePthreadMutexattrDestroy", 0x15dd0ULL },
    { "scePthreadMutexattrInit", 0x15de0ULL },
    { "scePthreadMutexattrSetprotocol", 0x15df0ULL },
    { "scePthreadMutexattrSettype", 0x15e00ULL },
    { "scePthreadRwlockDestroy", 0x10060ULL },
    { "scePthreadRwlockInit", 0x4ac0ULL },
    { "scePthreadRwlockRdlock", 0x4af0ULL },
    { "scePthreadRwlockUnlock", 0x4ae0ULL },
    { "scePthreadRwlockWrlock", 0x4ad0ULL },
    { "scePthreadSelf", 0x15e10ULL },
    { "scePthreadSetaffinity", 0x15e20ULL },
    { "scePthreadSetprio", 0x15e30ULL },
    { "scePthreadSetspecific", 0x15e40ULL },
    { "scePthreadYield", 0x15e50ULL },
    { "sceSysmoduleIsLoaded", 0x15e60ULL },
    { "sceSysmoduleLoadModule", 0x15e70ULL },
    { "sceSysmoduleLoadModuleInternal", 0x15e80ULL },
    { "sceSysmoduleUnloadModule", 0x15e90ULL },
    { "sceSysmoduleUnloadModuleInternal", 0x15ea0ULL },
    { "sceUserServiceGetInitialUser", 0x15eb0ULL },
    { "sceUserServiceGetLoginUserIdList", 0x15ec0ULL },
    { "sceUserServiceGetUserName", 0x15ed0ULL },
    { "sceUserServiceInitialize", 0x15ee0ULL },
    { "sceUserServiceTerminate", 0x15ef0ULL },
    { "sceVideoOutAddFlipEvent", 0x15f00ULL },
    { "sceVideoOutClose", 0x15f10ULL },
    { "sceVideoOutGetDeviceCapabilityInfo_", 0x15f20ULL },
    { "sceVideoOutGetFlipStatus", 0x15f30ULL },
    { "sceVideoOutGetResolutionStatus", 0x15f40ULL },
    { "sceVideoOutGetVblankStatus", 0x15f50ULL },
    { "sceVideoOutIsFlipPending", 0x15f60ULL },
    { "sceVideoOutModeSetAny_", 0x15f70ULL },
    { "sceVideoOutOpen", 0x15f80ULL },
    { "sceVideoOutRegisterBuffers", 0x15f90ULL },
    { "sceVideoOutSetBufferAttribute", 0x15fa0ULL },
    { "sceVideoOutSetFlipRate", 0x15fb0ULL },
    { "sceVideoOutSubmitFlip", 0x15fc0ULL },
    { "sceVideoOutUnregisterBuffers", 0x15fd0ULL },
    { "sched_get_priority_max", 0x15fe0ULL },
    { "sched_get_priority_min", 0x15ff0ULL },
    { "sched_yield", 0x16000ULL },
    { "select", 0x16010ULL },
    { "sem_destroy", 0x16020ULL },
    { "sem_getvalue", 0x16030ULL },
    { "sem_init", 0x16040ULL },
    { "sem_post", 0x16050ULL },
    { "sem_trywait", 0x16060ULL },
    { "sem_wait", 0x16070ULL },
    { "send", 0xd60ULL },
    { "sendto", 0x4bd0ULL },
    { "setsockopt", 0x10090ULL },
    { "shutdown", 0x16080ULL },
    { "sigaction", 0x16090ULL },
    { "sigprocmask", 0x160a0ULL },
    { "socket", 0x4be0ULL },
    { "stat", 0x160b0ULL },
    { "strcmp", 0x43c0ULL },
    { "strcpy", 0x160c0ULL },
    { "strlen", 0x4400ULL },
    { "strncpy", 0x160d0ULL },
    { "syscall", 0x160e0ULL },
    { "sysconf", 0x160f0ULL },
    { "unlink", 0x4ab0ULL },
    { "usleep", 0x16100ULL },
    { "write", 0x100e8ULL },
    { "writev", 0x4c00ULL },
    { NULL, 0 }
};

void recomp_register_guest_modules(void) {
    recomp_register_fn(0x15000ULL, shim_FT_Get_Char_Index);
    recomp_register_fn(0x15010ULL, shim_FT_Init_FreeType);
    recomp_register_fn(0x15020ULL, shim_FT_Load_Glyph);
    recomp_register_fn(0x15030ULL, shim_FT_New_Face);
    recomp_register_fn(0x15040ULL, shim_FT_Render_Glyph);
    recomp_register_fn(0x15050ULL, shim_FT_Set_Pixel_Sizes);
    recomp_register_fn(0x15060ULL, shim___stack_chk_fail);
    recomp_register_fn(0x15070ULL, shim_readv);
    recomp_register_fn(0x15080ULL, shim_clock_gettime);
    recomp_register_fn(0x15090ULL, shim_cpuset_getaffinity);
    recomp_register_fn(0x150a0ULL, shim_fstat);
    recomp_register_fn(0x150b0ULL, shim_getpeername);
    recomp_register_fn(0x150c0ULL, shim_getpid);
    recomp_register_fn(0x150d0ULL, shim_getrlimit);
    recomp_register_fn(0x150e0ULL, shim_getrusage);
    recomp_register_fn(0x150f0ULL, shim_getsockname);
    recomp_register_fn(0x15100ULL, shim_getsockopt);
    recomp_register_fn(0x15110ULL, shim_gettimeofday);
    recomp_register_fn(0x15120ULL, shim_madvise);
    recomp_register_fn(0x15130ULL, shim_mmap);
    recomp_register_fn(0x15140ULL, shim_munmap);
    recomp_register_fn(0x15150ULL, shim_nanosleep);
    recomp_register_fn(0x15160ULL, shim_pthread_attr_destroy);
    recomp_register_fn(0x15170ULL, shim_pthread_attr_init);
    recomp_register_fn(0x15180ULL, shim_pthread_attr_setdetachstate);
    recomp_register_fn(0x15190ULL, shim_pthread_attr_setstacksize);
    recomp_register_fn(0x151a0ULL, shim_pthread_cond_broadcast);
    recomp_register_fn(0x151b0ULL, shim_pthread_cond_destroy);
    recomp_register_fn(0x151c0ULL, shim_pthread_cond_init);
    recomp_register_fn(0x151d0ULL, shim_pthread_cond_signal);
    recomp_register_fn(0x151e0ULL, shim_pthread_cond_timedwait);
    recomp_register_fn(0x151f0ULL, shim_pthread_cond_wait);
    recomp_register_fn(0x15200ULL, shim_pthread_create);
    recomp_register_fn(0x15210ULL, shim_pthread_create_name_np);
    recomp_register_fn(0x15220ULL, shim_pthread_detach);
    recomp_register_fn(0x15230ULL, shim_pthread_equal);
    recomp_register_fn(0x15240ULL, shim_pthread_exit);
    recomp_register_fn(0x15250ULL, shim_pthread_getschedparam);
    recomp_register_fn(0x15260ULL, shim_pthread_getspecific);
    recomp_register_fn(0x15270ULL, shim_pthread_join);
    recomp_register_fn(0x15280ULL, shim_pthread_key_create);
    recomp_register_fn(0x15290ULL, shim_pthread_key_delete);
    recomp_register_fn(0x152a0ULL, shim_pthread_mutex_destroy);
    recomp_register_fn(0x152b0ULL, shim_pthread_mutex_init);
    recomp_register_fn(0x152c0ULL, shim_pthread_mutex_lock);
    recomp_register_fn(0x152d0ULL, shim_pthread_mutex_trylock);
    recomp_register_fn(0x152e0ULL, shim_pthread_mutex_unlock);
    recomp_register_fn(0x152f0ULL, shim_pthread_mutexattr_destroy);
    recomp_register_fn(0x15300ULL, shim_pthread_mutexattr_init);
    recomp_register_fn(0x15310ULL, shim_pthread_mutexattr_settype);
    recomp_register_fn(0x15320ULL, shim_pthread_once);
    recomp_register_fn(0x15330ULL, shim_pthread_rename_np);
    recomp_register_fn(0x15340ULL, shim_pthread_self);
    recomp_register_fn(0x15350ULL, shim_pthread_setcanceltype);
    recomp_register_fn(0x15360ULL, shim_pthread_setschedparam);
    recomp_register_fn(0x15370ULL, shim_pthread_setspecific);
    recomp_register_fn(0x15380ULL, shim_pthread_sigmask);
    recomp_register_fn(0x15390ULL, shim_raise);
    recomp_register_fn(0x153a0ULL, shim_readv);
    recomp_register_fn(0x153b0ULL, shim_sceAudioOutClose);
    recomp_register_fn(0x153c0ULL, shim_sceAudioOutInit);
    recomp_register_fn(0x153d0ULL, shim_sceAudioOutOpen);
    recomp_register_fn(0x153e0ULL, shim_sceAudioOutOutput);
    recomp_register_fn(0x153f0ULL, shim_sceCommonDialogInitialize);
    recomp_register_fn(0x15400ULL, shim_sceCommonDialogIsUsed);
    recomp_register_fn(0x15410ULL, shim_sceKernelAddUserEventEdge);
    recomp_register_fn(0x15420ULL, shim_sceKernelAllocateDirectMemory);
    recomp_register_fn(0x15430ULL, shim_sceKernelAllocateMainDirectMemory);
    recomp_register_fn(0x15440ULL, shim_sceKernelAvailableDirectMemorySize);
    recomp_register_fn(0x15450ULL, shim_sceKernelAvailableFlexibleMemorySize);
    recomp_register_fn(0x15460ULL, shim_chmod);
    recomp_register_fn(0x15470ULL, shim_sceKernelClearEventFlag);
    recomp_register_fn(0x15480ULL, shim_sceKernelCreateEqueue);
    recomp_register_fn(0x15490ULL, shim_sceKernelCreateEventFlag);
    recomp_register_fn(0x154a0ULL, shim_sceKernelDeleteEqueue);
    recomp_register_fn(0x154b0ULL, shim_sceKernelDeleteEventFlag);
    recomp_register_fn(0x154c0ULL, shim_sceKernelDlsym);
    recomp_register_fn(0x154d0ULL, shim_fstat);
    recomp_register_fn(0x154e0ULL, shim_sceKernelGetCpumode);
    recomp_register_fn(0x154f0ULL, shim_sceKernelGetCurrentCpu);
    recomp_register_fn(0x15500ULL, shim_sceKernelGetDirectMemorySize);
    recomp_register_fn(0x15510ULL, shim_sceKernelGetProcessTime);
    recomp_register_fn(0x15520ULL, shim_sceKernelGetProcessTimeCounter);
    recomp_register_fn(0x15530ULL, shim_sceKernelGetProcessTimeCounterFrequency);
    recomp_register_fn(0x15540ULL, shim_sceKernelGetPrtAperture);
    recomp_register_fn(0x15550ULL, shim_sceKernelGetSystemSwVersion);
    recomp_register_fn(0x15560ULL, shim_sceKernelGetTscFrequency);
    recomp_register_fn(0x15570ULL, shim_getdents);
    recomp_register_fn(0x15580ULL, shim_sceKernelGettimeofday);
    recomp_register_fn(0x15590ULL, shim_sceKernelHasNeoMode);
    recomp_register_fn(0x155a0ULL, shim_sceKernelIsAuthenticNeo);
    recomp_register_fn(0x155b0ULL, shim_sceKernelIsNeoMode);
    recomp_register_fn(0x155c0ULL, shim_sceKernelLoadStartModule);
    recomp_register_fn(0x155d0ULL, shim_sceKernelMapDirectMemory);
    recomp_register_fn(0x155e0ULL, shim_sceKernelMapDirectMemory);
    recomp_register_fn(0x155f0ULL, shim_sceKernelMapFlexibleMemory);
    recomp_register_fn(0x15600ULL, shim_sceKernelMapDirectMemory);
    recomp_register_fn(0x15610ULL, shim_sceKernelMapFlexibleMemory);
    recomp_register_fn(0x15620ULL, shim_sceKernelMemoryPoolCommit);
    recomp_register_fn(0x15630ULL, shim_sceKernelMemoryPoolDecommit);
    recomp_register_fn(0x15640ULL, shim_sceKernelMemoryPoolExpand);
    recomp_register_fn(0x15650ULL, shim_sceKernelMemoryPoolReserve);
    recomp_register_fn(0x15660ULL, shim_munmap);
    recomp_register_fn(0x15670ULL, shim_sceKernelPollEventFlag);
    recomp_register_fn(0x15680ULL, shim_sceKernelQueryMemoryProtection);
    recomp_register_fn(0x15690ULL, shim_sceKernelReadTsc);
    recomp_register_fn(0x156a0ULL, shim_sceKernelReleaseDirectMemory);
    recomp_register_fn(0x156b0ULL, shim_sceKernelSetEventFlag);
    recomp_register_fn(0x156c0ULL, shim_stat);
    recomp_register_fn(0x156d0ULL, shim_sceKernelStopUnloadModule);
    recomp_register_fn(0x156e0ULL, shim_sceKernelTriggerUserEvent);
    recomp_register_fn(0x156f0ULL, shim_utimes);
    recomp_register_fn(0x15700ULL, shim_sceKernelVirtualQuery);
    recomp_register_fn(0x15710ULL, shim_sceKernelWaitEqueue);
    recomp_register_fn(0x15720ULL, shim_sceKernelWaitEventFlag);
    recomp_register_fn(0x15730ULL, shim_sceKeyboardClose);
    recomp_register_fn(0x15740ULL, shim_sceKeyboardGetHandle);
    recomp_register_fn(0x15750ULL, shim_sceKeyboardGetKey2Char);
    recomp_register_fn(0x15760ULL, shim_sceKeyboardInit);
    recomp_register_fn(0x15770ULL, shim_sceKeyboardOpen);
    recomp_register_fn(0x15780ULL, shim_sceKeyboardReadState);
    recomp_register_fn(0x15790ULL, shim_sceMsgDialogClose);
    recomp_register_fn(0x157a0ULL, shim_sceMsgDialogGetResult);
    recomp_register_fn(0x157b0ULL, shim_sceMsgDialogGetStatus);
    recomp_register_fn(0x157c0ULL, shim_sceMsgDialogInitialize);
    recomp_register_fn(0x157d0ULL, shim_sceMsgDialogOpen);
    recomp_register_fn(0x157e0ULL, shim_sceMsgDialogProgressBarInc);
    recomp_register_fn(0x157f0ULL, shim_sceMsgDialogProgressBarSetMsg);
    recomp_register_fn(0x15800ULL, shim_sceMsgDialogProgressBarSetValue);
    recomp_register_fn(0x15810ULL, shim_sceMsgDialogTerminate);
    recomp_register_fn(0x15820ULL, shim_sceMsgDialogUpdateStatus);
    recomp_register_fn(0x15830ULL, shim_sceNetAccept);
    recomp_register_fn(0x15840ULL, shim_sceNetBind);
    recomp_register_fn(0x15850ULL, shim_sceNetConnect);
    recomp_register_fn(0x15860ULL, shim_sceNetEpollControl);
    recomp_register_fn(0x15870ULL, shim_sceNetEpollCreate);
    recomp_register_fn(0x15880ULL, shim_sceNetEpollDestroy);
    recomp_register_fn(0x15890ULL, shim_sceNetEpollWait);
    recomp_register_fn(0x158a0ULL, shim_sceNetErrnoLoc);
    recomp_register_fn(0x158b0ULL, shim_sceNetGetMacAddress);
    recomp_register_fn(0x158c0ULL, shim_sceNetGetSockInfo);
    recomp_register_fn(0x158d0ULL, shim_sceNetGetpeername);
    recomp_register_fn(0x158e0ULL, shim_sceNetGetsockname);
    recomp_register_fn(0x158f0ULL, shim_sceNetGetsockopt);
    recomp_register_fn(0x15900ULL, shim_sceNetHtonl);
    recomp_register_fn(0x15910ULL, shim_sceNetHtonll);
    recomp_register_fn(0x15920ULL, shim_sceNetHtons);
    recomp_register_fn(0x15930ULL, shim_sceNetInetNtop);
    recomp_register_fn(0x15940ULL, shim_sceNetInetPton);
    recomp_register_fn(0x15950ULL, shim_sceNetInit);
    recomp_register_fn(0x15960ULL, shim_sceNetListen);
    recomp_register_fn(0x15970ULL, shim_sceNetNtohl);
    recomp_register_fn(0x15980ULL, shim_sceNetNtohll);
    recomp_register_fn(0x15990ULL, shim_sceNetNtohs);
    recomp_register_fn(0x159a0ULL, shim_sceNetPoolCreate);
    recomp_register_fn(0x159b0ULL, shim_sceNetPoolDestroy);
    recomp_register_fn(0x159c0ULL, shim_sceNetRecv);
    recomp_register_fn(0x159d0ULL, shim_sceNetRecvfrom);
    recomp_register_fn(0x159e0ULL, shim_sceNetResolverCreate);
    recomp_register_fn(0x159f0ULL, shim_sceNetResolverDestroy);
    recomp_register_fn(0x15a00ULL, shim_sceNetResolverStartAton);
    recomp_register_fn(0x15a10ULL, shim_sceNetResolverStartNtoa);
    recomp_register_fn(0x15a20ULL, shim_sceNetResolverStartNtoaMultipleRecords);
    recomp_register_fn(0x15a30ULL, shim_sceNetSend);
    recomp_register_fn(0x15a40ULL, shim_sceNetSendto);
    recomp_register_fn(0x15a50ULL, shim_sceNetSetsockopt);
    recomp_register_fn(0x15a60ULL, shim_sceNetShutdown);
    recomp_register_fn(0x15a70ULL, shim_sceNetSocket);
    recomp_register_fn(0x15a80ULL, shim_sceNetSocketClose);
    recomp_register_fn(0x15a90ULL, shim_sceNetTerm);
    recomp_register_fn(0x15aa0ULL, shim_sceNpTrophyCreateContext);
    recomp_register_fn(0x15ab0ULL, shim_sceNpTrophyCreateHandle);
    recomp_register_fn(0x15ac0ULL, shim_sceNpTrophyDestroyContext);
    recomp_register_fn(0x15ad0ULL, shim_sceNpTrophyDestroyHandle);
    recomp_register_fn(0x15ae0ULL, shim_sceNpTrophyInit);
    recomp_register_fn(0x15af0ULL, shim_sceNpTrophyRegisterContext);
    recomp_register_fn(0x15b00ULL, shim_sceNpTrophyShowTrophyList);
    recomp_register_fn(0x15b10ULL, shim_sceNpTrophyTerm);
    recomp_register_fn(0x15b20ULL, shim_sceNpTrophyUnlockTrophy);
    recomp_register_fn(0x15b30ULL, shim_scePadClose);
    recomp_register_fn(0x15b40ULL, shim_scePadGetControllerInformation);
    recomp_register_fn(0x15b50ULL, shim_scePadGetHandle);
    recomp_register_fn(0x15b60ULL, shim_scePadInit);
    recomp_register_fn(0x15b70ULL, shim_scePadOpen);
    recomp_register_fn(0x15b80ULL, shim_scePadRead);
    recomp_register_fn(0x15b90ULL, shim_scePadReadState);
    recomp_register_fn(0x15ba0ULL, shim_scePadResetLightBar);
    recomp_register_fn(0x15bb0ULL, shim_scePadResetOrientation);
    recomp_register_fn(0x15bc0ULL, shim_scePadSetLightBar);
    recomp_register_fn(0x15bd0ULL, shim_scePadSetVibration);
    recomp_register_fn(0x15be0ULL, shim_scePthreadAttrDestroy);
    recomp_register_fn(0x15bf0ULL, shim_scePthreadAttrGetschedparam);
    recomp_register_fn(0x15c00ULL, shim_scePthreadAttrInit);
    recomp_register_fn(0x15c10ULL, shim_scePthreadAttrSetaffinity);
    recomp_register_fn(0x15c20ULL, shim_scePthreadAttrSetdetachstate);
    recomp_register_fn(0x15c30ULL, shim_scePthreadAttrSetinheritsched);
    recomp_register_fn(0x15c40ULL, shim_scePthreadAttrSetschedparam);
    recomp_register_fn(0x15c50ULL, shim_scePthreadAttrSetschedpolicy);
    recomp_register_fn(0x15c60ULL, shim_scePthreadAttrSetstacksize);
    recomp_register_fn(0x15c70ULL, shim_scePthreadCondBroadcast);
    recomp_register_fn(0x15c80ULL, shim_scePthreadCondDestroy);
    recomp_register_fn(0x15c90ULL, shim_scePthreadCondInit);
    recomp_register_fn(0x15ca0ULL, shim_scePthreadCondSignal);
    recomp_register_fn(0x15cb0ULL, shim_scePthreadCondTimedwait);
    recomp_register_fn(0x15cc0ULL, shim_scePthreadCondWait);
    recomp_register_fn(0x15cd0ULL, shim_scePthreadCondattrDestroy);
    recomp_register_fn(0x15ce0ULL, shim_scePthreadCondattrInit);
    recomp_register_fn(0x15cf0ULL, shim_scePthreadDetach);
    recomp_register_fn(0x15d00ULL, shim_scePthreadEqual);
    recomp_register_fn(0x15d10ULL, shim_scePthreadExit);
    recomp_register_fn(0x15d20ULL, shim_scePthreadGetaffinity);
    recomp_register_fn(0x15d30ULL, shim_scePthreadGetprio);
    recomp_register_fn(0x15d40ULL, shim_scePthreadGetspecific);
    recomp_register_fn(0x15d50ULL, shim_scePthreadGetthreadid);
    recomp_register_fn(0x15d60ULL, shim_scePthreadKeyCreate);
    recomp_register_fn(0x15d70ULL, shim_scePthreadKeyDelete);
    recomp_register_fn(0x15d80ULL, shim_scePthreadMutexDestroy);
    recomp_register_fn(0x15d90ULL, shim_scePthreadMutexInit);
    recomp_register_fn(0x15da0ULL, shim_scePthreadMutexLock);
    recomp_register_fn(0x15db0ULL, shim_scePthreadMutexTrylock);
    recomp_register_fn(0x15dc0ULL, shim_scePthreadMutexUnlock);
    recomp_register_fn(0x15dd0ULL, shim_scePthreadMutexattrDestroy);
    recomp_register_fn(0x15de0ULL, shim_scePthreadMutexattrInit);
    recomp_register_fn(0x15df0ULL, shim_scePthreadMutexattrSetprotocol);
    recomp_register_fn(0x15e00ULL, shim_scePthreadMutexattrSettype);
    recomp_register_fn(0x15e10ULL, shim_scePthreadSelf);
    recomp_register_fn(0x15e20ULL, shim_scePthreadSetaffinity);
    recomp_register_fn(0x15e30ULL, shim_scePthreadSetprio);
    recomp_register_fn(0x15e40ULL, shim_scePthreadSetspecific);
    recomp_register_fn(0x15e50ULL, shim_scePthreadYield);
    recomp_register_fn(0x15e60ULL, shim_sceSysmoduleIsLoaded);
    recomp_register_fn(0x15e70ULL, shim_sceSysmoduleLoadModule);
    recomp_register_fn(0x15e80ULL, shim_sceSysmoduleLoadModuleInternal);
    recomp_register_fn(0x15e90ULL, shim_sceSysmoduleUnloadModule);
    recomp_register_fn(0x15ea0ULL, shim_sceSysmoduleUnloadModuleInternal);
    recomp_register_fn(0x15eb0ULL, shim_sceUserServiceGetInitialUser);
    recomp_register_fn(0x15ec0ULL, shim_sceUserServiceGetLoginUserIdList);
    recomp_register_fn(0x15ed0ULL, shim_sceUserServiceGetUserName);
    recomp_register_fn(0x15ee0ULL, shim_sceUserServiceInitialize);
    recomp_register_fn(0x15ef0ULL, shim_sceUserServiceTerminate);
    recomp_register_fn(0x15f00ULL, shim_sceVideoOutAddFlipEvent);
    recomp_register_fn(0x15f10ULL, shim_sceVideoOutClose);
    recomp_register_fn(0x15f20ULL, shim_sceVideoOutGetDeviceCapabilityInfo_);
    recomp_register_fn(0x15f30ULL, shim_sceVideoOutGetFlipStatus);
    recomp_register_fn(0x15f40ULL, shim_sceVideoOutGetResolutionStatus);
    recomp_register_fn(0x15f50ULL, shim_sceVideoOutGetVblankStatus);
    recomp_register_fn(0x15f60ULL, shim_sceVideoOutIsFlipPending);
    recomp_register_fn(0x15f70ULL, shim_sceVideoOutModeSetAny_);
    recomp_register_fn(0x15f80ULL, shim_sceVideoOutOpen);
    recomp_register_fn(0x15f90ULL, shim_sceVideoOutRegisterBuffers);
    recomp_register_fn(0x15fa0ULL, shim_sceVideoOutSetBufferAttribute);
    recomp_register_fn(0x15fb0ULL, shim_sceVideoOutSetFlipRate);
    recomp_register_fn(0x15fc0ULL, shim_sceVideoOutSubmitFlip);
    recomp_register_fn(0x15fd0ULL, shim_sceVideoOutUnregisterBuffers);
    recomp_register_fn(0x15fe0ULL, shim_sched_get_priority_max);
    recomp_register_fn(0x15ff0ULL, shim_sched_get_priority_min);
    recomp_register_fn(0x16000ULL, shim_sched_yield);
    recomp_register_fn(0x16010ULL, shim_select);
    recomp_register_fn(0x16020ULL, shim_sem_destroy);
    recomp_register_fn(0x16030ULL, shim_sem_getvalue);
    recomp_register_fn(0x16040ULL, shim_sem_init);
    recomp_register_fn(0x16050ULL, shim_sem_post);
    recomp_register_fn(0x16060ULL, shim_sem_trywait);
    recomp_register_fn(0x16070ULL, shim_sem_wait);
    recomp_register_fn(0x16080ULL, shim_shutdown);
    recomp_register_fn(0x16090ULL, shim_sigaction);
    recomp_register_fn(0x160a0ULL, shim_sigprocmask);
    recomp_register_fn(0x160b0ULL, shim_stat);
    recomp_register_fn(0x160c0ULL, shim_strcpy);
    recomp_register_fn(0x160d0ULL, shim_strncpy);
    recomp_register_fn(0x160e0ULL, shim_syscall);
    recomp_register_fn(0x160f0ULL, shim_sysconf);
    recomp_register_fn(0x16100ULL, shim_usleep);
    recomp_module_register("libc.prx", recomp_host_shim_exports);
    recomp_module_register("libSceLibcInternal.prx", recomp_host_shim_exports);
    recomp_module_register("libkernel.prx", recomp_host_shim_exports);
    recomp_module_register("libSceFios2.prx", recomp_host_shim_exports);
}
