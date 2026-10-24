local spot = require("@spot")

-- Turns its entity around the vertical axis: the game's only behavior, written as data, not C++.
local speed = 1.5
local angle = 0

function OnUpdate(deltaTime: number)
    angle += speed * deltaTime
    spot.entity.transform.rotation = Vector3(0, angle, 0)
end
