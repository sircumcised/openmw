local I = require('openmw.interfaces')

I.Settings.registerPage({
    key = 'CameraJuice',
    l10n = 'CameraJuice',
    name = 'pageName',
    description = 'pageDescription',
})

local function boolSetting(key, default)
    return {
        key = key,
        renderer = 'checkbox',
        name = key,
        description = key .. 'Description',
        default = default,
    }
end

local function numberSetting(key, default, min, max)
    return {
        key = key,
        renderer = 'number',
        name = key,
        description = key .. 'Description',
        default = default,
        argument = { min = min, max = max },
    }
end

I.Settings.registerGroup({
    key = 'SettingsCameraJuice',
    page = 'CameraJuice',
    l10n = 'CameraJuice',
    name = 'groupName',
    permanentStorage = true,
    settings = {
        boolSetting('enabled', true),
        numberSetting('strafeTilt', 1.5, -10, 10),
        numberSetting('turnLean', 1.0, -10, 10),
        numberSetting('landingImpact', 1.0, 0, 5),
        numberSetting('sprintFov', 6, 0, 30),
        numberSetting('damageShake', 1.0, 0, 5),
        numberSetting('idleSway', 1.0, 0, 5),
    },
})
