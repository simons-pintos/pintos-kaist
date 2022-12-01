#ifndef VM_ANON_H
#define VM_ANON_H
#include "filesys/file.h"
#include "vm/vm.h"
struct page;
enum vm_type;

struct anon_page // simons added
{
    struct file *file;
    off_t offset;
    uint32_t read_bytes;
    uint32_t zero_bytes;
};

void vm_anon_init(void);
bool anon_initializer(struct page *page, enum vm_type type, void *kva);

#endif
