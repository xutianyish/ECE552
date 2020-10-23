#include "predictor.h"

typedef enum{
   STRONGLY_NOT_TAKEN,
   WEAKLY_NOT_TAKEN,
   WEAKLY_TAKEN,
   STRONGLY_TAKEN
} state_t;

/////////////////////////////////////////////////////////////
// 2bitsat
/////////////////////////////////////////////////////////////
#define SIZE_2BITSAT 4096

UINT32 mem_2bitsat[SIZE_2BITSAT];
void InitPredictor_2bitsat() {
   for(int i = 0; i < SIZE_2BITSAT; i++){
      mem_2bitsat[i] = WEAKLY_NOT_TAKEN;
   }
}

bool GetPrediction_2bitsat(UINT32 PC) {
   UINT32 pred = mem_2bitsat[PC % SIZE_2BITSAT]; 
   if(pred == STRONGLY_NOT_TAKEN || pred == WEAKLY_NOT_TAKEN){
      return NOT_TAKEN;
   }else{
      return TAKEN;
   }
}

void UpdatePredictor_2bitsat(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {
   UINT32 idx = PC % SIZE_2BITSAT;
   if(resolveDir == TAKEN){
      if(mem_2bitsat[idx] != STRONGLY_TAKEN){
         mem_2bitsat[idx]++;
      }
   } else {
      if(mem_2bitsat[idx] != STRONGLY_NOT_TAKEN){
         mem_2bitsat[idx]--;
      }
   }
}

/////////////////////////////////////////////////////////////
// 2level
/////////////////////////////////////////////////////////////
#define SIZE_BHT 512
#define PHT_COL 8
#define PHT_ROW 64
UINT32 BHT[SIZE_BHT]; 
UINT32 PHT[PHT_COL][PHT_ROW];

void InitPredictor_2level() {
   // initialize all prediction entries to weakly not taken 
   for(int i = 0; i < PHT_COL; i++){
      for(int j = 0; j < PHT_ROW; j++){
         PHT[i][j] = WEAKLY_NOT_TAKEN;
      }
   }

   // initialize all history entries to not taken
   for(int i = 0; i < SIZE_BHT; i++){
      BHT[i] = 0;
   }
}

bool GetPrediction_2level(UINT32 PC) {
   UINT32 BHT_idx = (PC >> 3) % SIZE_BHT;
   UINT32 PHT_idx1 = (PC % PHT_COL);
   UINT32 PHT_idx2 = (BHT[BHT_idx] % PHT_ROW);
   UINT32 pred = PHT[PHT_idx1][PHT_idx2];
   if(pred == STRONGLY_TAKEN || pred == WEAKLY_TAKEN){
      return TAKEN;
   }else{
      return NOT_TAKEN;
   }
}

void UpdatePredictor_2level(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {
   UINT32 PHT_idx1 = (PC % PHT_COL);
   UINT32 BHT_idx = (PC >> 3) % SIZE_BHT;
   UINT32 PHT_idx2 = (BHT[BHT_idx] % PHT_ROW);
   // update entry in BHT
   BHT[BHT_idx] = ((BHT[BHT_idx] << 1) | resolveDir) % PHT_ROW;
   
   // update prediction in PHT
   if(resolveDir == TAKEN && PHT[PHT_idx1][PHT_idx2] != STRONGLY_TAKEN){
      PHT[PHT_idx1][PHT_idx2]++;    
   }else if(resolveDir == NOT_TAKEN && PHT[PHT_idx1][PHT_idx2] != STRONGLY_NOT_TAKEN){
      PHT[PHT_idx1][PHT_idx2]--;
   }
}

/////////////////////////////////////////////////////////////
// openend
/////////////////////////////////////////////////////////////
UINT32 PAp_BHT_open[512]; // 11 bits for each entry
UINT32 PAp_PHT_open[8][2048]; // 4 bit counter
UINT32 GAp_BHT_open; // 9 bits
UINT32 GAp_PHT_open[8][512]; // 4 bit counter
UINT32 Selector[512]; // 4 bit counter

void InitPredictor_openend() {
   for(int i = 0; i < 512; i++){
      PAp_BHT_open[i] = 0;
   }
   for(int i = 0; i < 8; i++){
      for(int j = 0; j < 2048; j++){
         PAp_PHT_open[i][j] = 7;
      }
   }
   GAp_BHT_open = 0;
   for(int i = 0; i < 8; i++){
      for(int j = 0; j < 512; j++){
         GAp_PHT_open[i][j] = 7;
      }
   }
   
   for(int i = 0; i < 512; i++){
      Selector[i] = 8;
   }
}

bool GetPrediction_PAp(UINT32 PC) {
   UINT32 BHT_idx = (PC >> 3) % 512;
   UINT32 PHT_idx0 = PC % 3;
   UINT32 PHT_idx1 = PAp_BHT_open[BHT_idx];
   UINT32 pred = PAp_PHT_open[PHT_idx0][PHT_idx1];
   if(pred > 7) return TAKEN;
   else return NOT_TAKEN;
}

bool GetPrediction_GAp(UINT32 PC) {
   UINT32 idx0 = (GAp_BHT_open ^ (PC>>16)) % 8;
   UINT32 idx1 = ((GAp_BHT_open ^ (PC >> 16)) >> 3) % 512;
   UINT32 pred = GAp_PHT_open[idx0][idx1];
	if (pred > 7)
		return TAKEN;
	else
		return NOT_TAKEN;
}

bool GetPrediction_openend(UINT32 PC) {
   bool PAp_pred = GetPrediction_PAp(PC);
   bool GAp_pred = GetPrediction_GAp(PC);
   UINT32 bias = Selector[PC % 512];
   if(bias > 7){
      return PAp_pred;
   }else{
      return GAp_pred;
   }
}

void UpdatePredictor_openend(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {
   // update selector
   UINT32 selector = Selector[PC % 512];
   if(resolveDir == GetPrediction_PAp(PC) && resolveDir != GetPrediction_GAp(PC)){
      if(selector < 15) Selector[PC % 512]++;
   }
   if(resolveDir == GetPrediction_GAp(PC) && resolveDir != GetPrediction_PAp(PC)){
      if(selector > 0) Selector[PC % 512]--;
   }
   
   // update PAp
   UINT32 BHT_idx = (PC >> 3) % 512;
   UINT32 PHT_idx0 = PC % 3;
   UINT32 PHT_idx1 = PAp_BHT_open[BHT_idx];
   UINT32 pred = PAp_PHT_open[PHT_idx0][PHT_idx1];
   if(resolveDir == TAKEN && pred < 15)
      PAp_PHT_open[PHT_idx0][PHT_idx1]++;
   if(resolveDir == NOT_TAKEN && pred > 0)
      PAp_PHT_open[PHT_idx0][PHT_idx1]--;
   PAp_BHT_open[BHT_idx] = ((PAp_BHT_open[BHT_idx] << 1)|resolveDir) % 2048;

   // update GAp
   UINT32 idx0 = (GAp_BHT_open ^ (PC>>16)) % 8;
   UINT32 idx1 = ((GAp_BHT_open ^ (PC >> 16)) >> 3) % 512;
   if(resolveDir == TAKEN && GAp_PHT_open[idx0][idx1] < 15)
      GAp_PHT_open[idx0][idx1]++;
   if(resolveDir == NOT_TAKEN && GAp_PHT_open[idx0][idx1] > 0)
      GAp_PHT_open[idx0][idx1]--;
   
   GAp_BHT_open = ((GAp_BHT_open << 1)|resolveDir) % 4096;
}




