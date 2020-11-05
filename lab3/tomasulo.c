#include <limits.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
#include "memory.h"
#include "loader.h"
#include "syscall.h"
#include "dlite.h"
#include "options.h"
#include "stats.h"
#include "sim.h"
#include "decode.def"

#include "instr.h"

/* PARAMETERS OF THE TOMASULO'S ALGORITHM */

#define INSTR_QUEUE_SIZE   16

#define RESERV_INT_SIZE    5
#define RESERV_FP_SIZE     3
#define FU_INT_SIZE        3
#define FU_FP_SIZE         1

#define FU_INT_LATENCY     5
#define FU_FP_LATENCY      7

/* IDENTIFYING INSTRUCTIONS */

//unconditional branch, jump or call
#define IS_UNCOND_CTRL(op) (MD_OP_FLAGS(op) & F_CALL || \
                         MD_OP_FLAGS(op) & F_UNCOND)

//conditional branch instruction
#define IS_COND_CTRL(op) (MD_OP_FLAGS(op) & F_COND)

//floating-point computation
#define IS_FCOMP(op) (MD_OP_FLAGS(op) & F_FCOMP)

//integer computation
#define IS_ICOMP(op) (MD_OP_FLAGS(op) & F_ICOMP)

//load instruction
#define IS_LOAD(op)  (MD_OP_FLAGS(op) & F_LOAD)

//store instruction
#define IS_STORE(op) (MD_OP_FLAGS(op) & F_STORE)

//trap instruction
#define IS_TRAP(op) (MD_OP_FLAGS(op) & F_TRAP) 

#define USES_INT_FU(op) (IS_ICOMP(op) || IS_LOAD(op) || IS_STORE(op))
#define USES_FP_FU(op) (IS_FCOMP(op))

#define WRITES_CDB(op) (IS_ICOMP(op) || IS_LOAD(op) || IS_FCOMP(op))

/* FOR DEBUGGING */

//prints info about an instruction
#define PRINT_INST(out,instr,str,cycle)	\
  myfprintf(out, "%d: %s", cycle, str);		\
  md_print_insn(instr->inst, instr->pc, out); \
  myfprintf(stdout, "(%d)\n",instr->index);

#define PRINT_REG(out,reg,str,instr) \
  myfprintf(out, "reg#%d %s ", reg, str);	\
  md_print_insn(instr->inst, instr->pc, out); \
  myfprintf(stdout, "(%d)\n",instr->index);

/* VARIABLES */

//instruction queue for tomasulo
static instruction_t* instr_queue[INSTR_QUEUE_SIZE];
//number of instructions in the instruction queue
static int instr_queue_size = 0;

//reservation stations (each reservation station entry contains a pointer to an instruction)
static instruction_t* reservINT[RESERV_INT_SIZE];
static instruction_t* reservFP[RESERV_FP_SIZE];

//functional units
static instruction_t* fuINT[FU_INT_SIZE];
static instruction_t* fuFP[FU_FP_SIZE];

//common data bus
static instruction_t* commonDataBus = NULL;

//The map table keeps track of which instruction produces the value for each register
static instruction_t* map_table[MD_TOTAL_REGS];

//the index of the last instruction fetched
static int fetch_index = 1;
/* ECE552 Assignment 3 - BEGIN CODE */
/* FUNCTIONAL UNITS */
/* RESERVATION STATIONS */
/* INSTRUCTION FETCH QUEUE */
static int ifq_head = 0; // points to the head of ifq
static int ifq_tail = 0; // points to the tail of ifq
void ifq_insert(instruction_t* instr){
   if(instr_queue_size != 0){
      ifq_tail = (ifq_tail + 1) % INSTR_QUEUE_SIZE;
   }
   instr_queue[ifq_tail] = instr; 
   instr_queue_size++;
}

void ifq_delete(){
   instr_queue[ifq_head] = NULL;
   if(ifq_head != ifq_tail)
      ifq_head = (ifq_head+1) % INSTR_QUEUE_SIZE;
   instr_queue_size--;
}
/* ECE552 Assignment 3 - END CODE */



