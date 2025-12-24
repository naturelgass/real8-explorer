#include <3ds.h>

#include "3ds_host.hpp"
#include "real8_vm.h"
#include "real8_shell.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ThreeDSHost *host = new ThreeDSHost();
    Real8VM *vm = new Real8VM(host);
    host->debugVMRef = vm;

    if (!vm->initMemory()) {
        delete vm;
        delete host;
        return 1;
    }

    Real8Shell *shell = new Real8Shell(host, vm);

    vm->gpu.pal_reset();
    host->setInterpolation(vm->interpolation);

    host->log("Real-8 3DS Port Started.");

    bool running = true;
    double last = (double)osGetTime() / 1000.0;
    double accumulator = 0.0;
    const double fixedStep = 1.0 / 60.0;

    while (running && aptMainLoop()) {
        double now = (double)osGetTime() / 1000.0;
        double delta = now - last;
        last = now;
        if (delta > 0.25) delta = 0.25;
        accumulator += delta;

        hidScanInput();
        u32 held = hidKeysHeld();
        if ((held & KEY_START) && (held & KEY_SELECT)) {
            running = false;
        }

        host->crt_filter = vm->crt_filter;
        if (vm->interpolation != host->interpolation) {
            host->setInterpolation(vm->interpolation);
        }

        while (accumulator >= fixedStep) {
            shell->update();
            if (vm->quit_requested) {
                running = false;
                break;
            }
            accumulator -= fixedStep;
        }

        if (accumulator < fixedStep) {
            gspWaitForVBlank();
            svcSleepThread(1000000LL);
        }
    }

    delete shell;
    delete vm;
    delete host;

    return 0;
}
