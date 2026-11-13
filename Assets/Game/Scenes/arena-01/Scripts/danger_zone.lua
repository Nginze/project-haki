-- Trigger volume that sends characters back to their spawn point.
---@class DangerZone : HakiEntityScript
local DangerZone = {}

---@param other HakiEntity
function DangerZone:on_trigger_enter(other)
    if not other:is_character() then
        return
    end
    log.info(other:name(), "entered the danger zone")
    local behaviour = other:script()
    ---@cast behaviour Player?
    if behaviour and behaviour.respawn then
        behaviour:respawn()
    end
end

return DangerZone
