#define BLKSIZE      128 

int main(){
   char array[10000000];
   char b;
   int i = 0;
   int j = 0;
   int pattern[] = {1,2,3,4,5,6};
  
   while(i < 10000000){
      b = array[i];
      i += pattern[j]*BLKSIZE; 
      j = (j+1)%6;
   }
}


/* The performance of the open-ended prefetcher is demonstrated by 
   comparing it against the stride prefetcher. 
   The stride prefetch is not able to remember varying patterns. It can 
   only remember constant stride. 

   However, with a delta correlation prefetcher, the pattern is recorded 
   in the delta buffer. 

   In this example, we use a fixed pattern for the array index. 
   The stride prefetcher gives a miss rate of 10.18% while the delta correlation 
   prefetcher gives a miss rate of 2.69% which is way better.
*/
