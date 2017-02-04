

#ifndef _LINUX_CYTTSP5_DEVICE_ACCESS_API_H
#define _LINUX_CYTTSP5_DEVICE_ACCESS_API_H

#include <linux/types.h>
#include <linux/device.h>

int cyttsp5_device_access_user_command(const char *core_name, u16 read_len,
		u8 *read_buf, u16 write_len, u8 *write_buf,
		u16 *actual_read_len);

int cyttsp5_device_access_user_command_async(const char *core_name,
		u16 read_len, u8 *read_buf, u16 write_len, u8 *write_buf,
		void (*cont)(const char *core_name, u16 read_len, u8 *read_buf,
			u16 write_len, u8 *write_buf, u16 actual_read_length,
			int rc));
#endif /* _LINUX_CYTTSP5_DEVICE_ACCESS_API_H */
