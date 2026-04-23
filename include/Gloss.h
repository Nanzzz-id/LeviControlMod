
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void*     GlossOpen(const char* lib_name);
void*     GlossSymbol(void* handle, const char* sym_name);
int       GlossHook(void* target, void* hook, void** orig);
int       GlossHookByName(void* handle, const char* sym, void* hook, void** orig);
uintptr_t GlossGetLibBias(void* handle);
void      GlossInit();
void      GlossClose(void* handle);

#ifdef __cplusplus
}
#endif
