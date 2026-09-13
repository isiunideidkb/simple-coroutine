//coroutine设计
//0. SYSV AMD64 ABI
//1. create
//2. destory
//3. suspend
//4. resume


/*
TODOLIST:


!!!!!!!!!!!!请给startup设置RIP来提供跳转 还有判断是否是suspend回来和正常走的!!!!!! ok!


请提升robustness ok!
底层函数不太需要改


more compatibility with sysvabi

把一些hack给删除
把一些奇怪的语法删除
*/

/*
同步coroutine 具体的执行流程差不多是

create(main routine) -> startup(main r) -> coroutine执行 -> ... -> coroutine_yield/return(持有coroutine上下文) -> 返回到主程序(main r) -> 后续主程序执行 -> 主程序主动调用恢复最近的前面的上下文


其中 协程的切换有这集中模式
yield -> resume
resume -> yield
startup -> return
startup -> yield
return(yield) -> yield
resume -> yield(return)
每次模式切换都是同步的 串行的

协程享有和普通程序一样的栈 上下文 rbp,rsp 以及System V ABI中callee-saved registers中的上下文切换 通过 POSIX的mmap
*/

/*
hack: 由于编译器优化部分和代码本身冲突(例如resume/suspend某些函数在O2优化不会save-context,或者导致某些寄存器传参语义被打乱) 我们需要在内部函数加上none-optimization来保证代码语义相同
对于coroutine函数本体是否需要加上 还有待商议
*/


/*
User Doc
main routine func:
coroutine为一个过程函数 类型必须是 coro_proc_t 的函数类型 只有一个参数 并且不能有返回值
coro_create创建一个协程 并且创建栈
coro_startup开启一个协程 并且直接进入协程
coro_destory: 销毁协程极其上下文
coro_resume: 配合coro_suspend 重新进入协程

co routine func:
coro_suspend: 保存上下文 归还控制权给coro_resume,从resume/startup返回
coro_return: 从协程直接退出

一个协程的执行流程是这样子的

main:主程序运行 ->
main:协程创建 ->
main:协程启动 上下文1 ->
co:执行 ->
co:suspend 注意！这会归还 归还的位置是上下文1!!!->
main:程序继续执行 从main:上下文1后 ->
main:resume 从上一次退出的重新开始执行 ->

你绝对不能在coro里面使用coro_resume等函数
你绝对不能在主程里面使用coro_suspend等函数
否则程序会崩溃

*/


#ifdef __clang__
#define CORO_FUNC [[clang::optnone]]
#define CORO_FUNC_NORET [[clang::optnone,noreturn]]
#else
#define CORO_FUNC [[gnu::optimize("O0")]]
#define CORO_FUNC_NORET [[gnu::optimize("O0"),noreturn]]
#endif

#pragma once
#define CORO_PLATFORM_POINTER_SIZE 8
#define CORO_STACK_SIZE (64 * 1024)   // 改为 64KB
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
enum coro_state_t {
    CORO_INIT,
    CORO_SUSPEND,
    CORO_RUNNING,
    CORO_END
};

typedef volatile void* volatile register_t;
typedef void* coro_stack_ptr;
typedef void coro_return_t; //coro_return_t会在每次suspend返回 coro_return_t会在每次
struct coro_context_t;
typedef coro_return_t(*coro_proc_t)(struct coro_context_t*);
struct coro_jump_context_t{
    register_t rip; //仅用于suspend 仅用于suspend回来的
    register_t rsp; //保存上下文的rsp 通用
    register_t rbp; //保存上下文的rbp确保没有出问题 通用
    register_t rdi; //这是一个出参窗口 rdi是用于设置第一个变量的 正常情况下rdi并不需要管你(你不需要管rdi寄存器的上下文) 通用的噢
    
    //see https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf calling convention Function Calling Sequence 3.4 Register Usage
    register_t rbx;
    register_t r12;
    register_t r13;
    register_t r14;
    register_t r15;
};
struct coro_context_t { //WHOLE MALLOC: needs free
    bool __is_once; //default true);
    coro_proc_t proc;    
    coro_stack_ptr stack_base; //栈顶 请使用这个
    void* __stack_start; //mmap释放用的
    volatile struct coro_jump_context_t suspend_point_info_nullable; //suspend的记录桩
    volatile struct coro_jump_context_t  recall_info;  //召回函数的栈地址 不轻易改变 modify_once
    struct {void* coro_params;void* coro_ret} params_nullable;
    enum coro_state_t state;    
};


