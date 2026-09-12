#version 450
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform ColorBlock { vec4 color; };
void main() { fragColor = color; }
