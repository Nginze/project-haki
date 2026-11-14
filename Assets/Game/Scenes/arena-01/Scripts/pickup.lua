-- An item that can be picked up, carried, dropped and thrown. Attach to any dynamic body:
--   "script": { "path": "pickup.lua", "properties": { "display_name": "Barrel" } }
--
-- Duck-typed like damage: a carrier (see player.lua) only needs the item's script to have
-- can_pick_up / on_picked_up / on_dropped, and the item never needs to know what is carrying it.
-- While carried the body is kinematic, so it follows the carrier and still pushes things.
-- A thrown item hurts the first damageable thing it hits (see lib/damage.lua).
local damage = require("shared.lib.damage")

---@class Pickup : HakiEntityScript
---@field display_name string
---@field throw_damage number
---@field throw_window number
---@field carrier HakiEntity?
---@field thrown_by HakiEntity?
local Pickup = {
	properties = {
		display_name = "", -- shown in the prompt; "" = entity name without its grid suffix (Barrel_4_5 -> Barrel)
		throw_damage = 15,
		throw_window = 1.2, -- seconds after a throw during which a hit counts
	},
}

function Pickup:on_start()
	if self.display_name == "" then
		self.display_name = (self.entity:name():gsub("[_%d]+$", ""):gsub("_", " "))
	end
	self.carrier = nil
	self.thrown_by = nil
	self.thrown_timer = 0
end

function Pickup:on_update(dt)
	if self.thrown_timer > 0 then
		self.thrown_timer = math.max(self.thrown_timer - dt, 0)
		if self.thrown_timer == 0 then
			self.thrown_by = nil
		end
	end
end

-- ---------------------------------------------------------------- pickup contract

---@return string
function Pickup:pickup_name()
	return self.display_name
end

---@param by HakiEntity
---@return boolean
function Pickup:can_pick_up(by)
	return self.carrier == nil and self.entity:has_body()
end

-- Returns false if the item could not be taken (e.g. it has no physics body).
---@param by HakiEntity
---@return boolean
function Pickup:on_picked_up(by)
	if not self:can_pick_up(by) or not self.entity:set_kinematic(true) then
		return false
	end
	self.carrier = by
	self.thrown_by = nil
	self.thrown_timer = 0
	return true
end

-- Hands the item back to physics with `velocity`. `thrown` arms it to deal damage on impact.
---@param by HakiEntity
---@param velocity HakiVec3
---@param thrown boolean?
function Pickup:on_dropped(by, velocity, thrown)
	self.entity:set_kinematic(false)
	self.entity:set_velocity(velocity)
	self.carrier = nil
	if thrown then
		self.thrown_by = by
		self.thrown_direction = velocity:flat():length() > 0.001 and velocity:flat():normalized() or vec3.zero()
		self.thrown_timer = self.throw_window
	end
end

---@param other HakiEntity
function Pickup:on_contact_begin(other)
	if self.thrown_by and other ~= self.thrown_by then
		local hit = damage.deal(other, self.throw_damage, self.thrown_by, { direction = self.thrown_direction })
		if hit then
			self.thrown_by = nil -- one hit per throw
		end
	end
end

return Pickup