/* 
 * Description: 
 * 	Checks if simulation is done by finishing the very last instruction
 *      Remember that simulation is done only if the entire pipeline is empty
 * Inputs:
 * 	sim_insn: the total number of instructions simulated
 * Returns:
 * 	True: if simulation is finished
 */
static bool is_simulation_done(counter_t sim_insn) {
   /* ECE552 Assignment 3 - BEGIN CODE */
   if(fetch_index < sim_insn) return false;
   for(int i = 0; i < INSTR_QUEUE_SIZE; i++)
      if(instr_queue[i] != NULL) return false;
   for(int i = 0; i < RESERV_INT_SIZE; i++)
      if(reservINT[i] != NULL) return false;
   for(int i = 0; i < RESERV_FP_SIZE; i++)
      if(reservFP[i] != NULL) return false;
   for(int i = 0; i < FU_INT_SIZE; i++)
      if(fuINT[i] != NULL) return false;
   for(int i = 0; i < FU_FP_SIZE; i++)
      if(fuFP[i] != NULL) return false;
   if(commonDataBus != NULL) return false;
   
   return true;
   /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Retires the instruction from writing to the Common Data Bus
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void CDB_To_retire(int current_cycle) {
   /* ECE552 Assignment 3 - BEGIN CODE */
   if(commonDataBus != NULL){
      // broadcast to map_table
      for(int i = 0; i < MD_TOTAL_REGS; i++){
         if(map_table[i] == commonDataBus)
            map_table[i] = NULL;
      }
      // broadcast to reserv_INT
      for(int i = 0; i < RESERV_INT_SIZE; i++){
         for(int j = 0; j < 3; j++){
            if(reservINT[i] != NULL && reservINT[i]->Q[j] == commonDataBus)
               reservINT[i]->Q[j] = NULL;
         }
      }

      // broadcast to reserv_FP
      for(int i = 0; i < RESERV_FP_SIZE; i++){
         for(int j = 0; j < 3; j++){
            if(reservFP[i] != NULL && reservFP[i]->Q[j] == commonDataBus)
               reservFP[i]->Q[j] = NULL;
         }
      }
      commonDataBus = NULL; 
   }
   /* ECE552 Assignment 3 - END CODE */
}


/* 
 * Description: 
 * 	Moves an instruction from the execution stage to common data bus (if possible)
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void execute_To_CDB(int current_cycle) {
   /* ECE552 Assignment 3 - BEGIN CODE */
   int min_idx = -1;
   instruction_t* instr = NULL;

   for(int i = 0; i < FU_INT_SIZE; i++){
      if(fuINT[i] != NULL && (current_cycle - fuINT[i]->tom_execute_cycle >= FU_INT_LATENCY)){
         if(WRITES_CDB(fuINT[i]->op)){
            if(min_idx == -1 || fuINT[i]->index < min_idx){
               instr = fuINT[i];
               min_idx = fuINT[i]->index;
            } 
         } else {
            // free INT RS
            for(int j = 0; j < RESERV_INT_SIZE; j++){
               if(reservINT[j]==fuINT[i]){
                  reservINT[j] = NULL;
                  break;
               }
            }
            fuINT[i] = NULL;
         }
      }
   }
   
   for(int i = 0; i < FU_FP_SIZE; i++){
      if(fuFP[i] != NULL && (current_cycle - fuFP[i]->tom_execute_cycle >= FU_FP_LATENCY)){
         if(WRITES_CDB(fuFP[i]->op)){
             if(min_idx == -1 || fuFP[i]->index < min_idx){
                instr = fuFP[i];
                min_idx = fuFP[i]->index;
             }
          } else {
            // free FP RS
            for(int j = 0; j < RESERV_FP_SIZE; j++){
               if(reservFP[j]==fuFP[i]){
                  reservFP[j] = NULL;
                  break;
               }
            }
            fuFP[i] = NULL;
          }
      }
   }
  
   // instr finiches execution and ready to be written back
   if(instr != NULL){
      commonDataBus = instr;
      instr->tom_cdb_cycle = current_cycle;
      // release RS
      for(int i = 0; i < RESERV_INT_SIZE; i++){
         if(reservINT[i]==commonDataBus){
            reservINT[i] = NULL;
            break;
         }
      }
      for(int i = 0; i < RESERV_FP_SIZE; i++){
         if(reservFP[i] == commonDataBus){
            reservFP[i] = NULL;
            break;
         }
      }
      // release FU
      for(int i = 0; i < FU_INT_SIZE; i++){
         if(fuINT[i] == commonDataBus){
            fuINT[i] = NULL;
            break;
         }
      }
      for(int i = 0; i < FU_FP_SIZE; i++){
         if(fuFP[i] == commonDataBus){
            fuFP[i] = NULL;
            break;
         }
      }
   }
   /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Moves instruction(s) from the issue to the execute stage (if possible). We prioritize old instructions
 *      (in program order) over new ones, if they both contend for the same functional unit.
 *      All RAW dependences need to have been resolved with stalls before an instruction enters execute.
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void issue_To_execute(int current_cycle) {
   /* ECE552 Assignment 3 - BEGIN CODE */
   for(int i = 0; i < FU_INT_SIZE; i++){
      if(fuINT[i] == NULL){
         // find eldest ready instruction in RS
         instruction_t* instr = NULL;
         int min_idx = -1;
         for(int j = 0; j < RESERV_INT_SIZE; j++){
            if(reservINT[j] != NULL &&
               reservINT[j]->Q[0] == NULL &&
               reservINT[j]->Q[1] == NULL &&
               reservINT[j]->Q[2] == NULL &&
               reservINT[j]->tom_execute_cycle == 0){
               if(min_idx == -1 || reservINT[j]->index < min_idx){
                  min_idx = reservINT[j]->index;
                  instr = reservINT[j];
               }
            }
         }

         // allocate FU for the instruction
         if(instr != NULL){
            fuINT[i] = instr;
            instr->tom_execute_cycle = current_cycle;
         }
      }
   } 

   for(int i = 0; i < FU_FP_SIZE; i++){
      if(fuFP[i] == NULL){
         // find the oldest ready instruction in RS
         instruction_t* instr = NULL;
         int min_idx = -1;
         for(int j = 0; j < RESERV_FP_SIZE; j++){
            if(reservFP[j] != NULL &&
               reservFP[j]->Q[0] == NULL &&
               reservFP[j]->Q[1] == NULL &&
               reservFP[j]->Q[2] == NULL &&
               reservFP[j]->tom_execute_cycle == 0){
               if(min_idx == -1 || reservFP[j]->index < min_idx){
                  min_idx = reservFP[j]->index;
                  instr = reservFP[j];
               }      
            }
         }
      
         // allocate FU for the instruction
         if(instr != NULL){
            fuFP[i] = instr;
            instr->tom_execute_cycle = current_cycle;
         }
      }
   }
   /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Moves instruction(s) from the dispatch stage to the issue stage
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void dispatch_To_issue(int current_cycle) {
   /* ECE552 Assignment 3 - BEGIN CODE */
   if(instr_queue_size == 0) return;
   instruction_t* curr_instr = instr_queue[ifq_head];
   if(IS_COND_CTRL(curr_instr->op) || IS_UNCOND_CTRL(curr_instr->op)){
      ifq_delete();
      return;
   }
   // allocate new entry in INT RS
   else if(USES_INT_FU(curr_instr->op)){
      int reserv_int_idx;
      for(reserv_int_idx = 0; reserv_int_idx < RESERV_INT_SIZE ; reserv_int_idx++){
         if(reservINT[reserv_int_idx] == NULL) break;
      }
      if(reserv_int_idx >= RESERV_INT_SIZE) return;
      reservINT[reserv_int_idx] = curr_instr;
   }
   // allocate new entry in FP RS
   else if(USES_FP_FU(curr_instr->op)){
      int reserv_fp_idx;
      for(reserv_fp_idx = 0; reserv_fp_idx < RESERV_FP_SIZE; reserv_fp_idx++){
         if(reservFP[reserv_fp_idx] == NULL) break;
      }
      if(reserv_fp_idx >= RESERV_FP_SIZE) return;
      reservFP[reserv_fp_idx] = curr_instr;
   }
   // update start cycle of issue
   curr_instr->tom_issue_cycle = current_cycle;

   // update source registers
   for(int i = 0; i < 3; i++){
      if(curr_instr->r_in[i] != DNA && map_table[curr_instr->r_in[i]] != NULL){
         curr_instr->Q[i] = map_table[curr_instr->r_in[i]];
      }
   }

   // update map table
   for(int i = 0; i < 2; i++){
      if(curr_instr->r_out[i] != DNA){
         map_table[curr_instr->r_out[i]] = curr_instr;
      }
   }

   // remove instruction from ifq
   ifq_delete();
   /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Grabs an instruction from the instruction trace (if possible)
 * Inputs:
 *      trace: instruction trace with all the instructions executed
 * Returns:
 * 	None
 */
void fetch(instruction_trace_t* trace) {
   /* ECE552 Assignment 3 - BEGIN CODE */
   if(fetch_index > sim_num_insn)
      return;
   while(IS_TRAP(get_instr(trace, fetch_index)->op))
      fetch_index++;
   if(instr_queue_size < INSTR_QUEUE_SIZE){
      instruction_t* instr = get_instr(trace, fetch_index);
      ifq_insert(instr);
      fetch_index++;
   }
   /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Calls fetch and dispatches an instruction at the same cycle (if possible)
 * Inputs:
 *      trace: instruction trace with all the instructions executed
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void fetch_To_dispatch(instruction_trace_t* trace, int current_cycle) {

   fetch(trace);

   /* ECE552 Assignment 3 - BEGIN CODE */
   instruction_t* instr = instr_queue[ifq_tail];
   if(instr != NULL && instr->tom_dispatch_cycle == 0){
      instr->tom_dispatch_cycle = current_cycle;
   }
   /* ECE552 Assignment 3 - END CODE */
}

/* 
 * Description: 
 * 	Performs a cycle-by-cycle simulation of the 4-stage pipeline
 * Inputs:
 *      trace: instruction trace with all the instructions executed
 * Returns:
 * 	The total number of cycles it takes to execute the instructions.
 * Extra Notes:
 * 	sim_num_insn: the number of instructions in the trace
 */
counter_t runTomasulo(instruction_trace_t* trace)
{
  //initialize instruction queue
  int i;
  for (i = 0; i < INSTR_QUEUE_SIZE; i++) {
    instr_queue[i] = NULL;
  }

  //initialize reservation stations
  for (i = 0; i < RESERV_INT_SIZE; i++) {
      reservINT[i] = NULL;
  }

  for(i = 0; i < RESERV_FP_SIZE; i++) {
      reservFP[i] = NULL;
  }

  //initialize functional units
  for (i = 0; i < FU_INT_SIZE; i++) {
    fuINT[i] = NULL;
  }

  for (i = 0; i < FU_FP_SIZE; i++) {
    fuFP[i] = NULL;
  }

  //initialize map_table to no producers
  int reg;
  for (reg = 0; reg < MD_TOTAL_REGS; reg++) {
    map_table[reg] = NULL;
  }
  
  int cycle = 1;
  while (true) {
     /* ECE552 Assignment 3 - BEGIN CODE */
     CDB_To_retire(cycle);
     execute_To_CDB(cycle);
     issue_To_execute(cycle);
     dispatch_To_issue(cycle);
     fetch_To_dispatch(trace, cycle);
     cycle++;
     if (is_simulation_done(sim_num_insn))
        break;
     /* ECE552 Assignment 3 - END CODE */
  }
  
  return cycle;
}
