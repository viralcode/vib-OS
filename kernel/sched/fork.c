/*
 * UnixOS Kernel - Fork and Exec Implementation
 *
 * Implements process creation (fork) and program loading (exec).
 */

#include "fs/vfs.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "printk.h"
#include "sched/sched.h"

/* Forward declaration */
static void fork_entry(void *arg);

/* Simple ELF header definitions */
#define ELF_MAGIC 0x464C457F /* "\x7FELF" */
#define ET_EXEC 2
#define EM_AARCH64 183
#define PT_LOAD 1

struct elf64_hdr {
  uint32_t e_ident_magic;
  uint8_t e_ident_class;
  uint8_t e_ident_data;
  uint8_t e_ident_version;
  uint8_t e_ident_osabi;
  uint8_t e_ident_pad[8];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;
  uint64_t e_phoff;
  uint64_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

struct elf64_phdr {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset;
  uint64_t p_vaddr;
  uint64_t p_paddr;
  uint64_t p_filesz;
  uint64_t p_memsz;
  uint64_t p_align;
};

/* ===================================================================== */
/* Fork implementation */
/* ===================================================================== */

static int copy_mm(struct task_struct *child, struct task_struct *parent) {
  if (!parent->mm) {
    child->mm = NULL;
    child->active_mm = parent->active_mm;
    return 0;
  }

  child->mm = vmm_create_address_space();
  if (!child->mm) {
    return -1;
  }

  child->active_mm = child->mm;
  return 0;
}

static void copy_thread(struct task_struct *child, struct task_struct *parent) {
  child->cpu_context = parent->cpu_context;
}

static void fork_entry(void *arg) { (void)arg; }

long do_fork(unsigned long flags) {
  struct task_struct *current_task = get_current();
  struct task_struct *child;

  child = create_task(fork_entry, NULL, (uint32_t)flags);
  if (!child) {
    return -1;
  }

  if (copy_mm(child, current_task) < 0) {
    return -1;
  }

  copy_thread(child, current_task);
  child->parent = current_task;
  child->uid = current_task->uid;
  child->gid = current_task->gid;
  child->state = TASK_RUNNING;

  return child->pid;
}

/* ===================================================================== */
/* Exec implementation */
/* ===================================================================== */

static int load_elf_binary(const char *path, uint64_t *entry_point) {
  struct file *file;
  struct elf64_hdr ehdr;
  ssize_t bytes_read;

  file = vfs_open(path, O_RDONLY, 0);
  if (!file) {
    printk(KERN_ERR "exec: cannot open '%s'\n", path);
    return -1;
  }

  bytes_read = vfs_read(file, (char *)&ehdr, sizeof(ehdr));
  if (bytes_read < (ssize_t)sizeof(ehdr)) {
    vfs_close(file);
    return -1;
  }

  if (ehdr.e_ident_magic != ELF_MAGIC || ehdr.e_machine != EM_AARCH64) {
    vfs_close(file);
    return -1;
  }

  for (int i = 0; i < ehdr.e_phnum; i++) {
    struct elf64_phdr phdr;

    file->f_pos = ehdr.e_phoff + i * ehdr.e_phentsize;
    bytes_read = vfs_read(file, (char *)&phdr, sizeof(phdr));
    if (bytes_read < (ssize_t)sizeof(phdr) || phdr.p_type != PT_LOAD) {
      continue;
    }

    virt_addr_t vaddr = phdr.p_vaddr & ~(PAGE_SIZE - 1);
    size_t size = PAGE_ALIGN(phdr.p_memsz + (phdr.p_vaddr - vaddr));

    for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
      phys_addr_t paddr = pmm_alloc_page();
      if (!paddr) {
        vfs_close(file);
        return -1;
      }

      uint32_t vm_flags = VM_READ | VM_USER;
      if (phdr.p_flags & 0x1)
        vm_flags |= VM_EXEC;
      if (phdr.p_flags & 0x2)
        vm_flags |= VM_WRITE;

      vmm_map_page(vaddr + offset, paddr, vm_flags);
    }

    if (phdr.p_filesz > 0) {
      file->f_pos = phdr.p_offset;
      vfs_read(file, (char *)phdr.p_vaddr, phdr.p_filesz);
    }
  }

  vfs_close(file);
  *entry_point = ehdr.e_entry;
  return 0;
}

static uint64_t setup_user_stack(char *const argv[], char *const envp[]) {
  virt_addr_t stack_top = 0x7FFFFFFFE000UL;
  size_t stack_size = 1024 * 1024;
  virt_addr_t stack_bottom = stack_top - stack_size;

  for (size_t offset = 0; offset < stack_size; offset += PAGE_SIZE) {
    phys_addr_t paddr = pmm_alloc_page();
    if (paddr) {
      vmm_map_page(stack_bottom + offset, paddr, VM_READ | VM_WRITE | VM_USER);
    }
  }

  int argc = 0;
  if (argv) {
    while (argv[argc])
      argc++;
  }

  (void)envp;

  uint64_t sp = stack_top - 16;
  *(uint64_t *)sp = argc;

  return sp;
}

long do_execve(const char *filename, char *const argv[], char *const envp[]) {
  struct task_struct *current_task = get_current();
  uint64_t entry_point;

  printk(KERN_INFO "execve: loading '%s'\n", filename);

  if (!current_task->mm) {
    current_task->mm = vmm_create_address_space();
    if (!current_task->mm) {
      return -1;
    }
    current_task->active_mm = current_task->mm;
  }

  if (load_elf_binary(filename, &entry_point) < 0) {
    return -1;
  }

  uint64_t user_sp = setup_user_stack(argv, envp);

  const char *name = filename;
  while (*filename) {
    if (*filename == '/')
      name = filename + 1;
    filename++;
  }
  for (int i = 0; i < TASK_COMM_LEN - 1 && name[i]; i++) {
    current_task->comm[i] = name[i];
  }

  current_task->cpu_context.pc = entry_point;
  current_task->cpu_context.sp = user_sp;

  printk(KERN_INFO "execve: ready\n");
  return 0;
}
