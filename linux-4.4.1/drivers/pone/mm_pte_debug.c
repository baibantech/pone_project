/*************************************************************************
	> File Name: mm_pte_debug.c
	> Author: lijiyong
	> Mail: lijiyong0303@163.com 
	> Created Time: Mon 17 Apr 2017 07:21:57 AM EDT
 ************************************************************************/
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>

struct mm_struct *pone_debug_ljy_mm = NULL;
unsigned long pone_debug_begin_address = 0x7f96c347000;
EXPORT_SYMBOL(pone_debug_ljy_mm);
EXPORT_SYMBOL(pone_debug_begin_address);

void debug_mm_pte(void)
{
	struct mm_struct *mm = pone_debug_ljy_mm;	
	struct vm_area_struct *vma;
	unsigned long scan_address = pone_debug_begin_address;
	struct page *page = NULL;
	if(mm)
	{
		printk("mm is %p\r\n",mm);
		for(vma = mm->mmap;vma ;vma = vma->vm_next)
		{
			printk("display vma start is 0x%lx\r\n",vma->vm_start);
		}
		vma = find_vma(mm,pone_debug_begin_address);
		if(!vma)
		{
			printk("vma is NULL\r\n");
			return;
		}
		if(pone_debug_begin_address != vma->vm_start)
		{
			printk("vma is not expect\r\n");
			printk("start is 0x%lx\r\n",vma->vm_start);
			return;
		}

		while(scan_address < vma->vm_end)
		{
			page = follow_page(vma,scan_address,FOLL_GET);
			if(NULL == page)
			{
				printk("address 0x%lx page is NULL\r\n",scan_address);
			}
			else
			{
				printk("address 0x%lx page is %p\r\n",scan_address,page);
			}
			scan_address += PAGE_SIZE;

		}

	}
	return ;
}

