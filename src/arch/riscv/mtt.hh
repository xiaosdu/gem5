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


#ifndef __ARCH_RISCV_MTT_HH__
#define __ARCH_RISCV_MTT_HH__

#include "arch/riscv/isa.hh"
#include "cpu/thread_context.hh"
#include "mem/request.hh"
#include "sim/sim_object.hh"

#include "arch/generic/tlb.hh"
#include "base/addr_range.hh"
#include "base/types.hh"
#include "mem/packet.hh"
#include "params/MTT.hh"

namespace gem5
{

namespace RiscvISA
{

/**
 * This class implements the RISC-V Memory Type Table (MTT) checker,
 * similar to SMMTT as described in some RISC-V proposals.
 * It checks access permissions for memory accesses after PMP.
 */
class MTT : public SimObject
{
  public:
    PARAMS(MTT);
    MTT(const Params &params);

  public:
    // MTT root physical address (PPN << PGSHIFT)
    Addr mttRootAddr;

    // Optional: number of levels (if fixed in your MTT design)
    int mttLevels;

    // Optional: Cached value of the mttp CSR
    uint64_t cached_mttp;

    Fault createAddrfault(Addr vaddr, BaseMMU::Mode mode);

    // Helper to walk the MTT table in system memory
    Fault walkMTT(const RequestPtr &req, BaseMMU::Mode mode, PrivilegeMode pmode, ThreadContext *tc, Addr vaddr = 0UL);

    // Helper: decode mttp CSR to get table root/level
    bool decodeMTTP(uint64_t mttp, int &level, Addr &root_ppn);
};

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_MTT_HH__
