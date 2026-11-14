-- Duck-typed damage. Use from any script with: local damage = require("shared.lib.damage")
--
-- There is no base class or registry: anything whose script has a `take_damage` method can be hurt.
--
--   function MyThing:take_damage(amount, source, info) ... return true end
--
-- `source` is the entity that caused the damage (may be nil); `info` is an optional table such as
-- { direction = vec3 } for knockback. Return false to say the hit was ignored (dead, invulnerable...).
local damage = {}

-- The script that would receive damage for `entity`, or nil if it cannot be hurt.
---@param entity HakiEntity?
---@return table?
function damage.receiver(entity)
	if not (entity and entity:valid()) then
		return nil
	end
	local behaviour = entity:script()
	if behaviour and type(behaviour.take_damage) == "function" then
		return behaviour
	end
	return nil
end

---@param entity HakiEntity?
---@return boolean
function damage.can_damage(entity)
	return damage.receiver(entity) ~= nil
end

-- Hurts `target` if it knows how to be hurt. Returns true if the hit landed.
---@param target HakiEntity?
---@param amount number
---@param source HakiEntity?
---@param info table?
---@return boolean
function damage.deal(target, amount, source, info)
	local receiver = damage.receiver(target)
	if not receiver or not (amount > 0) then
		return false
	end
	return receiver:take_damage(amount, source, info or {}) ~= false
end

return damage
