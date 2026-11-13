---@meta
-- Editor definitions for ScriptBindings.cpp. Never require/dofile this file in gameplay.
---@class HakiVec3
---@field x number
---@field y number
---@field z number
---@operator add(HakiVec3): HakiVec3
---@operator sub(HakiVec3): HakiVec3
---@operator mul(number): HakiVec3
---@operator mul(HakiVec3): HakiVec3
---@operator div(number): HakiVec3
---@operator div(HakiVec3): HakiVec3
---@operator unm: HakiVec3
local Vec3 = {}
---@return number
function Vec3:length() end

---@return number
function Vec3:length_squared() end

---@return HakiVec3
function Vec3:normalized() end

---@return HakiVec3
function Vec3:flat() end

---@return HakiVec3
function Vec3:copy() end

---@param other HakiVec3
---@return number
function Vec3:dot(other) end

---@param other HakiVec3
---@return HakiVec3
function Vec3:cross(other) end

---@param other HakiVec3
---@return number
function Vec3:distance(other) end

---@param other HakiVec3
---@param t number
---@return HakiVec3
function Vec3:lerp(other, t) end

---@class HakiVec3Factory
---@overload fun(): HakiVec3
---@overload fun(s: number): HakiVec3
---@overload fun(x: number, y: number, z: number): HakiVec3
vec3 = {}
---@return HakiVec3
function vec3.zero() end

---@return HakiVec3
function vec3.up() end

---@param a HakiVec3
---@param b HakiVec3
---@param t number
---@return HakiVec3
function vec3.lerp(a, b, t) end

---@class HakiQuat
---@operator mul(HakiVec3): HakiVec3
---@operator mul(HakiQuat): HakiQuat
local Quat = {}
---@return number
function Quat:yaw() end

---@return HakiQuat
function Quat:inverse() end

---@return HakiVec3
function Quat:forward() end

---@return HakiVec3
function Quat:right() end

---@class HakiQuatFactory
quat = {}
---Angles are in radians.
---@return HakiQuat
function quat.identity() end

---Angles are in radians.
---@param radians number
---@return HakiQuat
function quat.from_yaw(radians) end

---Angles are in radians.
---@param x number
---@param y number
---@param z number
---@return HakiQuat
function quat.from_euler(x, y, z) end

---Angles are in radians.
---@param axis HakiVec3
---@param radians number
---@return HakiQuat
function quat.from_axis_angle(axis, radians) end

---Angles are in radians.
---@param direction HakiVec3
---@return HakiQuat
function quat.look_rotation(direction) end

---Angles are in radians.
---@param a HakiQuat
---@param b HakiQuat
---@param t number
---@return HakiQuat
function quat.slerp(a, b, t) end

---@class HakiEntity
Entity = {}
---@return boolean
function Entity:valid() end

---@return integer
function Entity:index() end

---@return string
function Entity:id() end

---@return string
function Entity:name() end

---@return HakiVec3
function Entity:position() end

---@return HakiQuat
function Entity:rotation() end

---@return number
function Entity:yaw() end

---@return HakiVec3
function Entity:scale() end

---@return HakiVec3
function Entity:forward() end

---@return HakiVec3
function Entity:right() end

---@return HakiEntityScript?
function Entity:script() end

---@return boolean
function Entity:is_character() end

---@return HakiVec3
function Entity:move_velocity() end

---@return boolean
function Entity:is_grounded() end

---@return HakiVec3
function Entity:ground_normal() end

---@return HakiVec3
function Entity:velocity() end

---@return boolean
function Entity:is_camera() end

---@return boolean
function Entity:is_orthographic() end

---@return number
function Entity:ortho_size() end

---@return number
function Entity:fov() end

---True if this entity, or one of its children, has an animator.
---@return boolean
function Entity:has_animator() end

---@param name string
---@return boolean
function Entity:has_animation(name) end

---@class HakiPlayAnimationOptions
---@field fade number? seconds to blend from the current pose (default: the animator's defaultFade)
---@field loop boolean? default false: play once, then return to locomotion
---@field speed number? playback speed multiplier (default 1)

---Plays a clip on this entity's animator (or its child's), crossfading from the current pose.
---A one-shot pauses locomotion and blends back into it when it ends.
---@param name string
---@param options HakiPlayAnimationOptions?
function Entity:play_animation(name, options) end

---Fades back to locomotion (or freezes the clip if there is no locomotion).
---@param fade number?
function Entity:stop_animation(fade) end

---The clip playing, or "locomotion" while the speed blend is in control.
---@return string
function Entity:animation() end

---@return number
function Entity:animation_time() end

---Length in seconds of `name`, or of the current clip.
---@param name string?
---@return number
function Entity:animation_length(name) end

---True once a one-shot has reached its end (until the animator returns to locomotion).
---@return boolean
function Entity:animation_finished() end

---Horizontal speed (m/s) the locomotion blend is using. Handy for tuning blend speeds.
---@return number
function Entity:locomotion_speed() end

---@return boolean
function Entity:has_body() end

---@param position HakiVec3
function Entity:set_position(position) end

---@param rotation HakiQuat
function Entity:set_rotation(rotation) end

---@param radians number
function Entity:set_yaw(radians) end

---@param scale HakiVec3
function Entity:set_scale(scale) end

---@param target HakiVec3
function Entity:look_at(target) end

function Entity:destroy() end

---@param velocity HakiVec3
function Entity:set_move_velocity(velocity) end

---@param size number?
function Entity:set_orthographic(size) end

