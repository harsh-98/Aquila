#include <core/arch.h>
#include <sys/binfmt.h>
#include <sys/elf.h>
#include <mm/vm.h>

static struct binfmt binfmt_list[] = {
    {binfmt_elf_check, binfmt_elf_load},
};

#define NR_BINFMT   ((sizeof(binfmt_list)/sizeof(binfmt_list[0])))

static int binfmt_fmt_load(struct proc *proc, const char *path, struct inode *inode, struct binfmt *binfmt, struct proc **ref)
{
    int err = 0;

    vm_space_destroy(&proc->vm_space);

    if ((err = binfmt->load(proc, path, inode)))
        goto error;

    kfree(proc->name);
    proc->name = strdup(path);

    /* Align heap */
    proc->heap_start = UPPER_PAGE_BOUNDARY(proc->heap_start);
    proc->heap = proc->heap_start;

    /* Create heap vm_entry */
    struct vm_entry *heap_vm = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, 0);
    memset(heap_vm, 0, sizeof(struct vm_entry));
    heap_vm->base  = proc->heap_start;
    heap_vm->size  = 0;
    heap_vm->flags = VM_URW | VM_ZERO;
    heap_vm->qnode = enqueue(&proc->vm_space.vm_entries, heap_vm);
    proc->heap_vm  = heap_vm;

    /* Create stack vm_entry */
    struct vm_entry *stack_vm = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, 0);
    memset(stack_vm, 0, sizeof(struct vm_entry));
    stack_vm->base  = USER_STACK_BASE;
    stack_vm->size  = USER_STACK_SIZE;
    stack_vm->flags = VM_URW;
    stack_vm->qnode = enqueue(&proc->vm_space.vm_entries, stack_vm);
    proc->stack_vm  = stack_vm;

    struct thread *thread = (struct thread *) proc->threads.head->value;
    thread->stack_base = USER_STACK_BASE;
    thread->stack_size = USER_STACK_BASE;

    return 0;

error:
    return err;
}

int binfmt_load(struct proc *proc, const char *path, struct proc **ref)
{
    struct vnode vnode;
    struct inode *inode = NULL;
    int err = 0;

    struct uio uio;
    memset(&uio, 0, sizeof(struct uio));

    if (proc) {
        uio = PROC_UIO(proc);
    }

    if ((err = vfs_lookup(path, &uio, &vnode, NULL)))
        return err;

    if ((err = vfs_vget(&vnode, &inode)))
        return err;

    for (size_t i = 0; i < NR_BINFMT; ++i) {
        if (!binfmt_list[i].check(inode)) {
            binfmt_fmt_load(proc, path, inode, &binfmt_list[i], ref);
            vfs_close(inode);
            return 0;
        }
    }

    vfs_close(inode);
    return -ENOEXEC;
}
