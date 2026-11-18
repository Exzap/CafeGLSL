#version 450

// Expands each point into a screen-aligned quad.
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(location = 0) in vec4 vColor[];
layout(location = 0) out vec4 gColor;
layout(location = 1) out vec2 gUv;

layout(binding = 1, std140) uniform SpriteData {
   vec4 halfSize;
};

void main()
{
   for (int i = 0; i < 4; ++i) {
      vec2 corner = vec2(float(i & 1), float(i >> 1));
      gColor = vColor[0];
      gUv = corner;
      gl_Position = gl_in[0].gl_Position +
                    vec4((corner * 2.0 - 1.0) * halfSize.xy, 0.0, 0.0);
      EmitVertex();
   }
   EndPrimitive();
}
