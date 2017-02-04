#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/of.h>
#include <asm/uaccess.h>
#include <linux/of_address.h>


#include "srecorder_log.h"
#include "srecorder_sahara.h"

void srecorder_sahara_clear(struct sahara_boot_log *head)
{
	head->sbl_log_addr=0;
	head->lk_log_addr=0;
	head->sbl_log_size=0;
	head->lk_log_size=0;
	printk(KERN_ERR "srecorder bootloader to kernel addr clear\n");
}

void lk_log_write_to_kernel(char *buf,unsigned int len)
{
    unsigned int i=0;
    char *p=buf;
    if(p==NULL)
        return;
	if(len>=SAHARA_BOOT_LOG_SIZE_MAX)
	{
		printk(KERN_ERR "Srecorder sahara bBootloader log size error \n");
		return;
	}
    printk(KERN_ERR "Bootloader log start : %lx\n",(long unsigned int)buf);
    for(i=0;i<len;i++)
    {
        if(buf[i]=='\0')
            buf[i]=' ';
        if(buf[i]=='\n')
        {
            buf[i]='\0';
            printk(KERN_ERR "Bootloader log: %s\n",p);
            buf[i]='\n';
            p=&buf[i+1];
        }
    }

    printk(KERN_ERR "srecorder:i:%d   len:%d\n",i,len);
    printk(KERN_ERR "Bootloader log end\n");

}

void srecorder_save_kernel_log_addr()
{
    struct sahara_boot_log *boot_log = NULL;
    unsigned long sahara_log_addr = 0;
    unsigned long sahara_log_size = 0;
    void *head_addr = NULL;
    struct device_node *sahara_mem_dts_node = NULL;
    const u32 *sahara_mem_dts_basep = NULL;

    sahara_mem_dts_node = of_find_compatible_node(NULL, NULL, "sahara_mem");
    if (sahara_mem_dts_node == 0)
    {
        SRECORDER_PRINTK("sahara_log_addr Finding compatible node failed.\n");
        return;
    }

    sahara_mem_dts_basep = of_get_address(sahara_mem_dts_node, 0, (u64*)&sahara_log_size, NULL);
    sahara_log_addr = (unsigned long)of_translate_address(sahara_mem_dts_node, sahara_mem_dts_basep);

    SRECORDER_PRINTK("sahara_log_addr:0x%lx sahara_log_size:0x%lx\n",sahara_log_addr,sahara_log_size);

    if (sahara_log_addr == 0 || sahara_log_size < SAHARA_BOOT_LOG_SIZE_MAX)
    {
         SRECORDER_PRINTK("srecorder_sahara get dts addr error \n");
         return;
    }

#ifdef CONFIG_ARM
    head_addr = (void *)ioremap_nocache(sahara_log_addr,sahara_log_size);
#else
    head_addr = (void *)ioremap_wc(sahara_log_addr, sahara_log_size);
#endif

    boot_log=(struct sahara_boot_log *)head_addr;

#ifdef CONFIG_KALLSYMS
    boot_log->kernel_log_addr = virt_to_phys((void *)kallsyms_lookup_name("__log_buf"));
#else
    boot_log->kernel_log_addr = 0;
#endif

#ifdef CONFIG_LOG_BUF_SHIFT
    boot_log->kernel_log_size = (1 << CONFIG_LOG_BUF_SHIFT);
#else
    boot_log->kernel_log_size = 0;
#endif
    lk_log_write_to_kernel((char *)(head_addr+sizeof(struct sahara_boot_log)),(unsigned int)(boot_log->lk_log_size+boot_log->sbl_log_size));

    SRECORDER_PRINTK("srecorder_sahara get dts addr %lx\n",(long unsigned int)head_addr);
    SRECORDER_PRINTK("srecorder_sahara sbl1_log_addr:%x sbl1_log_size:%x \n",boot_log->sbl_log_addr,boot_log->sbl_log_size);
    SRECORDER_PRINTK("srecorder_sahara lk_log_addr:%x lk_log_size:%x \n",boot_log->lk_log_addr,boot_log->lk_log_size);
    SRECORDER_PRINTK("srecorder_sahara kernel_log_addr:%x kernel_log_size:%x \n",boot_log->kernel_log_addr,boot_log->kernel_log_size);

    srecorder_sahara_clear(boot_log);
}
