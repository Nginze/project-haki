-- Animation helpers. Use from any script with: local anim = require("shared.lib.anim")
local anim = {}

-- Imported clips are often prefixed with the character's name ("Player_Punch", "AI_01_RecieveHit").
-- Returns the clip name this entity actually has for `base`, or nil.
---@param entity HakiEntity
---@param base string
---@return string?
function anim.resolve(entity, base)
	if not entity:has_animator() then
		return nil
	end
	if entity:has_animation(base) then
		return base
	end
	local prefixed = entity:name() .. "_" .. base
	if entity:has_animation(prefixed) then
		return prefixed
	end
	return nil
end

-- Plays `base` (or its prefixed variant) if the entity has it. Returns true if a clip started.
---@param entity HakiEntity
---@param base string
---@param options HakiPlayAnimationOptions?
---@return boolean
function anim.play(entity, base, options)
	local clip = anim.resolve(entity, base)
	if clip then
		entity:play_animation(clip, options)
		return true
	end
	return false
end

return anim
