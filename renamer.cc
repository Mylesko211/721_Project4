#include <iostream>
#include <cassert>
#include <cstring>
#include "renamer.h"

using namespace std;

renamer::renamer(uint64_t n_log_regs,
    uint64_t n_phys_regs,
    uint64_t n_branches,
    uint64_t n_active)
{
    assert(n_phys_regs > n_log_regs);
    assert(n_branches >= 1 && n_branches <= 64);
    assert(n_active > 0);

    this->n_log_regs = n_log_regs;
    this->n_phys_regs = n_phys_regs;
    this->n_branches = n_branches;
    this->n_active = n_active;

    RMT = new uint64_t[n_log_regs];
    AMT = new uint64_t[n_log_regs];

    for (uint64_t i = 0; i < n_log_regs; i++) {
        RMT[i] = i;
        AMT[i] = i;
    }

    FL_size = n_phys_regs - n_log_regs;
    FreeList = new uint64_t[FL_size];

    for (uint64_t i = 0; i < FL_size; i++) {
        FreeList[i] = n_log_regs + i;
    }
    
    FL_head = 0;
    FL_tail = 0;
    FL_head_phase = 0;
    FL_tail_phase = 1;

    AL_size = n_active;
    ActiveList = new AL_entry[AL_size];
    AL_head = 0;
    AL_tail = 0;
    AL_head_phase = 0;
    AL_tail_phase = 0;

    PRF = new uint64_t[n_phys_regs];
    PRF_ready = new bool[n_phys_regs];
    
    for (uint64_t i = 0; i < n_phys_regs; i++) {
        if (i < n_log_regs)
            PRF_ready[i] = true;
        else
            PRF_ready[i] = true; 
        PRF[i] = 0; 
    }

    GBM = 0;

    Checkpoints = new Checkpoint_entry[n_branches];
    for (uint64_t i = 0; i < n_branches; i++) {
        Checkpoints[i].shadow_RMT = new uint64_t[n_log_regs];
        Checkpoints[i].GBM = 0;
        Checkpoints[i].FL_head = 0;
        Checkpoints[i].FL_head_phase = 0;
    }
}

renamer::~renamer() {
    delete[] RMT;
    delete[] AMT;
    delete[] FreeList;
    delete[] ActiveList;
    delete[] PRF;
    delete[] PRF_ready;
    for (uint64_t i = 0; i < n_branches; i++) {
        delete[] Checkpoints[i].shadow_RMT;
    }
    delete[] Checkpoints;
}

bool renamer::stall_reg(uint64_t bundle_dst) {
    uint64_t available = 0;

    if (FL_head_phase == FL_tail_phase) {
        available = FL_tail - FL_head;
    } else {
        available = FL_size - (FL_head - FL_tail);
    }
    
    return available < bundle_dst;
}

bool renamer::stall_branch(uint64_t bundle_branch) {
    uint64_t free_checkpoints = 0;
    for (uint64_t i = 0; i < n_branches; i++) {
        if (!((GBM >> i) & 1)) {
            free_checkpoints++;
        }
    }
    return free_checkpoints < bundle_branch;
}

uint64_t renamer::get_branch_mask() {
    return GBM;
}

int renamer::get_free_phys_regs() {
    uint64_t available = 0;

    if (FL_head_phase == FL_tail_phase) {
        available = FL_tail - FL_head;
    } else {
        available = FL_size - (FL_head - FL_tail);
    }
    
    return available;
}

int renamer::get_free_checkpoints() {
    uint64_t free_checkpoints = 0;
    for (uint64_t i = 0; i < n_branches; i++) {
        if (!((GBM >> i) & 1)) {
            free_checkpoints++;
        }
    }
    return free_checkpoints;
}

uint64_t renamer::rename_rsrc(uint64_t log_reg) {
    return RMT[log_reg];
}

uint64_t renamer::rename_rdst(uint64_t log_reg) {
    uint64_t phys_reg = FreeList[FL_head];
    
    RMT[log_reg] = phys_reg;
    
    FL_head++;
    if (FL_head == FL_size) {
        FL_head = 0;
        FL_head_phase = !FL_head_phase;
    }
    
    return phys_reg;
}

uint64_t renamer::checkpoint() {
    int branch_id = -1;
    for (uint64_t i = 0; i < n_branches; i++) {
        if (!((GBM >> i) & 1)) {
            branch_id = i;
            break;
        }
    }
    
    memcpy(Checkpoints[branch_id].shadow_RMT, RMT, sizeof(uint64_t) * n_log_regs);
    Checkpoints[branch_id].FL_head = FL_head;
    Checkpoints[branch_id].FL_head_phase = FL_head_phase;
    Checkpoints[branch_id].GBM = GBM;

    GBM |= (1ULL << branch_id);

    return branch_id;
}

bool renamer::stall_dispatch(uint64_t bundle_inst) {
    uint64_t count = 0;
    if (AL_tail_phase == AL_head_phase) {
        count = AL_tail - AL_head;
    } else {
        count = AL_size - (AL_head - AL_tail);
    }
    
    uint64_t space = AL_size - count;
    return space < bundle_inst;
}