//just init the context, and pass the arg
CORO_FUNC struct coro_context_t* coro_create(coro_proc_t proc){
    struct coro_context_t* coro_context = (struct coro_context_t*)malloc(sizeof(struct coro_context_t));
    if (!coro_context) return nullptr;

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;
    size_t aligned_size = (CORO_STACK_SIZE + page_size - 1) & ~(page_size - 1);

    void* addr = mmap(nullptr, aligned_size,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_GROWSDOWN,
                      -1, 0);
    if (addr == MAP_FAILED) {
        free(coro_context);
        return nullptr;
    }

    void* stack_base = (char*)addr + CORO_STACK_SIZE;

    (*coro_context) = (struct coro_context_t){
        .proc = proc,
        .stack_base = stack_base,
        .__stack_start = addr,
        .suspend_point_info_nullable = { .rip = nullptr, .rsp = nullptr, .rdi = nullptr, .rbp = nullptr },
        .params_nullable = { .coro_params = nullptr, .coro_ret = nullptr },
        .recall_info = { .rip = nullptr, .rsp = nullptr, .rdi = nullptr, .rbp = nullptr },
        .state = CORO_INIT,
	.__is_once = true
    };

    *((struct coro_context_t**)(&(coro_context->recall_info.rdi))) = coro_context;

    return coro_context;
}
struct __coro_internal_callee_saved_registers {
    register_t* rbx;
    register_t* r12;
    register_t* r13;
    register_t* r14;
    register_t* r15;    
};
CORO_FUNC extern void __coro_resume_callee_saved_registers(register_t* rbx,register_t* r12,register_t* r13,register_t* r14,register_t* r15);
CORO_FUNC void __coro_resume_callee_saved_registers_wrapper(struct __coro_internal_callee_saved_registers* registers){
   __coro_resume_callee_saved_registers((registers->rbx),(registers->r12),(registers->r13),(registers->r14),(registers->r15));
   return;
}
CORO_FUNC extern void __coro_save_callee_saved_registers(register_t* rbx,register_t* r12,register_t* r13,register_t* r14,register_t* r15);
CORO_FUNC void __coro_save_callee_saved_registers_wrapper(struct __coro_internal_callee_saved_registers* registers){
   __coro_save_callee_saved_registers((registers->rbx),(registers->r12),(registers->r13),(registers->r14),(registers->r15));
   return;
}
CORO_FUNC extern void __coro_internal_startup(void** stackptr,void** procptr,register_t* recall_rdi,register_t* recall_rsp,register_t* recall_rbp,register_t* recall_rip,struct __coro_internal_callee_saved_registers* callee_register_save,struct __coro_internal_callee_saved_registers* callee_register_resume);
//extern void __coro_internal_store_suspend_info(void** suspend_rip,void** suspend_rsp,void** suspend_rbp);
CORO_FUNC extern void* __coro_internal_magic_fetch_ip(void);
CORO_FUNC bool coro_startup(struct coro_context_t* context){    
    if(context->state != CORO_INIT)
        return false;
    context->__is_once = true;
    context->state = CORO_RUNNING;
    //STACK INIT
    void** stackptr_ptr = &(context->stack_base);
    void** procptr = (void**)&(context->proc);
    register_t* recall_rdi = &(context->recall_info.rdi);
    register_t* recall_rsp = &(context->recall_info.rsp);
    register_t* recall_rbp = &(context->recall_info.rbp);
    register_t* recall_rip = &(context->recall_info.rip);
    struct __coro_internal_callee_saved_registers save = (struct __coro_internal_callee_saved_registers){
        .rbx = &(context->recall_info.rbx),
	.r12 = &(context->recall_info.r12),
	.r13 = &(context->recall_info.r13),
	.r14 = &(context->recall_info.r14),
	.r15 = &(context->recall_info.r15)
    };

    struct __coro_internal_callee_saved_registers resume = (struct __coro_internal_callee_saved_registers){
        .rbx = &(context->suspend_point_info_nullable.rbx),
	.r12 = &(context->suspend_point_info_nullable.r12),
	.r13 = &(context->suspend_point_info_nullable.r13),
	.r14 = &(context->suspend_point_info_nullable.r14),
	.r15 = &(context->suspend_point_info_nullable.r15)
    };
    __coro_internal_startup(stackptr_ptr,procptr,recall_rdi,recall_rsp,recall_rbp,recall_rip,&save,&resume);
    if (context->state != CORO_SUSPEND)
        context->state = CORO_END;
    return true;
}
//                             
CORO_FUNC extern void __coro_internal_end(register_t* rsp,register_t* rbp,void** stack_base,struct __coro_internal_callee_saved_registers* resume);
CORO_FUNC extern void __coro_internal_save_suspend_context(register_t* suspend_rsp,register_t* suspend_rbp); //请不要乱动这个函数 只能在switch_rip前面有严格的顺序
CORO_FUNC extern void __coro_internal_save_resume_context(register_t* recall_rsp,register_t* recall_rbp); 
CORO_FUNC extern void __coro_internal_resume_switch_rip(register_t* suspend_rip,register_t* save_recall_rip,register_t* suspend_rsp,register_t* suspend_rbp,struct __coro_internal_callee_saved_registers* save,struct __coro_internal_callee_saved_registers* resume);
CORO_FUNC extern void __coro_internal_suspend_switch_rip(register_t* suspend_rip,register_t* save_recall_rip,register_t* resume_rsp,register_t* resume_rbp,struct __coro_internal_callee_saved_registers* save,struct __coro_internal_callee_saved_registers* resume);
CORO_FUNC bool coro_resume(struct coro_context_t* context){
    //来resume 我们需要更新recall_info 并且设置一个专属的回来的特征 rax会是一个字符串resume
    if(context->state == CORO_END)
        return true;
    if (context->state != CORO_SUSPEND)
        return false;
    context->state = CORO_RUNNING;
    struct __coro_internal_callee_saved_registers save = (struct __coro_internal_callee_saved_registers){
        .rbx = &(context->recall_info.rbx),
	.r12 = &(context->recall_info.r12),
	.r13 = &(context->recall_info.r13),
	.r14 = &(context->recall_info.r14),
	.r15 = &(context->recall_info.r15)
    };
    struct __coro_internal_callee_saved_registers resume = (struct __coro_internal_callee_saved_registers){
        .rbx = &(context->suspend_point_info_nullable.rbx),
	.r12 = &(context->suspend_point_info_nullable.r12),
	.r13 = &(context->suspend_point_info_nullable.r13),
	.r14 = &(context->suspend_point_info_nullable.r14),
	.r15 = &(context->suspend_point_info_nullable.r15)
    };
    __coro_internal_save_resume_context(&(context->recall_info.rsp),&(context->recall_info.rbp));
    //判断程序是否主动yield和suspend实现不一样 嵌入进这个switch_rip了 所以我们应该
    __coro_internal_resume_switch_rip(&(context->suspend_point_info_nullable.rip),&(context->recall_info.rip),
                                      &(context->suspend_point_info_nullable.rsp),
				      &(context->suspend_point_info_nullable.rbp),&save,&resume);
    return true;
}
CORO_FUNC bool coro_suspend(struct coro_context_t* context){
    //来suspend! 储存rip上下文
//    suspend_point_info_nullable; //suspend的记录桩
//    context->suspend_point_info_nullable.rip
    if(context->state == CORO_END)
        goto ended;
    if(context->state != CORO_RUNNING)
        return false;
    context->__is_once = false;    
    context->state = CORO_SUSPEND;
ended:
    struct __coro_internal_callee_saved_registers resume = (struct __coro_internal_callee_saved_registers){
        .rbx = &(context->recall_info.rbx),
	.r12 = &(context->recall_info.r12),
	.r13 = &(context->recall_info.r13),
	.r14 = &(context->recall_info.r14),
	.r15 = &(context->recall_info.r15)
    };    
    struct __coro_internal_callee_saved_registers save  = (struct __coro_internal_callee_saved_registers){
        .rbx = &(context->suspend_point_info_nullable.rbx),
	.r12 = &(context->suspend_point_info_nullable.r12),
	.r13 = &(context->suspend_point_info_nullable.r13),
	.r14 = &(context->suspend_point_info_nullable.r14),
	.r15 = &(context->suspend_point_info_nullable.r15)
    };
    __coro_internal_save_suspend_context(&(context->suspend_point_info_nullable.rsp),&(context->suspend_point_info_nullable.rbp));
    __coro_internal_suspend_switch_rip(&(context->suspend_point_info_nullable.rip),&(context->recall_info.rip),
                                      &(context->recall_info.rsp),
				      &(context->recall_info.rbp),&save,&resume);
    return true;				   
}
CORO_FUNC_NORET void coro_return(struct coro_context_t* context){
    context->state = CORO_END;
    if(!context->__is_once){
        coro_suspend(context);
    }
    struct __coro_internal_callee_saved_registers* resume = malloc(sizeof(struct __coro_internal_callee_saved_registers));
    (*resume) = (struct __coro_internal_callee_saved_registers){
        .rbx = &(context->recall_info.rbx),
	.r12 = &(context->recall_info.r12),
	.r13 = &(context->recall_info.r13),
	.r14 = &(context->recall_info.r14),
	.r15 = &(context->recall_info.r15)
    };  
    __coro_internal_end(&(context->recall_info.rsp),&(context->recall_info.rbp),&(context->stack_base),resume);
}
CORO_FUNC void coro_destory(struct coro_context_t* context){
    if (!context) return;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;
    size_t aligned_size = (CORO_STACK_SIZE + page_size - 1) & ~(page_size - 1);
    munmap(context->__stack_start, aligned_size);
    free(context);
}
void demo_function(struct coro_context_t* ctx){
    int a = 100;
    int b = 200;
    int c = 300;
    printf("[DemoFunction] HelloWorld and ctx: %p\n",ctx);
    printf("[DemoFunction] HelloWorld and ctx.rsp: %p\n",ctx->recall_info.rsp);
    printf("[DemoFunction] resume ptr: %p\n",&(ctx->recall_info.rip));
    ctx->params_nullable.coro_ret = (void*)malloc(sizeof(int));
    *(int*)(ctx->params_nullable.coro_ret) = 114514;
    coro_suspend(ctx);
    printf("test \n");
    coro_suspend(ctx);
    printf("test2 \n");
    coro_return(ctx);
}
void demo_function02(struct coro_context_t* ctx){
    coro_return(ctx);
}
