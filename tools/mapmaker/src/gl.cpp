#include "gl.h"

#include <SDL.h>

#include <cstdio>

#define NM_GL_DEFINE(type, name) type nm_##name = nullptr;
NM_GL_FUNCS(NM_GL_DEFINE)
#undef NM_GL_DEFINE

bool nmLoadGl()
{
	bool ok = true;
#define NM_GL_LOAD(type, name)                                                                                                             \
	nm_##name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));                                                                     \
	if (!nm_##name) {                                                                                                                      \
		fprintf(stderr, "[mapmaker] falta la función de OpenGL %s\n", #name);                                                             \
		ok = false;                                                                                                                        \
	}
	NM_GL_FUNCS(NM_GL_LOAD)
#undef NM_GL_LOAD
	return ok;
}
