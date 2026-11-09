#version 440
// The reflected range's quad (ReflectedRange.cpp). Its second attribute is
// the item's own pixels: the batch renderer may move vertex into the scene.

layout(location = 0) in vec4 vertex;
layout(location = 1) in vec2 itemPos;
layout(location = 0) out vec2 pos;

// The same block as reflectedrange.frag.
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 color;
    vec4 farColor;
    vec4 glowColor;
    vec4 history[24];
    vec2 size;
    float lines;
    float row;
    float frac;
    float sub;
    float time;
    float rest;
    float lineWidth;
    float glowWidth;
    float px;
    float alpha;
    float ink;
};

void main()
{
    pos = itemPos;
    gl_Position = qt_Matrix * vertex;
}
