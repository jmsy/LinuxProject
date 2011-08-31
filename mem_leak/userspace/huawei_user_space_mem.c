#include<linux/module.h>
#include<linux/kernel.h>
#include<asm/pgtable.h>
#include<linux/sched.h>
#include"huawei_user_space_mem.h"

static struct task_struct *now_task;

static inline void handle_4_k_page(pte_t *pte, unsigned long paddr,
        int pgd_offset, int pud_offset, int pmd_offset, int pte_offset)
{
    if(pte_pfn(*pte) == (paddr >> PAGE_SHIFT))
    {
        unsigned long vaddr;

        vaddr = ((unsigned long)pgd_offset << PGDIR_SHIFT)
            + ((unsigned long)pud_offset << PUD_SHIFT)
            + ((unsigned long)pmd_offset << PMD_SHIFT)
            + ((unsigned long)pte_offset << PAGE_SHIFT);

        printk("paddr:[%p], TASK:[%d:%s], vaddr:(4K)[%p]\n", (void*)paddr, 
                now_task->pid, now_task->comm, (void*)vaddr);
    }
}

static inline void walk_pte(pte_t *base, unsigned long paddr, int pgd_offset,
        int pud_offset, int pmd_offset)
{
    pte_t *pte;
    int pte_i;

    for(pte_i=0;pte_i<PTRS_PER_PTE;pte_i++)
    {
        pte = base + pte_i;
        
        if(!pte_present(*pte))
        {
            continue;
        }

        handle_4_k_page(pte, paddr, pgd_offset, pud_offset, pmd_offset, pte_i);
    }
}

static inline int pmd_is_2_m_page(pmd_t pte)
{
    return pmd_large(pte);
}

static inline void handle_2_m_page(pmd_t *pmd, unsigned long paddr, 
        int pgd_offset, int pud_offset, int pmd_offset)
{
    /*
     * 2M bytes = 2 ^ (12 + 9)
     */
    if( ((pmd_val(*pmd) & PTE_PFN_MASK) >> (12 + 9))
            == (paddr >> (12 + 9)) )
    {
        unsigned long vaddr;

        vaddr = ((unsigned long)pgd_offset << PGDIR_SHIFT)
            + ((unsigned long)pud_offset << PUD_SHIFT)
            + ((unsigned long)pmd_offset << PMD_SHIFT);

        printk("paddr:[%p], TASK:[%d:%s], vaddr:(2M)[%p]\n", (void*)paddr, 
                now_task->pid, now_task->comm, (void*)vaddr);
    }
}

static inline void walk_pmd(pmd_t *base, unsigned long paddr, int pgd_offset,
        int pud_offset)
{
    pmd_t *pmd;
    int pmd_i;

    for(pmd_i=0;pmd_i<PTRS_PER_PMD;pmd_i++)
    {
        pte_t *pte;

        pmd = base + pmd_i;

        if(!pmd_present(*pmd))
        {
            continue;
        }

        if(unlikely(pmd_is_2_m_page(*pmd)))
        {
            handle_2_m_page(pmd, paddr, pgd_offset, pud_offset, pmd_i);
        }
        else
        {
            pte = (pte_t *)pmd_page_vaddr(*pmd);
            walk_pte(pte, paddr, pgd_offset, pud_offset, pmd_i);
        }
    }
}

static inline int pud_is_1_g_page(pud_t pte)
{
    return pud_large(pte);
}

static inline void handle_1_g_page(pud_t *pud, unsigned long paddr, 
        int pgd_offset, int pud_offset)
{
    /*
     * 1G bytes = 2 ^ (12 + 9 + 9)
     */
    if( ((pud_val(*pud) & PTE_PFN_MASK) >> (12 + 9 + 9))
            == (paddr >> (12 + 9 + 9)) )
    {
        unsigned long vaddr;

        vaddr = ((unsigned long)pgd_offset << PGDIR_SHIFT)
            + ((unsigned long)pud_offset << PUD_SHIFT);

        printk("paddr:[%p], TASK:[%d:%s], vaddr:(1G)[%p]\n", (void*)paddr, 
                now_task->pid, now_task->comm, (void*)vaddr);
    }
}

static inline void walk_pud(pud_t *base, unsigned long paddr, int pgd_offset)
{
    pud_t *pud;
    int pud_i;

    for(pud_i=0;pud_i<(PTRS_PER_PUD);pud_i++)
    {
        pmd_t *pmd;

        pud = base + pud_i;

        if(!pud_present(*pud))
        {
            continue;
        }

        if(unlikely(pud_is_1_g_page(*pud)))
        {
            handle_1_g_page(pud, paddr, pgd_offset, pud_i);
        }
        else
        {
            pmd = (pmd_t *)pud_page_vaddr(*pud);
            walk_pmd(pmd, paddr, pgd_offset, pud_i);
        }
    }
}

static inline void walk_pgd(pgd_t *base, unsigned long paddr)
{
    pgd_t *pgd;
    int pgd_i;

    /*
     * 线性地址空间共48位，其中用户态地址空间使用低47位线性地址。
     */
    for(pgd_i=0;pgd_i<(PTRS_PER_PGD/2);pgd_i++)
    {
        pud_t *pud;

        pgd = base + pgd_i;
        
        if(!pgd_present(*pgd))
        {
            continue;
        }

        pud = (pud_t *)pgd_page_vaddr(*pgd);
        walk_pud(pud, paddr, pgd_i);
    }
}

/*
 * 这里我们只遍历用户态页表，即(Documentation/x86/x86_64/mm.txt)
 * 0000000000000000 - 00007fffffffffff (=47 bits) user space, different per mm
 *
 * 因为页表项所占用的内存地址都是使用page_address(page*)来表示，
 * 所以它们都是内核态内存，我们不用考虑
 */
void walk_mm_struct(struct mm_struct *mm, unsigned long paddr)
{
    pgd_t *cr3;

    spin_lock(&mm->page_table_lock);

    cr3 = mm->pgd;

    walk_pgd(cr3, paddr);

    spin_unlock(&mm->page_table_lock);
}

struct user_mem_vaddr {
    struct task_struct *tsk;
    unsigned long vaddr;
};

void print_vaddr_of_paddr(unsigned long paddr)
{
    struct task_struct *p;
    struct list_head *_p, *_n;

    printk(KERN_INFO "%s\n", __func__);

    list_for_each_safe(_p, _n, &current->tasks) {
        p = list_entry(_p, struct task_struct, tasks);

        /*
         * 所有的用户态页表都在p->mm中，
         * 因此不用考虑p->active_mm了
         */
        if(p->mm)
        {
            /*
             * 确保我们遍历该地址空间时它不被释放。
             */
            struct mm_struct *mm = p->mm;
            atomic_inc(&mm->mm_users);

            now_task = p;
            walk_mm_struct(p->mm, paddr);
            now_task = NULL;

            atomic_dec(&mm->mm_users);
        }
    }
}
EXPORT_SYMBOL(print_vaddr_of_paddr);

int huawei_user_space_mem_init(void)
{
	printk(KERN_INFO "------------Hi all, %s\n", __func__);

	return 0;
}

void huawei_user_space_mem_exit(void)
{
	printk(KERN_INFO "------------Goodbye %s\n", __func__);
}

module_init(huawei_user_space_mem_init);
module_exit(huawei_user_space_mem_exit);
