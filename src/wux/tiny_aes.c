/*
 * tiny-AES-c (public domain, third_party/tiny-aes), built for CBC mode
 * only. The same three settings are repeated in aes_cbc.cpp, because they
 * decide the layout of struct AES_ctx.
 */
#define CBC 1
#define ECB 0
#define CTR 0
#include "third_party/tiny-aes/aes.c"
