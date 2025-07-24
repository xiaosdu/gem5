/*
 * Copyright (c) 2025 xiao xu
 * Copyright (c) 2025 Shandong University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "arch/riscv/mtt.hh"
#include "arch/riscv/isa.hh"
#include "arch/riscv/regs/misc.hh"
#include "cpu/thread_context.hh"
#include "mem/request.hh"
#include "sim/system.hh"

#include "arch/generic/tlb.hh"
#include "arch/riscv/faults.hh"
#include "base/addr_range.hh"
#include "base/types.hh"
#include "math.h"
#include "sim/sim_object.hh"

namespace gem5
{

namespace RiscvISA
{

static const uint64_t mtt_masks[] = {
    0xFULL << 12,       // L1 page mask
    0x1FFULL << 16,     // L1 mask
    0x1FFFFFULL << 25,  // L2 mask
    0x3FFULL << 46      // L3 mask
};

static const int mtt_shifts[] = {
    12,  // L1 page
    16,  // L1
    25,  // L2
    46   // L3
};


#define MTT_PERM_FIELD(idx) \
    0b11 << ((idx) * 2)

#define MTTL2_GET_TYPE(entry, val) \
    val = (entry & 0xe0000) >> 17

#define MTTL2_GET_INFO(entry, val) \
    val = entry & 0x1fffffffff

#define MTTL2_GET_PERMS(entry, addr, val) \
    val = entry & MTT_PERM_FIELD((addr & 0x00000001e00000) >> 21)
    
#define MTTL3_GET_INFO(entry, val) \
    val = entry & 0xfffffffffff 

#define MTTL1_GET_PERMS(entry, val) \
    val = (entry & 0x0000000000f000) >> 12

MTT::MTT(const Params &params)
    : SimObject(params),
      mttRootAddr(0),
      mttLevels(3),
      cached_mttp(0)
{
    // You can initialize or reset here if needed
}

Fault
MTT::createAddrfault(Addr vaddr, BaseMMU::Mode mode)
{
    ExceptionCode code;
    if (mode == BaseMMU::Read) {
        code = ExceptionCode::LOAD_ACCESS;
    } else if (mode == BaseMMU::Write) {
        code = ExceptionCode::STORE_ACCESS;
    } else {
        code = ExceptionCode::INST_ACCESS;
    }
    warn("mtt access fault.\n");
    return std::make_shared<AddressFault>(vaddr, code);
}

Fault
MTT::walkMTT(const RequestPtr &req, BaseMMU::Mode mode, PrivilegeMode pmode, ThreadContext *tc, Addr vaddr)
{
    int level = 0;
    int index = 0;
    int perms = 0;
    Addr mtt_ppn = 0;
    Addr addr = req->getPaddr();

    if (pmode == PrivilegeMode::PRV_M) {
        // Machine mode always allows access
        return NoFault;
    }

    uint64_t mttp = tc->readMiscReg(MISCREG_MTTP);
    
    if (!decodeMTTP(mttp, level, mtt_ppn))
        return NoFault;

    bool found = false;
    Addr cur_ppn = mtt_ppn;

    for (; level > 0 && !found; level--)
    {
        index = (addr & mtt_masks[level]) >> mtt_shifts[level];
        Addr entry_addr = (cur_ppn >> 12) + index * sizeof(uint64_t);

        uint64_t entry_val = 0;
        tc->getSystemPtr()->physProxy.readBlob(entry_addr, (uint8_t*)&entry_val, sizeof(entry_val));
        if (level == 3)
        {
            if (entry_val == 0)
            {
                return createAddrfault(vaddr, mode);
            }
            MTTL3_GET_INFO(entry_val, cur_ppn);
        }
        else if (level == 2)
        {
            int type = 0;
            MTTL2_GET_TYPE(entry_val, type);
            switch(type)
            {
                case 0: // 1G_disallow
                    return createAddrfault(vaddr, mode);
                case 1: // 1G_rx
                    if (mode == BaseMMU::Write) {
                        return createAddrfault(vaddr, mode);
                    }
                    break;
                case 2: // 1G_rw
                    if (mode == BaseMMU::Execute) {
                        return createAddrfault(vaddr, mode);
                    }
                    break;
                case 3: // 1G_rwx
                    return NoFault;
                case 4: // MTTL1
                    if (entry_val == 0)
                    {
                        return createAddrfault(vaddr, mode);
                    }
                    MTTL2_GET_INFO(entry_val, cur_ppn);
                    break;
                case 6: // 2M
                    if (entry_val == 0)
                    {
                        return createAddrfault(vaddr, mode);
                    }
                    MTTL2_GET_PERMS(entry_val, addr, perms);
                    break;
                default:
                    warn("Unsupported MTT type %d at level %d\n", type, level);
                    return createAddrfault(vaddr, mode);
            }
        }
        else if (level == 1)
        {
            MTTL1_GET_PERMS(entry_val, perms);
        }
    }
    switch (perms)
    {
    case 0:
        createAddrfault(addr, mode);
    case 1:
        if (mode == BaseMMU::Write)
        {
            return createAddrfault(addr, mode);
        }
    case 2:
        if (mode == BaseMMU::Execute)
        {
            return createAddrfault(addr, mode);
        }
    case 3:
        return NoFault;
    default:
        return createAddrfault(addr, mode);
    }
}

bool
MTT::decodeMTTP(uint64_t mttp, int &level, Addr &root_ppn)
{
    // TODO: Extract level and root PPN from mttp (refer to your QEMU's smmtt_decode_mttp)
    // Example stub:
    int mode = (mttp >> 60) & 0xf;
    switch (mode)
    {
        case 0: 
            level = 0;
            break;
        case 1:
            level = 2;
            break;
        case 2:
            level = 3;
            break;
        default:
            warn("Unsupported MTTP mode: %d\n", mode);
    }
    root_ppn = (mttp & 0xFFFFFFFFFFFULL); // Adjust mask and shift to your format
    return true;
}

} // namespace RiscvISA
} // namespace gem5
