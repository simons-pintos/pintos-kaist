#ifndef VM_ANON_H
#define VM_ANON_H
#include "filesys/file.h"
#include "vm/vm.h"
struct page;
enum vm_type;

struct anon_page // simons added
{
    enum vm_type type;
    int test;
};

void vm_anon_init(void);
bool anon_initializer(struct page *page, enum vm_type type, void *kva);

#endif
