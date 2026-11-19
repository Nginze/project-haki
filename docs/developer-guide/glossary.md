# Glossary

[Guide index](README.md)

These are working definitions for this project, not formal textbook definitions.

| Term | Plain meaning |
| --- | --- |
| Accumulator | Unprocessed simulation time saved between rendered frames |
| Alpha | A value used for transparency or cutout coverage |
| Animation clip | Timed keys that change skeleton node transforms |
| Armature | Blender's skeleton used to pose a character |
| Asset | A reusable resource such as a mesh, texture, or clip |
| Atlas | One texture containing many smaller images, such as font glyphs |
| Authoritative state | The state a game treats as the final truth; especially important for networking |
| Backface culling | Skipping the side of a triangle defined as facing away |
| Billboard | A visual that faces the camera; Haki canvas billboards follow a world anchor |
| Bind pose | The reference pose used when a mesh was attached to its skeleton |
| Binding | A connection that lets Lua call a C++ feature |
| Blend | Mix two values, images, or poses |
| Bloom | A post-process glow spread from bright image regions |
| Broad phase | The cheap physics search for possible collision pairs |
| Buffer | An area of memory storing data; GPU buffers hold data used for drawing |
| Catalog | A table connecting stable asset names to typed sources |
| Collider | A shape used for collision, separate from visible geometry |
| Component | One piece of data attached to an entity |
| Context, OpenGL | The active connection/state needed to use OpenGL for a window |
| Crossfade | A gradual transition between poses |
| Delta time / dt | The seconds represented by an update |
| Deserialize | Read stored data and rebuild usable objects from it |
| Draw call | One instruction to the GPU to draw a range of geometry |
| Dynamic body | A body moved by physics forces, velocity, and collision response |
| ECS | Entity-component system: build objects from data and process them with systems |
| Entity | A runtime object identifier to which components can be attached |
| Factory | A function that creates an object when requested |
| Fixed step | A simulation update using the same time interval each tick |
| Fragment shader | GPU code that computes a candidate pixel's surface output |
| Frame | One rendered picture, or one pass through the outer game loop |
| Framebuffer | A selection of images into which drawing writes |
| Frustum | The volume visible through a camera |
| Frustum culling | Avoiding ordinary drawing for bounds outside that volume |
| Generation | A version in a handle that prevents reuse of an old slot reference |
| GLB | Binary container form of glTF model data |
| glTF | Model format containing meshes and related scene/material/animation data |
| Handle | A small checked reference to a resource, rather than owning its contents |
| Hash | A number calculated from data such as a string |
| HDR | A color representation that can keep brightness above the normal display range |
| Hierarchy | Parent/child relationships |
| Hot reload | Replacing supported loaded code/resources after their source files change |
| IBL | Image-based lighting: use an environment image to help light surfaces |
| Index | A reference into a list; mesh indices let triangles reuse vertices |
| Interpolation | Finding an in-between value from known values |
| Joint palette | The matrices used to deform a skinned mesh for the current pose |
| Kinematic body | A physics body whose movement target is supplied by gameplay |
| Local transform | Placement relative to a parent or model coordinate system |
| Locomotion | Repeating movement animation such as idle, walk, and run |
| Material | Shader inputs and drawing rules for one surface appearance |
| Matrix | Numbers arranged to represent and combine transformations |
| Mesh | Geometry: vertices, triangle/index data, and related surface attributes |
| Narrow phase | Physics tests using the actual candidate shapes |
| Normal | A direction describing which way a surface faces |
| Orthographic | Projection where distance does not shrink an object's apparent size |
| Ownership | Responsibility for a resource's lifetime or a piece of state |
| PBR | A lighting/material approach using consistent reflection properties such as roughness |
| Perspective | Projection where farther objects appear smaller |
| Pose | Skeleton transforms at one moment |
| Post-processing | Filtering/adjusting a rendered image |
| Prefab | A reusable description expanded into scene entities at load time |
| Projection | The camera operation mapping a 3D view toward a 2D image |
| Quaternion | A four-number representation of a rotation |
| Rasterization | Turning projected triangles into covered screen samples |
| Raycast | Ask which collider a line hits first |
| Registry | EnTT's storage for the entities and components of a scene |
| Render pass | One stage of drawing with a particular purpose/output |
| Rigid body | Physics data describing a solid shape's motion and response |
| Root motion | Movement taken from an animation's root and applied to the gameplay object |
| Runtime | The engine while it is executing, with live state in memory |
| Schema | The accepted fields, types, and rules of a stored format |
| Sensor / trigger | An overlap-detecting shape that does not act as solid cover |
| Serialize | Convert structured data into a form that can be stored or sent |
| Shader | A program running on the GPU |
| Shadow map | Depth recorded from a light's view to test light blocking |
| Skinning | Deforming mesh vertices using weighted joint transforms |
| SSAO | Screen-space estimate of nearby surfaces blocking ambient light |
| Submesh | A drawable range of a mesh, often associated with a material |
| System | Code that operates on a particular set of components |
| Texture | An image or numeric grid sampled by the GPU |
| Tick | One simulation update |
| Tone mapping | Compressing HDR brightness into a displayable range |
| Transform | Position, rotation, and scale |
| Uniform | Shader input supplied by the CPU, shared across a drawing operation |
| UV | Texture lookup coordinates on a surface |
| Vertex | A mesh point with attributes such as position, normal, and UV |
| Vertex shader | GPU code that transforms a mesh's vertices |
| View matrix | Transform expressing the world relative to a camera |
| Viewport | The output rectangle used for rendering |
| World transform | Placement in the scene's shared coordinate system |
