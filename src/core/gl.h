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
    F(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                     \
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
    F(unsigned char, UnmapBuffer, (GLenum))                                          \
    F(void, BufferSubData, (GLenum, ptrdiff_t, ptrdiff_t, const void*))              \
    F(void, GetBufferSubData, (GLenum, ptrdiff_t, ptrdiff_t, void*))                 \
    F(void, BindBufferBase, (GLenum, GLuint, GLuint))                                \
    F(void, VertexAttribPointer, (GLuint, GLint, GLenum, unsigned char, GLsizei, const void*)) \
    F(void, EnableVertexAttribArray, (GLuint))                                       \
    F(void, DisableVertexAttribArray, (GLuint))                                      \
    F(void, DrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))                  \
    F(void, DrawElementsInstanced, (GLenum, GLsizei, GLenum, const void*, GLsizei))  \
    F(void, UniformMatrix3fv, (GLint, GLsizei, unsigned char, const GLfloat*))       \
    F(void, UniformMatrix4fv, (GLint, GLsizei, unsigned char, const GLfloat*))

// Fonctions facultatives (OpenGL 4.3 : passes de calcul) : chargees sans faire echouer le chargement ; computeAvailable() dit si le contexte les offre.
#define GL_FUNCS_OPT(F)                                                              \
    F(void, DispatchCompute, (GLuint, GLuint, GLuint))                               \
    F(void, MemoryBarrier, (GLbitfield))                                                 F(void, GetProgramBinary, (GLuint, GLsizei, GLsizei*, GLenum*, void*))               F(void, ProgramBinary, (GLuint, GLenum, const void*, GLsizei))                       F(void, ProgramParameteri, (GLuint, GLenum, GLint))

#define F(r, n, a)                                \
    typedef r(APIENTRY* PFN_##n) a;               \
    extern PFN_##n n;
GL_FUNCS(F)
GL_FUNCS_OPT(F)
#undef F

bool load();  // charge les fonctions ci-dessus (contexte courant requis)
// Le contexte courant offre OpenGL 4.3 (passes de calcul : GLSL 4.30, tampons de stockage) et ses fonctions sont chargees.
bool computeAvailable();

// Constantes (prefixees pour ne jamais entrer en conflit avec un gl.h/glext.h)
constexpr GLenum K_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum K_VERTEX_SHADER = 0x8B31;
constexpr GLenum K_COMPILE_STATUS = 0x8B81;
constexpr GLenum K_LINK_STATUS = 0x8B82;
constexpr GLenum K_PROGRAM_BINARY_LENGTH = 0x8741;
constexpr GLenum K_PROGRAM_BINARY_RETRIEVABLE_HINT = 0x8257;
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
constexpr GLenum K_ARRAY_BUFFER = 0x8892;
constexpr GLenum K_ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum K_STATIC_DRAW = 0x88E4;
constexpr GLenum K_DYNAMIC_DRAW = 0x88E8;
constexpr GLenum K_SHADER_STORAGE_BUFFER = 0x90D2;
constexpr GLenum K_COMPUTE_SHADER = 0x91B9;
constexpr GLenum K_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum K_DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum K_DEPTH_COMPONENT32F = 0x8CAC;
constexpr GLenum K_TEXTURE_CUBE_MAP = 0x8513;
constexpr GLenum K_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515;
constexpr GLenum K_TEXTURE_WRAP_R = 0x8072;
constexpr GLenum K_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum K_FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLbitfield K_SHADER_STORAGE_BARRIER_BIT = 0x2000;
constexpr GLbitfield K_BUFFER_UPDATE_BARRIER_BIT = 0x0200;
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
// scene : programme d'une scene 3D (edition Pro) : etage sommets fourni, version GLSL choisie ; le fragment est le meme que celui du mode image.
// colorStage : code GLSL d'une extension (edition Pro) qui definit `vec3 st_color(vec3 c)`, applique a la couleur avant le fondu et l'ecriture ; "" = aucun.
// En cas d'echec de compilation de l'etage sommets, vertexFailed passe a vrai (les erreurs se rapportent alors au fichier des sommets).
struct SceneProgram {
    std::string version = "330";  // « 330 » ou « 430 » (passes de calcul)
    std::string vertex;           // vertex shader complet, ligne #version comprise
    std::string fragmentPrelude;  // declarations GLSL ajoutees avant le code du fragment shader (uniforms de la scene)
    bool vertexFailed = false;
};
GLuint buildProgram(const std::string& userCode, std::wstring& err, bool bufferOut = false, const std::string* chType = nullptr,
                    bool cubemapOut = false, bool soundOut = false, int vrMode = 0, const std::string& preamble = std::string(),
                    bool alphaOut = false, SceneProgram* scene = nullptr, const std::string& colorStage = std::string());
// Compile un programme a partir de deux sources completes (#version comprise) : 0 + err si echec ; *vertexFailed dit si l'etage sommets a echoue.
GLuint buildProgramFromSources(const std::string& vs, const std::string& fs, std::wstring& err, bool* vertexFailed = nullptr);
// Programme de calcul (OpenGL 4.3) : source complete avec #version 430 ; 0 + err si echec ou si le contexte n'offre pas les passes de calcul.
GLuint buildComputeProgram(const std::string& cs, std::wstring& err);
// Programme plein ecran sans enveloppe Shadertoy (post-traitement) : fs est un fragment shader complet (#version 330 core), la sortie
// est `out vec4 st_o`. 0 + err si echec.
GLuint buildFullscreenProgram(const std::string& fs, std::wstring& err);
// Programme de recopie : accumulation flottante (flou de mouvement) -> image 8 bits
// alpha : l'accumulation contient des couleurs prémultipliées (alpha en 4e composante) : la copie rend une couleur a alpha droit.
GLuint buildPresentProgram(std::wstring& err, bool alpha = false);