uint64_t renamer::dispatch_inst(bool dest_valid,
                       uint64_t log_reg,
                       uint64_t phys_reg,
                       bool load,
                       bool store,
                       bool branch,
                       bool amo,
                       bool csr,
                       uint64_t PC) {
    uint64_t index = AL_tail;
    
    ActiveList[index].dest_valid = dest_valid;
    ActiveList[index].log_reg = log_reg;
    ActiveList[index].phys_reg = phys_reg;
    ActiveList[index].load = load;
    ActiveList[index].store = store;
    ActiveList[index].branch = branch;
    ActiveList[index].amo = amo;
    ActiveList[index].csr = csr;
    ActiveList[index].PC = PC;
    
    ActiveList[index].completed = false;
    ActiveList[index].exception = false;
    ActiveList[index].load_viol = false;
    ActiveList[index].br_misp = false;
    ActiveList[index].val_misp = false;

    AL_tail++;
    if (AL_tail == AL_size) {
        AL_tail = 0;
        AL_tail_phase = !AL_tail_phase;
    }

    return index;
}

bool renamer::is_ready(uint64_t phys_reg) {
    return PRF_ready[phys_reg];
}

void renamer::clear_ready(uint64_t phys_reg) {
    PRF_ready[phys_reg] = false;
}

uint64_t renamer::read(uint64_t phys_reg) {
    return PRF[phys_reg];
}

void renamer::set_ready(uint64_t phys_reg) {
    PRF_ready[phys_reg] = true;
}

void renamer::write(uint64_t phys_reg, uint64_t value) {
    PRF[phys_reg] = value;
    PRF_ready[phys_reg] = true;
}

void renamer::set_complete(uint64_t AL_index) {
    ActiveList[AL_index].completed = true;
}

void renamer::resolve(uint64_t AL_index,
		     uint64_t branch_ID,
		     bool correct) {
    if (correct) {
        GBM &= ~(1ULL << branch_ID);
        for (uint64_t i = 0; i < n_branches; i++) {
             Checkpoints[i].GBM &= ~(1ULL << branch_ID);
        }
    } else {
        GBM = Checkpoints[branch_ID].GBM;
        GBM &= ~(1ULL << branch_ID);

        memcpy(RMT, Checkpoints[branch_ID].shadow_RMT, sizeof(uint64_t) * n_log_regs);

        FL_head = Checkpoints[branch_ID].FL_head;
        FL_head_phase = Checkpoints[branch_ID].FL_head_phase;

        AL_tail = AL_index + 1;
        if (AL_tail == AL_size) {
            AL_tail = 0;
        }

        if (AL_head <= AL_index) {
            AL_tail_phase = AL_head_phase;
        } else {
            AL_tail_phase = !AL_head_phase;
        }

        if (AL_tail == 0) {
            AL_tail_phase = !AL_tail_phase;
        }
    }
}

bool renamer::precommit(bool &completed,
                       bool &exception, bool &load_viol, bool &br_misp, bool &val_misp,
	               bool &load, bool &store, bool &branch, bool &amo, bool &csr,
		       uint64_t &PC) {
    if (AL_head == AL_tail && AL_head_phase == AL_tail_phase) {
        return false;
    }
    
    AL_entry &head = ActiveList[AL_head];
    
    completed = head.completed;
    exception = head.exception;
    load_viol = head.load_viol;
    br_misp = head.br_misp;
    val_misp = head.val_misp;
    load = head.load;
    store = head.store;
    branch = head.branch;
    amo = head.amo;
    csr = head.csr;
    PC = head.PC;
    
    return true;
}

void renamer::commit() {
    AL_entry &head = ActiveList[AL_head];
    
    if (head.dest_valid) {
        uint64_t old_phys_reg = AMT[head.log_reg];
        AMT[head.log_reg] = head.phys_reg;
        
        FreeList[FL_tail] = old_phys_reg;
        FL_tail++;
        if (FL_tail == FL_size) {
            FL_tail = 0;
            FL_tail_phase = !FL_tail_phase;
        }
    }
    
    AL_head++;
    if (AL_head == AL_size) {
        AL_head = 0;
        AL_head_phase = !AL_head_phase;
    }
}

void renamer::squash() {
    memcpy(RMT, AMT, sizeof(uint64_t) * n_log_regs);
    
    FL_head = 0;
    FL_tail = 0;
    FL_head_phase = 0;
    FL_tail_phase = 0; 
    
    bool *is_mapped = new bool[n_phys_regs];
    for (uint64_t i = 0; i < n_phys_regs; i++) is_mapped[i] = false;
    
    for (uint64_t i = 0; i < n_log_regs; i++) {
        is_mapped[AMT[i]] = true;
    }
    
    for (uint64_t i = 0; i < n_phys_regs; i++) {
        if (!is_mapped[i]) {
            FreeList[FL_tail] = i;
            FL_tail++;
        }
    }
    delete[] is_mapped;
    
    if (FL_tail == FL_size) {
        FL_tail = 0;
        FL_tail_phase = 1;
    }
    
    AL_head = 0;
    AL_tail = 0;
    AL_head_phase = 0;
    AL_tail_phase = 0;
    
    GBM = 0;
    
    for (uint64_t i = 0; i < n_phys_regs; i++) {
        PRF_ready[i] = true;
    }
}

void renamer::set_exception(uint64_t AL_index) {
    ActiveList[AL_index].exception = true;
}

void renamer::set_load_violation(uint64_t AL_index) {
    ActiveList[AL_index].load_viol = true;
}

void renamer::set_branch_misprediction(uint64_t AL_index) {
    ActiveList[AL_index].br_misp = true;
}

void renamer::set_value_misprediction(uint64_t AL_index) {
    ActiveList[AL_index].val_misp = true;
}

bool renamer::get_exception(uint64_t AL_index) {
    return ActiveList[AL_index].exception;
}