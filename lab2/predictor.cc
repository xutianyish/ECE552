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
// Table configuration parameters
#define NSTEP 4
#define NDIFF 3
#define NALLOC 5
#define NHIST 20
#define NSTAT 3
#define MSTAT 3
#define TSTAT (NSTAT+MSTAT+1)
#define CBIT 2
#define UBIT 3
// History length settings
#define MAXHIST 880
// Tage parameters
#define HYSTSHIFT 2
#define LOGB 13
#define LOGG  9
// Statistic corrector parameters
#define LOGC 11
#define CBANK 3
#define CSTAT 6
// Maximum history width
#define PHISTWIDTH 8
// LHT parameters
#define LHTBITS 6
#define LHTSIZE (1<<LHTBITS)
#define LHISTWIDTH 14
// Misc counter width
#define UC_WIDTH 5
#define UT_WIDTH 6
#define UA_WIDTH 4
#define TK_WIDTH 8

// Counter base class
template<int MAX, int MIN>
class Counter {
private:
  int32_t ctr;
public:
   int32_t read(int32_t val=0) { return ctr+val; }
   bool pred() { return ctr >= 0; }
   bool satmax(){ return ctr == MAX; }
   bool satmin(){ return ctr == MIN; }
   void setmax(){ ctr = MAX; }
   void setmin(){ ctr = MIN; }
   void write(int32_t v) { ctr = v; }
   void add(int32_t d) {
      ctr = ctr + d;
      if (ctr > MAX) ctr = MAX;
      else if (ctr < MIN) ctr = MIN;
   }
   void update(bool incr) {
      if (incr) {
         if (ctr < MAX) ctr = ctr + 1;
      } else {
         if (ctr > MIN) ctr = ctr - 1;
      }
   }
};

//signed integer counter
template<int WIDTH>
class SCounter : public Counter<((1<<(WIDTH-1))-1),(-(1<<(WIDTH-1)))>{
};

//unsigned integer counter
template<int WIDTH>
class UCounter : public Counter<((1<<(WIDTH))-1),0>{
public:
};

//////////////////////////////////////////////////////
// history managemet data structure
//////////////////////////////////////////////////////
class GlobalHistoryBuffer {
private:
  bool bhr[MAXHIST];

public:
  void init() { for(int i=0; i<MAXHIST; i++) { bhr[i] = false; } }
  void push(bool taken) {
    for(int i=MAXHIST-2; i>=0; i--) { bhr[i+1] = bhr[i]; }
    bhr[0] = taken;
  }
  bool read(int n) { return bhr[n]; }
};

class GlobalHistory : public GlobalHistoryBuffer {
private:
  class FoldedHistory {
  public:
    unsigned comp;
    int CLENGTH;
    int OLENGTH;
    int OUTPOINT;
    
    void init (int original_length, int compressed_length) {
      comp = 0;
      OLENGTH = original_length;
      CLENGTH = compressed_length;
      OUTPOINT = OLENGTH % CLENGTH;
    }
    
    void update (GlobalHistoryBuffer *h) {
      comp = (comp << 1) | h->read(0);
      comp ^= (h->read(OLENGTH) ? 1 : 0) << OUTPOINT;
      comp ^= (comp >> CLENGTH);
      comp &= (1 << CLENGTH) - 1;
    }
  };
  FoldedHistory ch_i[NHIST];
  FoldedHistory ch_c[NSTAT];
  FoldedHistory ch_t[3][NHIST];
  
public:
  void updateFoldedHistory() {
    for (int i=0; i<NSTAT; i++) {
      ch_c[i].update(this);
    }
    for (int i = 0; i < NHIST; i++) {
      ch_i[i].update(this);
      ch_t[0][i].update(this);
      ch_t[1][i].update(this);
      ch_t[2][i].update(this);
    }
  }
  void setup(int *m, int *l, int *t, int *c, int size) {
    for (int i = 0; i < NHIST; i++) {
      ch_i[i].init(m[i], l[i]);
      ch_t[0][i].init(m[i], t[i]);
      ch_t[1][i].init(m[i], t[i] - 1);
      ch_t[2][i].init(m[i], t[i] - 2);
    }
    for (int i=0; i<NSTAT; i++) {
      ch_c[i].init(c[i], size);
    }
  }
  uint32_t gidx(int n, int length, int clength) { return ch_i[n].comp;}
  uint32_t gtag(int n, int length, int clength) { return ch_t[0][n].comp^(ch_t[1][n].comp<<1)^(ch_t[2][n].comp<<2); }
  uint32_t cgidx(int n, int length, int clength) { return ch_c[n].comp; } 
  void update(bool taken) {
    push(taken);
    updateFoldedHistory();
  }
};


