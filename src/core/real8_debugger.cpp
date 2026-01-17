#include "real8_debugger.h"
#include "real8_vm.h" 
//#include "real8_tools.h"

#include <sstream>
#include <iomanip>
#include <algorithm>

Real8Debugger::Real8Debugger(Real8VM* vm_instance) : vm(vm_instance) {
}

void Real8Debugger::setSource(const std::string& src) {
    debugSource = src;
}

std::string Real8Debugger::getSource() const {
    return debugSource;
}

void Real8Debugger::printSourceContext(int line, int margin) {
    // Safety check: VM and Host must exist
    if (debugSource.empty() || !vm || !vm->getHost()) return;

    std::stringstream ss(debugSource);
    std::string segment;
    int currentLine = 1;
    int startLine = std::max(1, line - margin);
    int endLine = line + margin;

    vm->log(Real8VM::LOG_GENERIC, "--- CODE CONTEXT [Line %d] ---", line);
    
    while (std::getline(ss, segment)) {
        if (!segment.empty() && segment.back() == '\r') segment.pop_back();

        if (currentLine >= startLine && currentLine <= endLine) {
            const char* marker = (currentLine == line) ? ">> " : "   ";
            vm->log(Real8VM::LOG_GENERIC, "%s%4d: %s", marker, currentLine, segment.c_str());
        }

        if (currentLine > endLine) break; 
        currentLine++;
    }
    
    vm->log(Real8VM::LOG_GENERIC, "------------------------------");
}

void Real8Debugger::togglePause() {
    paused = !paused; 
    step_mode = false;
    
    if (paused) {
        if (vm && vm->getHost()) {
            vm->log(Real8VM::LOG_GENERIC, "[DEBUG] Paused by User.");
            vm->getHost()->setConsoleState(true); 
        }
    } else {
        if (vm && vm->getHost()) vm->log(Real8VM::LOG_GENERIC, "[DEBUG] Resuming...");
    }
}

void Real8Debugger::step() {
    step_mode = true;
    paused = false;
}

void Real8Debugger::forceExit() {
    paused = false;
    step_mode = false;
}

void Real8Debugger::addBreakpoint(int line) {
    breakpoints.insert(line);
    if (vm) vm->log(Real8VM::LOG_GENERIC, "[DEBUG] Breakpoint set at line %d", line);
}

void Real8Debugger::removeBreakpoint(int line) {
    breakpoints.erase(line);
    if (vm) vm->log(Real8VM::LOG_GENERIC, "[DEBUG] Breakpoint removed at line %d", line);
}

void Real8Debugger::clearBreakpoints() {
    breakpoints.clear();
    if (vm) vm->log(Real8VM::LOG_GENERIC, "[DEBUG] All breakpoints cleared.");
}

std::string Real8Debugger::inspectVariable(lua_State* L, const std::string& varName) {
    if (!L || !paused) return "Error: Must be paused to inspect.";

    lua_getglobal(L, varName.c_str());
    std::string result;
    
    if (lua_isnil(L, -1)) {
        result = "nil (Global)";
    } else if (lua_isstring(L, -1)) {
        result = "\"" + std::string(lua_tostring(L, -1)) + "\"";
    } else if (lua_isnumber(L, -1)) {
        char buf[64]; 
        snprintf(buf, 64, "%f", (double)lua_tonumber(L, -1));
        result = std::string(buf);
    } else if (lua_isboolean(L, -1)) {
        result = lua_toboolean(L, -1) ? "true" : "false";
    } else if (lua_istable(L, -1)) {
        result = "table: " + std::to_string((size_t)lua_topointer(L, -1));
    } else {
        result = "unknown type";
    }
    
    lua_pop(L, 1);
    return result;
}

std::string Real8Debugger::dumpMemory(int addr, int length) {
    if (!vm || !vm->ram) return "VM Memory Error";
    if (addr < 0 || addr + length > 0x8000) return "Out of Bounds";
    
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    
    for (int i = 0; i < length; i++) {
        if (i % 16 == 0) ss << "\n" << std::setw(4) << (addr + i) << ": ";
        ss << std::setw(2) << (int)vm->ram[addr + i] << " ";
    }
    return ss.str();
}

void Real8Debugger::poke(int addr, uint8_t val) {
    if (vm && vm->ram && addr >= 0 && addr < 0x8000) {
        vm->ram[addr] = val;
        vm->log(Real8VM::LOG_MEM, "[DEBUG] Poked %02X to addr %04X", val, addr);
    }
}

#if !defined(__GBA__)
// --------------------------------------------------------------------------
// STATIC HOOK
// --------------------------------------------------------------------------
void Real8Debugger::luaHook(lua_State *L, lua_Debug *ar)
{
    lua_getglobal(L, "__pico8_vm_ptr");
    void* ptr = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (!ptr) return; // Safety check
    Real8VM* vm = (Real8VM*)ptr;

    Real8Debugger& dbg = vm->debug; 

    lua_getinfo(L, "Sl", ar);
    real8_set_last_lua_line(ar->currentline, ar->short_src);

    // Hit Breakpoint OR Stepping Mode
    bool hit_break = (ar->currentline > 0 && dbg.breakpoints.count(ar->currentline));
    
    if (hit_break || dbg.step_mode) {
        
        dbg.paused = true;
        
        if (hit_break) dbg.step_mode = false;

        vm->log(Real8VM::LOG_GENERIC, ""); 
        dbg.printSourceContext(ar->currentline, 7);

        vm->show_frame();

        // Blocking Loop: Waits for host UI events (Step/Resume)
        while (dbg.paused) {
            if (vm->getHost()) vm->getHost()->waitForDebugEvent();
            else break; // Safety break if host died
        }
    }
}
#endif
