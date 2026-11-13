#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in vec4 aColor;
layout(location = 5) in uvec4 aJoints;
layout(location = 6) in vec4 aWeights;
layout(location = 7) in vec2 aTexCoord1;

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

layout(std140) uniform HakiSkin
{
    mat4 JointMatrices[128];
};

uniform int HakiSkinningEnabled;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec3 vWorldTangent;
out vec3 vWorldBitangent;
out vec2 vTexCoord;
out vec2 vTexCoord1;
out vec4 vColor;
flat out float vModelScale;

// Must match the depth prepass bit-for-bit so the forward pass can depth-test with GL_LEQUAL.
invariant gl_Position;

void main()
{
    mat4 skinMatrix = mat4(1.0);
    if (HakiSkinningEnabled != 0)
    {
        vec4 weights = aWeights / max(dot(aWeights, vec4(1.0)), 0.000001);
        skinMatrix = JointMatrices[aJoints.x] * weights.x +
                     JointMatrices[aJoints.y] * weights.y +
                     JointMatrices[aJoints.z] * weights.z +
                     JointMatrices[aJoints.w] * weights.w;
    }

    vec4 worldPosition = Model * skinMatrix * vec4(aPosition, 1.0);
    mat3 skinNormalMatrix = transpose(inverse(mat3(skinMatrix)));
    vec3 normal = normalize((NormalMatrix * vec4(skinNormalMatrix * aNormal, 0.0)).xyz);
    vec3 tangent = (Model * vec4(mat3(skinMatrix) * aTangent.xyz, 0.0)).xyz;
    tangent = normalize(tangent - normal * dot(normal, tangent));
    float orientation = determinant(mat3(Model) * mat3(skinMatrix)) < 0.0 ? -1.0 : 1.0;

    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normal;
    vWorldTangent = tangent;
    vWorldBitangent = normalize(cross(normal, tangent)) * aTangent.w * orientation;
    vTexCoord = aTexCoord;
    vTexCoord1 = aTexCoord1;
    vColor = aColor;
    vModelScale = max(length(Model[0].xyz), max(length(Model[1].xyz), length(Model[2].xyz)));
    gl_Position = ViewProjection * worldPosition;
}
