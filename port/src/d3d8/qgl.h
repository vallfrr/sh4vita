// GL command queue (Vita only). The game thread queues the "hot" GL calls with a copy of the data;
// a render thread (core 1) executes them in order. The other calls (creation, queries, texture uploads, readbacks)
// first flush the queue and then run directly: vitaGL is therefore never called by two threads at once.
// SH4_NORTHREAD=1: everything as direct calls (for comparison).
#pragma once
#include <vitaGL.h>
#include <stddef.h>

extern int q_on;   // -1 not started yet, 0 direct calls, 1 queue active
void q_sync_n(const char *why);
static inline void q_sync(const char *why) { if (q_on == 1) q_sync_n(why); }
void q_swap(void);
void q_tex565(GLint level, GLsizei w, GLsizei h, const void *px, volatile int *busy);   // glTexSubImage2D 565 on the render thread; *busy = 0 once read
void q_report(double frames);

#ifndef QGL_IMPL
void q_glEnable(GLenum); void q_glDisable(GLenum); void q_glTexParameteri(GLenum, GLenum, GLint);
void q_glBindFramebuffer(GLenum, GLuint); void q_glBindTexture(GLenum, GLuint);
void q_glViewport(GLint, GLint, GLsizei, GLsizei); void q_glUniform4f(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
void q_glFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint);
void q_glColorMask(GLboolean, GLboolean, GLboolean, GLboolean); void q_glClear(GLbitfield); void q_glActiveTexture(GLenum);
void q_glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
void q_glUniform4fv(GLint, GLsizei, const GLfloat *); void q_glUniform1i(GLint, GLint); void q_glUniform1f(GLint, GLfloat);
void q_glUseProgram(GLuint); void q_glEnableVertexAttribArray(GLuint); void q_glDisableVertexAttribArray(GLuint);
void q_glDrawArrays(GLenum, GLint, GLsizei);
void q_glDrawRangeElements(GLenum, GLuint, GLuint, GLsizei, GLenum, const void *);
void q_glClearColor(GLfloat, GLfloat, GLfloat, GLfloat); void q_glClearDepth(GLdouble); void q_glClearStencil(GLint);
void q_glDepthRange(GLdouble, GLdouble); void q_glDepthFunc(GLenum); void q_glDepthMask(GLboolean);
void q_glStencilMask(GLuint); void q_glStencilOp(GLenum, GLenum, GLenum); void q_glStencilFunc(GLenum, GLint, GLuint);
void q_glScissor(GLint, GLint, GLsizei, GLsizei); void q_glPolygonOffset(GLfloat, GLfloat);
void q_glFrontFace(GLenum); void q_glCullFace(GLenum); void q_glBlendFunc(GLenum, GLenum); void q_glBlendEquation(GLenum);
void q_glBindBuffer(GLenum, GLuint); void q_glBufferData(GLenum, GLsizei, const GLvoid *, GLenum);
void q_glBufferSubData(GLenum, GLintptr, GLsizeiptr, const void *);
void q_glDeleteBuffers(GLsizei, const GLuint *); void q_glDeleteTextures(GLsizei, const GLuint *);
void q_glDeleteRenderbuffers(GLsizei, const GLuint *);
void q_glBindRenderbuffer(GLenum, GLuint); void q_glRenderbufferStorage(GLenum, GLenum, GLsizei, GLsizei);
void q_glFramebufferRenderbuffer(GLenum, GLenum, GLenum, GLuint);

#define glEnable q_glEnable
#define glDisable q_glDisable
#define glTexParameteri q_glTexParameteri
#define glBindFramebuffer q_glBindFramebuffer
#define glBindTexture q_glBindTexture
#define glViewport q_glViewport
#define glUniform4f q_glUniform4f
#define glFramebufferTexture2D q_glFramebufferTexture2D
#define glColorMask q_glColorMask
#define glClear q_glClear
#define glActiveTexture q_glActiveTexture
#define glVertexAttribPointer q_glVertexAttribPointer
#define glUniform4fv q_glUniform4fv
#define glUniform1i q_glUniform1i
#define glUniform1f q_glUniform1f
#define glUseProgram q_glUseProgram
#define glEnableVertexAttribArray q_glEnableVertexAttribArray
#define glDisableVertexAttribArray q_glDisableVertexAttribArray
#define glDrawArrays q_glDrawArrays
#define glDrawRangeElements q_glDrawRangeElements
#define glClearColor q_glClearColor
#define glClearDepth q_glClearDepth
#define glClearStencil q_glClearStencil
#define glDepthRange q_glDepthRange
#define glDepthFunc q_glDepthFunc
#define glDepthMask q_glDepthMask
#define glStencilMask q_glStencilMask
#define glStencilOp q_glStencilOp
#define glStencilFunc q_glStencilFunc
#define glScissor q_glScissor
#define glPolygonOffset q_glPolygonOffset
#define glFrontFace q_glFrontFace
#define glCullFace q_glCullFace
#define glBlendFunc q_glBlendFunc
#define glBlendEquation q_glBlendEquation
#define glBindBuffer q_glBindBuffer
#define glBufferData q_glBufferData
#define glBufferSubData q_glBufferSubData
#define glDeleteBuffers q_glDeleteBuffers
#define glDeleteTextures q_glDeleteTextures
#define glDeleteRenderbuffers q_glDeleteRenderbuffers
#define glBindRenderbuffer q_glBindRenderbuffer
#define glRenderbufferStorage q_glRenderbufferStorage
#define glFramebufferRenderbuffer q_glFramebufferRenderbuffer

