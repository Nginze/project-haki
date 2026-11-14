-- Locally controlled brawler. Attach with: "script": { "path": "player.lua" }
local movement = require("shared.lib.movement")
local damage = require("shared.lib.damage")
local anim = require("shared.lib.anim")
local Health = require("shared.lib.health")

---@class Player : HakiEntityScript
---@field speed number
---@field max_health number
---@field punch_damage number
---@field shove_range number
---@field shove_radius number
---@field shove_force number
---@field shove_cooldown number
---@field cooldown number
---@field spawn HakiVec3
---@field health Health
---@field pickup_radius number
---@field carry_height number
---@field throw_speed number
---@field carrying HakiEntity?
---@field focus_item HakiEntity?
local Player = {
	properties = {
		speed = 5.0, -- m/s
		max_health = 100,
		punch_damage = 12, -- damage dealt to anything that can take it (see lib/damage.lua)
		shove_range = 1.2, -- how far in front of the player the shove reaches (m)
		shove_radius = 0.9, -- size of the shove hit sphere (m)
		shove_force = 120.0, -- impulse given to every body hit (N*s)
		shove_cooldown = 0.4, -- seconds between shoves
		pickup_radius = 1.6, -- items closer than this can be picked up (m)
		carry_height = 1.85, -- carried items are held above the head (m above the feet)
		throw_speed = 9.0, -- m/s
	},
}

function Player:on_start()
	self.spawn = self.entity:position()
	self.cooldown = 0
	self.health = Health.new(self.entity, self.max_health, {
		on_death = function()
			log.info("player was knocked out")
			self:respawn()
		end,
	})
	self.carrying = nil
	self.focus_item = nil
	log.info("player ready at", self.spawn)
end

function Player:on_update(dt)
	self.health:update(dt)

	local camera = scene.primary_camera()
	local direction = movement.camera_relative(input.move(), camera)

	self.entity:set_move_velocity(direction * self.speed)
	if direction:length() > 0.001 then
		self.entity:set_rotation(quat.look_rotation(direction))
	end

	self:update_carry()
	self.focus_item = not self.carrying and self:find_pickup() or nil

	local interact = input.key_pressed("e") or input.gamepad_pressed(0, "y")
	local attack = input.key_pressed("space") or input.gamepad_pressed(0, "x")
	if interact then
		if self.carrying then
			self:drop(false)
		elseif self.focus_item then
			self:pick_up(self.focus_item)
		end
	end

	self.cooldown = math.max(self.cooldown - dt, 0)
	if attack and self.carrying then
		self:drop(true)
	elseif attack and self.cooldown == 0 then
		self:shove()
		self.cooldown = self.shove_cooldown
	end
end

function Player:on_draw(dt)
	self.health:draw(dt)

	local pad = input.gamepad_connected(0)
	if self.carrying and self.carrying:valid() then
		local hint = pad and "[Y] Drop   [X] Throw" or "[E] Drop   [Space] Throw"
		self:prompt(self.carrying:position() + vec3(0, 1.1, 0), hint)
	elseif self.focus_item and self.focus_item:valid() then
		local name = self.focus_item:script():pickup_name()
		self:prompt(self.focus_item:position() + vec3(0, 1.1, 0), (pad and "[Y]" or "[E]") .. " Pick up " .. name)
	end
end

-- Billboard prompt: a label on a dark rounded-looking panel, anchored above a point in the world.
function Player:prompt(anchor, text)
	local size, offset = 18, 12
	local w, h = draw.measure(text, size)
	local pad_x, pad_y = 10, 5
	draw.rect(anchor, -w / 2 - pad_x, -offset - h - pad_y, w + pad_x * 2, h + pad_y * 2, {
		color = { 0.06, 0.07, 0.09, 0.78 },
		depth_test = false,
		layer = 40,
	})
	draw.rect(anchor, -w / 2 - pad_x, -offset - h - pad_y, w + pad_x * 2, h + pad_y * 2, {
		color = { 1, 0.85, 0.35, 0.9 },
		outline = 1,
		depth_test = false,
		layer = 41,
	})
	draw.label(anchor, text, { size = size, offset_y = offset, depth_test = false, layer = 42 })
end

-- ---------------------------------------------------------------- pickups

-- Nearest item in reach that agrees to be picked up (duck-typed: its script has can_pick_up).
---@return HakiEntity?
function Player:find_pickup()
	local position = self.entity:position()
	local best, best_distance = nil, math.huge
	for _, candidate in ipairs(physics.overlap_sphere(position + vec3(0, 0.5, 0), self.pickup_radius)) do
		local item = candidate ~= self.entity and candidate:script() or nil
		if item and type(item.can_pick_up) == "function" and item:can_pick_up(self.entity) then
			local distance = (candidate:position() - position):flat():length()
			if distance < best_distance then
				best, best_distance = candidate, distance
			end
		end
	end
	return best
end

---@param target HakiEntity
function Player:pick_up(target)
	if target:script():on_picked_up(self.entity) then
		self.carrying = target
		self:update_carry()
	end
end

-- Drop gently in front, or throw hard along the facing direction.
---@param thrown boolean
function Player:drop(thrown)
	local item = self.carrying
	self.carrying = nil
	if not (item and item:valid()) then
		return
	end
	local forward = self.entity:forward():flat():normalized()
	local velocity = thrown and (forward * self.throw_speed + vec3(0, 0.5, 0)) or (forward * 2.5 + vec3(0, 1, 0))
	item:script():on_dropped(self.entity, velocity, thrown)
	if thrown then
		anim.play(self.entity, "Punch", { fade = 0.08 })
	end
end

-- Keeps the carried item above the head, turned with the player.
function Player:update_carry()
	local item = self.carrying
	if not item then
		return
	end
	if not item:valid() then
		self.carrying = nil
		return
	end
	item:set_position(self.entity:position() + vec3(0, self.carry_height, 0))
	item:set_rotation(quat.from_yaw(self.entity:yaw()))
end

-- First "attack": hurt and push everything in a sphere in front of the player.
function Player:shove()
	-- Blends into the punch and back into running/idle by itself when it ends.
	anim.play(self.entity, "Punch", { fade = 0.08 })
	local forward = self.entity:forward()
	local center = self.entity:position() + vec3(0, 0.9, 0) + forward * self.shove_range
	for _, target in ipairs(physics.overlap_sphere(center, self.shove_radius)) do
		if target ~= self.entity then
			damage.deal(target, self.punch_damage, self.entity, { direction = forward })
			target:add_impulse((forward + vec3(0, 0.3, 0)) * self.shove_force)
		end
	end
end

-- Duck-typed damage receiver (see lib/damage.lua).
function Player:take_damage(amount, source, info)
	return self.health:take(amount, source, info)
end

-- Called by hazards (see danger_zone.lua) and on death.
function Player:respawn()
	if self.carrying then
		self:drop(false)
	end
	self.entity:set_position(self.spawn)
	self.health:reset()
end

return Player
