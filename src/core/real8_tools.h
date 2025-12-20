#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "real8_vm.h"

// Forward declaration of the Host interface
class IReal8Host;

class Real8Tools
{
public:
    struct StaticVarEntry {
        enum class Type { Number, Boolean, String };
        std::string name;
        std::string value;
        Type type;
    };

    // --------------------------------------------------------------------------
    // CONFIGURATION & ASSETS
    // --------------------------------------------------------------------------
    static void LoadSettings(Real8VM* vm, IReal8Host* host);
    static void SaveSettings(Real8VM* vm, IReal8Host* host);
    static void LoadSkin(Real8VM* vm, IReal8Host* host);

    // --------------------------------------------------------------------------
    // MODDING SYSTEM
    // --------------------------------------------------------------------------
    static void ApplyMods(Real8VM* vm, IReal8Host* host, const std::string &cartPath);
    
    // Individual Mod Injections
    static bool InjectSpriteMod(Real8VM* vm, IReal8Host* host, const std::string &path, uint32_t dest_offset = 0x0000);
    static bool InjectLuaMod(Real8VM* vm, IReal8Host* host, const std::string &path, bool persistent = false);
    static bool InjectBinaryMod(Real8VM* vm, IReal8Host* host, const std::string &path, uint32_t addr);

    // --------------------------------------------------------------------------
    // EXPORTERS
    // --------------------------------------------------------------------------
    static void ExportGFX(Real8VM* vm, IReal8Host* host, const std::string &outputFolder);
    static void ExportMAP(Real8VM* vm, IReal8Host* host, const std::string &outputFolder);
    static void ExportStaticVars(Real8VM* vm, IReal8Host* host, const std::string &outputFolder);
    static void ExportStaticVars(Real8VM* vm, IReal8Host* host, const std::string &outputFolder, const std::vector<StaticVarEntry>& entries);
    static void ExportMusic(Real8VM* vm, IReal8Host* host, const std::string &outputFolder);
    static std::vector<StaticVarEntry> CollectStaticVars(Real8VM* vm);

private:
    // Helpers
    static uint8_t FindClosestP8Color(uint8_t r, uint8_t g, uint8_t b);
    static void WriteVarLen(std::vector<uint8_t>& buf, uint32_t val);
};
