-- Hit points + floating damage numbers + a small health bar, for any script that can be hurt.
--
--   local Health = require("shared.lib.health")
--   function Thing:on_start()  self.health = Health.new(self.entity, 100, { on_death = function() ... end }) end
--   function Thing:on_update(dt) self.health:update(dt) end
--   function Thing:take_damage(amount, source, info) return self.health:take(amount, source, info) end
--   function Thing:on_draw(dt) self.health:draw(dt) end
local anim = require("shared.lib.anim")

---@class Health
---@field entity HakiEntity
---@field max number
---@field current number
local Health = {}
Health.__index = Health

local POPUP_LIFE = 0.9 -- seconds a damage number stays up
local POPUP_RISE = 60 -- pixels it floats upwards
local BAR_SHOW_TIME = 2.5 -- seconds the health bar stays visible after a hit
local BAR_WIDTH = 64

---@class HealthOptions
---@field head_height number? metres above the entity's origin where numbers and the bar appear (2.0)
---@field invulnerable number? seconds of immunity after each hit (0.15)
---@field hit_animation string? clip played on a hit that is not fatal ("RecieveHit"; "" for none)
---@field on_hurt fun(amount: number, source: HakiEntity?, info: table)?
---@field on_death fun(source: HakiEntity?, info: table)?

---@param entity HakiEntity
---@param max number
---@param options HealthOptions?
---@return Health
function Health.new(entity, max, options)
	options = options or {}
	return setmetatable({
		entity = entity,
		max = max,
		current = max,
		head_height = options.head_height or 2.0,
		invulnerable = options.invulnerable or 0.15,
		hit_animation = options.hit_animation or "RecieveHit",
		on_hurt = options.on_hurt,
		on_death = options.on_death,
		immune_timer = 0,
		bar_timer = 0,
		popups = {},
	}, Health)
end

---@return boolean
function Health:alive()
	return self.current > 0
end

-- Applies a hit. Returns false if it was ignored (dead or still immune from the previous hit).
---@param amount number
---@param source HakiEntity?
---@param info table?
---@return boolean
function Health:take(amount, source, info)
	info = info or {}
	if not self:alive() or self.immune_timer > 0 or not (amount > 0) then
		return false
	end
	amount = math.max(1, math.floor(amount + 0.5))
	self.current = math.max(self.current - amount, 0)
	self.immune_timer = self.invulnerable
	self.bar_timer = BAR_SHOW_TIME

	-- Spread popups a little so rapid hits don't stack exactly on top of each other.
	local angle = math.random() * math.pi * 2
	table.insert(self.popups, {
		text = tostring(amount),
		amount = amount,
		age = 0,
		offset = vec3(math.cos(angle), 0, math.sin(angle)) * (0.15 + math.random() * 0.2),
	})

	if self:alive() then
		if self.hit_animation ~= "" then
			anim.play(self.entity, self.hit_animation, { fade = 0.05 })
		end
		if self.on_hurt then
			self.on_hurt(amount, source, info)
		end
	elseif self.on_death then
		self.on_death(source, info)
	end
	return true
end

---@param amount number
function Health:heal(amount)
	if self:alive() then
		self.current = math.min(self.current + amount, self.max)
	end
end

function Health:reset()
	self.current = self.max
	self.immune_timer = 0
	self.bar_timer = 0
end

-- Call from on_update.
---@param dt number
function Health:update(dt)
	self.immune_timer = math.max(self.immune_timer - dt, 0)
end

-- White for chip damage, yellow for solid hits, red for big ones (relative to max health).
local function popup_color(amount, max, alpha)
	local share = amount / math.max(max, 1)
	if share >= 0.25 then
		return { 1.0, 0.25, 0.2, alpha }
	elseif share >= 0.1 then
		return { 1.0, 0.85, 0.25, alpha }
	end
	return { 1.0, 1.0, 1.0, alpha }
end

-- Call from on_draw: floating damage numbers and, after a recent hit, a health bar.
---@param dt number
function Health:draw(dt)
	local head = self.entity:position() + vec3(0, self.head_height, 0)

	for i = #self.popups, 1, -1 do
		local popup = self.popups[i]
		popup.age = popup.age + dt
		local t = popup.age / POPUP_LIFE
		if t >= 1 then
			table.remove(self.popups, i)
		else
			local rise = POPUP_RISE * (1 - (1 - t) * (1 - t)) -- ease out
			local alpha = t < 0.6 and 1 or 1 - (t - 0.6) / 0.4
			local pop = 1 + 0.5 * math.max(0, 1 - t * 6) -- starts big, settles quickly
			local size = 26 * pop
			local anchor = head + popup.offset
			local shadow = { 0, 0, 0, 0.75 * alpha }
			draw.label(anchor, popup.text, { size = size, color = shadow, offset_y = 18 + rise - 2, depth_test = false, layer = 30 })
			draw.label(anchor, popup.text, {
				size = size,
				color = popup_color(popup.amount, self.max, alpha),
				offset_y = 18 + rise,
				depth_test = false,
				layer = 31,
			})
		end
	end

	if self.bar_timer > 0 and self:alive() then
		self.bar_timer = math.max(self.bar_timer - dt, 0)
		local fade = math.min(self.bar_timer / 0.4, 1)
		local fraction = self.current / self.max
		local x, y, h = -BAR_WIDTH / 2, -6, 7
		draw.rect(head, x, y, BAR_WIDTH, h, { color = { 0.05, 0.05, 0.07, 0.75 * fade }, depth_test = false, layer = 20 })
		draw.rect(head, x, y, BAR_WIDTH * fraction, h, {
			color = { 1 - fraction * 0.7, 0.3 + fraction * 0.6, 0.25, fade },
			depth_test = false,
			layer = 21,
		})
		draw.rect(head, x - 1, y - 1, BAR_WIDTH + 2, h + 2, { color = { 0, 0, 0, 0.9 * fade }, outline = 1, depth_test = false, layer = 22 })
	end
end

return Health
