#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <linux/elf.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include "Ptrace.h"
#include "Utils.h"

#define DEV_CLASS_LEN 3

typedef uint8_t DEV_CLASS[DEV_CLASS_LEN];

#if defined(__aarch64__)
#define pt_regs  user_pt_regs
#define uregs    regs
#define ARM_r0   regs[0]
#define ARM_lr   regs[30]
#define ARM_sp   sp
#define ARM_pc   pc
#define ARM_cpsr pstate
#endif

#define CPSR_T_MASK (1u << 5)

#define LIBC_PATH_OLD		"/system/lib/libc.so"
#define LIBC_PATH_NEW		"/apex/com.android.runtime/lib/bionic/libc.so"
#define LINKER_PATH_OLD		"/system/lib/libdl.so"
#define LINKER_PATH_NEW		"/apex/com.android.runtime/lib/bionic/libdl.so"

static int android_os_version = -1;

int get_os_version() {
    if (android_os_version != -1) {
        return android_os_version;
    }

    char os_version[PROP_VALUE_MAX + 1];
    __system_property_get("ro.build.version.release", os_version);
    android_os_version = atoi(os_version);

    return android_os_version;
}

const char* get_linker_path() {
    if (get_os_version() >= 10) {
        return LINKER_PATH_NEW;
    } else {
        return LINKER_PATH_OLD;
    }
}

const char* get_libc_path() {
    if (get_os_version() >= 10) {
        return LIBC_PATH_NEW;
    } else {
        return LIBC_PATH_OLD;
    }
}

static void ptrace_get_regs(pid_t pid, struct pt_regs *regs) {
#if defined(__aarch64__)
    struct {
        void* ufb;
        size_t len;
    } regsvec = { regs, sizeof(struct pt_regs) };

    ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &regsvec);
#else
    ptrace(PTRACE_GETREGS, pid, NULL, regs);
#endif
}

static void ptrace_set_regs(pid_t pid, struct pt_regs *regs) {
#if defined(__aarch64__)
    struct {
        void* ufb;
        size_t len;
    } regsvec = { regs, sizeof(struct pt_regs) };

    ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &regsvec);
#else
    ptrace(PTRACE_SETREGS, pid, NULL, regs);
#endif
}

static void ptrace_cont(pid_t pid) {
    ptrace(PTRACE_CONT, pid, NULL, NULL);
}

void ptrace_read(pid_t pid, long* dest, uint8_t* addr, size_t size) {
    for (int i = 0; i < size; ++i) {
        dest[i] = ptrace(PTRACE_PEEKDATA, pid, addr, NULL);
    }
}

void ptrace_write(pid_t pid, uint8_t* addr, uint8_t* data, size_t size) {
    const size_t WORD_SIZE = sizeof(long);
    int mod = size % WORD_SIZE;
    int loop_count = size / WORD_SIZE;

    uint8_t* tmp_addr = addr;
    uint8_t* tmp_data = data;

    for (int i = 0; i < loop_count; ++i) {
        ptrace(PTRACE_POKEDATA, pid, tmp_addr, *((long*) tmp_data));
        tmp_addr += WORD_SIZE;
        tmp_data += WORD_SIZE;
    }

    if (mod > 0) {
        long val = ptrace(PTRACE_PEEKDATA, pid, tmp_addr, NULL);
        auto* p = (uint8_t*) &val;

        for(int i = 0; i < mod; ++i) {
            *p = *(tmp_data);
            p++;
            tmp_data++;
        }

        ptrace(PTRACE_POKEDATA, pid, tmp_addr, val);
    }

    ALOGD("write %zu bytes to %p process %d", size, (void*) addr, pid);
}

long call_remote_function_from_namespace(pid_t pid, long function_addr, long return_addr, long* args, size_t argc) {
    #if defined(__aarch64__)
        #define REGS_ARG_NUM    6
    #else
        #define REGS_ARG_NUM    4
    #endif

    struct pt_regs regs{};
    struct pt_regs backup_regs{};

    ptrace_get_regs(pid, &regs);
    memcpy(&backup_regs, &regs, sizeof(struct pt_regs));

    for (int i = 0; i < argc && i < REGS_ARG_NUM; ++i) {
        regs.uregs[i] = args[i];
    }

    if (argc > REGS_ARG_NUM) {
        regs.ARM_sp -= (argc - REGS_ARG_NUM) * sizeof(long);
        long* data = args + REGS_ARG_NUM;
        ptrace_write(pid, (uint8_t*) regs.ARM_sp, (uint8_t*) data, (argc - REGS_ARG_NUM) * sizeof(long));
    }

    regs.ARM_lr = return_addr;
    regs.ARM_pc = function_addr;

    #if !defined(__aarch64__)
        if (regs.ARM_pc & 1) {
                regs.ARM_pc &= (~1u);
                regs.ARM_cpsr |= CPSR_T_MASK;
            } else {
                regs.ARM_cpsr &= ~CPSR_T_MASK;
            }
    #endif

    ptrace_set_regs(pid, &regs);
    ptrace_cont(pid);

    waitpid(pid, nullptr, WUNTRACED);

    ptrace_get_regs(pid, &regs);
    ptrace_set_regs(pid, &backup_regs);

    ALOGD("call remote function %lx with %zu arguments, return value is %llx",
          function_addr, argc, (long long) regs.ARM_r0);

    return regs.ARM_r0;
}

