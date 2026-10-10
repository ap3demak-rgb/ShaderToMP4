// Anneaux lumineux qui se propagent depuis le centre
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float d = length(uv);
    float rings = 0.5 + 0.5 * sin(40.0 * d - iTime * 4.0);
    vec3 col = mix(vec3(0.05, 0.1, 0.35), vec3(1.0, 0.7, 0.2), rings * smoothstep(0.6, 0.0, d));
    col += 0.25 * vec3(0.4, 0.2, 0.8) * smoothstep(0.5, 0.0, abs(d - 0.25 - 0.05 * sin(iTime)));
    fragColor = vec4(col, 1.0);
}
