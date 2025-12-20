#pragma once

#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <iomanip>
#include "../../lib/z8lua/lua.h"

// Forward declaration
class Real8VM;

struct DebugState {
    bool paused = false;
    bool step_mode = false;     
    int step_depth = 0;         
    std::set<int> breakpoints;  
    std::string last_error;
};

class Real8Debugger : public DebugState {
public:
    Real8Debugger(Real8VM* vm_instance);
    ~Real8Debugger() = default;

    // Source Code Management
    void setSource(const std::string& src);
    std::string getSource() const;
    void printSourceContext(int line, int margin = 2);

    // Flow Control
    void togglePause();
    void step();
    void forceExit(); 

    // Breakpoints
    void addBreakpoint(int line);
    void removeBreakpoint(int line);
    void clearBreakpoints();

    // Inspection
    std::string inspectVariable(lua_State* L, const std::string& varName);
    std::string dumpMemory(int addr, int length);
    void poke(int addr, uint8_t val);

    // Static Hook
    static void luaHook(lua_State *L, lua_Debug *ar);

private:
    Real8VM* vm;
    std::string debugSource; 
};