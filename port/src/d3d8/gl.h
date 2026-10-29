// OpenGL common to PC / Vita. On PC, GL >= 1.2 functions are loaded through SDL.
#pragma once
#ifdef __vita__
#include <vitaGL.h>
#include "qgl.h"
static inline void gl_load(void) {}
#else
#include <SDL.h>
#include <SDL_opengl.h>

#define GL_FUNCS(X) \
    X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORM1FPROC, glUniform1f) X(PFNGLUNIFORM4FVPROC, glUniform4fv) X(PFNGLUNIFORM4FPROC, glUniform4f) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) X(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) X(PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation) X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLBINDBUFFERPROC, glBindBuffer) X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData) X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)

#define GL_DECL(type, name) extern type p_##name;
GL_FUNCS(GL_DECL)
#undef GL_DECL
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glBindAttribLocation p_glBindAttribLocation
#define glLinkProgram p_glLinkProgram
#define glGetProgramiv p_glGetProgramiv
#define glGetProgramInfoLog p_glGetProgramInfoLog
#define glUseProgram p_glUseProgram
#define glGetUniformLocation p_glGetUniformLocation
#define glUniform1i p_glUniform1i
#define glUniform1f p_glUniform1f
#define glUniform4fv p_glUniform4fv
#define glUniform4f p_glUniform4f
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glDisableVertexAttribArray p_glDisableVertexAttribArray
#define glVertexAttribPointer p_glVertexAttribPointer
#define glVertexAttrib4f p_glVertexAttrib4f
#define glActiveTexture p_glActiveTexture
#define glCompressedTexImage2D p_glCompressedTexImage2D
#define glGenFramebuffers p_glGenFramebuffers
#define glBindFramebuffer p_glBindFramebuffer
#define glFramebufferTexture2D p_glFramebufferTexture2D
#define glGenRenderbuffers p_glGenRenderbuffers
#define glBindRenderbuffer p_glBindRenderbuffer
#define glRenderbufferStorage p_glRenderbufferStorage
#define glFramebufferRenderbuffer p_glFramebufferRenderbuffer
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glDeleteFramebuffers p_glDeleteFramebuffers
#define glDeleteRenderbuffers p_glDeleteRenderbuffers
#define glBlendEquation p_glBlendEquation
#define glDeleteProgram p_glDeleteProgram
#define glDeleteShader p_glDeleteShader
#define glGenBuffers p_glGenBuffers
#define glBindBuffer p_glBindBuffer
#define glBufferData p_glBufferData
#define glBufferSubData p_glBufferSubData
#define glDeleteBuffers p_glDeleteBuffers
void gl_load(void);
#endif
