void main() {

	int i;
	for(i = 0; i < 1000000 ; ++i ){
      asm(
         ".set noat\n \t"
         "addi $1, $0, 1 \n \t"
         "addi $2, $0, 2 \n \t"
         "addi $3, $0, 3 \n \t"
         "add $3, $1, $2 \n \t"
         "lw  $4, 2($3) \n \t"
         "add $5, $4, $3 \n \t"
         "add $2, $1, $3 \n \t"
         "add $2, $5, $4 \n \t"
         "add $3, $1, $4 \n \t"
      );
	}	
}

/* Question 1
   By observing the code in assembly and the output of objump, 
   Here are the instructions executed in the loop:

   $L2:
   	lw	 $2,16($fp)
   	lui $3, 15
      ori $3, $3, 16959 // 2-cycle-stall
   	slt $2,$3,$2      // 2-cycle-stall
   	beq $2,$0,$L5     // 2-cycle-stall
   $L5:
    	addi $1, $0, 1 
    	addi $2, $0, 2    
    	addi $3, $0, 3    
    	add $3, $1, $2    // 1-cycle-stall
    	lw  $4, 2($3)     // 2-cycle-stall
    	add $5, $4, $3    // 2-cycle-stall
    	add $2, $1, $3     
    	add $2, $5, $4    // 1-cycle-stall
    	add $3, $1, $4 
   $L4:
   	lw	$3,16($fp) 
   	addu	$2,$3,1     // 2-cycle-stall
   	move	$3,$2       // 2-cycle-stall
   	sw	$3,16($fp)     // 2-cycle-stall
   	j	$L2
   
   #2-cycle-stall = 8, #1-cycle-stall=2, #insn= 19
   CPI = 1 + (#2-cycle-stall * 2 / #insn + #1-cycle-stall / #insn) = 1 + (2*8/19 + 2/19) = 1.9474

   the CPI got from sim-safe is 1.9471 which is approximately 1.9474

*/

/* Question 2
   By observing the code in assembly and the output of objump, 
   Here are the instructions executed in the loop:

   $L2:
   	lw	 $2,16($fp)
   	lui $3, 15
      ori $3, $3, 16959 // 1-cycle-stall
   	slt $2,$3,$2      // 1-cycle-stall
   	beq $2,$0,$L5     // 1-cycle-stall
   $L5:
    	addi $1, $0, 1 
    	addi $2, $0, 2    
    	addi $3, $0, 3    
    	add $3, $1, $2    
    	lw  $4, 2($3)     // 1-cycle-stall
    	add $5, $4, $3    // 2-cycle-stall
    	add $2, $1, $3     
    	add $2, $5, $4    
    	add $3, $1, $4 
   $L4:
   	lw	$3,16($fp) 
   	addu	$2,$3,1     // 2-cycle-stall
   	move	$3,$2       // 1-cycle-stall
   	sw	$3,16($fp)     
   	j	$L2
   
   #2-cycle-stall = 2, #1-cycle-stall=5, #insn= 19
   CPI = 1 + (#2-cycle-stall * 2 / #insn + #1-cycle-stall / #insn) = 1 + (2*2/19 + 5/19) = 1.4737

   the CPI got from sim-safe is 1.9471 which is approximately 1.4736

*/