// Calls with a result / unknown buffer size: queue flushed first, then direct call.
#define vglGetShaderBinary(...) (q_sync("vglGetShaderBinary"), vglGetShaderBinary(__VA_ARGS__))
#define vglMemFree(...) (q_sync("vglMemFree"), vglMemFree(__VA_ARGS__))
#define glGetUniformLocation(...) (q_sync("glGetUniformLocation"), glGetUniformLocation(__VA_ARGS__))
#define glGenFramebuffers(...) (q_sync("glGenFramebuffers"), glGenFramebuffers(__VA_ARGS__))
#define glGenRenderbuffers(...) (q_sync("glGenRenderbuffers"), glGenRenderbuffers(__VA_ARGS__))
#define glGenTextures(...) (q_sync("glGenTextures"), glGenTextures(__VA_ARGS__))
#define glGenBuffers(...) (q_sync("glGenBuffers"), glGenBuffers(__VA_ARGS__))
#define glTexImage2D(...) (q_sync("glTexImage2D"), glTexImage2D(__VA_ARGS__))
#define glCompressedTexImage2D(...) (q_sync("glCompressedTexImage2D"), glCompressedTexImage2D(__VA_ARGS__))
#define glReadPixels(...) (q_sync("glReadPixels"), glReadPixels(__VA_ARGS__))
#define glCheckFramebufferStatus(...) (q_sync("glCheckFramebufferStatus"), glCheckFramebufferStatus(__VA_ARGS__))
#define glGetError(...) (q_sync("glGetError"), glGetError(__VA_ARGS__))
#define glGetString(...) (q_sync("glGetString"), glGetString(__VA_ARGS__))
#define glCreateShader(...) (q_sync("glCreateShader"), glCreateShader(__VA_ARGS__))
#define glCreateProgram(...) (q_sync("glCreateProgram"), glCreateProgram(__VA_ARGS__))
#define glShaderSource(...) (q_sync("glShaderSource"), glShaderSource(__VA_ARGS__))
#define glShaderBinary(...) (q_sync("glShaderBinary"), glShaderBinary(__VA_ARGS__))
#define glCompileShader(...) (q_sync("glCompileShader"), glCompileShader(__VA_ARGS__))
#define glAttachShader(...) (q_sync("glAttachShader"), glAttachShader(__VA_ARGS__))
#define glDeleteShader(...) (q_sync("glDeleteShader"), glDeleteShader(__VA_ARGS__))
#define glBindAttribLocation(...) (q_sync("glBindAttribLocation"), glBindAttribLocation(__VA_ARGS__))
#define glLinkProgram(...) (q_sync("glLinkProgram"), glLinkProgram(__VA_ARGS__))
#define glGetProgramiv(...) (q_sync("glGetProgramiv"), glGetProgramiv(__VA_ARGS__))
#define glGetShaderiv(...) (q_sync("glGetShaderiv"), glGetShaderiv(__VA_ARGS__))
#define glGetShaderInfoLog(...) (q_sync("glGetShaderInfoLog"), glGetShaderInfoLog(__VA_ARGS__))
#define glGetProgramInfoLog(...) (q_sync("glGetProgramInfoLog"), glGetProgramInfoLog(__VA_ARGS__))
#define glVertexAttrib4f(...) (q_sync("glVertexAttrib4f"), glVertexAttrib4f(__VA_ARGS__))
#define glDeleteProgram(...) (q_sync("glDeleteProgram"), glDeleteProgram(__VA_ARGS__))
#define glDeleteFramebuffers(...) (q_sync("glDeleteFramebuffers"), glDeleteFramebuffers(__VA_ARGS__))
#define glCopyTexSubImage2D(...) (q_sync("glCopyTexSubImage2D"), glCopyTexSubImage2D(__VA_ARGS__))
#define glDrawElements(...) (q_sync("glDrawElements"), glDrawElements(__VA_ARGS__))
#define vglWaitVblankStart(...) (q_sync("vglWaitVblankStart"), vglWaitVblankStart(__VA_ARGS__))
#endif
