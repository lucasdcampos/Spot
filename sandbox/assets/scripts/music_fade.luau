local spot = require("@spot")

-- Fade the existing music source; keep playback running so M preserves the song's position.
local audio
local fullVolume = 0
local muted = false

function OnStart()
    audio = spot.entity:GetComponent("AudioSource")
    fullVolume = audio.volume
    audio.volume = 0
end

function OnUpdate(deltaTime: number)
    if deltaTime <= 0 then
        return
    end
    if spot.Input:IsKeyPressed("M") then
        muted = not muted
    end

    local target = if muted then 0 else fullVolume
    local volume = audio.volume
    local step = fullVolume * deltaTime / 1.5
    if volume < target then
        volume = math.min(volume + step, target)
    elseif volume > target then
        volume = math.max(volume - step, target)
    end
    audio.volume = volume
end
