/*

    %rdi
    %rsi
    %rdx
    %rcx
    %r8
    %r9
这里是内部的一些核心实现
*/


.global __coro_internal_magic_fetch_ip
__coro_internal_magic_fetch_ip:
    movq (%rsp),%rax
    ret

.global __coro_resume_callee_saved_registers
__coro_resume_callee_saved_registers:
    movq %rdi,-8(%rsp)
    movq %rsi,-16(%rsp)
    movq %rdx,-24(%rsp)
    movq %rcx,-32(%rsp)
    movq %r8,-40(%rsp)
    
    movq -8(%rsp),%r11
    movq (%r11),%rbx
    
    movq -16(%rsp),%r11
    movq (%r11),%r12
    
    movq -24(%rsp),%r11
    movq (%r11),%r13
    
    movq -32(%rsp),%r11
    movq (%r11),%r14
    
    movq -40(%rsp),%r11
    movq (%r11),%r15
    xor %rax,%rax
    ret
    
.global __coro_save_callee_saved_registers
__coro_save_callee_saved_registers:
    movq %rdi,-8(%rsp)
    movq %rsi,-16(%rsp)
    movq %rdx,-24(%rsp)
    movq %rcx,-32(%rsp)
    movq %r8,-40(%rsp)

    movq -8(%rsp),%r11
    movq %rbx,(%r11)
    
    movq -16(%rsp),%r11
    movq %r12,(%r11)
    
    movq -24(%rsp),%r11
    movq %r13,(%r11)
    
    movq -32(%rsp),%r11
    movq %r14,(%r11)
    
    movq -40(%rsp),%r11
    movq %r15,(%r11)
    xor %rax,%rax
    ret

/*

    %rdi
    %rsi
    %rdx
    %rcx
    %r8
    %r9

rax r10 r11
*/
.global __coro_internal_startup
__coro_internal_startup:
    movq %rdi,-8(%rsp) #stackptr_ptr**
    movq %rsi,-16(%rsp) #procptr**
    movq %rdx,-24(%rsp) #recall_rdi**
    movq %rcx,-32(%rsp) #recall_rsp**
    movq %r8,-40(%rsp) #recall_rbp**
    movq %r9,-48(%rsp) #recall_rip**
    movq 16(%rsp),%rax
    movq %rax,-56(%rsp) #resume*
    movq 8(%rsp),%rax
    movq %rax,-64(%rsp) #save*

//note SYSVABI 是 push arg8 & push arg7

    #save startup context rsp(current rsp) rbp
    movq -32(%rsp),%rax
    movq %rsp,(%rax) #rsp
    movq -40(%rsp),%rax
    movq %rbp,(%rax) #rbp

    #save startup context callee_registers
    movq -64(%rsp),%rdi
    subq $64,%rsp
    call __coro_save_callee_saved_registers_wrapper
    addq $64,%rsp
    #save startup rip & save context
    subq $64,%rsp    
    call __coro_internal_magic_fetch_ip #returns rax(IP)
    movq $0x0073757370656E64,%r10 #S
    cmp %r10,%rax
    je 2f
1:    
# i f it's not jump_from_suspend set recall_rip and switch the rsp,rbp,and contexts finally call with rdi = recall_rdi
    addq $64,%rsp    
    movq -48(%rsp),%rdi #因为是双重指针 我们需要取出这个指针解引用才能取出这个指针
    movq %rax,(%rdi) #restore rip
    movq -56(%rsp),%rdi
    subq $64,%rsp    
    call __coro_resume_callee_saved_registers_wrapper
    addq $64,%rsp    
    movq -8(%rsp),%rax
    movq -16(%rsp),%r10
    movq -24(%rsp),%r11
#-----------WARNING-LINE
    movq (%rax),%rbp
    movq %rbp,%rsp
    movq (%r11),%rdi
    call *(%r10)  #只有一种可能会ret到下面 那就是startup 紧接 return走internal_end 我们需要手动恢复一下栈
#此时的栈还是coroutine的 我们需要恢复 我们用约定已经用了栈
    movq %r9,%rbp
    movq %r10,%rsp
    jmp 0f
2:
    cmp %r10,%rdx
    jne 1b
0: # if it's jump_from_suspend
    xor %rax,%rax
    ret



/*

    %rdi
    %rsi
    %rdx
    %rcx
    %r8
    %r9

rax r8 r9 r10 r11
*/
.global __coro_internal_end
__coro_internal_end:
    movq %rdi,-8(%rsp)  #rsp**
    movq %rsi,-16(%rsp) #rbp**
    movq %rdx,-24(%rsp) #stack_base **
    movq %rcx,-32(%rsp) #resume *
    
    movq -32(%rsp),%rdi
    subq $32,%rsp
    call __coro_resume_callee_saved_registers_wrapper
    addq $32,%rsp
    movq -16(%rsp),%rax
    movq -8(%rsp),%r8
    movq (%rax),%r9 #rbp derefed
    movq (%r8),%r10 #rsp derefed
    movq -24(%rsp),%r11

    movq (%r11),%rsp
    sub $8,%rsp #startupcall出来 push的
    ret

