// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// mainVR : rendu 360 equirectangulaire mono (sans mainImage, le mode est choisi tout seul). La couleur est la direction du rayon :
// au centre de l'image (devant, -Z) c'est (0.5, 0.5, 0), au quart gauche (-X) c'est (0, 0.5, 0.5), en haut (+Y) le vert tend vers 1
void mainVR(out vec4 fragColor, in vec2 fragCoord, in vec3 fragRayOri, in vec3 fragRayDir)
{
    fragColor = vec4(fragRayDir * 0.5 + 0.5, 1.0);
}
