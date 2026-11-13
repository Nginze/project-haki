-- Locally controlled brawler. Attach with: "script": { "path": "player.lua" }
local movement = require("shared.lib.movement")

---@class Player : HakiEntityScript
---@field speed number
---@field shove_range number
---@field shove_radius number
---@field shove_force number
---@field shove_cooldown number
---@field cooldown number
---@field spawn HakiVec3
local Player = {
	properties = {
		speed = 5.0, -- m/s
		shove_range = 1.2, -- how far in front of the player the shove reaches (m)
		shove_radius = 0.9, -- size of the shove hit sphere (m)
		shove_force = 120.0, -- impulse given to every body hit (N*s)
		shove_cooldown = 0.4, -- seconds between shoves
	},
}

function Player:on_start()
	self.spawn = self.entity:position()
	self.cooldown = 0
	log.info("player ready at", self.spawn)
end

function Player:on_update(dt)
	local camera = scene.primary_camera()
	local direction = movement.camera_relative(input.move(), camera)

	self.entity:set_move_velocity(direction * self.speed)
	if direction:length() > 0.001 then
		self.entity:set_rotation(quat.look_rotation(direction))
	end

	self.cooldown = math.max(self.cooldown - dt, 0)
	if self.cooldown == 0 and (input.key_pressed("space") or input.gamepad_pressed(0, "x")) then
		self:shove()
		self.cooldown = self.shove_cooldown
	end
end

-- First "attack": push every dynamic body in a sphere in front of the player.
function Player:shove()
	-- Blends into the punch and back into running/idle by itself when it ends.
	if self.entity:has_animation("Punch") then
		self.entity:play_animation("Punch", { fade = 0.08 })
	end
	local forward = self.entity:forward()
	local center = self.entity:position() + vec3(0, 0.9, 0) + forward * self.shove_range
	for _, target in ipairs(physics.overlap_sphere(center, self.shove_radius)) do
		if target ~= self.entity then
			target:add_impulse((forward + vec3(0, 0.3, 0)) * self.shove_force)
		end
	end
end

-- Called by hazards (see danger_zone.lua).
function Player:respawn()
	self.entity:set_position(self.spawn)
end

return Player