class LocalHistory {
  uint32_t lht[LHTSIZE];
  uint32_t getIndex(uint32_t pc) {
    pc = pc ^ (pc >> LHTBITS) ^ (pc >> (2*LHTBITS));
    return pc & (LHTSIZE-1);
  }
public:
  void init() {
    for(int i=0; i<LHTSIZE; i++) {
      lht[i] = 0;
    }
  }
  
  void update(uint32_t pc, bool taken) {
    lht[getIndex(pc)] <<= 1;
    lht[getIndex(pc)] |= taken ? 1 : 0;
    lht[getIndex(pc)] &= (1<<LHISTWIDTH) - 1;
  }
  
  uint32_t read(uint32_t pc, int length, int clength) {
    uint32_t h = lht[getIndex(pc)];
    h &= (1 << length) - 1;
    
    uint32_t v = 0;
    while(length > 0) {
      v ^= h;
      h >>= clength;
      length -= clength;
    }
    return v & ((1 << clength) - 1);
  }
};

//////////////////////////////////////////////////////////
// Base predictor for TAGE predictor
// This predictor is derived from CBP3 ISL-TAGE
//////////////////////////////////////////////////////////
template <int BITS, int HSFT>
class Bimodal {
private:
  bool pred[1 << BITS];
  bool hyst[1 << BITS];
  uint32_t getIndex(uint32_t pc) {
    return pc & ((1 << BITS)-1);
  }
  
public:
   void init() {
      for(int i=0; i<(1<<BITS); i++) { pred[i] = 0; }
      for(int i=0; i<(1<<BITS); i++) { hyst[i] = 1; }
   }
  
   bool predict(uint32_t pc) {
      return pred[getIndex(pc)];
   }
  
   void update(uint32_t pc, bool taken) {
      int inter = (pred[getIndex(pc)] << 1) + hyst[getIndex(pc)>>HSFT];
      if(taken) {
         if (inter < 3) { inter++; }
      } else {
         if (inter > 0) { inter--; }
      }
      pred[getIndex(pc)] = inter >> 1;
      hyst[getIndex(pc)>>HSFT] = inter & 1;
  }
};

//////////////////////////////////////////////////////////
// Global component for TAGE predictor
// This predictor is derived from CBP3 ISL-TAGE
//////////////////////////////////////////////////////////
class GEntry {
public:
  uint32_t tag;
  SCounter<CBIT> c;
  UCounter<UBIT> u;
  
  GEntry () {
    tag = 0;
    c.write(0);
    u.write(0);
  }
  
  void init(uint32_t t, bool taken, int uval=0) {
    tag = t;
    c.write(taken ? 0 : -1);
    u.write(uval);
  }
  
  bool newalloc() {
    return (abs(2*c.read() + 1) == 1);
  }
};

//////////////////////////////////////////////////////////
// Put it all together.
// The predictor main component class
//////////////////////////////////////////////////////////
// Configuration of table sharing strategy
static const int STEP[NSTEP+1] = {0, NDIFF, NHIST/2, NHIST};
class my_predictor {
  // Tag width and index width of TAGE predictor
  int TB[NHIST] = {8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10};
  int logg[NHIST] = {10, 7, 7, 11, 8, 8, 8, 8, 8, 8, 10, 7, 7, 7, 7, 7, 7, 7, 7, 7};
  
