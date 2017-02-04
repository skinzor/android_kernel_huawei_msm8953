

#include "cyttsp5_regs.h"

static void cyttsp5_final_sync(struct input_dev *input, int max_slots,
		int mt_sync_count, unsigned long *ids)
{
	if (mt_sync_count)
		input_sync(input);
}

static void cyttsp5_input_sync(struct input_dev *input)
{
	input_mt_sync(input);
}

static void cyttsp5_input_report(struct input_dev *input, int sig,
		int t, int type)
{
	if (type == CY_OBJ_STANDARD_FINGER || type == CY_OBJ_GLOVE
			|| type == CY_OBJ_HOVER) {
		input_report_key(input, BTN_TOOL_FINGER, CY_BTN_PRESSED);
		input_report_key(input, BTN_TOOL_PEN, CY_BTN_RELEASED);
	} else if (type == CY_OBJ_STYLUS) {
		input_report_key(input, BTN_TOOL_PEN, CY_BTN_PRESSED);
		input_report_key(input, BTN_TOOL_FINGER, CY_BTN_RELEASED);
	}

	if (type != CY_OBJ_HOVER)
		input_report_key(input, BTN_TOUCH, CY_BTN_PRESSED);

	input_report_abs(input, sig, t);
}

static void cyttsp5_report_slot_liftoff(struct cyttsp5_mt_data *md,
		int max_slots)
{
	input_report_key(md->input, BTN_TOUCH, CY_BTN_RELEASED);
	input_report_key(md->input, BTN_TOOL_FINGER, CY_BTN_RELEASED);
	input_report_key(md->input, BTN_TOOL_PEN, CY_BTN_RELEASED);

}

static int cyttsp5_input_register_device(struct input_dev *input, int max_slots)
{
	__set_bit(BTN_TOUCH, input->keybit);
	__set_bit(BTN_TOOL_FINGER, input->keybit);
	__set_bit(BTN_TOOL_PEN, input->keybit);
	return input_register_device(input);
}

void cyttsp5_init_function_ptrs(struct cyttsp5_mt_data *md)
{
	md->mt_function.report_slot_liftoff = cyttsp5_report_slot_liftoff;
	md->mt_function.final_sync = cyttsp5_final_sync;
	md->mt_function.input_sync = cyttsp5_input_sync;
	md->mt_function.input_report = cyttsp5_input_report;
	md->mt_function.input_register_device = cyttsp5_input_register_device;
}
