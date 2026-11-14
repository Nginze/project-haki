-- Simple wandering NPC. Attach to a character with: "script": { "path": "npc.lua" }
--
-- States:
--   idle   - stand still for a few seconds
--   wander - walk to a random reachable point near home, steering around obstacles
--   watch  - stop and look at the player while they are close
--   flinch - knocked back for a moment after taking a hit
--   dead   - plays the death clip, then respawns at home with full health
--
-- NPCs can be hurt through the duck-typed damage contract (take_damage, see lib/damage.lua).
-- The body's locomotion blend picks Idle/Run from the velocity; only hits and death play clips.
local anim = require("shared.lib.anim")
local Health = require("shared.lib.health")

---@class Npc : HakiEntityScript
---@field walk_speed number
---@field turn_speed number
---@field wander_radius number
---@field idle_min number
---@field idle_max number
---@field sight_radius number
---@field watch_time number
---@field watch_cooldown number
---@field avoid_distance number
---@field player_name string
---@field max_health number
---@field flinch_time number
---@field knockback number
---@field respawn_time number
---@field debug boolean
---@field health Health
local Npc = {
	properties = {
		walk_speed = 2.2, -- m/s
		turn_speed = 8.0, -- how quickly the NPC turns to face where it walks
		wander_radius = 6.0, -- never picks a destination further than this from its spawn (m)
		idle_min = 1.0, -- seconds to stand around between walks
		idle_max = 3.0,
		sight_radius = 4.0, -- notices the player inside this distance (m); 0 disables
		watch_time = 2.0, -- seconds spent looking at the player
		watch_cooldown = 4.0, -- seconds before the player can be noticed again
		avoid_distance = 1.2, -- how far ahead obstacles are felt (m)
		player_name = "Player",
		max_health = 60,
		flinch_time = 0.35, -- seconds stunned after a hit
		knockback = 3.5, -- m/s pushed away from the hit at the start of a flinch
		respawn_time = 4.0, -- seconds lying down before respawning at home
		debug = false, -- log state changes
	},
}

local BODY_RADIUS = 0.45 -- capsule radius + margin: rays start outside our own hit capsule
local WAIST = vec3(0, 0.6, 0)
local ARRIVE_DISTANCE = 0.4
local STUCK_WINDOW = 1.5
local STUCK_DISTANCE = 0.3
local MAX_STEP = 0.35 -- targets must be on ground roughly level with home

-- Horizontal unit direction from a to b (zero if they are on top of each other).
local function flat_dir(a, b)
	local d = (b - a):flat()
	if d:length() < 0.0001 then
		return vec3.zero()
	end
	return d:normalized()
end

-- Rotates a flat direction around the up axis.
local function rotate_y(direction, radians)
	return quat.from_yaw(radians) * direction
end

-- Random point on a disc around `center` (uniform area).
local function random_point(center, radius)
	local angle = math.random() * math.pi * 2
	local r = radius * math.sqrt(math.random())
	return center + vec3(math.cos(angle) * r, 0, math.sin(angle) * r)
end

---@param self Npc
---@param direction HakiVec3
---@param length number
---@return boolean
local function ray_blocked(self, direction, length)
	local origin = self.entity:position() + WAIST + direction * BODY_RADIUS
	local hit = physics.raycast(origin, direction, length)
	return hit ~= nil and hit.entity ~= self.entity
end

-- ---------------------------------------------------------------- lifecycle

function Npc:on_start()
	self.home = self.entity:position()
	self.notice_cooldown = math.random() * 1.0
	self.health = Health.new(self.entity, self.max_health, {
		on_hurt = function(_, source, info)
			self.knock_direction = info.direction and info.direction:flat()
				or (source and flat_dir(source:position(), self.entity:position()))
				or vec3.zero()
			self.attacker = source
			self:set_state("flinch")
		end,
		on_death = function()
			self:set_state("dead")
		end,
	})
	self:set_state("idle")
end

function Npc:on_update(dt)
	self.health:update(dt)
	self.state_time = self.state_time + dt
	self.notice_cooldown = math.max(self.notice_cooldown - dt, 0)
	self.states[self.state].update(self, dt)
end

function Npc:on_draw(dt)
	self.health:draw(dt)
end

-- Duck-typed damage receiver (see lib/damage.lua).
function Npc:take_damage(amount, source, info)
	return self.health:take(amount, source, info)
end

---@param name string
---@param reason string?
function Npc:set_state(name, reason)
	if self.debug then
		log.info(self.entity:name(), self.state or "-", "->", name, reason or "", self.entity:position())
	end
	self.state = name
	self.state_time = 0
	self.states[name].enter(self)
end

-- ---------------------------------------------------------------- helpers

---@return HakiEntity?
function Npc:player()
	if not (self.player_entity and self.player_entity:valid()) then
		self.player_entity = scene.find_by_name(self.player_name)
	end
	return self.player_entity
end

-- True when the player is close enough to be noticed.
function Npc:sees_player()
	if self.sight_radius <= 0 or self.notice_cooldown > 0 then
		return false
	end
	local player = self:player()
	if not player then
		return false
	end
	local offset = (player:position() - self.entity:position()):flat()
	return offset:length() <= self.sight_radius
end

-- Smoothly turns to face `direction` (flat).
function Npc:face(direction, dt)
	if direction:length() < 0.001 then
		return
	end
	local target = quat.look_rotation(direction)
	local alpha = 1 - math.exp(-self.turn_speed * dt)
	self.entity:set_rotation(quat.slerp(self.entity:rotation(), target, alpha))
