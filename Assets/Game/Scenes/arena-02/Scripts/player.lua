-- Arena player: camera-relative movement. Hold Shift to walk; the visual's speed blend
-- picks Idle / Walk / Run from the controller's actual speed.
local movement = require("shared.lib.movement")

---@class ArenaPlayer : HakiEntityScript
---@field speed number
---@field walk_speed number
---@field spawn HakiVec3
local Player = { properties = { speed = 5.0, walk_speed = 1.3 } }

function Player:on_start()
    self.spawn = self.entity:position():copy()
end

function Player:on_update(dt)
    local direction = movement.camera_relative(input.move(), scene.primary_camera())
    local speed = input.key_down("shift") and self.walk_speed or self.speed
    self.entity:set_move_velocity(direction * speed)
    if direction:length() > 0.001 then
        self.entity:set_rotation(quat.look_rotation(direction))
    end
    if self.entity:position().y < -5 then
        self.entity:set_position(self.spawn)
    end
end

return Player
