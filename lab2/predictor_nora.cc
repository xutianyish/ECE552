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
// URL: http://www.jilp.org/jwac-2/
// URL: http://www.jilp.org/vol8/v8paper1.pdf
// Table configuration parameters
#define NSTEP 3
#define NDIFF 3
#define NHIST 12
#define CTRWIDTH 3
#define UWIDTH 2

// History length settings
#define MAXHIST 342
// Tage parameters
#define HISTORYSHIFT 2
#define BIMODALWIDTH 11
#define PHISTWIDTH 8

// LHT parameters
#define LHTBITS 4
#define LHTSIZE (1<<LHTBITS)
#define LHTMASK (LHTSIZE-1)
#define LHTWIDTH 5
#define LHTWIDTHMASK (1<<LHTWIDTH)-1

// Misc counter width
#define UA_WIDTH 4
#define TK_WIDTH 8

//////////////////////////////////////////////////////
// Base counter class
//////////////////////////////////////////////////////
template<int WIDTH, bool SIGNED>
class Counter{
private:
   int32_t ctr;
   int max;
   int min;
public:
   Counter(){
      ctr = 0;
      if(SIGNED){
         min = -(1<<(WIDTH-1));
         max = (1<<(WIDTH-1))-1;
      } else {
         min = 0;
         max = (1<<(WIDTH))-1;
      }
   }
   int32_t get() { return ctr; }
   bool pred() { return ctr > 0; }
   bool ismax() { return ctr == max; }
   void setmax() { ctr = max; }
   void set(int32_t val){ ctr = val;}
   void update(bool incr){
      if(incr && ctr < max) ctr++;
      if(!incr && ctr > min) ctr--;
   } 
   
   int budget(){ return WIDTH; }
};

//////////////////////////////////////////////////////
// history managemet data structure
//////////////////////////////////////////////////////
class GlobalHistoryBuffer {
private:
  bool ghr[MAXHIST];

public:
   int init() {
      for(int i=0; i<MAXHIST; i++) {
         ghr[i] = NOT_TAKEN; 
      }
      return MAXHIST;
   }

   void push(bool pred) {
      for(int i = MAXHIST-2; i >= 0; i--) ghr[i+1] = ghr[i];
      ghr[0] = pred;
   }
  
  bool get(int n) {
    return ghr[n];
  }
};


class GlobalHistory : public GlobalHistoryBuffer {
private:
  // Folded index (this register save the hash value of the global history,
  // this values can be regenerated if the all branch histories are stored in the GHR)
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
      comp = (comp << 1) | h->get(0);
      comp ^= (h->get(OLENGTH) ? 1 : 0) << OUTPOINT;
      comp ^= (comp >> CLENGTH);
      comp &= (1 << CLENGTH) - 1;
    }
  };
  FoldedHistory ch_i[NHIST];
  FoldedHistory ch_t[3][NHIST];
  
public:
  void updateFoldedHistory() {
    for (int i = 0; i < NHIST; i++) {
      ch_i[i].update(this);
      ch_t[0][i].update(this);
      ch_t[1][i].update(this);
      ch_t[2][i].update(this);
    }
  }
  
  void setup(int *m, int *l, int *t) {
    for (int i = 0; i < NHIST; i++) {
      ch_i[i].init(m[i], l[i]);
      ch_t[0][i].init(m[i], t[i]);
      ch_t[1][i].init(m[i], t[i] - 1);
      ch_t[2][i].init(m[i], t[i] - 2);
    }
  }
  
  uint32_t gidx(int n, int length, int clength) {
    return ch_i[n].comp;
  }
  
  uint32_t gtag(int n, int length, int clength) {
    return ch_t[0][n].comp^(ch_t[1][n].comp<<1)^(ch_t[2][n].comp<<2);
  }
  
  
public:
  void update(bool resolveDir) {
    push(resolveDir);
    updateFoldedHistory();
  }
};


class LHT {
  uint32_t lht[LHTSIZE];
  uint32_t getIndex(uint32_t pc) {
    pc = pc ^ (pc >> LHTBITS) ^ (pc >> (2*LHTBITS));
    return pc & (LHTMASK);
  }
  
public:
  int init() {
    for(int i=0; i<LHTSIZE; i++) {
      lht[i] = 0;
    }
    return LHTWIDTH * LHTSIZE;
  }
  
