-- Shared movement helpers. Use from any script with: local movement = require("lib.movement")
local movement = {}

-- Turns stick/WASD input (x = right, y = forward) into a world-space direction on the ground,
-- relative to where the camera is looking, so "forward" always means "up the screen".
---@param move HakiMoveInput
---@param camera HakiEntity?
---@return HakiVec3
function movement.camera_relative(move, camera)
    local forward = vec3(0, 0, -1)
    if camera then
        local flat = camera:forward():flat()
        if flat:length() > 0.001 then
            forward = flat:normalized()
        end
    end
    local right = vec3(-forward.z, 0, forward.x)
    return forward * move.y + right * move.x
end

return movement
