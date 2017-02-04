#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <asm/barrier.h>
#include <linux/platform_device.h>
#include <linux/of_fdt.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/of_address.h>
#include <linux/kallsyms.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/hwboot_fail.h>

struct boot_log_struct *boot_log = NULL;

static u32 hwboot_calculate_checksum(unsigned char *addr, u32 len)
{
	int i;
	uint8_t *w = addr;
	u32 sum = 0;

	for(i=0;i<len;i++)
	{
		sum += *w++;
	}

	return sum;
}

static int hwboot_match_checksum(struct boot_log_struct *bl)
{

	u32 cksum_calc = hwboot_calculate_checksum((uint8_t *)bl, BOOT_LOG_CHECK_SUM_SIZE);

	if (cksum_calc != bl->hash_code )
	{
		pr_notice("hwboot_match_checksum: Checksum error\r\n");
		return 1;
	}
	return 0;
}

void hwboot_stage_inc(void)
{
	if(NULL != boot_log)
	{
		boot_log->boot_stage++;
	}
	return;
}
EXPORT_SYMBOL(hwboot_stage_inc);

void set_boot_stage(enum BFI_BOOT_STAGE_CODE stage)
{
	if(NULL != boot_log)
	{
		boot_log->boot_stage = stage;
	}
       return ;
}
EXPORT_SYMBOL(set_boot_stage);

u32 get_boot_stage(void)
{
	if(NULL != boot_log)
	{
		pr_info(" boot_stage = 0x%08x\n", boot_log->boot_stage);
		return boot_log->boot_stage;
	}
	pr_err("get boot stage fail\n");
	return 0;
}
EXPORT_SYMBOL(get_boot_stage);

void boot_fail_err(enum BFI_ERRNO_CODE bootErrNo,
	enum SUGGESTED_RECOVERED_METHOD sugRcvMtd/* = NO_SUGGESTION*/,
	char * logFilePath/* = 0*/)
{
	if(NULL != boot_log)
	{
		if(!boot_log->boot_error_no)
			boot_log->boot_error_no = bootErrNo;
	}
	return;
}
EXPORT_SYMBOL(boot_fail_err);

u32 get_boot_fail_errno(void)
{
	if(NULL != boot_log)
	{
		pr_info(" boot_stage = 0x%08x\n", boot_log->boot_error_no);
		return boot_log->boot_error_no;
	}
	pr_err("get boot fail errno failed\n");
	return 0;
}
EXPORT_SYMBOL(get_boot_fail_errno);

static struct bootlog_inject_struct *bootlog_inject =NULL;
void  hwboot_fail_init_struct(void)
{
	u64 *fseq,*nseq;
	u32 *fidx;
	void * boot_log_virt = ioremap_nocache(HWBOOT_LOG_INFO_START_BASE,HWBOOT_LOG_INFO_SIZE);
	boot_log = (struct boot_log_struct *)(boot_log_virt);
	bootlog_inject = (struct bootlog_inject_struct *)(boot_log_virt + sizeof(struct boot_log_struct) + sizeof(struct bootlog_read_struct));

	pr_notice("hwboot:boot_log=%p\n", boot_log);

	if(NULL != boot_log)
	{
		if(hwboot_match_checksum(boot_log))
		{
			pr_err("hwboot checksum fail\n");
			return;
		}

		/* save log address and size */
		boot_log->kernel_addr = virt_to_phys((void *)log_buf_addr_get());
		boot_log->kernel_log_buf_size = log_buf_len_get();
		boot_log->boot_stage = KERNEL_STAGE_START;

		hwboot_get_printk_buf_info(&fseq, &fidx, &nseq);
		boot_log->klog_first_seq_addr = virt_to_phys((void *)fseq);
		boot_log->klog_first_idx_addr = virt_to_phys((void *)fidx);
		boot_log->klog_next_seq_addr =  virt_to_phys((void *)nseq);

		pr_notice("hwboot: sbl_log=%x %d\n", boot_log->sbl_addr, boot_log->sbl_log_buf_size);
		pr_notice("hwboot: aboot=%x %d\n", boot_log->aboot_addr, boot_log->aboot_log_buf_size);
		pr_notice("hwboot: kernel=%x %d\n", boot_log->kernel_addr, boot_log->kernel_log_buf_size);

		boot_log->hash_code = hwboot_calculate_checksum((u8 *)boot_log, BOOT_LOG_CHECK_SUM_SIZE);
		//iounmap((void*)boot_log_virt);
	}
	else {
		pr_notice("hwboot: bootlog is null\n");
	}

	return;
}
EXPORT_SYMBOL(hwboot_fail_init_struct);