  void update(uint32_t pc, bool resolveDir) {
    lht[getIndex(pc)] <<= 1;
    lht[getIndex(pc)] |= resolveDir ? 1 : 0;
    lht[getIndex(pc)] &= LHTWIDTHMASK;
  }
  
  uint32_t get(uint32_t pc, int length, int clength) {
    uint32_t h = lht[getIndex(pc)];
    h &= (1 << length) - 1;
    uint32_t v = 0;
    if(length > 0) {
      v ^= h;
      h >>= clength;
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
    return pc & ((1 << BITS)-1) ;
  }
  
public:
  int init() {
    for(int i=0; i<(1<<BITS); i++) { pred[i] = 0; }
    for(int i=0; i<(1<<BITS); i++) { hyst[i] = 1; }
    return 2*(1<<BITS);
  }
  bool predict(uint32_t pc) {
    return pred[getIndex(pc)];
  }
  void update(uint32_t pc, bool resolveDir) {
    int inter = (pred[getIndex(pc)] << 1) + hyst[getIndex(pc)>>HSFT];
    if(resolveDir) {
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
  Counter<CTRWIDTH, 1> ctr;
  Counter<UWIDTH, 0> u;
  GEntry () {
    tag = 0;
    ctr.set(0);
    u.set(0);
  }
  void alloc(uint32_t _tag, bool resolveDir) {
    tag = _tag;
    ctr.set(resolveDir ? 0 : -1);
    u.set(0);
  }
  bool lowconfidence() {
    return (ctr.get() == 0 || ctr.get()== -1);
  }
};

//////////////////////////////////////////////////////////
// Configuration of table sharing strategy
static const int STEP[NSTEP+1] = {0, NDIFF, NHIST/2, NHIST};
class my_predictor {
  // Tag width and index width of TAGE predictor
  int TB[NHIST] = {8,8,8,9,9,9,10,10,10,10,10,10};
  int logg[NHIST] = {9,6,6,10,7,7,9,6,6,6,6,6};
  // History length for TAGE predictor
  int m[NHIST] = {5,7,11,16,23,34,50,74,108,159,233,342};
  int l[NHIST] = {0,0,0,0,0,0,1,1,2,4,5,5};
  int p[NHIST] = {5,7,8,8,8,8,8,8,8,8,8,8};
  
  // Index variables of TAGE and some other predictors
  uint32_t GI[NHIST];
  uint32_t GTAG[NHIST];
  
  // Intermediate prediction result for TAGE
  bool HitPred, AltPred, TagePred;
  int HitBank, AltBank, TageBank;
  
  /////////////////////////////////////////////////////////
  // Hardware resoruce
  // These variables are counted as the actual hardware
  /////////////////////////////////////////////////////////
  
  // Prediction Tables
  Bimodal<BIMODALWIDTH,HISTORYSHIFT> btable; // bimodal table
  GEntry *gtable[NHIST]; // global components
  
  // Branch Histories
  GlobalHistory ghist; // global history register
  uint32_t phist; // path history register

  // Profiling Counters
  Counter<TK_WIDTH, 0> TICK; // tick counter for reseting u bit of global entryies
  Counter<UA_WIDTH, 1> UA[NSTEP+1][NSTEP+1]; // newly allocated entry counter
  
private:
public:
  my_predictor (void) {
    // Setup misc registers
    TICK.set(0);
    for(int i=0; i<NSTEP+1; i++) {
      for(int j=0; j<NSTEP+1; j++) {
        UA[i][j].set(0);
      }
    }

   gtable[0] = new GEntry[1 << 9]; // the tag width is 8
   gtable[1] = gtable[0];
   gtable[2] = gtable[1];

   gtable[3] = new GEntry[1 << 10]; // the tag width is 9
   gtable[4] = gtable[3];
   gtable[5] = gtable[3];

   gtable[6] = new GEntry[1 << 9]; // the tag width is 10
   gtable[7] = gtable[6];
   gtable[8] = gtable[6];
   gtable[9] = gtable[6];
   gtable[10] = gtable[6];
   gtable[11] = gtable[6];
   
   btable.init();
   phist = 0;
   ghist.init();
   ghist.setup(m, logg, TB);
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

  int uaindex(int bank) {
    for(int i=0; i<NSTEP; i++) {
      if(bank < STEP[i]) return i;
    }
    return NSTEP;
  }
  
  //////////////////////////////////////////////////////////////
  // Actual branch prediction and training algorithm
  //////////////////////////////////////////////////////////////
  //compute the prediction
  bool predict(uint32_t pc) {
    // Final prediction result
    bool pred_resolveDir = true;
    
    // Compute index values
    for (int i = 0; i < NHIST; i++) {
      GI[i] = gindex(pc, i, phist);
      GTAG[i] = gtag(pc, i);
    }
    
    // Compute the pred result of TAGE
    HitBank = AltBank = -1;
    HitPred = AltPred = btable.predict(pc);
    for (int i=NHIST-1; i>=0; i--) {
      if (gtable[i][GI[i]].tag == GTAG[i]) {
        AltBank = HitBank;
        HitBank = i;
        AltPred = HitPred;
        HitPred = gtable[i][GI[i]].ctr.pred();
        break;
      }        
    }
      
      // Select the highest confident prediction result
      TageBank = HitBank;
      TagePred = HitPred;
      if (HitBank >= 0) {
        int u = UA[uaindex(HitBank)][uaindex(AltBank)].get();
        if((u>=0)&&gtable[HitBank][GI[HitBank]].lowconfidence()) {
          TagePred = AltPred;
          TageBank = AltBank;
        }
      }
      pred_resolveDir = TagePred;
    
    return pred_resolveDir;
  }
  
  void update(uint32_t pc, bool resolveDir, uint32_t target) {
      // Determining the allocation of new entries
      bool ALLOC = (TagePred != resolveDir) && (HitBank < (NHIST-1));
      if (HitBank >= 0) {
        if (gtable[HitBank][GI[HitBank]].lowconfidence()) {
          if (HitPred == resolveDir) {
            ALLOC = false;
          }
          if (HitPred != AltPred) {
            UA[uaindex(HitBank)][uaindex(AltBank)].update(AltPred == resolveDir);
          }
        }
      }
      
      if (ALLOC) {
        for (int i=HitBank+1; i<NHIST; i+=1) {
          if (gtable[i][GI[i]].u.get() == 0) {
            gtable[i][GI[i]].alloc(GTAG[i], resolveDir);
            TICK.update(false);
          } else {
            TICK.update(true);
          }
        }
        
        // Reset useful bit to release OLD useful entries
        bool resetUbit = TICK.ismax();
        if (resetUbit) {
          TICK.set(0);
          for (int s=0; s<NSTEP; s++) {
            for (int j=0; j<(1<<logg[STEP[s]]); j++)
              gtable[STEP[s]][j].u.update(false);
          }
        }
      }
      
      // Update prediction tables
      if (HitBank >= 0) {
        gtable[HitBank][GI[HitBank]].ctr.update(resolveDir);
        if ((gtable[HitBank][GI[HitBank]].u.get() == 0)) {
          if (AltBank >= 0) {
            gtable[AltBank][GI[AltBank]].ctr.update(resolveDir);
          } else {
            btable.update(pc, resolveDir);
          }
        }
      } else {
        btable.update(pc, resolveDir);
      }
      
      // Update useful bit counter
      if (HitBank >= 0) {
        bool useful = (HitPred == resolveDir) && (AltPred != resolveDir) ;
        if(useful) {
          gtable[HitBank][GI[HitBank]].u.setmax();
        }
      }

    //////////////////////////////////////////////////
    // Branch history management.
    //////////////////////////////////////////////////
    // Branch history information
    // (This feature is derived from ISL-TAGE)
    ghist.update((((target ^ (target >> 3) ^ pc) << 1) | resolveDir) & 1);
    phist <<= 1;
    phist += (pc) & 1;
  }
};


my_predictor tage;

void InitPredictor_openend() {
}

bool GetPrediction_openend(UINT32 PC) {
   return tage.predict(PC); 
}

void UpdatePredictor_openend(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {
   tage.update(PC, resolveDir, branchTarget); 
}
