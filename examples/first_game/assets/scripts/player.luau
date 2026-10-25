local spot = require("@spot")

-- Moves the player on the ground with the Move action: WASD or a gamepad's left stick.
speed = 5

function OnUpdate(deltaTime: number)
    local move = spot.Input:GetActionVector2("Move")
    local position = spot.entity.transform.position
    position.x += move.x * speed * deltaTime
    position.z -= move.y * speed * deltaTime
    spot.entity.transform.position = position
end
