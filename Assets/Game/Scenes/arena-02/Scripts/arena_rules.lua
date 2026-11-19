-- Keep authored framing until the moving player approaches a screen edge.
---@class SandboxRules : HakiSceneScript
---@field player_name string
---@field edge_threshold number
---@field center_threshold number
---@field pan_sharpness number
---@field movement_threshold number
---@field coverage_min_x number
---@field coverage_max_x number
---@field coverage_min_z number
---@field coverage_max_z number
---@field camera HakiEntity?
---@field player HakiEntity?
---@field center_offset HakiVec3?
---@field last_player_position HakiVec3?
---@field ground_height number?
---@field following boolean?
---@field camera_version integer?
local Rules = {
	properties = {
		player_name = "Player",
		edge_threshold = 0.65, -- 0 = screen center, 1 = edge; start panning outside this region
		center_threshold = 0.15, -- stop panning once the player is back near center
		pan_sharpness = 0.5, -- lower = slower, gentler panning
		movement_threshold = 0.0001, -- minimum horizontal displacement per tick (metres)
		-- Authored terrain rectangle (engine X/Z), from tools/blender/arenas/arena_02.py coverage().
		-- The whole camera footprint stays inside it, so no world edge or sky can show.
		coverage_min_x = -32.0,
		coverage_max_x = 32.0,
		coverage_min_z = -24.0,
		coverage_max_z = 22.0,
	},
}

---@return boolean
function Rules:acquire_camera()
	self.camera = scene.primary_camera()
	self.player = scene.find_by_name(self.player_name or "Player")
	if not (self.camera and self.player) then
		return false
	end

	local position = self.player:position()
	local camera_position = self.camera:position()
	local forward = self.camera:forward():normalized()
	if forward.y >= -0.001 or camera_position.y <= position.y then
		return false
	end

	self.ground_height = position.y
	self.center_offset = -forward * ((position.y - camera_position.y) / forward.y)
	self.last_player_position = position:copy()
	self.following = false
	self.camera_version = 5
	return true
end

function Rules:on_start()
	if not self:acquire_camera() then
		log.warn(
			"Arena camera needs a downward-facing primary camera above the player named",
			self.player_name or "Player"
		)
	end
end

---@param dt number
function Rules:on_update(dt)
	if
		self.camera_version ~= 5
		or not self.camera
		or not self.player
		or not self.camera:valid()
		or not self.player:valid()
	then
		if not self:acquire_camera() then
			return
		end
	end
	if dt <= 0 then
		return
	end

	local position = self.player:position()
	local moved = (position - self.last_player_position):flat():length()
	self.last_player_position = position:copy()
	-- Even an off-center spawn stays in its authored framing until the player actually moves.
	-- Stop immediately on release: no velocity lead or easing tail after movement stops.
	if moved <= math.max(self.movement_threshold or 0.0001, 0) then
		return
	end

	local focus = vec3(position.x, self.ground_height, position.z)
	local camera_position = self.camera:position()
	local relative = focus - camera_position
	local forward = self.camera:forward():normalized()
	local right = self.camera:right():normalized()
	local up = right:cross(forward):normalized()
	local depth = relative:dot(forward)
	local half_height = self.camera:is_orthographic() and self.camera:ortho_size()
		or depth * math.tan(math.rad(self.camera:fov()) * 0.5)
	local edge_distance = math.huge
	if depth > 0 and half_height > 0 then
		edge_distance = math.max(
			math.abs(relative:dot(right)) / (half_height * scene.viewport_aspect()),
			math.abs(relative:dot(up)) / half_height
		)
	end

	local outer = math.max(0.05, math.min(self.edge_threshold or 0.65, 0.95))
	local inner = math.max(0, math.min(self.center_threshold or 0.15, outer - 0.01))
	if edge_distance <= inner then
		self.following = false
	elseif edge_distance >= outer then
		self.following = true
	end
	if not self.following then
		return
	end

	local target_focus = self:clamp_focus(focus, forward, right, up)
	local target = target_focus + self.center_offset
	local alpha = 1 - math.exp(-math.max(self.pan_sharpness or 3, 0) * dt)
	self.camera:set_position(vec3.lerp(camera_position, target, alpha))
end

-- Ground-plane offsets (relative to the focus) of the four screen corners, from the real frustum
-- (perspective or orthographic) at the current viewport aspect.
---@return number?, number?, number?, number?
function Rules:footprint_offsets(focus, forward, right, up)
	local eye = focus + self.center_offset
	local aspect = scene.viewport_aspect()
	local min_x, max_x, min_z, max_z = math.huge, -math.huge, math.huge, -math.huge
	for _, sx in ipairs({ -1, 1 }) do
		for _, sy in ipairs({ -1, 1 }) do
			local origin, ray
			if self.camera:is_orthographic() then
				local half = self.camera:ortho_size()
				origin = eye + right * (sx * half * aspect) + up * (sy * half)
				ray = forward
			else
				local tan_y = math.tan(math.rad(self.camera:fov()) * 0.5)
				origin = eye
				ray = forward + right * (sx * tan_y * aspect) + up * (sy * tan_y)
			end
			if ray.y >= -1e-4 then
				return nil -- this corner sees the horizon; nothing sensible to clamp against
			end
			local hit = origin + ray * ((self.ground_height - origin.y) / ray.y)
			min_x = math.min(min_x, hit.x - focus.x)
			max_x = math.max(max_x, hit.x - focus.x)
			min_z = math.min(min_z, hit.z - focus.z)
			max_z = math.max(max_z, hit.z - focus.z)
		end
	end
	return min_x, max_x, min_z, max_z
end

-- Keep the whole camera footprint inside the authored terrain rectangle.
---@return HakiVec3
function Rules:clamp_focus(focus, forward, right, up)
	local min_x, max_x, min_z, max_z = self:footprint_offsets(focus, forward, right, up)
	if not min_x then
		return focus
	end
	local function clamp(value, lo, hi)
		if lo > hi then
			return (lo + hi) * 0.5 -- footprint wider than the terrain: centre it
		end
		return math.max(lo, math.min(value, hi))
	end
	return vec3(
		clamp(focus.x, self.coverage_min_x - min_x, self.coverage_max_x - max_x),
		focus.y,
		clamp(focus.z, self.coverage_min_z - min_z, self.coverage_max_z - max_z)
	)
end

return Rules
