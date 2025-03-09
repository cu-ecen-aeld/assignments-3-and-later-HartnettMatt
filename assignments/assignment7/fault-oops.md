# Oops Fault
#### Author: Matt Hartnett
Here is a brief breakdown of an intentional kernel fault that I caused in my buildroot system.

## Full Error Log:
    # echo "hello_world" > /dev/faulty
    Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
    Mem abort info:
      ESR = 0x0000000096000045
      EC = 0x25: DABT (current EL), IL = 32 bits
      SET = 0, FnV = 0
      EA = 0, S1PTW = 0
      FSC = 0x05: level 1 translation fault
    Data abort info:
      ISV = 0, ISS = 0x00000045
      CM = 0, WnR = 1
    user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b59000
    [0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
    Internal error: Oops: 0000000096000045 [#1] SMP
    Modules linked in: hello(O) faulty(O) scull(O) [last unloaded: hello(O)]
    CPU: 0 PID: 152 Comm: sh Tainted: G           O       6.1.44 #1
    Hardware name: linux,dummy-virt (DT)
    pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
    pc : faulty_write+0x10/0x20 [faulty]
    lr : vfs_write+0xc8/0x390
    sp : ffffffc008db3d20
    x29: ffffffc008db3d80 x28: ffffff8001ba5cc0 x27: 0000000000000000
    x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
    x23: 000000000000000c x22: 000000000000000c x21: ffffffc008db3dc0
    x20: 00000055866519c0 x19: ffffff8001b5f800 x18: 0000000000000000
    x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
    x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
    x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
    x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
    x5 : 0000000000000001 x4 : ffffffc000787000 x3 : ffffffc008db3dc0
    x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
    Call trace:
     faulty_write+0x10/0x20 [faulty]
     ksys_write+0x74/0x110
     __arm64_sys_write+0x1c/0x30
     invoke_syscall+0x54/0x130
     el0_svc_common.constprop.0+0x44/0xf0
     do_el0_svc+0x2c/0xc0
     el0_svc+0x2c/0x90
     el0t_64_sync_handler+0xf4/0x120
     el0t_64_sync+0x18c/0x190
    Code: d2800001 d2800000 d503233f d50323bf (b900003f)
    ---[ end trace 0000000000000000 ]---

## Breakdown:
This kernel fault was caused by the faulty module, which intentionally creates kernal faults.
Here is a breakdown of different pieces of information within the fault log that can be used to understand what occured:
* The `Unable to handle kernel NULL pointer deference at virtual adddress 0000000000000000` indicates that the code tried to reference a NULL pointer.
In the user space, this would result in a segfault and program termination, but in the kernel, it causes the kernel to ender the oops state.
* The `Modules linked in: hello(O) faulty(O) scull(O) [last unloaded: hello(O)]` tell use which kernel modules are at fault and how they are linked together.
This can be very handy when running a custom image with many modules.
* There is a section dedicated to the CPU and register state.
For very low level programming like kernel modules, sometimes errors and debugging can require this detailed of information, even if traditional user space programming would rarely require the developer to fully understand the state of registers within the system.
This section occurs between `CPU: 0 PID: 152 Comm: sh Tainted: G           O       6.1.44 #1` and `x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000`
* The call trace can also help understand where in the code the fault occured.
In this example, we can see that the top of call trace is `faulty_write+0x10/0x20 [faulty]`, which gives us the function responsible for the fault.
Oftentimes in programming, it's very important to understand the larger context in which a fault occured, which is where the full stack trace can be very handy, because you see everything that led up the function in question.
* The `Code: d2800001 d2800000 d503233f d50323bf (b900003f)` indicates the instruction ad the faulting address is `b900003f`, which corresponds to a store operation in ARM assembly.
From this information, and the reference information at the start of the log tells us that the program specifically tried to write to a NULL pointer.

## Conclusion:
Reading and understand the error log can be a key aspect of kernel module development and debugging.
By reading logs, the programmer can indentify the location of the error, the cause of the error, and the context of the error, all of which are crucial for debugging.
One of the true skills in programming is debugging, and having a good understanding of error logs and messages can often save a lot of time and effort trying to narrow down the problem. 

<br> <br>