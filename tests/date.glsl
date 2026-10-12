// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iDate animee : a t = 1 s avec +rate=11, la date passe de 2026-12-31 23:59:50 a 2027-01-01 00:00:01 (changement de jour, de mois et d'annee)
// date=2026-12-31T23:59:50+rate=11
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c = vec3((iDate.x - 2026.0) * 0.5, iDate.y / 11.0, iDate.z / 31.0);
    fragColor = vec4(uv.x < 0.5 ? c : vec3(iDate.w / 60.0), 1.0);
}
