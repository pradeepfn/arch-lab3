#include "procsim.hpp"

proc_settings_t cpu;

std::vector<proc_inst_ptr_t> all_instrs;

std::deque<proc_inst_ptr_t> dispatching_queue;
std::vector<proc_inst_ptr_t> scheduling_queue;
int scheduling_queue_limit;

std::unordered_map<uint32_t, register_info_t> register_file;

std::vector<proc_cdb_t> cdb;
std::unordered_map<uint32_t, uint32_t> fu_cnt;


/**
 * Subroutine for initializing the processor. You many add and initialize any global or heap
 * variables as needed.
 * XXX: You're responsible for completing this routine
 *
 * @r Number of r result buses
 * @k0 Number of k0 FUs
 * @k1 Number of k1 FUs
 * @k2 Number of k2 FUs
 * @f Number of instructions to fetch
 */
void setup_proc(proc_stats_t *p_stats, uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f, uint64_t begin_dump, uint64_t end_dump) {
    p_stats->retired_instruction = 0;
    p_stats->cycle_count = 1;

    cpu = proc_settings_t(f, begin_dump, end_dump);

    for(int i = 0; i < 64; i++){
        register_file[i] = {true};    
    }

    scheduling_queue_limit = 2 * (k0 + k1 + k2);
    cdb.resize(r, {true});
    fu_cnt[0] = k0;
    fu_cnt[1] = k1;
    fu_cnt[2] = k2;
}

/**
 * Subroutine for cleaning up any outstanding instructions and calculating overall statistics
 * such as average IPC, average fire rate etc.
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */
void complete_proc(proc_stats_t *p_stats) {
    p_stats->avg_disp_size = p_stats->sum_disp_size / p_stats->cycle_count;
    p_stats->avg_inst_retired = p_stats->retired_instruction * 1.f / p_stats->cycle_count; 
}

/**
 * Subroutine that simulates the processor.
 *   The processor should fetch instructions as appropriate, until all instructions have executed
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */
void run_proc(proc_stats_t* p_stats) {   
    while (!cpu.finished) {
        // invoke pipline for current cycle
        state_update(p_stats, cycle_half_t::FIRST);
        execute(p_stats, cycle_half_t::FIRST);
        schedule(p_stats, cycle_half_t::FIRST);
        dispatch(p_stats, cycle_half_t::FIRST);

        state_update(p_stats, cycle_half_t::SECOND);

        if (!cpu.finished){
            execute(p_stats, cycle_half_t::SECOND);
            schedule(p_stats, cycle_half_t::SECOND);
            dispatch(p_stats, cycle_half_t::SECOND);
            instr_fetch_and_decode(p_stats, cycle_half_t::SECOND);            
        
            p_stats->cycle_count++;
        }
    }
    
    // print result
    if(cpu.begin_dump > 0){
        std::cout << "INST\tFETCH\tDISP\tSCHED\tEXEC\tSTATE" << std::endl;

        for(unsigned i = 0; i < all_instrs.size(); i++){
            auto instr = all_instrs[i];
            if(instr->id >= cpu.begin_dump && instr->id <= cpu.end_dump){
                std::cout << instr->id << "\t"
                          << instr->cycle_fetch_decode << "\t" 
                          << instr->cycle_dispatch << "\t"
                          << instr->cycle_schedule << "\t"
                          << instr->cycle_execute << "\t"
                          << instr->cycle_status_update << std::endl;  
            }
        }
        std::cout << std::endl;
    }
}

/** STATE UPDATE stage */
void state_update(proc_stats_t* p_stats, const cycle_half_t &half) {
    if (half == cycle_half_t::FIRST) {
        // record instr entry cycle
        for(unsigned i = 0; i < scheduling_queue.size(); i++){
            auto instr = scheduling_queue[i];
            if (instr->executed && !instr->cycle_status_update) {
                instr->cycle_status_update = p_stats->cycle_count;              
            }
        }        
    } else {
        // delete instructions from scheduling queue
        auto it = scheduling_queue.begin();
        while(it != scheduling_queue.end()){
            auto instr = *it;

            if(instr->cycle_status_update){
                it = scheduling_queue.erase(it);
                p_stats->retired_instruction++;
            }else{
                it++;
            }
        }
        
        if (cpu.read_finished && p_stats->retired_instruction == cpu.read_cnt) 
            cpu.finished = true;        
    }
}

/** EXECUTE stage */
void execute(proc_stats_t* p_stats, const cycle_half_t &half) {
    if (half == cycle_half_t::FIRST) {
        // record instr entry cycle
        for(unsigned i = 0; i < scheduling_queue.size(); i++){
            auto instr = scheduling_queue[i];            
            if (instr->fired == true && !instr->cycle_execute) {
                instr->cycle_execute = p_stats->cycle_count;                  

                instr->executed = true;   
            }
        }
    } else {
    }
}

/** SCHEDULE stage */
void schedule(proc_stats_t* p_stats, const cycle_half_t &half) {
    if (half == cycle_half_t::FIRST) {
        // record instr entry cycle
        for(unsigned i = 0; i < scheduling_queue.size(); i++){
            auto instr = scheduling_queue[i];            
            if (instr->fire)
                continue;
            
            if (!instr->cycle_schedule) {
                instr->cycle_schedule = p_stats->cycle_count;                 
            } 
            
            instr->fire = true;
        } 
    } else {        
        // fire all marked instructions if possible
        for(unsigned i = 0; i < scheduling_queue.size(); i++){
            auto instr = scheduling_queue[i];            
            if (instr->fire && !instr->fired) {                
                instr->fired = true;
            }
        }
    }
}

/** DISPATCH stage */
void dispatch(proc_stats_t* p_stats, const cycle_half_t &half) {
    if (half == cycle_half_t::FIRST) {    
        if (p_stats->max_disp_size < dispatching_queue.size())
            p_stats->max_disp_size = dispatching_queue.size();
            
        p_stats->sum_disp_size += dispatching_queue.size();

        for(unsigned i = 0; i < dispatching_queue.size(); i++){
            auto instr = dispatching_queue[i];            
            instr->reserved = true;
        }
    } else {
        while (!dispatching_queue.empty()) {
            auto instr = dispatching_queue.front();
            
            if (!instr->reserved)
                break;
                
            scheduling_queue.push_back(instr);            

            dispatching_queue.pop_front();
        }        
    }
}

/** INSTR-FETCH & DECODE stage */
void instr_fetch_and_decode(proc_stats_t* p_stats, const cycle_half_t &half) {
    if (half == cycle_half_t::SECOND) {          
        // read the next instructions 
        if (!cpu.read_finished){
            for (uint64_t i = 0; i < cpu.f; i++) { 
                proc_inst_ptr_t instr = proc_inst_ptr_t(new proc_inst_t());
              
                all_instrs.push_back(instr);
                                
                if (read_instruction(instr.get())) { 
                    // reset counters
                    instr->id = cpu.read_cnt + 1;

                    instr->fire = false;
                    instr->fired = false;
                    instr->executed = false;

                    instr->cycle_fetch_decode = p_stats->cycle_count;
                    instr->cycle_dispatch = p_stats->cycle_count + 1;
                    instr->cycle_schedule = 0;
                    instr->cycle_execute = 0;
                    instr->cycle_status_update = 0;                               
                    
                    dispatching_queue.push_back(instr);                                              
                    cpu.read_cnt++;                     
                } else {
                    all_instrs.pop_back();
                
                    cpu.read_finished = true;  
                    break;
                }
            }
        }   
    }
}





