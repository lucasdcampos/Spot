local spot = require("@spot")

-- Spins in place until the player comes close, then tells the scene it was collected and goes away.
pickupDistance = 1
spinSpeed = 3

local player
local angle = 0

function OnStart()
    player = spot.Scene:FindEntity("Player")
end

function OnUpdate(deltaTime: number)
    angle += spinSpeed * deltaTime
    spot.entity.transform.rotation = Vector3(0, angle, 0)

    local coin = spot.entity.transform.position
    local target = player.transform.position
    local dx, dz = coin.x - target.x, coin.z - target.z
    if dx * dx + dz * dz < pickupDistance * pickupDistance then
        spot.Scene:Emit("CoinCollected")
        spot.entity:Destroy()
    end
end
