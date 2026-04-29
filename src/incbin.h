#pragma once

#if defined(_MSC_VER) && !defined(__llvm__)
#error "incbin.h is not supported on MSVC."
#endif

#define STR2(x) #x
#define STR(x) STR2(x)

#if defined(__APPLE__)
    #define INCBIN_SECTION "__TEXT,__const"
    #define INCBIN_PREFIX "_"
#elif defined(_WIN32)
    #define INCBIN_SECTION ".rdata, \"dr\""
    #define INCBIN_PREFIX ""
#else
    #define INCBIN_SECTION ".rodata"
    #define INCBIN_PREFIX ""
#endif

#define INCBIN(name, file) \
    __asm__(".section " INCBIN_SECTION "\n" \
            ".global " INCBIN_PREFIX "incbin_" STR(name) "_start\n" \
            ".balign 16\n" \
            INCBIN_PREFIX "incbin_" STR(name) "_start:\n" \
            ".incbin \"" file "\"\n" \
            \
            ".global " INCBIN_PREFIX "incbin_" STR(name) "_end\n" \
            ".balign 1\n" \
            INCBIN_PREFIX "incbin_" STR(name) "_end:\n" \
            ".byte 0\n" \
    ); \
    extern __attribute__((aligned(16))) const char incbin_ ## name ## _start[]; \
    extern                              const char incbin_ ## name ## _end[]
