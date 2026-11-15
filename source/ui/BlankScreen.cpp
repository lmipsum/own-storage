// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/BlankScreen.h"

#include "app/DebugLog.h"

#include <malloc.h>
#include <rsx/gcm_sys.h>
#include <sys/systime.h>
#include <sysutil/video.h>

#include <cstdlib>
#include <cstring>

namespace
{
// The block of main memory the RSX reads its commands from: a MB, aligned to a MB, as the RSX
// maps main memory in such blocks; the command buffer takes half of it.
const std::uint32_t kHostBytes = 1024 * 1024;
const std::uint32_t kCommandBytes = 512 * 1024;
// A frame's pixel, x then red, green and blue: the dark grey the system's dialogs sit on.
const std::uint32_t kDarkPixel = 0x00181818;
// The label the RSX writes once it has done the commands before it.
const std::uint8_t kIdleLabel = 255;
const unsigned int kWaitMicroseconds = 200;
// Command buffer words kept free for a flip, with a wide margin.
const std::uint32_t kFlipCommandRoom = 0x800;

/**
 * Calls the context's callback, which hands the filled command buffer to the RSX and starts over.
 * librsx's rsxContextCallback does the same but does not put the context and the count in r3 and
 * r4. After SplinterGU/BennuGD2 ps3-psl1ght-patch/sdl2_psl1ght-cmdbuf-wrap.patch.
 */
s32 ContextCallback(gcmContextData *context, u32 count)
{
    register u64 r3 asm("r3") = reinterpret_cast<u64>(context);
    register u64 r4 asm("r4") = count;
    const u64 callback = reinterpret_cast<u64>(context->callback);

    // The callback's frame goes below the 288-byte red zone, which the compiler may use here
    // because it sees no call in this function.
    __asm__ __volatile__("stdu   1,-416(1)\n"
                         "mr     31,2\n"
                         "lwz    0,0(%2)\n"
                         "lwz    2,4(%2)\n"
                         "mtctr  0\n"
                         "bctrl\n"
                         "mr     2,31\n"
                         "addi   1,1,416\n"
                         : "+r"(r3), "+r"(r4)
                         : "b"(callback)
                         : "r31", "r0", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "fr0",
                           "fr1", "fr2", "fr3", "fr4", "fr5", "fr6", "fr7", "fr8", "fr9", "fr10",
                           "fr11", "fr12", "fr13", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
                           "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17",
                           "v18", "v19", "lr", "ctr", "cr0", "cr1", "cr5", "cr6", "cr7", "xer",
                           "memory");

    return static_cast<s32>(r3);
}

/** Waits until the RSX has done every command so far, as the display is set up only then. */
void WaitForIdle(gcmContextData *context)
{
    const u32 value = 1;
    rsxSetWriteBackendLabel(context, kIdleLabel, value);
    rsxFlushBuffer(context);
    while (*reinterpret_cast<vu32 *>(gcmGetLabelAddress(kIdleLabel)) != value)
    {
        sysUsleep(kWaitMicroseconds);
    }
}
} // namespace

BlankScreen::~BlankScreen()
{
    if (m_context != nullptr)
    {
        // Without a flip under way there is none to wait for.
        if (m_flipping)
        {
            gcmSetWaitFlip(m_context);
        }
        rsxFinish(m_context, 1);
    }
    for (std::uint32_t *buffer : m_buffers)
    {
        if (buffer != nullptr)
        {
            rsxFree(buffer);
        }
    }
    std::free(m_host);
}

bool BlankScreen::Open()
{
    m_host = memalign(kHostBytes, kHostBytes);
    if (m_host == nullptr)
    {
        return false;
    }
    const s32 started = rsxInit(&m_context, kCommandBytes, kHostBytes, m_host);
    videoState state;
    videoResolution resolution;
    if (started != 0 || m_context == nullptr || videoGetState(0, 0, &state) != 0 ||
        state.state != 0 || videoGetResolution(state.displayMode.resolution, &resolution) != 0)
    {
        debugPrintf("BlankScreen: the display cannot be read\n");
        return false;
    }
    debugPrintf("Resolution %u,%u\n", resolution.width, resolution.height);
    videoConfiguration configuration;
    std::memset(&configuration, 0, sizeof configuration);
    configuration.resolution = state.displayMode.resolution;
    configuration.format = VIDEO_BUFFER_FORMAT_XRGB;
    configuration.pitch = resolution.width * sizeof(std::uint32_t);
    configuration.aspect = state.displayMode.aspect;
    WaitForIdle(m_context);
    if (videoConfigure(0, &configuration, nullptr, 0) != 0)
    {
        debugPrintf("BlankScreen: the display cannot be set up\n");
        return false;
    }
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for (int id = 0; id < kBuffers; ++id)
    {
        if (!MakeBuffer(id, resolution.width, resolution.height))
        {
            debugPrintf("BlankScreen: no room for the frames\n");
            return false;
        }
    }
    gcmResetFlipStatus();
    // Queued now, so that every Flip() has a flip before it to wait for.
    gcmSetFlip(m_context, kBuffers - 1);
    rsxFlushBuffer(m_context);
    gcmSetWaitFlip(m_context);
    m_flipping = true;
    return true;
}

bool BlankScreen::MakeBuffer(int id, std::uint16_t width, std::uint16_t height)
{
    const std::uint32_t pixels = static_cast<std::uint32_t>(width) * height;
    std::uint32_t *buffer =
        static_cast<std::uint32_t *>(rsxMemalign(64, pixels * sizeof(std::uint32_t)));
    m_buffers[id] = buffer;
    u32 offset = 0;
    if (buffer == nullptr || rsxAddressToOffset(buffer, &offset) != 0)
    {
        return false;
    }
    for (std::uint32_t pixel = 0; pixel < pixels; ++pixel)
    {
        buffer[pixel] = kDarkPixel;
    }
    return gcmSetDisplayBuffer(id, offset, width * sizeof(std::uint32_t), width, height) == 0;
}

void BlankScreen::Flip()
{
    while (gcmGetFlipStatus() != 0)
    {
        sysUsleep(kWaitMicroseconds);
    }
    gcmResetFlipStatus();
    // gcmSetFlip and gcmSetWaitFlip write without checking for room, so a program that only
    // flips runs past the end of the buffer and stalls the RSX after about 256 frames.
    if (static_cast<std::uint32_t>(m_context->end - m_context->current) < kFlipCommandRoom)
    {
        ContextCallback(m_context, kFlipCommandRoom);
    }
    gcmSetFlip(m_context, m_current);
    rsxFlushBuffer(m_context);
    gcmSetWaitFlip(m_context);
    m_current = (m_current + 1) % kBuffers;
}
