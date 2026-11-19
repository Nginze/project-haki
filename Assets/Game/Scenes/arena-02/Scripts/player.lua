-- Mesh-free controller for testing the arena; attach character visuals later.
local movement = require("shared.lib.movement")

---@class ArenaPlayer : HakiEntityScript
---@field speed number
---@field spawn HakiVec3
local Player = { properties = { speed = 5.0 } }

function Player:on_start()
    self.spawn = self.entity:position():copy()
end

function Player:on_update(dt)
    local direction = movement.camera_relative(input.move(), scene.primary_camera())
    self.entity:set_move_velocity(direction * self.speed)
    if direction:length() > 0.001 then
        self.entity:set_rotation(quat.look_rotation(direction))
    end
    if self.entity:position().y < -5 then
        self.entity:set_position(self.spawn)
    end
end

return Player
