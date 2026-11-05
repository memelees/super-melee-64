-- name: Melee Combat Plumbing Probe
-- description: Experimental percentage HUD and PvP hook probe. Uses Mario models and attacks; this is NOT the Melee fighter port. Disable other game modes for this test.
-- incompatible: gamemode
-- deluxe: true

-- Only the victim's owning client changes its damage/velocity. Coop Deluxe
-- synchronizes these values and the player state; remote slots are read-only.
local pendingLaunch = nil
local restoreHealth = nil

local function reset()
    local state = gPlayerSyncTable[0]
    state.meleeProbePercent = 0
    state.meleeProbeHits = 0
    pendingLaunch = nil
    restoreHealth = nil
end

-- This boundary can receive an authoritative result from a future native
-- Melee runtime. No character physics are inferred from a skin's name.
local function receive_hit(damage, velocity)
    local state = gPlayerSyncTable[0]
    if type(damage) ~= 'number' or damage < 0 or damage ~= damage then return false end
    for _, axis in ipairs({'x', 'y', 'z'}) do
        local v = velocity and velocity[axis]
        if type(v) ~= 'number' or v ~= v or math.abs(v) == math.huge then return false end
    end
    state.meleeProbePercent = math.min(999, (state.meleeProbePercent or 0) + damage)
    state.meleeProbeHits = (state.meleeProbeHits or 0) + 1
    pendingLaunch = {x = velocity.x, y = velocity.y, z = velocity.z}
    return true
end

-- Temporary fixture values for testing the network/HUD connection. Replace
-- this entire adapter with actual Melee hit results before calling it Melee.
local function on_pvp_attack(attacker, victim, interaction)
    if victim.playerIndex ~= 0 then return end
    local state = gPlayerSyncTable[0]
    local dx, dz = victim.pos.x - attacker.pos.x, victim.pos.z - attacker.pos.z
    local length = math.sqrt(dx * dx + dz * dz)
    if length < 0.01 then
        dx, dz, length = sins(attacker.faceAngle.y), coss(attacker.faceAngle.y), 1
    end
    local damage = 10
    local percent = math.min(999, (state.meleeProbePercent or 0) + damage)
    local horizontal = math.min(120, 24 + percent * 0.32)
    restoreHealth = victim.health
    receive_hit(damage, {x = dx / length * horizontal, y = 20 + horizontal * 0.4, z = dz / length * horizontal})
    victim.hurtCounter = 0 -- PvP uses percentage; world hazards keep normal health.
    print(string.format('[MELEE-PROBE] hit victim=%d percent=%d speed=%.2f',
        gNetworkPlayers[0].globalIndex, state.meleeProbePercent, horizontal))
end

local function mario_update(m)
    if m.playerIndex ~= 0 then return end
    if pendingLaunch then
        drop_and_set_mario_action(m, ACT_FORWARD_AIR_KB, 0)
        local v = pendingLaunch
        m.faceAngle.y = atan2s(v.z, v.x)
        m.forwardVel = math.sqrt(v.x * v.x + v.z * v.z)
        m.vel.x, m.vel.y, m.vel.z = v.x, v.y, v.z
        m.hurtCounter = 0
        m.health = restoreHealth or m.health
        m.invincTimer = math.max(m.invincTimer, 12)
        pendingLaunch, restoreHealth = nil, nil
    end
end

local function hud()
    djui_hud_set_resolution(RESOLUTION_N64)
    djui_hud_set_font(FONT_NORMAL)
    local width = djui_hud_get_screen_width()
    djui_hud_set_color(10, 14, 24, 195)
    djui_hud_render_rect(7, 34, 181, 25)
    djui_hud_set_color(255, 210, 100, 255)
    djui_hud_print_text('COMBAT PROBE - MARIO ATTACKS', 11, 38, 0.27)
    local row = 0
    for i = 0, MAX_PLAYERS - 1 do
        local np = gNetworkPlayers[i]
        if np.connected then
            local percent = gPlayerSyncTable[i].meleeProbePercent or 0
            local x = 10 + (row % 4) * math.min(91, (width - 20) / 4)
            local y = djui_hud_get_screen_height() - 25 - math.floor(row / 4) * 22
            djui_hud_set_color(10, 14, 24, 195)
            djui_hud_render_rect(x - 3, y - 2, 85, 21)
            djui_hud_set_color(255, math.max(50, 255 - percent), math.max(50, 255 - percent), 255)
            djui_hud_print_text(string.format('P%d  %d%%', np.globalIndex + 1, percent), x, y, 0.5)
            row = row + 1
        end
    end
end

hook_event(HOOK_ON_MODS_LOADED, function()
    reset()
    if network_is_server() then
        gServerSettings.playerInteractions = PLAYER_INTERACTIONS_PVP
    end
    print('[MELEE-PROBE] loaded: percentage plumbing only; Melee runtime not integrated')
end)
hook_event(HOOK_ON_PVP_ATTACK, on_pvp_attack)
hook_event(HOOK_MARIO_UPDATE, mario_update)
hook_event(HOOK_ON_HUD_RENDER, hud)
hook_event(HOOK_ON_DEATH, function() reset() end)
hook_chat_command('meleeprobe', 'reset: reset your test damage', function(msg)
    if msg == 'reset' then reset(); return true end
    djui_chat_message_create('Combat plumbing probe: Mario attacks, test knockback, synchronized %. /meleeprobe reset')
    return true
end)

_G.MeleeCombatProbe = {
    receive_hit = receive_hit, reset = reset, version = 1,
    get_state = function(index)
        local s = gPlayerSyncTable[index]
        return {percent = s.meleeProbePercent or 0, hits = s.meleeProbeHits or 0}
    end,
}