/*

    %rdi
    %rsi
    %rdx
    %rcx
    %r8
    %r9
    
rax r10 r11
*/
.global __coro_internal_save_suspend_context
__coro_internal_save_suspend_context:
    movq %rsp,(%rdi)
    movq %rbp,(%rsi)
    xor %rax,%rax
    ret

.global __coro_internal_save_resume_context
__coro_internal_save_resume_context:
    movq %rsp,(%rdi)
    movq %rbp,(%rsi)
    xor %rax,%rax
    ret
    
/*

    %rdi
    %rsi
    %rdx
    %rcx
    %r8
    %r9
rax r10 r11
*/
.global __coro_internal_resume_switch_rip

__coro_internal_resume_switch_rip:
    movq %rdi,-8(%rsp) #suspend_rip **
    movq %rsi,-16(%rsp) #recall_rip **
    movq %rdx,-24(%rsp) #suspend_rsp **
    movq %rcx,-32(%rsp) #suspend_rbp **
    movq %r8,-40(%rsp) #save *
    movq %r9,-48(%rsp) #resume *
    movq $0,-56(%rsp) #占位的捏    
    
    movq -40(%rsp),%rdi
    subq $56,%rsp
    call __coro_save_callee_saved_registers_wrapper
    addq $56,%rsp

    subq $56,%rsp
    xor %rax,%rax
    call __coro_internal_magic_fetch_ip
    movq $0x0073757370656E64,%r10
    cmp %r10,%rax
    je 2f
1:
    addq $56,%rsp   
    movq -16(%rsp),%r10 #restore rip for recall
    movq %rax,(%r10)
    xor %rax,%rax
#如果不是 那就直接恢复高级上下文
    movq -48(%rsp),%rdi
    subq $56,%rsp
    call __coro_resume_callee_saved_registers_wrapper
    addq $56,%rsp
    movq -8(%rsp),%rax #set suspend rip
    movq -24(%rsp),%r10 #sp**
    movq -32(%rsp),%r11 #bp**
    movq (%r10),%rsp
    movq (%r11),%rbp
    movq (%rax),%r8
    movq $0x0000726573756D65,%rax #set magic flag
    movq $0x0000726573756D65,%rdx #set magic flag
    jmp *%r8
    #永远不会往下走
2:
    cmp %r10,%rdx
    jne 1b #如果不满足 代表纯属巧合 重新跳回来我们     如果满足 就代表不是巧合 过
0:
    xor %rax,%rax
    ret
.global __coro_internal_suspend_switch_rip
#__coro_internal_magic_fetch_ip
#__coro_resume_callee_saved_registers
__coro_internal_suspend_switch_rip:
    movq %rdi,-8(%rsp) #suspend_rip **
    movq %rsi,-16(%rsp) #recall_rip **
    movq %rdx,-24(%rsp) #resume_rsp **
    movq %rcx,-32(%rsp) #resume_rbp **
    movq %r8,-40(%rsp) #save *
    movq %r9,-48(%rsp) #resume *
    movq $0,-56(%rsp) #占位的捏    
    
    movq -40(%rsp),%rdi
    subq $56,%rsp
    call __coro_save_callee_saved_registers_wrapper
    addq $56,%rsp

    subq $56,%rsp
    xor %rax,%rax
    call __coro_internal_magic_fetch_ip
    movq $0x0000726573756D65,%r10
    cmp %r10,%rax
    je 2f
1:
    addq $56,%rsp   
    movq -8(%rsp),%r10 #store rip to suspend_rip
    movq %rax,(%r10)
    xor %rax,%rax
#如果不是 那就直接恢复高级上下文
    movq -48(%rsp),%rdi
    subq $56,%rsp
    call __coro_resume_callee_saved_registers_wrapper
    addq $56,%rsp
    movq -16(%rsp),%rax #set rip to recall
    movq -24(%rsp),%r10 #sp**
    movq -32(%rsp),%r11 #bp**
    movq (%r10),%rsp
    movq (%r11),%rbp
    movq (%rax),%r8
    movq $0x0073757370656E64,%rax #set magic flag
    movq $0x0073757370656E64,%rdx #set magic flag
    jmp *%r8
    #永远不会往下走
2:
    cmp %r10,%rdx
    jne 1b #如果不满足 代表纯属巧合 重新跳回来我们     如果满足 就代表不是巧合 过
0:
    xor %rax,%rax
    ret
    