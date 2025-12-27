#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psppower.h>

#include "psp_host.h"
#include "../../core/real8_vm.h"
#include "../../core/real8_shell.h"

PSP_MODULE_INFO("REAL8", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

namespace {
int exitCallback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    sceKernelExitGame();
    return 0;
}

int callbackThread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exitCallback, nullptr);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

void setupCallbacks()
{
    SceUID thid = sceKernelCreateThread("Real8Callbacks", callbackThread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}
} // namespace

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    setupCallbacks();
    scePowerSetClockFrequency(333, 333, 166);

    PspHost *host = new PspHost();
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

    host->log("Real-8 PSP port started.");

    bool running = true;
    u64 last = sceKernelGetSystemTimeWide();
    double accumulator = 0.0;
    const double fixedStep = 1.0 / 60.0;

    while (running) {
        u64 now = sceKernelGetSystemTimeWide();
        double delta = (double)(now - last) / 1000000.0;
        last = now;
        if (delta > 0.25) delta = 0.25;
        accumulator += delta;

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
            sceDisplayWaitVblankStart();
        }
    }

    delete shell;
    delete vm;
    delete host;

    return 0;
}
