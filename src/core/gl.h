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
    F(void, Uniform2f, (GLint, GLfloat, GLfloat))                                    \
    F(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                           \
    F(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                  \
    F(void, Uniform1fv, (GLint, GLsizei, const GLfloat*))                            \
    F(void, Uniform3fv, (GLint, GLsizei, const GLfloat*))                            \
    F(void, ActiveTexture, (GLenum))                                                 \
    F(void, GenerateMipmap, (GLenum))                                                \
    F(void, TexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    F(void, GenVertexArrays, (GLsizei, GLuint*))                                     \
    F(void, BindVertexArray, (GLuint))                                               \
    F(void, DeleteVertexArrays, (GLsizei, const GLuint*))                            \
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
constexpr GLenum K_RGBA16F = 0x881A;
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
// chType : type GLSL des quatre canaux (sampler2D, samplerCube, sampler3D) ; nullptr = quatre sampler2D.
// cubemapOut : passe « Cubemap A » (mainCubemap) : le programme est execute une fois par face (uniform st_face 0 a 5).
// soundOut : passe « Sound » (mainSound) : un pixel = un echantillon stereo 16 bits (uniform st_sampleBase = indice du premier).
// alphaOut : l'image ecrit aussi sa transparence (fragColor.a) au lieu d'un alpha de 1 ; le fondu (st_weight) agit alors sur l'alpha.
// vrMode : image en realite virtuelle (mainVR) rendue en 360 equirectangulaire : 1 = mono, 2 = stereo haut / bas (oeil gauche en haut,
// iResolution = taille d'un oeil, uniform st_ipd = ecart des yeux).
GLuint buildProgram(const std::string& userCode, std::wstring& err, bool bufferOut = false, const std::string* chType = nullptr,
                    bool cubemapOut = false, bool soundOut = false, int vrMode = 0, const std::string& preamble = std::string(),
                    bool alphaOut = false);
// Programme plein ecran sans enveloppe Shadertoy (post-traitement) : fs est un fragment shader complet (#version 330 core), la sortie
// est `out vec4 st_o`. 0 + err si echec.
GLuint buildFullscreenProgram(const std::string& fs, std::wstring& err);
// Programme de recopie : accumulation flottante (flou de mouvement) -> image 8 bits
// alpha : l'accumulation contient des couleurs prémultipliées (alpha en 4e composante) : la copie rend une couleur a alpha droit.
GLuint buildPresentProgram(std::wstring& err, bool alpha = false);
