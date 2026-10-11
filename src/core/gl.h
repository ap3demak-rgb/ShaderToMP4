// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// OpenGL 3.3 : fonctions chargees a la main (pas de GLEW/GLAD), contexte sur fenetre cachee, programmes de shaders.
#pragma once
#include "common.h"

namespace ogl {
#define GL_FUNCS(F)                                                                  \
    F(GLuint, CreateShader, (GLenum))                                                \
    F(void, ShaderSource, (GLuint, GLsizei, const char* const*, const GLint*))       \
    F(void, CompileShader, (GLuint))                                                 \
    F(void, GetShaderiv, (GLuint, GLenum, GLint*))                                   \
    F(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, char*))                    \
    F(void, DeleteShader, (GLuint))                                                  \
    F(GLuint, CreateProgram, (void))                                                 \
    F(void, AttachShader, (GLuint, GLuint))                                          \
    F(void, LinkProgram, (GLuint))                                                   \
    F(void, GetProgramiv, (GLuint, GLenum, GLint*))                                  \
    F(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, char*))                   \
    F(void, UseProgram, (GLuint))                                                    \
    F(void, DeleteProgram, (GLuint))                                                 \
    F(GLint, GetUniformLocation, (GLuint, const char*))                              \
    F(void, Uniform1f, (GLint, GLfloat))                                             \
    F(void, Uniform1i, (GLint, GLint))                                               \
    F(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                           \
    F(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                  \
    F(void, Uniform1fv, (GLint, GLsizei, const GLfloat*))                            \
    F(void, Uniform3fv, (GLint, GLsizei, const GLfloat*))                            \
    F(void, ActiveTexture, (GLenum))                                                 \
    F(void, GenerateMipmap, (GLenum))                                                \
    F(void, GenVertexArrays, (GLsizei, GLuint*))                                     \
    F(void, BindVertexArray, (GLuint))                                               \
    F(void, GenFramebuffers, (GLsizei, GLuint*))                                     \
    F(void, DeleteFramebuffers, (GLsizei, const GLuint*))                            \
    F(void, BindFramebuffer, (GLenum, GLuint))                                       \
    F(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))           \
    F(GLenum, CheckFramebufferStatus, (GLenum))                                      \
    F(void, GenBuffers, (GLsizei, GLuint*))                                          \
    F(void, DeleteBuffers, (GLsizei, const GLuint*))                                 \
    F(void, BindBuffer, (GLenum, GLuint))                                            \
    F(void, BufferData, (GLenum, ptrdiff_t, const void*, GLenum))                    \
    F(void*, MapBuffer, (GLenum, GLenum))                                            \
    F(unsigned char, UnmapBuffer, (GLenum))

#define F(r, n, a)                                \
    typedef r(APIENTRY* PFN_##n) a;               \
    extern PFN_##n n;
GL_FUNCS(F)
#undef F

bool load();  // charge les fonctions ci-dessus (contexte courant requis)

// Constantes (prefixees pour ne jamais entrer en conflit avec un gl.h/glext.h)
constexpr GLenum K_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum K_VERTEX_SHADER = 0x8B31;
constexpr GLenum K_COMPILE_STATUS = 0x8B81;
constexpr GLenum K_LINK_STATUS = 0x8B82;
constexpr GLenum K_FRAMEBUFFER = 0x8D40;
constexpr GLenum K_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum K_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum K_PIXEL_PACK_BUFFER = 0x88EB;
constexpr GLenum K_STREAM_READ = 0x88E1;
constexpr GLenum K_READ_ONLY = 0x88B8;
constexpr GLenum K_RGBA8 = 0x8058;
constexpr GLenum K_RGBA16 = 0x805B;
constexpr GLenum K_RGBA32F = 0x8814;
}  // namespace ogl

// Le contexte est lie au thread qui l'a cree : a creer et detruire dans le thread de rendu.
struct GLContext {
    void* impl = nullptr;  // contexte de la couche plateforme
};

void destroyGLContext(GLContext& c);
bool createGLContext(GLContext& c, std::wstring& err);  // contexte 3.3 core + chargement de ogl::*

// Programme principal : enveloppe Shadertoy autour du code utilisateur (mainImage). 0 + err si echec.
// bufferOut : passe de tampon (Buffer A-D) : la couleur complete, alpha compris, est ecrite sans ponderation de fondu.
GLuint buildProgram(const std::string& userCode, std::wstring& err, bool bufferOut = false);
// Programme de recopie : accumulation flottante (flou de mouvement) -> image 8 bits
GLuint buildPresentProgram(std::wstring& err);
