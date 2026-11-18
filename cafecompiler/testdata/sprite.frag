#version 450

layout(location = 0) in vec4 gColor;
layout(location = 1) in vec2 gUv;
layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D spriteTexture;

void main()
{
   color = texture(spriteTexture, gUv) * gColor;
}
