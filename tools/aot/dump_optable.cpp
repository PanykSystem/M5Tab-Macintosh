// Dump UAE's decoded 68k opcode table as tab-separated text for aot_translate.py.
//
// Build (see tools/aot/build_tools.sh):
//   c++ -I tools/cpu_gen -o dump_optable dump_optable.cpp tools/cpu_gen/readcpu.cpp \
//       src/basilisk/uae_cpu/generated/cpudefs.cpp
//
// One line per legal opcode:
//   opcode handler mnemonic size smode sreg suse dmode dreg duse cc plev clev cflow
#include <cstdio>
#include <cstdlib>

#include "sysdeps.h"
#include "readcpu.h"

int main()
{
    read_table68k();
    do_merges();
    for (unsigned opcode = 0; opcode < 65536; ++opcode) {
        const struct instr &in = table68k[opcode];
        if (in.mnemo == i_ILLG) continue;
        const char *name = "?";
        for (struct mnemolookup *l = lookuptab; l->name[0]; ++l) {
            if (l->mnemo == (instrmnem)in.mnemo) { name = l->name; break; }
        }
        printf("%04x\t%04lx\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",
               opcode, in.handler == -1 ? (long)opcode : in.handler, name,
               (unsigned)in.size, (unsigned)in.smode, (unsigned)in.sreg, (unsigned)in.suse,
               (unsigned)in.dmode, (unsigned)in.dreg, (unsigned)in.duse,
               (unsigned)in.cc, (unsigned)in.plev, (unsigned)in.clev, (unsigned)in.cflow);
    }
    return 0;
}
