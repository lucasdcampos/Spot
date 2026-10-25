local spot = require("@spot")

-- Counts the coins collected and says when they all are.
coinCount = 3

local collected = 0

function OnStart()
    spot.Scene:On("CoinCollected", function()
        collected += 1
        print(`Coins: {collected}/{coinCount}`)
        if collected == coinCount then
            print("You collected every coin!")
        end
    end)
end
