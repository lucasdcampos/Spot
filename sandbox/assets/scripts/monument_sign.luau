local spot = require("spot")

-- The plaza's neon sign breathes and floats without moving its solid plinth.
local transform
local basePosition: Vector3
local baseScale: Vector3
local neon
local pulse = 0
local direction = 1

function OnStart()
    transform = spot.entity.transform
    basePosition = transform.position
    baseScale = transform.scale
    neon = spot.entity:FindChild("Neon")
end

function OnUpdate(deltaTime: number)
    if deltaTime <= 0 then
        return
    end

    pulse += direction * deltaTime * 0.5
    if pulse >= 1 then
        pulse = 1
        direction = -1
    elseif pulse <= 0 then
        pulse = 0
        direction = 1
    end

    transform.position.y = basePosition.y + pulse * 0.08
    local size = math.lerp(1, 1.04, pulse)
    transform.scale = Vector3(baseScale.x * size, baseScale.y * size, baseScale.z * size)

    if spot.Input:IsKeyPressed("N") and neon and neon:IsValid() then
        local mesh = neon:GetComponent("MeshRenderer")
        mesh.enabled = not mesh.enabled
    end
end
