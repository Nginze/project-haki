#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(std140) uniform HakiCamera
{
    mat4 View;
    mat4 Projection;
    mat4 ViewProjection;
    mat4 InverseViewProjection;
    vec4 CameraWorldPosition;
    vec4 Viewport;
};

layout(std140) uniform HakiObject
{
    mat4 Model;
    mat4 NormalMatrix;
};

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord;

// Must match the depth prepass bit-for-bit so the forward pass can depth-test with GL_LEQUAL.
invariant gl_Position;

void main()
{
    vec4 worldPosition = Model * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize((NormalMatrix * vec4(aNormal, 0.0)).xyz);
    vTexCoord = aTexCoord;
    gl_Position = ViewProjection * worldPosition;
}
