// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Lien entre l'interface GLFW (thread principal) et le thread de rendu (Linux et macOS).
#pragma once

namespace platform {

// L'interface crée dans le thread principal une fenêtre GLFW cachée (contexte OpenGL 3.3 core) et la confie ici.
// createGLContext() la rend alors courante dans le thread de rendu, et destroyGLContext() la libère sans la détruire.
// Sans fenêtre confiée (auto-test en ligne de commande), createGLContext() en crée une : à appeler depuis le thread principal.
void setSharedGLWindow(void* glfwWindow);

}  // namespace platform
