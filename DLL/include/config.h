#pragma once

namespace Config
{
    struct Settings
    {
        bool  godModeEnabled;
        float godModeHealth;
        int   godModeHealthBoost;
        int   godModeHealthUpgrade;
    };

    inline Settings& Mutable()
    {
        static Settings settings{
            true,   // godModeEnabled
            10000.0f, // godModeHealth
            9999,   // godModeHealthBoost
            99      // godModeHealthUpgrade
        };
        return settings;
    }

    inline const Settings& Get()
    {
        return Mutable();
    }

    inline void ResetDefaults()
    {
        Settings& s = Mutable();
        s.godModeEnabled = true;
        s.godModeHealth = 10000.0f;
        s.godModeHealthBoost = 9999;
        s.godModeHealthUpgrade = 99;
    }
}
