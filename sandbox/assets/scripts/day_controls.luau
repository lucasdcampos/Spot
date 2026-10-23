local spot = require("@spot")

-- Drive the game's C++ DayCycle through its registered API.
local sun
local runningDayLength = 480

function OnStart()
    sun = spot.Scene:FindEntity("Sun")
    if sun and sun:GetComponent("DayCycle") then
        runningDayLength = sun:GetComponent("DayCycle").dayLength
    end
    print("Luau demos: J morning, K noon, L night, P pause day, M fade music, N toggle monument neon.")
end

function OnUpdate(deltaTime: number)
    if deltaTime <= 0 or not sun or not sun:IsValid() then
        return
    end
    local cycle = sun:GetComponent("DayCycle")
    if not cycle then
        return
    end

    if spot.Input:IsKeyPressed("J") then
        cycle:SetTimeOfDay(0.30)
    end
    if spot.Input:IsKeyPressed("K") then
        cycle:SetTimeOfDay(0.50)
    end
    if spot.Input:IsKeyPressed("L") then
        cycle:SetTimeOfDay(0.90)
    end

    -- A zero day length stops time while keeping the cycle available to the night lamps.
    if spot.Input:IsKeyPressed("P") then
        if cycle.dayLength > 0 then
            runningDayLength = cycle.dayLength
            cycle.dayLength = 0
        else
            cycle.dayLength = runningDayLength
        end
    end
end
