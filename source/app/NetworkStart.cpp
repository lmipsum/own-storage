// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/NetworkStart.h"

#include "app/DebugLog.h"

#include <net/netctl.h>
#include <sys/systime.h>
#include <sysmodule/sysmodule.h>

namespace
{
// The dialog's parameter and result as the system lays them out. The SDK's own structs start
// with a 64-bit size_t where the system has 32 bits, and its net/netctl.h keeps the functions
// in a comment, though its libnetctl exports them.
struct NetStartParam
{
    u32 size;
    s32 type;
    u32 cid;
};
struct NetStartResult
{
    u32 size;
    s32 result;
};
static_assert(sizeof(NetStartParam) == 12 && sizeof(NetStartResult) == 8,
              "the system checks these sizes");

// Short enough that the app goes on without a felt delay once the dialog closes.
const unsigned int kPollMicroseconds = 20000;
} // namespace

extern "C"
{
    s32 netCtlNetStartDialogLoadAsync(NetStartParam *param);
    s32 netCtlNetStartDialogAbortAsync(void);
    s32 netCtlNetStartDialogUnloadAsync(NetStartResult *result);
}

NetworkStart::NetworkStart(const std::atomic<int> &quit)
    : m_quit(quit), m_loaded(false), m_started(false), m_finished(false), m_unloaded(false)
{
}

void NetworkStart::Start()
{
    // netInitialize() loads the network itself, not the library of its dialog.
    const s32 loaded = sysModuleLoad(SYSMODULE_NETCTL);
    m_loaded = loaded == 0;
    const s32 started = m_loaded ? netCtlInit() : loaded;
    m_started = m_loaded && started == 0;
    if (!m_started)
    {
        debugPrintf("NetworkStart::Start: the dialog's library cannot start: %08x\n",
                    static_cast<unsigned int>(started));
    }
}

void NetworkStart::Stop()
{
    if (m_started)
    {
        netCtlTerm();
    }
    if (m_loaded)
    {
        sysModuleUnload(SYSMODULE_NETCTL);
    }
    m_started = false;
    m_loaded = false;
}

void NetworkStart::OnSystemEvent(u64 status)
{
    if (status == SYSUTIL_NET_CTL_NETSTART_FINISHED)
    {
        m_finished = true;
    }
    else if (status == SYSUTIL_NET_CTL_NETSTART_UNLOADED)
    {
        m_unloaded = true;
    }
}

bool NetworkStart::Connect()
{
    if (!m_started)
    {
        return true;
    }
    m_finished = false;
    m_unloaded = false;
    NetStartParam param = {sizeof param, NET_CTL_NETSTART_TYPE_NET, 0};
    const s32 loaded = netCtlNetStartDialogLoadAsync(&param);
    if (loaded != 0)
    {
        debugPrintf("NetworkStart::Connect: the dialog cannot open: %08x\n",
                    static_cast<unsigned int>(loaded));
        return true;
    }
    if (!WaitFor(m_finished))
    {
        // The app is closing; the dialog must not stay over the XMB.
        netCtlNetStartDialogAbortAsync();
        return false;
    }
    NetStartResult result = {sizeof result, 0};
    const s32 unloading = netCtlNetStartDialogUnloadAsync(&result);
    if (unloading != 0)
    {
        // No result and no event to wait for; a request that fails says so in its own words.
        debugPrintf("NetworkStart::Connect: the dialog cannot close: %08x\n",
                    static_cast<unsigned int>(unloading));
        return true;
    }
    if (!WaitFor(m_unloaded))
    {
        return false;
    }
    if (result.result != 0)
    {
        debugPrintf("NetworkStart::Connect: no connection: %08x\n",
                    static_cast<unsigned int>(result.result));
    }
    return result.result == 0;
}

bool NetworkStart::WaitFor(const std::atomic<bool> &event) const
{
    while (!event)
    {
        if (m_quit != 0)
        {
            return false;
        }
        sysUsleep(kPollMicroseconds);
    }
    return true;
}
