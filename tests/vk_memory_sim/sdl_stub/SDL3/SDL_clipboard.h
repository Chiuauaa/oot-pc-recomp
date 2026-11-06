// Clipboard stand-in so runtime/platform/linux.c builds without SDL3 in the vk_memory_sim tests.
#pragma once
#include <stdbool.h>
static inline bool SDL_SetClipboardText(const char *t){(void)t;return true;}
static inline char *SDL_GetClipboardText(void){return 0;}
static inline void SDL_free(void*p){(void)p;}
static inline bool SDL_HasClipboardText(void){return false;}