---@param fovDegrees number?
function Entity:set_perspective(fovDegrees) end

---@param impulse HakiVec3
---@return boolean
function Entity:add_impulse(impulse) end

---@param velocity HakiVec3
---@return boolean
function Entity:set_velocity(velocity) end

---@class HakiSceneAPI
scene = {}
---@param id string
---@return HakiEntity?
function scene.find(id) end

---@param name string
---@return HakiEntity?
function scene.find_by_name(name) end

---@return HakiEntity?
function scene.primary_camera() end

---@param path string
---@return HakiEntity[]
function scene.entities_with_script(path) end

---@return HakiEntity[]
function scene.characters() end

---@param entity HakiEntity
function scene.destroy(entity) end

---@class HakiRaycastHit
---@field entity HakiEntity
---@field point HakiVec3
---@field normal HakiVec3
---@field distance number
---@class HakiPhysicsAPI
physics = {}
---@param origin HakiVec3
---@param direction HakiVec3
---@param maxDistance number
---@param includeTriggers boolean?
---@return HakiRaycastHit?
function physics.raycast(origin, direction, maxDistance, includeTriggers) end

---@param center HakiVec3
---@param radius number
---@param includeTriggers boolean?
---@return HakiEntity[]
function physics.overlap_sphere(center, radius, includeTriggers) end

---@class HakiMoveInput
---@field x number Right, normalized with y to length <= 1.
---@field y number Forward.
---@class HakiInputAPI
input = {}
---@param name string
---@return boolean
function input.key_down(name) end

---@param name string
---@return boolean
function input.key_pressed(name) end

---@param name string
---@return boolean
function input.key_released(name) end

---@param name string
---@return boolean
function input.mouse_down(name) end

---@param name string
---@return boolean
function input.mouse_pressed(name) end

---@param pad integer
---@return boolean
function input.gamepad_connected(pad) end

---Gamepad index starts at 0.
---@param pad integer
---@param name string
---@return boolean
function input.gamepad_down(pad, name) end

---Gamepad index starts at 0.
---@param pad integer
---@param name string
---@return boolean
function input.gamepad_pressed(pad, name) end

---Gamepad index starts at 0. Sticks are -1..1 with right = +1 and up = +1; triggers are 0..1.
---@param pad integer
---@param name string
---@return number
function input.gamepad_axis(pad, name) end

---@return HakiMoveInput
function input.move() end

---@class HakiTime
---@field dt number Fixed tick duration in seconds.
---@field elapsed number Simulation elapsed time in seconds.
---@field frame integer Simulation tick counter.
time = {}
---@class HakiLogAPI
log = {}
---@param ... any
function log.info(...) end
---@param ... any
function log.warn(...) end
---@param ... any
function log.error(...) end

---@alias HakiScriptProperty boolean|number|string|HakiVec3

---Extend this class on entity behaviour tables. Property defaults/overrides become fields on self.
---@class HakiEntityScript
---@field entity HakiEntity Injected by the runtime; not present on scene scripts.
---@field properties? table<string, HakiScriptProperty>
---@field on_start? fun(self: HakiEntityScript)
---@field on_update? fun(self: HakiEntityScript, dt: number)
---@field on_destroy? fun(self: HakiEntityScript)
---@field on_trigger_enter? fun(self: HakiEntityScript, other: HakiEntity)
---@field on_trigger_exit? fun(self: HakiEntityScript, other: HakiEntity)
---@field on_contact_begin? fun(self: HakiEntityScript, other: HakiEntity)
---@field on_contact_end? fun(self: HakiEntityScript, other: HakiEntity)

---Scene callbacks receive both contact participants and have no self.entity.
---@class HakiSceneScript
---@field properties? table<string, HakiScriptProperty>
---@field on_start? fun(self: HakiSceneScript)
---@field on_update? fun(self: HakiSceneScript, dt: number)
---@field on_destroy? fun(self: HakiSceneScript)
---@field on_trigger_enter? fun(self: HakiSceneScript, first: HakiEntity, second: HakiEntity)
---@field on_trigger_exit? fun(self: HakiSceneScript, first: HakiEntity, second: HakiEntity)
---@field on_contact_begin? fun(self: HakiSceneScript, first: HakiEntity, second: HakiEntity)
---@field on_contact_end? fun(self: HakiSceneScript, first: HakiEntity, second: HakiEntity)

---@param value HakiVec3
---@return number
function vec3.length(value) end

---@param value HakiVec3
---@return number
function vec3.length_squared(value) end

---@param value HakiVec3
---@return HakiVec3
function vec3.normalized(value) end

---@param value HakiVec3
---@return HakiVec3
function vec3.flat(value) end

---@param value HakiVec3
---@return HakiVec3
function vec3.copy(value) end

---@param a HakiVec3
---@param b HakiVec3
---@return number
function vec3.dot(a, b) end

---@param a HakiVec3
---@param b HakiVec3
---@return HakiVec3
function vec3.cross(a, b) end

---@param a HakiVec3
---@param b HakiVec3
---@return number
function vec3.distance(a, b) end

---@param value HakiQuat
---@return number
function quat.yaw(value) end

---@param value HakiQuat
---@return HakiQuat
function quat.inverse(value) end

---@param value HakiQuat
---@return HakiVec3
function quat.forward(value) end

---@param value HakiQuat
---@return HakiVec3
function quat.right(value) end


---Current viewport width divided by height (16:9 default in headless scenes).
---@return number
function scene.viewport_aspect() end
