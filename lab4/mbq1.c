#define STRIDE 128 
#define ARR_SIZE 1000000

int main(){
   char array[ARR_SIZE];
   char a;
   int i;
   
   for(i = 0; i < ARR_SIZE; i += STRIDE) 
   	a = array[i];
}

/*There are 6 memory accesses in total for each iteration of the for loop: 
   4 accesses to get the value of i; 1 access to get a;
   1 access to the the value of array[i]

   since the cache configuration I use has a block size of 64, 
   It will always fetch the next block of size 64 upon a memory access

   By having a stride of 128, it makes sure that all array accesses are 
   2 blocks apart, meaning there is a miss for each array access.
   
   So the overall miss rate will be 1/6=16.7%. This is the same as the design with no prefetcher used.
   However, If you change the STRIDE to be 64, the miss rate is close to zero, which is better than no prefecthing.
*/