bool check_bootfail_inject(u32 err_code)
{
    if (bootlog_inject->flag == HWBOOT_FAIL_INJECT_MAGIC) {
		if(err_code == bootlog_inject->inject_boot_fail_no) {
			bootlog_inject->flag = 0;
			bootlog_inject->inject_boot_fail_no = 0;
			return true;
		}
    }
    return false;
}
EXPORT_SYMBOL(check_bootfail_inject);

void hwboot_clear_magic(void)
{
	boot_log->boot_magic = 0;
	return;
}
EXPORT_SYMBOL(hwboot_clear_magic);

static u32 bfm_ctl_code = 0;
static ssize_t bfm_ctl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    u32 bfm_ctl_value = 0;

	if(!bfm_ctl_code) {
		return 0;
	}

    switch(bfm_ctl_code) {
	case BFM_STAGE_CODE:
		bfm_ctl_value  = get_boot_stage();
		break;
	case BFM_ERROR_CODE:
		bfm_ctl_value = get_boot_fail_errno();
		break;
	case BFM_TIMER_EN_CODE:
		boot_timer_get_enable((bool *)(&bfm_ctl_value));
		break;
	case BFM_TIMER_SET_CODE:
		boot_timer_get(&bfm_ctl_value);
		break;
	default:
		break;
	}
    pr_info("%s, buf is %s, sizeof bfm_ctl_value is %lu, bfm_ctl_value is %u\n",__func__, buf, sizeof(bfm_ctl_value), bfm_ctl_value);
    return snprintf(buf, PAGE_SIZE, "0x%x\n", bfm_ctl_value);
}

static ssize_t bfm_ctl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 bfm_ctl_value = 0xFFFFFFFF;

    if((buf[0]=='0') && ((buf[1]=='x') || (buf[1]=='X'))) {
        sscanf(buf, "0x%x 0x%x", &bfm_ctl_code, &bfm_ctl_value);
    }else {
        sscanf(buf, "%u %u", &bfm_ctl_code, &bfm_ctl_value);
    }

    pr_info("%s, buf is %s, bfm_ctl_code is 0x%x, bfm_ctl_value 0x%x\n", __func__, buf, bfm_ctl_code, bfm_ctl_value);

	if(0xFFFFFFFF == bfm_ctl_value) {
		return count;
	}

    switch(bfm_ctl_code) {
	case BFM_STAGE_CODE:
		set_boot_stage((enum BFI_BOOT_STAGE_CODE)(bfm_ctl_value));
    	if(isBootSuccess(bfm_ctl_value))
		{
			pr_info("set boot_timer_stop\n");
			boot_timer_stop();
		}
		break;
	case BFM_ERROR_CODE:
		if(isCoverKernelStage(bfm_ctl_value)) {
			boot_fail_err((enum BFI_ERRNO_CODE)bfm_ctl_value, 0, NULL);
			panic("%s:boot fail happend(0x%x)",__func__,bfm_ctl_value);
		}
		break;
	case BFM_TIMER_EN_CODE:
		if(bfm_ctl_value)
			boot_timer_set_enable(true);
		else
			boot_timer_set_enable(false);
		break;
	case BFM_TIMER_SET_CODE:
		boot_timer_set(bfm_ctl_value);
		break;
	default:
		break;
	}
    return count;
}

static struct miscdevice hw_bfm_miscdev = {
	.minor = 255,
	.name = "hw_bfm",
};

static DEVICE_ATTR(bfm_ctl, (S_IRUGO | S_IWUSR), bfm_ctl_show,
		   bfm_ctl_store);

static int __init hwboot_fail_init(void)
{
    int ret = 0;

	ret = misc_register(&hw_bfm_miscdev);
	if (0 != ret) {
		pr_err("%s: misc_register failed, ret.%d.\n", __func__, ret);
		return ret;
	}

	ret = device_create_file(hw_bfm_miscdev.this_device, &dev_attr_bfm_ctl);
	if (0 != ret) {
		pr_err("%s: Faield : dev_attr_bfm_ctl device_create_file.%d\n",
			__func__, ret);
		return ret;
	}

    boot_timer_init();

    if(check_bootfail_inject(KERNEL_AP_PANIC))
    {
        panic("hwboot: inject KERNEL_AP_PANIC");
    }
    pr_err("%s\n",__func__);
    return ret;
}

static void __exit hwboot_fail_exit(void)
{
    pr_err("%s\n",__func__);
    return;
}

module_init(hwboot_fail_init);
module_exit(hwboot_fail_exit);

MODULE_AUTHOR("Huawei");
MODULE_DESCRIPTION("Huawei DFX Boot Monitor");
MODULE_LICENSE("GPL");
