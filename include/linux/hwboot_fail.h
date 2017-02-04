#ifndef __HWBOOT_FAIL_H__
#define __HWBOOT_FAIL_H__
#include "hwboot_fail_public_interface.h"

struct boot_log_struct {
  u32 boot_magic;        /* must be initialized in slb1 */

  u32 sbl_meta_info_addr;/* must be initialized in slb1 */
  u32 last_sbl_meta_info_addr;/* must be initialized in slb1 */  
  u32 sbl_addr;          /* must be initialized in slb1 */
  u32 last_sbl_addr;          /* must be initialized in slb1 */  
  u32 sbl_log_buf_size;

  u32 aboot_addr;        /* must be initialized in lk */
  u32 aboot_log_buf_size;

  u32 kernel_addr;       /* must be initialized in linux kernel */
  u32 kernel_log_buf_size;
  u32 klog_first_idx_addr;
  u32 klog_first_seq_addr;
  u32 klog_next_seq_addr;  

  u32 hash_code;         /*!!!THIS IS MUST BE LAST MEMBER!!! any change in this sturct must be set this code again */ 

  u32 boot_stage;        /* must be initialized in every stage,and set it's val via  set_boot_stage */
  u32 last_boot_stage;

  u32 boot_error_no;     /* set this via set_boot_error */
  u32 last_boot_error_no;
};

struct bootlog_read_struct {
 u32 number_of_logs;
 u32 boot_share_addr_start;   /* used for pass file from sbl1 to lk,must be initialized in lk */
 u32 boot_share_addr_write;
 u32 boot_share_size;
 u32 read_magic;
 u32 hash_code;         /*!!!THIS IS MUST BE LAST MEMBER!!! any change in this sturct must be set this code again */ 
};

struct bootlog_inject_struct {
  u32 flag;
  u32 inject_boot_fail_no;
};

#define BOOT_TIMER_INTERVAL  (10000)
#define BOOT_TOO_LONG_TIME   (1000*60*30)
#define BOOT_TOO_LONG_TIME_EXACT   (1000*60*3)
#define BFM_STAGE_CODE              0x00000001
#define BFM_ERROR_CODE              0x00000002
#define BFM_TIMER_EN_CODE           0x00000003
#define BFM_TIMER_SET_CODE          0x00000004


/* IMEM ALLOCATION */
/* SHARED_IMEM_UNUSED_SPACE_BASE (0x08600B1C) */
#define HWBOOT_LOG_INFO_START_BASE  0x08600B1C
#define HWBOOT_LOG_INFO_SIZE (0x100)

#define HWBOOT_MAGIC_NUMBER   *((u32 *)("BOOT"))
#define HWBOOT_FAIL_INJECT_MAGIC  0x12345678

//#define BOOT_LOG_CHECK_SUM_SIZE  (sizeof(struct boot_log_struct) - 16)
#define BOOT_LOG_CHECK_SUM_SIZE ((int)((u8 *)(&(((struct boot_log_struct *)0)->hash_code)) - (u8 *)0))

void hwboot_stage_inc(void);

void set_boot_stage(enum BFI_BOOT_STAGE_CODE stage);
u32 get_boot_stage(void);
void boot_fail_err(enum BFI_ERRNO_CODE bootErrNo,
	enum SUGGESTED_RECOVERED_METHOD sugRcvMtd/* = NO_SUGGESTION*/, char * logFilePath/* = 0*/);
u32 get_boot_fail_errno(void);

void hwboot_fail_init_struct(void);
void hwboot_clear_magic(void);
bool check_bootfail_inject(u32 err_code);
int boot_timer_init(void);
void boot_timer_stop(void);
void boot_timer_set_enable(bool enable);
void boot_timer_get_enable(bool *enable);
void boot_timer_set(u32 bt_value);
void boot_timer_get(u32 *bt_value_ptr);

#endif