  // History length for TAGE predictor
  int m[NHIST] = {5, 7, 9, 11, 15, 19, 26, 34, 44, 58, 76, 100, 131, 172, 226, 296, 389, 511, 670, 880};
  int l[NHIST] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 4, 6, 8, 11, 14};
  int p[NHIST] = {5, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
  
  // History length for statistical corrector predictors
  int cg[NSTAT] = {2, 5, 13};
  int cp[NSTAT] = {2, 5, 8};
  int cl[MSTAT] = {2, 5, 12};

  // Index variables of TAGE and some other predictors
  uint32_t CI[TSTAT];
  uint32_t GI[NHIST];
  uint32_t GTAG[NHIST];
  
  // Intermediate prediction result for TAGE
  bool HitPred, AltPred, TagePred;
  int HitBank, AltBank, TageBank;
  
  // Intermediate prediction result for statistical corrector predictor
  bool SCPred;
  int SCSum;
  
  // Intermediate prediction result for loop predictor
  bool loopPred;
  bool loopValid;

  // Prediction Tables
  Bimodal<LOGB,HYSTSHIFT> btable; // bimodal table
  GEntry *gtable[NHIST]; // global components
  SCounter<CSTAT> *ctable[2]; // statistical corrector predictor table
  
  // Branch Histories
  GlobalHistory ghist; // global history register
  LocalHistory lhist; // local history table
  uint32_t phist; // path history register

  // Profiling Counters
  UCounter<UC_WIDTH> UC; // statistical corrector predictor tracking counter
  SCounter<UT_WIDTH> UT; // statistical corrector predictor threshold counter
  UCounter<TK_WIDTH> TICK; // tick counter for reseting u bit of global entryies
  SCounter<UA_WIDTH> UA[NSTEP+1][NSTEP+1]; // newly allocated entry counter
private:
public:
my_predictor (void) {
  // Setup misc registers
  UC.write(0);
  UT.write(0);
  TICK.write(0);

  for(int i=0; i<NSTEP+1; i++) {
    for(int j=0; j<NSTEP+1; j++) {
      UA[i][j].write(0);
    }
  }

  // Setup global components
  for(int i=0; i<NSTEP; i++) {
    gtable[STEP[i]] = new GEntry[1 << logg[STEP[i]]];
  }
  for(int i=0; i<NSTEP; i++) {
    for (int j=STEP[i]+1; j<STEP[i+1]; j++) {
      gtable[j] = gtable[STEP[i]];
    }
  }
  btable.init();
  
  // Setup statistic corrector predictor
  ctable[0] = new SCounter<CSTAT>[1 << LOGC];
  ctable[1] = new SCounter<CSTAT>[1 << LOGC];
  for(int i=0; i<(1<<LOGC); i++) {
    ctable[0][i].write(-(i&1));
    ctable[1][i].write(-(i&1));
  }
  // Setup history register & table
  phist = 0;
  ghist.init();
  lhist.init();
  ghist.setup(m, logg, TB, cg, LOGC-CBANK);
}


//////////////////////////////////////////////////////////////
// Hash functions for TAGE and static corrector predictor
//////////////////////////////////////////////////////////////

int F (int A, int size, int bank, int width) {
  int A1, A2;
  int rot = (bank+1) % width;
  A = A & ((1 << size) - 1);
  A1 = (A & ((1 << width) - 1));
  A2 = (A >> width);
  A2 = ((A2 << rot) & ((1 << width) - 1)) + (A2 >> (width - rot));
  A = A1 ^ A2;
  A = ((A << rot) & ((1 << width) - 1)) + (A >> (width - rot));
  return (A);
}

// gindex computes a full hash of pc, ghist and phist
uint32_t gindex(uint32_t pc, int bank, int hist) {
  // we combine local branch history for the TAGE index computation
  uint32_t index =
    lhist.read(pc, l[bank], logg[bank]) ^
    ghist.gidx(bank, m[bank], logg[bank]) ^
    F(hist, p[bank], bank, logg[bank]) ^
    (pc >> (abs (logg[bank] - bank) + 1)) ^ pc ;
  return index & ((1 << logg[bank]) - 1);
}

//  tag computation for TAGE predictor
uint32_t gtag(uint32_t pc, int bank) {
  uint32_t tag = ghist.gtag(bank, m[bank], TB[bank]) ^ pc ;
  return (tag & ((1 << TB[bank]) - 1));
}

// index computation for statistical corrector predictor
uint32_t cgindex (uint32_t pc, int bank, int hist, int size) {
  uint32_t index =
    ghist.cgidx(bank, cg[bank], size) ^
    F(hist, cp[bank], bank, size) ^
    (pc >> (abs (size - (bank+1)) + 1)) ^ pc ;
  return index & ((1 << size) - 1);
}

// index computation for statistical corrector predictor
uint32_t clindex (uint32_t pc, int bank, int size) {
  uint32_t index =
    lhist.read(pc, cl[bank], size) ^
    (pc >> (abs (size - (bank+1)) + 1)) ^ pc ;
  return index & ((1 << size) - 1);
}

int uaindex(int bank) {
  for(int i=0; i<NSTEP; i++) {
    if(bank < STEP[i]) return i;
  }
  return NSTEP;
}
  
bool predict(uint32_t pc) {
  bool pred_taken = true;
  
    // Compute index values
    for (int i = 0; i < NHIST; i++) {
      GI[i] = gindex(pc, i, phist);
      GTAG[i] = gtag(pc, i);
    }
    
    // Update the index values for interleaving
    for (int s=0; s<NSTEP; s++) {
      for (int i=STEP[s]+1; i<STEP[s+1]; i++) {
        GI[i]=((GI[STEP[s]]&7)^(i-STEP[s]))+(GI[i]<<3);
      }
    }
    
    // Compute the prediction result of TAGE predictor
    HitBank = AltBank = -1;
    HitPred = AltPred = btable.predict(pc);
    for (int i=0; i<NHIST; i++) {
      if (gtable[i][GI[i]].tag == GTAG[i]) {
        AltBank = HitBank;
        HitBank = i;
        AltPred = HitPred;
        HitPred = gtable[i][GI[i]].c.pred();
      }        
    }
    
    // Select the highest confident prediction result
    TageBank = HitBank;
    TagePred = HitPred;
    if (HitBank >= 0) {
      int u = UA[uaindex(HitBank)][uaindex(AltBank)].read();
      if((u>=0)&&gtable[HitBank][GI[HitBank]].newalloc()) {
        TagePred = AltPred;
        TageBank = AltBank;
      }
    }
    pred_taken = TagePred;
    
    // Compute the index values of the static corrector predictor
    CI[0] = pc & ((1<<LOGC)-1);
    for (int i=0; i<NSTAT; i++) {
      CI[i+1] = cgindex(pc, i, phist, LOGC-CBANK);
    }
    for (int i=0; i<MSTAT; i++) {
      CI[i+NSTAT+1] = clindex(pc, i, LOGC-CBANK);
    }
    for (int i=0; i<TSTAT; i++) {
      if (i == (NSTAT)) { CI[i] ^= (TageBank+1) ; }
      CI[i] <<= CBANK;
      CI[i] ^= (pc ^ i) & ((1 << CBANK)-1);
      CI[i] &= (1<<LOGC) - 1;
    }
    
    // Overwrite TAGE prediction result if the confidence of
    // the static corrector predictor is higher than the threshold.
    if (HitBank >= 0) {
      SCSum = 6 * (2 * gtable[HitBank][GI[HitBank]].c.read() + 1);
      for (int i=0; i < TSTAT; i++) {
        SCSum += (2 * ctable[TagePred][CI[i]].read()) + 1;
      }
      SCPred = (SCSum >= 0);
      if (abs (SCSum) >= UC.read(5)) {
        pred_taken = SCPred;
      }
    }
  return pred_taken;
}

void update(uint32_t pc, bool taken, uint32_t target) {
    if (HitBank >= 0) {
      // Updates the threshold of the static corrector predictor
      if (TagePred != SCPred) {
        if ( (abs (SCSum) >= UC.read(1)) &&
             (abs (SCSum) <= UC.read(3)) ) {
          UT.update(SCPred == taken);
          if (UT.satmax() && !UC.satmin()) {
            UT.write(0);
            UC.add(-2);
          }
          if (UT.satmin() && !UC.satmax()) {
            UT.write(0);
            UC.add(+2);
          }
        }
      }
      
      // Updates the static corrector predictor tables
      if ((SCPred != taken) || (abs (SCSum) < (0 + 6 * UC.read(5)))) {
        for (int i=0; i<TSTAT; i++) {
          ctable[TagePred][CI[i]].update(taken);
        }
      }
    }
    
    // Determining the allocation of new entries
    bool ALLOC = (TagePred != taken) && (HitBank < (NHIST-1));
    if (HitBank >= 0) {
      if (gtable[HitBank][GI[HitBank]].newalloc()) {
        if (HitPred == taken) {
          ALLOC = false;
        }
        if (HitPred != AltPred) {
          UA[uaindex(HitBank)][uaindex(AltBank)].update(AltPred == taken);
        }
      }
    }
    
    if (ALLOC) {
      // Allocate new entries up to "NALLOC" entries are allocated
      int T = 0;
      for (int i=HitBank+1; i<NHIST; i+=1) {
        if (gtable[i][GI[i]].u.read() == 0) {
          gtable[i][GI[i]].init(GTAG[i], taken, 0);
          TICK.add(-1);
          if (T == NALLOC) break;
          T += 1;
          i += 1 + T/2; // After T th allocation, we skip 1+(T/2) tables.
        } else {
          TICK.add(+1);
        }
      }
      
      // Reset useful bit to release OLD useful entries
      bool resetUbit = TICK.satmax();
      if (resetUbit) {
        TICK.write(0);
        for (int s=0; s<NSTEP; s++) {
          for (int j=0; j<(1<<logg[STEP[s]]); j++)
            gtable[STEP[s]][j].u.add(-1);
        }
      }
    }
    
    // Update prediction tables
    // This part is same with ISL-TAGE branch predictor.
    if (HitBank >= 0) {
      gtable[HitBank][GI[HitBank]].c.update(taken);
      if ((gtable[HitBank][GI[HitBank]].u.read() == 0)) {
        if (AltBank >= 0) {
          gtable[AltBank][GI[AltBank]].c.update(taken);
        } else {
          btable.update(pc, taken);
        }
      }
    } else {
      btable.update(pc, taken);
    }
    
    // Update useful bit counter
    // This useful counter updating strategy is derived from
    // Re-reference interval prediction.
    if (HitBank >= 0) {
      bool useful = (HitPred == taken) && (AltPred != taken) ;
      if(useful) {
        gtable[HitBank][GI[HitBank]].u.setmax();
      }
    }

  //////////////////////////////////////////////////
  // Branch history management.
  //////////////////////////////////////////////////
  // Update global history and path history
  ghist.update((((target ^ (target >> 3) ^ pc) << 1) + taken) & 1);
  phist <<= 1;
  phist += pc & 1;
  phist &= (1 << PHISTWIDTH) - 1;
  lhist.update(pc, taken);
}
};

my_predictor* tage;
void InitPredictor_openend(){
   tage = new my_predictor();
}
bool GetPrediction_openend(UINT32 PC){
   return tage->predict(PC);
}  
void UpdatePredictor_openend(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget){
   tage->update(PC, resolveDir, branchTarget);   
}
