#include <stdbool.h>

int main(){
   int a = 0;
   for(int i = 0; i < 1000000; i++){
      for(int j = 0; j < 7; j++){
         a++;
      }

      for(int j = 0; j < 3; j++){
         a++;
      }
   }
}


/*
Here are the assembly instructions executed in the loop:
.L7:
	movl	$0, -12(%rbp)
	jmp	.L3
.L4:
	addl	$1, -4(%rbp)
	addl	$1, -12(%rbp)
.L3:
	cmpl	$6, -12(%rbp)
	jle	.L4               // 8 br instr, pattern: TTTTTTTN
	movl	$0, -16(%rbp)
	jmp	.L5
.L6:
	addl	$1, -4(%rbp)
	addl	$1, -16(%rbp)
.L5:
	cmpl	$2, -16(%rbp)
	jle	.L6               // 4 br instr, pattern: TTTN
	addl	$1, -8(%rbp)
.L2:
	cmpl	$999999, -8(%rbp)
	jle	.L7               // 1 br instr, pattern: T


-total number of branch instructions:
since the number of iteration is 1000000 and there are 8+4+1=13 conditional branch instructions
the number of conditional branch instructions should be roughly 9000000.
The reported result is 13021048 which is close.

-total number of mispredictions:
The entire outcome sequence is TTTTTTTTNTTTN*
for every loop, there should be 1 misprediction since the pattern TTTTTT has two potential outcomes.
Assume that the entry is initialized to WEAKLY_NOT_TAKEN
The first time when the pattern is encountered the outcome is T, so the predictor state becomes WEAKLY_TAKEN 
The second time when the pattern is encountered the outcome is T, so the predictor state becomes STRONGLY_TAKEN
The third time when the pattern is encountered the outcome is N, so the predictor state becomes WEAKLY_TAKEN
...
Once it stablizes, the prediction and outcomes for the TTTTTT entry of the histroy table for 1 iteration are:
prediction: TTT
outcomes:   TTN
Therefore there is 1 misprediction for each loop.
so the number of misprediction should be around 1000000, the reported one is 1001848, which is close.

-MPKI:
the total number of instruction should be roughly 1000000 * number of instructions in the loop.
The number of instructions in the loop = 7 + 4*7(for 7 taken inner branch ) + 2*1(for 1 not taken inner branch) + 4*3 + 2*1 = 51
the reported number of instructions is 51111361 which is close to expected one.
MPKI should be (1/51)*1000 (1 misprediction per loop) = 19.608 which is close to the report one (19.601).

*/