end

-- A destination is good if there is floor under it (not the top of a wall) and nothing standing on it.
---@return HakiVec3? point on the ground
function Npc:ground_at(point)
	local ground = physics.raycast(point + vec3(0, 2, 0), vec3(0, -1, 0), 4)
	if not ground or ground.entity == self.entity or math.abs(ground.point.y - self.home.y) > MAX_STEP then
		return nil
	end
	for _, other in ipairs(physics.overlap_sphere(ground.point + WAIST + vec3(0, 0.3, 0), BODY_RADIUS)) do
		if other ~= self.entity then
			return nil
		end
	end
	return ground.point
end

-- True if nothing blocks a straight walk from here to `point`.
function Npc:has_clear_path(point)
	local from = self.entity:position()
	local direction = flat_dir(from, point)
	local distance = (point - from):flat():length() - BODY_RADIUS
	return distance <= 0 or not ray_blocked(self, direction, distance)
end

-- Tries random spots (preferring ones in plain view), shrinking the search towards home so NPCs spawned near an edge still find one.
---@return HakiVec3?
function Npc:pick_target()
	local tries = 12
	local fallback = nil
	for i = 1, tries do
		local radius = self.wander_radius * (1 - 0.7 * (i - 1) / tries)
		local point = self:ground_at(random_point(self.home, radius))
		if point then
			if self:has_clear_path(point) then
				return point
			end
			fallback = fallback or point -- reachable, but needs steering around something
		end
	end
	return fallback
end

-- Direction to walk this tick: straight at the goal, bent sideways when a whisker ray hits something.
function Npc:steer(goal_direction)
	local reach = self.avoid_distance
	local side = math.rad(35)
	if not ray_blocked(self, goal_direction, reach) then
		return goal_direction
	end
	local left_free = not ray_blocked(self, rotate_y(goal_direction, side), reach)
	local right_free = not ray_blocked(self, rotate_y(goal_direction, -side), reach)
	if left_free and not right_free then
		return rotate_y(goal_direction, math.rad(70))
	elseif right_free and not left_free then
		return rotate_y(goal_direction, math.rad(-70))
	elseif left_free and right_free then
		return rotate_y(goal_direction, math.rad(70) * self.avoid_sign)
	end
	return rotate_y(goal_direction, math.rad(110) * self.avoid_sign) -- boxed in: back away
end

-- ---------------------------------------------------------------- states

Npc.states = {
	idle = {
		enter = function(self)
			self.entity:set_move_velocity(vec3.zero())
			self.idle_duration = self.idle_min + math.random() * math.max(self.idle_max - self.idle_min, 0)
		end,
		update = function(self, _)
			if self:sees_player() then
				self:set_state("watch")
			elseif self.state_time >= self.idle_duration then
				self:set_state("wander")
			end
		end,
	},

	wander = {
		enter = function(self)
			self.target = self:pick_target()
			self.avoid_sign = math.random() < 0.5 and -1 or 1
			self.stuck_origin = self.entity:position()
			self.stuck_timer = 0
		end,
		update = function(self, dt)
			if not self.target then
				self:set_state("idle", "no target") -- nowhere to go right now; try again after a rest
				return
			end
			if self:sees_player() then
				self:set_state("watch")
				return
			end

			local position = self.entity:position()
			if (self.target - position):flat():length() <= ARRIVE_DISTANCE then
				self:set_state("idle", "arrived")
				return
			end

			-- Give up on goals we cannot make progress towards.
			self.stuck_timer = self.stuck_timer + dt
			if self.stuck_timer >= STUCK_WINDOW then
				if (position - self.stuck_origin):flat():length() < STUCK_DISTANCE then
					self:set_state("idle", "stuck")
					return
				end
				self.stuck_origin = position
				self.stuck_timer = 0
			end

			local direction = self:steer(flat_dir(position, self.target))
			self:face(direction, dt)
			self.entity:set_move_velocity(direction * self.walk_speed)
		end,
	},

	watch = {
		enter = function(self)
			self.entity:set_move_velocity(vec3.zero())
		end,
		update = function(self, dt)
			local player = self:player()
			if player then
				self:face(flat_dir(self.entity:position(), player:position()), dt)
			end
			if not player or self.state_time >= self.watch_time then
				self.notice_cooldown = self.watch_cooldown
				self:set_state("wander")
			end
		end,
	},

	flinch = {
		enter = function(self) end,
		update = function(self, dt)
			local t = math.min(self.state_time / self.flinch_time, 1)
			local direction = self.knock_direction or vec3.zero()
			if direction:length() > 0.001 then
				direction = direction:normalized()
			end
			self.entity:set_move_velocity(direction * (self.knockback * (1 - t)))
			if self.attacker and self.attacker:valid() then
				self:face(flat_dir(self.entity:position(), self.attacker:position()), dt)
			end
			if t >= 1 then
				self:set_state("idle", "recovered")
			end
		end,
	},

	dead = {
		enter = function(self)
			self.entity:set_move_velocity(vec3.zero())
			anim.play(self.entity, "Death", { fade = 0.1, hold = true })
		end,
		update = function(self, _)
			if self.state_time >= self.respawn_time then
				self.entity:set_position(self.home)
				self.health:reset()
				if self.entity:has_animator() then
					self.entity:stop_animation(0.2)
				end
				self:set_state("idle", "respawned")
			end
		end,
	},
}

return Npc
