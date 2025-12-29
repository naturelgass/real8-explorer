#include "../../core/real8_tools.h"
#include "../../hal/real8_host.h"

void Real8Tools::LoadSettings(Real8VM* vm, IReal8Host* host) {
    (void)vm;
    (void)host;
}

void Real8Tools::SaveSettings(Real8VM* vm, IReal8Host* host) {
    (void)vm;
    (void)host;
}

void Real8Tools::LoadSkin(Real8VM* vm, IReal8Host* host) {
    (void)vm;
    (void)host;
}

void Real8Tools::ApplyMods(Real8VM* vm, IReal8Host* host, const std::string &cartPath) {
    (void)vm;
    (void)host;
    (void)cartPath;
}

bool Real8Tools::InjectSpriteMod(Real8VM* vm, IReal8Host* host, const std::string &path, uint32_t dest_offset) {
    (void)vm;
    (void)host;
    (void)path;
    (void)dest_offset;
    return false;
}

bool Real8Tools::InjectLuaMod(Real8VM* vm, IReal8Host* host, const std::string &path, bool persistent) {
    (void)vm;
    (void)host;
    (void)path;
    (void)persistent;
    return false;
}

bool Real8Tools::InjectBinaryMod(Real8VM* vm, IReal8Host* host, const std::string &path, uint32_t addr) {
    (void)vm;
    (void)host;
    (void)path;
    (void)addr;
    return false;
}

void Real8Tools::ExportGFX(Real8VM* vm, IReal8Host* host, const std::string &outputFolder) {
    (void)vm;
    (void)host;
    (void)outputFolder;
}

void Real8Tools::ExportMAP(Real8VM* vm, IReal8Host* host, const std::string &outputFolder) {
    (void)vm;
    (void)host;
    (void)outputFolder;
}

void Real8Tools::ExportStaticVars(Real8VM* vm, IReal8Host* host, const std::string &outputFolder) {
    (void)vm;
    (void)host;
    (void)outputFolder;
}

void Real8Tools::ExportStaticVars(Real8VM* vm, IReal8Host* host, const std::string &outputFolder, const std::vector<StaticVarEntry>& entries) {
    (void)vm;
    (void)host;
    (void)outputFolder;
    (void)entries;
}

void Real8Tools::ExportMusic(Real8VM* vm, IReal8Host* host, const std::string &outputFolder) {
    (void)vm;
    (void)host;
    (void)outputFolder;
}

std::vector<Real8Tools::StaticVarEntry> Real8Tools::CollectStaticVars(Real8VM* vm) {
    (void)vm;
    return {};
}
