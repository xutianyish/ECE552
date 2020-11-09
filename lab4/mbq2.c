#define STRIDE    128 
#define STEP      2 
#define ITER      1000000
#define ARR_SIZE  STRIDE * STEP * ITER

int main(){
   char array[ARR_SIZE];
   char a;
   int i;
   int arr_idx = 0;
   
   for(i = 0; i < ITER; i ++){ // 3 accesses to i
      if(i % 2 == 0){ // 1 access to i
         arr_idx += STRIDE; // 2 accesses to arr_idx
      }else{
         arr_idx += STRIDE * STEP; // same as above
      }
      a = array[arr_idx]; // 1 access to a, 1 access to arr_idx, 1 access to array[arr_idx] 
   } 
}

/*There are 9 memory accesses on average for each iteration of the for loop: 
   4 accesses to get the value of i; 3 accessed to arr_idx;
   1 access to a; 1 access to array[arr_idx]

   The cache configuration has a block size of 64, a set size of 64, and a rpt size of 16 
   if STEP is not 1, the arr_idx is incremented differently every iteration
   The RPT will not be able to predict the correct block to fetch. 
   And the miss rate will be close to 1/9 (1 access to array element in every loop)

   However, if the step size is 1, the arr_idx is incremented by STRIDE for every iteration,
   therefore, the stride prefetcher will be able to predict the correct block to fetch.
   Thus, giving a miss rate that is close to zero.

   Note: for next-line prefetcher, even if the STEP is 1. It will not be able to prefetch the correct block since the array elements accessed are 2 blocks apart.
*/