long get_module_base_addr(pid_t pid, const char* module_name) {
    if (pid == -1) {
        return 0;
    }

    long base_addr_long = 0;

    char* file_name = (char*) calloc(50, sizeof(char));
    snprintf(file_name, 50, "/proc/%d/maps", pid);

    FILE* fp = fopen(file_name, "r");
    free(file_name);

    char line[512];
    if (fp != nullptr) {
        while(fgets(line, 512, fp) != nullptr) {
            if (strstr(line, module_name) != nullptr) {
                char* base_addr = strtok(line, "-");
                base_addr_long = strtoul(base_addr, nullptr, 16);
                break;
            }
        }

        fclose(fp);
    }

    return base_addr_long;
}

long get_remote_function_addr(pid_t remote_pid, const char* module_name, long local_function_addr) {
    long remote_base_addr = get_module_base_addr(remote_pid, module_name);
    long local_base_addr = get_module_base_addr(getpid(), module_name);

    if (remote_base_addr == 0 || local_base_addr == 0) {
        return 0;
    }

    return local_function_addr + (remote_base_addr - local_base_addr);
}

long call_mmap(pid_t pid, size_t length) {
    long params[6];
    params[0] = 0;
    params[1] = length;
    params[2] = PROT_READ | PROT_WRITE;
    params[3] = MAP_PRIVATE | MAP_ANONYMOUS;
    params[4] = 0;
    params[5] = 0;

    long function_addr = get_remote_function_addr(pid, get_libc_path(), ((long) (void*) mmap));
    return call_remote_function_from_namespace(pid, function_addr, 0, params, 6);
}

long call_munmap(pid_t pid, long addr, size_t length) {
    long params[2];
    params[0] = addr;
    params[1] = length;

    long function_addr = get_remote_function_addr(pid, get_libc_path(), ((long) (void*) munmap));
    return call_remote_function_from_namespace(pid, function_addr, 0, params, 2);
}

int main(int argc, char const* argv[]) {
    if (argc < 2 || argc > 3 ||
        (argc == 2 && strcmp(argv[1], "get") != 0) ||
        (argc == 3 && strcmp(argv[1], "set") != 0)) {
        printf("Usage: %s [get|set] [device class]\n", argv[0]);
        return -1;
    }

    const char* procName = "com.android.bluetooth";

    pid_t pid;
    if ((pid = Utils::getProcessId(procName)) == -1) {
        printf("Unable to find process %s", procName);
        return -1;
    }

    long baseAddress = Utils::getModuleBaseAddress(pid, "/system/lib/libbluetooth.so");

    if (Ptrace::attach(pid) != 0) {
        printf("Unable to attach to process %d", pid);
        return -1;
    }

    // TODO BEGIN: code cleanup, move into classes
    char virtualMemory[64];
    sprintf(virtualMemory, "/proc/%d/mem", pid);

    int fd = open(virtualMemory, O_RDONLY);
    if (!fd) {
        printf("Unable to open memory from process %d", pid);
    }

    char memoryBuffer[64];
    if (!pread(fd, memoryBuffer, 4, baseAddress)) {
        printf("Unable to read memory from process %d", pid);
    }

    struct stat buf;
    stat("/system/lib/libbluetooth.so", &buf);

    char* memory = (char*) malloc(sizeof(char) * buf.st_size);
    pread(fd, memory, buf.st_size, baseAddress);

    bool foundFlag = false;
    long remoteFunctionAddress = -1;

    const char signature[] = "\x48\x41\xf2\x38\x51\x78\x44\x00\x68\x08\x44\x70\x47\x00\xbf\x7a\xa7\x1d\x00\x80\xb5\x3b\xf7\x0b\xfe\xc1";
    for (long i = 0; i < buf.st_size; ++i) {
        for (int j = 0; j < (unsigned long int) strlen(signature); ++j) {
            foundFlag = signature[j] == memory[i + j] || signature[j] == '?';

            if (!foundFlag) {
                break;
            }
        }

        if (foundFlag) {
            remoteFunctionAddress = baseAddress + i;
        }
    }

    free(memory);

    if (!remoteFunctionAddress) {
        printf("Unable to find signature %s", signature);
        return -1;
    }

    long mmap_ret = call_mmap(pid, 0x400);
    call_remote_function_from_namespace(pid, remoteFunctionAddress, mmap_ret, nullptr, 0);
    long dev_class;
    ptrace_read(pid, &dev_class, (uint8_t*) mmap_ret, DEV_CLASS_LEN);
    printf("dev_class: %ld", dev_class);
    call_munmap(pid, mmap_ret, 0x400);

    close(fd);
    // TODO END

    if (Ptrace::detach(pid) != 0) {
        printf("Unable to detach from process %d", pid);
        return -1;
    }

    return 0;
}
