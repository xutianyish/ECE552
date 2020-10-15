Lab2 Report  
Tianyi Xu 1003130809, Hao Wang 1001303500
Microbenchmark:
Here is the microbenchmark used to verify the 2-level branch predictor:
   int a = 0;
   for(int i = 0; i < 1000000; i++){
      for(int j = 0; j < 7; j++){ // 8 branches with pattern: TTTTTTTN
         a++;
      }
      for(int j = 0; j < 3; j++){ // 4 branches with pattern: TTTN
         a++;
      }
   }
The branch outcome pattern for one loop is T TTTTTTTN TTTN*. The history table has 6 bits per entry. Considering all the 6-bit patterns, there is only one pattern (TTTTTT) that yields two possible outcomes. So, there should be 1 misprediction for each loop. 
The number of mispredictions should be 1 * 1000000 = 1000000 since there are 1000000 iterations. The MPKI should be 1/51 * 1000 = 19.608 since there are 51 instructions per loop. The reported results are NUM_MISPREDICTION = 1001848, MPKI = 19.601, which are close to what we predict. To see the assembly code, refer to mb.c and -O0 compilation flag is used.   

Branch predictor performance:
Benchmark	2-bit saturating	2-level	open-ended
	Mispredictions	MPKI	Mispredictions	MPKI	Mispredictions	MPKI
astar	3695830	24.639	1785464	11.903	704915	2.720
bwaves	1182969	7.886	1071909	7.146	830011	0.948
bzip2	1224967	8.166	1297677	8.651	1115939	6.910
gcc	3161868	21.079	2223671	14.824	9226675	0.668
gromacs	1363248	9.088	1122586	7.484	742368	4.507
hmmer	2035080	13.567	2230774	14.872	1750537	11.273
mcf	3657986	24.387	2024172	13.494	1541013	8.757
soplex	1065988	7.107	1022869	6.819	789766	3.720

open-ended branch predictor implementation: 
The open-ended branch predictor is a TAGE predictor consisting of a base predictor, a statistic predictor and 3 tagged predictors indexed with increasing history lengths. The base predictor is a 2-bit saturating counter. The statistic predictor is used to predict branches that are not correlated with the previous branch history pattern. Each tagged predictor has an unsigned counter, signed counter and a tag in each entry. The unsigned counter indicates the confidence score, the signed counter indicates the prediction, and the tag is used to check for match. There is a TICK counter indicating whether the unsigned counter inside the predictor needs to be updated. The second longest match prediction is used when UA counter show the second longest match prediction is more accurate than that of the longest match prediction when the longest match entry shows weakly biased.
There are three history registers, a global history register, a local history register, and a path history register. There are passed through a hash function and the value is used to get the predictor tag. The history registers are passed through another hash function to get the tag of the history and it is compared with the predictor tag to determine whether the predictor result should be taken. If there are multiple matches, the prediction from the larger predictor is taken. 
Counters = TICK + UA + UC + UT + statistic corrector counters = 8 + 4 + 6 + 16*4 + 2 * 6 * (2^9) = 6226 bits; History registers: 880 (global) + 2^6*14 (local) + 8 (path) = 1784 bits; Bimodal (base predictor): 2^13 * 2 = 16384 bits; The tagged predictors: 2^10*(2+3+8) + 2^11*(2+3+9) + 2^10*(2+3+10) = 57344 bits; Total: 81738 bits = 80Kbits

Area, access latency, and leakage power:
Two-level:
	Configuration	Area (mm^2)	Access latency (ns)	Leakage power (mW)
BHT
2level-bpred-1.cfg	Type: RAM
Size: 512
Block size: 1			
PHT
2level-bpred-2.cfg	Type: RAM
Size: 128
Block size: 2			
Open-ended:
	Configuration	Area (mm^2)	Access latency (ns)	Leakage power (mW)
Bimodal
open-ended-bpred-1.cfg	Type: RAM
Size: 8192
Block size: 1			
Tagged predictor1
open-ended-bpred-2.cfg	Type: Cache 
Size: 2048
Block size: 1
Tag size: 8			
Tagged predictor2
open-ended-bpred-3.cfg	Type: Cache
Size: 4352
Block size: 1
Tag size: 9			
Tagged predictor3
open-ended-bpred-4.cfg	Type: Cache
Size: 2304
Block size: 1
Tag size: 10			
Statistic corrector
Open-ended-bpred-5.cfg	Type: RAM
Size: 512
Block size: 2			

Work Distribution:
Tianyi Xu: implemented part 1, part2, part3 parameter tunning, wrote report, CACTI configuration
Wang Hao: implemented part 3
