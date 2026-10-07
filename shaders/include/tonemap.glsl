vec3 displayFromScene(vec3 scene, vec2 tonemap) {
    vec3 exposed       = scene * tonemap.x;
    float whiteSquared = tonemap.y * tonemap.y;
    return exposed * (1.0 + (exposed / whiteSquared)) / (1.0 + exposed);
}

vec3 sceneFromDisplay(vec3 display, vec2 tonemap) {
    float whiteSquared = tonemap.y * tonemap.y;
    vec3 gap           = 1.0 - display;
    vec3 exposed       = 0.5 * whiteSquared * (sqrt((gap * gap) + (4.0 * display / whiteSquared)) - gap);
    return exposed / tonemap.x;
}
