/******************************************************************************
 * @file cm_cp_test.c
 *
 * cm_cp_test.c
 *
 * @version 0.0.1
 * @authors btok
 *
 *****************************************************************************//*
 * Copyright (2014), Cypress Semiconductor Corporation. All rights reserved.
 *
 * This software, associated documentation and materials ("Software") is owned
 * by Cypress Semiconductor Corporation ("Cypress") and is protected by and
 * subject to worldwide patent protection (United States and foreign), United
 * States copyright laws and international treaty provisions. Therefore, unless
 * otherwise specified in a separate license agreement between you and Cypress,
 * this Software must be treated like any other copyrighted material.
 * Reproduction, modification, translation, compilation, or representation of
 * this Software in any other form (e.g., paper, magnetic, optical, silicon) is
 * prohibited without Cypress's express written permission.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * Cypress reserves the right to make changes to the Software without notice.
 * Cypress does not assume any liability arising out of the application or use
 * of Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use as critical components in any products
 * where a malfunction or failure may reasonably be expected to result in
 * significant injury or death ("High Risk Product"). By including Cypress's
 * product in a High Risk Product, the manufacturer of such system or
 * application assumes all risk of such use and in doing so indemnifies Cypress
 * against all liability.
 *
 * Use of this Software may be limited by and subject to the applicable Cypress
 * software license agreement.
 *****************************************************************************/

#include <linux/device.h>
#include "pip.h"
#include "lookup.h"
#include "parameter.h"
#include "configuration.h"
#include "result.h"
#include "cm_cp_test.h"
#include "../cyttsp5_regs.h"
#include "../cyttsp5_core.h"

#define IDAC_LSB_DEFAULT	80
#define MTX_SUM_DEFAULT		1
#define CLK_DEFAULT		48
#define VREF_DEFAULT     2

#define CONVERSION_CONST	1000
#define MAX_READ_LENGTH     100

//#define ARRAY_SIZE(x)		sizeof(x)/sizeof(x[0])

#define ABS(x)			(((x) < 0) ? -(x) : (x))

//#define LOG_IS_NOT_SHOW_RAW_DATA

static struct rx_attenuator_lookup rx_attenuator_lookup_table[] =
    {RX_ATTENUATOR_LOOKUP_TABLE};

static int8_t mtx_sum_lookup_table[] =
    {MTX_SUM_LOOKUP_TABLE};

static int selfcap_signal_swing_lookup_table[] =
    {SELFCAP_SIGNAL_SWING_LOOKUP_TABLE};


static int rx_attenuator_lookup(uint8_t index, int *value)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(rx_attenuator_lookup_table); i++)
        if (index == rx_attenuator_lookup_table[i].index) {
            *value = rx_attenuator_lookup_table[i].value;
            return 0;
        }

    return -EINVAL;
}

static int mtx_sum_lookup(uint8_t mtx_order, int *mtx_sum)
{
    if (IS_MTX_ORDER_VALID(mtx_order)) {
        *mtx_sum = (int)mtx_sum_lookup_table[
                GET_MTX_SUM_OFFSET(mtx_order)];
        return 0;
    }

    return -EINVAL;
};

static int selfcap_signal_swing_lookup(uint8_t ref_scale, uint8_t rxdac,
		int *vref_low, int *vref_mid, int *vref_high)
{
	if (IS_REF_SCALE_VALID(ref_scale) && IS_RXDAC_VALID(rxdac)) {
        *vref_low = selfcap_signal_swing_lookup_table[
                GET_VREF_LOW_OFFSET(ref_scale, rxdac)];
        *vref_mid = selfcap_signal_swing_lookup_table[
                GET_VREF_MID_OFFSET(ref_scale, rxdac)];

        *vref_high = selfcap_signal_swing_lookup_table[
                GET_VREF_HIGH_OFFSET(ref_scale, rxdac)];

        return 0;
    }

    return -EINVAL;
}

static int get_configuration_parameter(enum parameter_id id,
        enum parameter_type *parameter_type,
        union parameter_value *parameter_value)
{
    uint16_t parameter_address;
    uint16_t parameter_size;
    uint32_t parameter_mask;
    const union parameter_value *enumerated_value;
    uint16_t row_number;
    uint16_t row_offset; 
    uint16_t read_length;
    uint8_t data[ROW_SIZE];
    uint32_t value;
    int ret;
    int i;

    tp_log_debug("%s,id:%d\n", __func__, id);
    ret = parameter_get_info(id, &parameter_address, &parameter_size,
    		&parameter_mask, parameter_type);
    if (ret) {
        tp_log_err("%s:Unable to get parameter info!\n",__func__);
        goto exit;
    }

    row_number = parameter_address / ROW_SIZE;
    row_offset = parameter_address % ROW_SIZE;

    ret = pip_read_data_block(row_number, row_offset + parameter_size,
            CONFIG_BLOCK_ID, &read_length, data);
    if (ret) {
        tp_log_err("%s:Unable to read data block!\n",__func__);
        goto exit;
    }
#ifdef LOG_IS_NOT_SHOW_RAW_DATA
    tp_log_info("%s: data[0..%d]:\n", __func__, read_length - 1);
    for (i = 0; i < read_length; i++)
    {
        tp_log_info("%02X ", data[i]);
    }
    tp_log_info("\n");
#endif

    value = data[row_offset];
    if (parameter_size == 2 || parameter_size == 4)
        value += data[row_offset + 1] << 8;
    if (parameter_size == 4) {
        value += data[row_offset + 2] << 16;
        value += data[row_offset + 3] << 24;
    }

    if (parameter_mask) {
        value &= parameter_mask;
        while ((parameter_mask & 0x01) == 0) {
            value >>= 1;
            parameter_mask >>= 1;
        }
    }

    ret = parameter_get_enumerated_value(id, (int)value,
            &enumerated_value);
    if (ret) {
        tp_log_err("%s:Unable to get parameter enumerated value!\n",__func__);
        goto exit;
    }

    if (enumerated_value)
        memcpy(parameter_value, enumerated_value,
            sizeof(union parameter_value));
    else if (!enumerated_value && *parameter_type == INTEGER)
        parameter_value->integer = (int32_t)value;
    else {
        tp_log_err("%s:Unable to get parameter value!\n",__func__);
        ret = -EINVAL;
    }
    i=0;
exit:
    return ret;
}

/*
 * Wrapper function for pip_retrieve_scan to call it multiple times
 * to gather data if necessary
 */
static int retrieve_panel_scan(uint16_t read_offset, uint16_t read_length,
        uint8_t data_id, uint16_t *actual_read_length,
        uint8_t *data_format, uint8_t *data)
{
    uint16_t actual_read_len;
	uint16_t myread = 0;
    uint16_t total_read_len = 0;
    int ret = 0;

    while (read_length > 0) {
        if (read_length > MAX_READ_LENGTH){
            myread = MAX_READ_LENGTH;
        } else {
            myread = read_length;
        }
        ret = pip_retrieve_panel_scan(read_offset, myread,
                data_id, &actual_read_len, data_format, data);
        if (ret) {
            tp_log_err("%s:Unable to retrieve panel scan!\n",__func__);
            goto exit;
        }
        //mdelay(5000);
        if (actual_read_len == 0)
            break;
        tp_log_debug("%s: read_offset: %d\n", __func__, read_offset);
        tp_log_debug("%s: actual_read_len: %d\n", __func__,
                actual_read_len);
        tp_log_debug("%s: read_length: %d\n", __func__, read_length);
        tp_log_debug("%s: data_format: %02X\n", __func__,
                *data_format);

        read_offset += actual_read_len;
        total_read_len += actual_read_len;

        data += actual_read_len * GET_ELEMENT_SIZE(*data_format);

        read_length -= actual_read_len;
    }

    *actual_read_length = total_read_len;

exit:
    return ret;
}

static int32_t get_element(uint8_t element_size, uint8_t sign, uint8_t *data)
{
    if (element_size == 1) {
        if (sign == SIGN_UNSIGNED){
            return *data;
        }else{
            return (int8_t)*data;
        }
    } else if (element_size == 2) {
        if (sign == SIGN_UNSIGNED){
            return get_unaligned_le16(data);
        }else{
            return (int16_t)get_unaligned_le16(data);
        }
    } else if (element_size == 4) {
        if (sign == SIGN_UNSIGNED){
            return get_unaligned_le32(data);
        }else{
            return (int)get_unaligned_le32(data);
        }
    }

    return 0;
}

static int retrieve_panel_raw_data(uint8_t data_id, uint16_t read_offset,
        uint16_t read_length, uint8_t *data_format, int32_t *raw_data)
{
    uint16_t actual_read_length;
    uint8_t element_size;
    uint8_t sign;
    uint8_t *data;
    int ret;
    int i;
    
    data = kzalloc(read_length * sizeof(uint32_t), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    
    ret = retrieve_panel_scan(read_offset, read_length, data_id,
            &actual_read_length, data_format, data);
    if (ret) {
        tp_log_err("%s:Unable to retrieve panel raw data!\n",__func__);
        goto free;
    }

    if (read_length != actual_read_length) {
        ret = -EINVAL;
        goto free;
    }

    element_size = GET_ELEMENT_SIZE(*data_format);
    if (element_size != 1 && element_size != 2 && element_size != 4) {
        ret = -EINVAL;
        goto free;
    }

    sign = GET_SIGN(*data_format);

    for (i = 0; i < read_length; i++)
        raw_data[i] = get_element(element_size, sign,
                    &data[i << (element_size / 2)]);

free:
    kfree(data);

    return ret;
}


static int calculate_cm_gen6(int raw_data, int tx_period_mutual,
		int balancing_target_mutual, int scaling_factor_mutual,
		int idac_lsb, int idac_mutual, int rx_atten_mutual,
		int gidac_mult, int clk, int vtx, int mtx_sum)
{
	long long i_bias = gidac_mult * idac_lsb * idac_mutual;
	long long adc = (((long long)tx_period_mutual * balancing_target_mutual*scaling_factor_mutual *idac_mutual * rx_atten_mutual) -
						50000 * raw_data) / (scaling_factor_mutual *idac_mutual);

	return i_bias * adc / (clk * vtx * mtx_sum*1000);  //when bypass mode,vtx*1000
}


static int get_cm_uniformity_test_results_gen6(int vdda, uint16_t tx_num,
        uint16_t rx_num,  uint16_t button_num, bool skip_cm_button,
        int32_t **sensor_raw_data,
        int **cm_sensor_data, 
        int *cm_sensor_average)
{
    union parameter_value parameter_value;
    enum parameter_type parameter_type;
    uint16_t read_length;
    uint16_t sensor_element_num = 0;
    uint8_t data_format = 0;
    int8_t idac_mutual;
    int rx_atten_mutual;
    uint32_t tx_period_mutual;
    char* vdda_mode;
    uint32_t scaling_factor_mutual;
    uint32_t balancing_target_mutual;
    uint32_t gidac_mult;
    int vtx;
    int idac_lsb = IDAC_LSB_DEFAULT;
    int mtx_sum = MTX_SUM_DEFAULT;
    int clk = CLK_DEFAULT; 
    uint8_t data[IDAC_AND_RX_ATTENUATOR_CALIBRATION_DATA_LENGTH];
    int ret;
    int i;
    int j;

    sensor_element_num = rx_num * tx_num;
    tp_log_info("%s, get_cm_uniformity_test_results_gen6 called\n", __func__);
    *sensor_raw_data = kzalloc(sensor_element_num * sizeof(int32_t), GFP_KERNEL);
    *cm_sensor_data = kzalloc(sensor_element_num * sizeof(int),GFP_KERNEL);

    if (!*sensor_raw_data || !*cm_sensor_data) {
        ret = -ENOMEM;
        goto exit;
    }

    tp_log_info("%s: Set FORCE_SINGLE_TX to 1\n", __func__);

    /* Step 1: Set force single TX */
    ret = pip_set_parameter(FORCE_SINGLE_TX, 1, 0x01);
    if (ret) {
        tp_log_info("%s:Unable to set FORCE_SINGLE_TX parameter!\n",__func__);
        goto exit;
    }

    /*workaround for CDT193384*/
    ret = pip_resume_scanning();
    if (ret) {
        tp_log_info("%s:Unable to resume panel scan!\n",__func__);
        goto restore_multi_tx;
    }

    ret = pip_suspend_scanning();
    if (ret) {
        tp_log_info("%s:Unable to suspend panel scan!\n",__func__);
        goto restore_multi_tx;
    }
    /*end workaround for CDT193384*/

    tp_log_info("%s: Perform calibrate IDACs\n", __func__);

    /* Step 2: Perform calibration */
    ret = pip_calibrate_idacs(0);
    if (ret) {
        tp_log_info("%s:Unable to calibrate IDACs!\n",__func__);
        goto restore_multi_tx;
    }
    if(button_num > 0)
    {
        ret = pip_calibrate_idacs(1);
        if (ret) {
            tp_log_info("%s Unable to calibrate button IDACs!\n",__func__);
            goto restore_multi_tx;
        }
    }

    tp_log_info("%s: Get Mutual IDAC and RX Attenuator values\n",__func__);

    /* Step 3: Get Mutual IDAC and RX Attenuator values */
    ret = pip_retrieve_data_structure(0,
            IDAC_AND_RX_ATTENUATOR_CALIBRATION_DATA_LENGTH,
            IDAC_AND_RX_ATTENUATOR_CALIBRATION_DATA_ID,
            &read_length, &data_format, data);
    if (ret) {
        tp_log_info("%s:Unable to retrieve data structure!\n",__func__);
        goto restore_multi_tx;
    }

    ret = rx_attenuator_lookup(data[RX_ATTENUATOR_MUTUAL_INDEX],
            &rx_atten_mutual);
    if (ret) {
        tp_log_info("%s:Invalid RX Attenuator Index!\n",__func__);
        goto restore_multi_tx;
    }

    idac_mutual = (int8_t)data[IDAC_MUTUAL_INDEX];

    tp_log_info("%s: idac_mutual: %d\n", __func__, idac_mutual);
    tp_log_info("%s: rx_atten_mutual: %d\n", __func__, rx_atten_mutual);

    /* step 3 Get CDC:VDDA_MODE */
    ret = get_configuration_parameter(VDDA_MODE, &parameter_type,
                &parameter_value);
    if (ret || parameter_type != STRING) {
        tp_log_err("%s:Unable to get vdda_mode!\n",__func__);
        goto restore_multi_tx;
    }
    vdda_mode = parameter_value.string;
    tp_log_info("%s: vdda_mode: %s\n", __func__, vdda_mode);

    if (!strcmp(vdda_mode, VDDA_MODE_BYPASS)) {
        if (vdda != 0)
            vtx = vdda;
        else {
            tp_log_err("%s:VDDA cannot be zero when VDDA_MODE is bypass!\n",__func__);
            ret = -EINVAL;
            goto restore_multi_tx;
        }
    } else if (!strcmp(vdda_mode, VDDA_MODE_PUMP)) {
        /* Get CDC:TX_PUMP_VOLTAGE */
        ret = get_configuration_parameter(TX_PUMP_VOLTAGE,
                &parameter_type, &parameter_value);
        if (ret || parameter_type != FLOAT) {
            tp_log_err("%s:Unable to get tx_pump_voltage!\n", __func__);
            goto restore_multi_tx;
        }
        vtx = parameter_value.flt;
    } else {
        tp_log_err("%s:Invalid VDDA_MODE: %s!\n",__func__, vdda_mode);
        ret = -EINVAL;
        goto restore_multi_tx;
    }

    tp_log_info("%s: vtx: %d\n", __func__, vtx);

    /* Get CDC:TX_PERIOD_MUTUAL */
    ret = get_configuration_parameter(TX_PERIOD_MUTUAL, &parameter_type,
                &parameter_value);
    if (ret || parameter_type != INTEGER) {
        tp_log_err("%s:Unable to get tx_period_mutual!\n",__func__);
        goto restore_multi_tx;
    }

    tx_period_mutual = parameter_value.integer;
    tp_log_info("%s: tx_period_mutual: %d\n", __func__, tx_period_mutual);

    /* Get CDC:SCALING_FACTOR_MUTUAL */
    ret = get_configuration_parameter(SCALING_FACTOR_MUTUAL,
                &parameter_type, &parameter_value);
    if (ret || parameter_type != INTEGER) {
        tp_log_err("%s:Unable to get scaling_factor_mutual!\n", __func__);
        goto restore_multi_tx;
    }

    scaling_factor_mutual = parameter_value.integer;
    tp_log_info("%s: scaling_factor_mutual: %d\n", __func__,
            scaling_factor_mutual);

    /* Get Calibration:BALANCING_TARGET_MUTUAL */
    ret = get_configuration_parameter(BALANCING_TARGET_MUTUAL,
                &parameter_type, &parameter_value);
    if (ret || parameter_type != INTEGER) {
        tp_log_info("%s:Unable to get balancing_target_mutual!\n",__func__);
        goto restore_multi_tx;
    }

    balancing_target_mutual = parameter_value.integer;
    tp_log_info("%s: balancing_target_mutual: %d\n", __func__,
            balancing_target_mutual);

    /* Get CDC:INFRA_CTRL:GIDAC_MULT */
    ret = get_configuration_parameter(GIDAC_MULT,
                &parameter_type, &parameter_value);
    if (ret || parameter_type != INTEGER) {
        tp_log_err("%s:Unable to get gidac_mult!\n",__func__);
        goto restore_multi_tx;
    }

    gidac_mult = parameter_value.integer;

    tp_log_info("%s: gidac_mult:%d\n", __func__, gidac_mult);

    /* Step 4a: Execute Panel Scan */
    ret = pip_execute_panel_scan();
    if (ret) {
        tp_log_err("%s:Unable to execute panel scan!\n",__func__);
        goto restore_multi_tx;
    }

    /* Step 4b: Retrieve Panel Scan raw data */
    ret = retrieve_panel_raw_data(MUTUAL_CAP_RAW_DATA_ID, 0,
            sensor_element_num, &data_format, *sensor_raw_data);
    if (ret) {
        tp_log_err("%s:Unable to retrieve panel raw data!\n",__func__);
        goto restore_multi_tx;
    }

#ifdef LOG_IS_NOT_SHOW_RAW_DATA
    tp_log_info("%s: sensor_raw_data[0..%d]:\n", __func__,
            sensor_element_num - 1);
    for (i = 0; i < tx_num; i++) {
        for (j = 0; j < rx_num; j++)
        {
            tp_log_info("%5d ",
                (*sensor_raw_data)[i * rx_num + j]);
        }
        tp_log_info("\n");
    }
#endif

    /* Step 5 and 6: Calculate Cm_sensor and Cm_sensor_ave */
    *cm_sensor_average = 0;
    for (i = 0; i < sensor_element_num; i++) {
        (*cm_sensor_data)[i] = calculate_cm_gen6((*sensor_raw_data)[i],
            tx_period_mutual, balancing_target_mutual,
            scaling_factor_mutual, idac_lsb, idac_mutual,
            rx_atten_mutual, gidac_mult, clk, vtx, mtx_sum);
        *cm_sensor_average += (*cm_sensor_data)[i];
    }
    *cm_sensor_average /= sensor_element_num;

#ifdef LOG_IS_NOT_SHOW_RAW_DATA
    tp_log_info("%s: cm_sensor_data[0..%d]:\n", __func__,
        sensor_element_num - 1);
    for (i = 0; i < tx_num; i++) {
        for (j = 0; j < rx_num; j++)
        {
            tp_log_info("%d ",
                (*cm_sensor_data)[i * rx_num + j]);
        }
        tp_log_info("\n");
    }
#endif

    tp_log_info("%s: cm_sensor_average: %d\n", __func__,
            *cm_sensor_average);

restore_multi_tx:
	tp_log_info("%s: Set FORCE_SINGLE_TX to 0\n", __func__);

	/* Step 13: Set force single TX */
	ret = pip_set_parameter(FORCE_SINGLE_TX, 1, 0x00);
	if (ret) {
		tp_log_err("%s:Unable to set FORCE_SINGLE_TX parameter!\n",__func__);
		goto exit;
	}

	tp_log_info("%s: Perform calibrate IDACs\n", __func__);

	/* Step 14: Perform calibration */
	ret = pip_calibrate_idacs(0);
	if (ret) {
		tp_log_err("%s:Unable to calibrate mutual IDACs!\n", __func__);
		goto exit;
	}
	if(button_num > 0)
	{
		ret = pip_calibrate_idacs(1);
		if (ret) {
			tp_log_err("%s:Unable to calibrate button IDACs!\n", __func__);
			goto exit;
		}
	}

exit:
	if (ret) {
		kfree(*cm_sensor_data);
		kfree(*sensor_raw_data);
		*cm_sensor_data = NULL;
		*sensor_raw_data = NULL;
	}
	j=0;
	return ret;
}

static int calculate_cm_calibration_gen6(int tx_period_mutual,
		int balancing_target_mutual, int idac_lsb,
		int idac_mutual, int rx_atten_mutual, int gidac_mult,
		int clk, int vtx, int mtx_sum)
{
	int i_bias = gidac_mult * idac_lsb * idac_mutual;
	int t_cal = tx_period_mutual * balancing_target_mutual /clk;

	return ((long long)i_bias * t_cal * rx_atten_mutual )/ (vtx * mtx_sum*1000); //when bypass mode,vtx*1000
}

static int get_cm_calibration_check_test_results_gen6(int vdda,
		bool skip_cm_button,int *cm_sensor_calibration)
{
	union parameter_value parameter_value;
	enum parameter_type parameter_type;
	uint16_t read_length;
	uint8_t data_format;
	int8_t idac_mutual;
	int rx_atten_mutual;
	int mtx_sum;
	char *vdda_mode;
	uint32_t tx_period_mutual;
	uint32_t mtx_order;
	uint32_t balancing_target_mutual;
	uint32_t gidac_mult;
	int vtx;
	int idac_lsb = IDAC_LSB_DEFAULT;
	int clk = CLK_DEFAULT; 
	uint8_t data[IDAC_AND_RX_ATTENUATOR_CALIBRATION_DATA_LENGTH];
	int ret;

	tp_log_info("%s: Get Mutual IDAC and RX Attenuator values\n",
			__func__);

	/* Step 1: Get Mutual IDAC and RX Attenuator values */
	ret = pip_retrieve_data_structure(0,
			IDAC_AND_RX_ATTENUATOR_CALIBRATION_DATA_LENGTH,
			IDAC_AND_RX_ATTENUATOR_CALIBRATION_DATA_ID,
			&read_length, &data_format, data);
	if (ret) {
		tp_log_err("%s:Unable to retrieve data structure!\n",__func__);
		goto exit;
	}

	ret = rx_attenuator_lookup(data[RX_ATTENUATOR_MUTUAL_INDEX],
			&rx_atten_mutual);
	if (ret) {
		tp_log_err("%s:Invalid RX Attenuator Index!\n",__func__);
		goto exit;
	}

	idac_mutual = (int8_t)data[IDAC_MUTUAL_INDEX];

	tp_log_info("%s: idac_mutual: %d\n", __func__, idac_mutual);
	tp_log_info("%s: rx_atten_mutual: %d\n", __func__, rx_atten_mutual);

	/* Get CDC:VDDA_MODE */
	ret = get_configuration_parameter(VDDA_MODE, &parameter_type,
				&parameter_value);
	if (ret || parameter_type != STRING) {
		tp_log_err("%s:Unable to get vdda_mode!\n",__func__);
		goto exit;
	}

	vdda_mode = parameter_value.string;
	tp_log_info("%s: vdda_mode: %s\n", __func__, vdda_mode);

	if (!strcmp(vdda_mode, VDDA_MODE_BYPASS)) {
		if (vdda != 0)
			vtx = vdda;
		else {
			tp_log_err("%s:VDDA cannot be zero when VDDA_MODE is bypass!\n",__func__);
			ret = -EINVAL;
			goto exit;
		}
	} else if (!strcmp(vdda_mode, VDDA_MODE_PUMP)) {
		/* Get CDC:TX_PUMP_VOLTAGE */
		ret = get_configuration_parameter(TX_PUMP_VOLTAGE,
				&parameter_type, &parameter_value);
		if (ret || parameter_type != FLOAT) {
			tp_log_err("%s:Unable to get tx_pump_voltage!\n",__func__);
			goto exit;
		}
		vtx = parameter_value.flt;
	} else {
		tp_log_err("%s:Invalid VDDA_MODE: %s!\n",__func__, vdda_mode);
		ret = -EINVAL;
		goto exit;
	}

	tp_log_info("%s: vtx: %d\n", __func__, vtx);

	/* Get CDC:TX_PERIOD_MUTUAL */
	ret = get_configuration_parameter(TX_PERIOD_MUTUAL, &parameter_type,
				&parameter_value);
	if (ret || parameter_type != INTEGER) {
		tp_log_err("%s:Unable to get tx_period_mutual!\n",__func__);
		goto exit;
	}

	tx_period_mutual = parameter_value.integer;
	tp_log_info("%s: tx_period_mutual: %d\n", __func__, tx_period_mutual);

	/* Get CDC:MTX_ORDER */
	ret = get_configuration_parameter(MTX_ORDER, &parameter_type,
				&parameter_value);
	if (ret || parameter_type != INTEGER) {
		tp_log_err("%s:Unable to get mtx_order!\n",__func__);
		goto exit;
	}

	mtx_order = parameter_value.integer;
	tp_log_info("%s: mtx_order: %d\n", __func__,  mtx_order);

	ret = mtx_sum_lookup(mtx_order, &mtx_sum);
	if (ret) {
		tp_log_err("%s:Invalid MTX Order Index!\n",__func__);
		goto exit;
	}

	tp_log_info("%s: mtx_sum: %d\n", __func__, mtx_sum);

	/* Get Calibration:BALANCING_TARGET_MUTUAL */
	ret = get_configuration_parameter(BALANCING_TARGET_MUTUAL,
				&parameter_type, &parameter_value);
	if (ret || parameter_type != INTEGER) {
		tp_log_err("%s:Unable to get balancing_target_mutual!\n",__func__);
		goto exit;
	}

	balancing_target_mutual = parameter_value.integer;
	tp_log_info("%s: balancing_target_mutual: %d\n", __func__,
			balancing_target_mutual);

	/* Get CDC:INFRA_CTRL:GIDAC_MULT */
	ret = get_configuration_parameter(GIDAC_MULT,
				&parameter_type, &parameter_value);
	if (ret || parameter_type != INTEGER) {
		tp_log_err("%s:Unable to get gidac_mult!\n",__func__);
		goto exit;
	}

	gidac_mult = parameter_value.integer;
	tp_log_info("%s: gidac_mult: %d\n", __func__, gidac_mult);

	*cm_sensor_calibration = calculate_cm_calibration_gen6(tx_period_mutual,
			balancing_target_mutual, idac_lsb, idac_mutual,
			rx_atten_mutual, gidac_mult, clk, vtx, mtx_sum);

	tp_log_info("%s: cm_sensor_calibration: %d\n", __func__,
			*cm_sensor_calibration);

exit:

	return ret;
}

static int validate_cm_test_results_gen6(struct configuration *configuration,
		struct result *result, uint32_t tx_num, uint32_t rx_num,
		uint32_t button_num, bool skip_cm_button,
		int *cm_sensor_data, 
		int cm_sensor_calibration, 
		int cm_sensor_average, 
		int cm_sensor_delta, 
		int **cm_sensor_column_delta, int **cm_sensor_row_delta,
		bool *pass)
{
	uint32_t sensor_num = tx_num * rx_num;
	uint32_t configuration_sensor_num;
	uint32_t configuration_button_num;
	int ret = 0;
	int i, j;

	*cm_sensor_column_delta = kzalloc(rx_num * tx_num *sizeof(float), GFP_KERNEL);
	*cm_sensor_row_delta = kzalloc(tx_num * rx_num  * sizeof(float), GFP_KERNEL);
	if (!*cm_sensor_column_delta || !*cm_sensor_row_delta) {
		ret = -ENOMEM;
		goto exit;
	}

	configuration_sensor_num =
			configuration->cm_min_max_table_sensor_size / 2;
	configuration_button_num =
			configuration->cm_min_max_table_button_size / 2;

	if (configuration_sensor_num != sensor_num) {
		tp_log_err("%s: Configuration and Device number of sensors mismatch! (Configuration:%d, Device:%d)\n",
				__func__, configuration_sensor_num,
				sensor_num);
		ret = -EINVAL;
		goto exit;
	}

	if (!skip_cm_button && (button_num != configuration_button_num)) {
		tp_log_err("%s: Configuration and Device number of buttons mismatch! (Configuration:%d, Device:%d)\n",
				__func__, configuration_button_num,
				button_num);
		ret = -EINVAL;
		goto exit;
	}

	/* Check each sensor Cm data for min/max values */
	result->cm_sensor_validation_pass = true;
	for (i = 0; i < sensor_num; i++) {
		int row=i % rx_num;
		int col=i / rx_num;
		int cm_sensor_min =
			configuration->cm_min_max_table_sensor[(row*tx_num+col)*2];
		int cm_sensor_max =
			configuration->cm_min_max_table_sensor[(row*tx_num+col)*2 + 1];
		if ((cm_sensor_data[i] < cm_sensor_min)
				|| (cm_sensor_data[i] > cm_sensor_max)) {
			tp_log_err("%s: Sensor[%d,%d]:%d (%d,%d)\n",
					"Cm sensor min/max test",
					row,col, 
					(int)cm_sensor_data[i],
					cm_sensor_min, cm_sensor_max);
			result->cm_sensor_validation_pass = false;
		}
	}

	/* Check each row Cm data with neighbor for difference */
	result->cm_sensor_row_delta_pass = true;
	for (i = 0; i < tx_num; i++) {
		for (j = 1; j < rx_num; j++) {
			int cm_sensor_row_diff =
				ABS((int)cm_sensor_data[i * rx_num + j] -
					(int)cm_sensor_data[i * rx_num + j - 1]);
			(*cm_sensor_row_delta)[i * rx_num + j - 1] =
					cm_sensor_row_diff;
			if (cm_sensor_row_diff
					> configuration->cm_range_limit_row) {
				tp_log_err("%s: Sensor[%d,%d]:%d (%d)\n",
					"Cm sensor row range limit test",
					j, i,
					cm_sensor_row_diff,
					configuration->cm_range_limit_row);
				result->cm_sensor_row_delta_pass = false;
			}
		}
	}

	/* Check each column Cm data with neighbor for difference */
	result->cm_sensor_col_delta_pass = true;
	for (i = 1; i < tx_num; i++) {
		for (j = 0; j < rx_num; j++) {
			int cm_sensor_col_diff =
				ABS((int)cm_sensor_data[i * rx_num + j] -
					(int)cm_sensor_data[(i - 1) * rx_num + j]);
			(*cm_sensor_column_delta)[(i - 1) * rx_num + j] =
					cm_sensor_col_diff;
			if (cm_sensor_col_diff
					> configuration->cm_range_limit_col) {
				tp_log_err("%s: Sensor[%d,%d]:%d (%d)\n",
					"Cm sensor column range limit test",
					j, i,
					cm_sensor_col_diff,
					configuration->cm_range_limit_col);
				result->cm_sensor_col_delta_pass = false;
			}
		}
	}

	/* Check sensor calculated Cm for min/max values */
	result->cm_sensor_calibration_pass = true;
	if (cm_sensor_calibration < configuration->cm_min_limit_cal
			|| cm_sensor_calibration >
				configuration->cm_max_limit_cal) {
		tp_log_err("%s: Cm_cal:%d (%d,%d)\n",
				"Cm sensor Cm_cal min/max test",
				cm_sensor_calibration,
				configuration->cm_min_limit_cal,
				configuration->cm_max_limit_cal);
		result->cm_sensor_calibration_pass = false;
	}

	/* Check sensor Cm delta for range limit */
	result->cm_sensor_delta_pass = true;
	if (cm_sensor_delta > configuration->cm_max_delta_sensor_percent) {
		tp_log_err("%s: Cm_sensor_delta:%d (%d)\n",
 				"Cm sensor delta range limit test",
				cm_sensor_delta,
				configuration->cm_max_delta_sensor_percent);
		result->cm_sensor_delta_pass = false;
	}

	result->cm_test_pass = result->cm_sensor_validation_pass
			& result->cm_sensor_row_delta_pass
			& result->cm_sensor_col_delta_pass
			& result->cm_sensor_calibration_pass
			& result->cm_sensor_delta_pass;
	tp_log_info("cm_test_pass=%d \n", result->cm_test_pass);
exit:
	if (ret) {
		kfree(*cm_sensor_row_delta);
		kfree(*cm_sensor_column_delta);
		*cm_sensor_row_delta = NULL;
		*cm_sensor_column_delta = NULL;
	} else if (pass)
		*pass = result->cm_test_pass;
	return ret;
}


static int calculate_cp_calibration_gen6(int tx_period_self,
		int balancing_target_self, int idac_lsb, int idac_self,
		int rx_atten_self, int gidac_mult, int clk,
		int v_swing)
{
	int i_bias = gidac_mult * idac_lsb * idac_self;
	int t_cal = tx_period_self * balancing_target_self* CONVERSION_CONST / (1000 * clk);

	return i_bias * t_cal * rx_atten_self / v_swing;
}

static int calculate_cp_gen6(int raw_data, int tx_period_self,
		int balancing_target_self, int scaling_factor_self,
		int idac_lsb, int idac_self, int rx_atten_self,
		int gidac_mult, int clk, int v_swing)
{
	int i_bias = gidac_mult * idac_lsb * idac_self;
	int adc = tx_period_self * balancing_target_self / 100 +
				500 * raw_data / (scaling_factor_self * idac_self *rx_atten_self);

	return i_bias * adc * rx_atten_self / (clk * v_swing/100);
}

static int get_cp_calibration_check_test_results_gen6(
		uint16_t tx_num, uint16_t rx_num, uint16_t button_num,
		bool skip_cp_sensor, bool skip_cp_button,
		int32_t **sensor_tx_raw_data, int32_t **sensor_rx_raw_data,
		int **cp_sensor_tx_data,
		int **cp_sensor_rx_data,
		int *cp_sensor_tx_average, int *cp_sensor_rx_average,
		int *cp_sensor_rx_calibration,
		int *cp_sensor_tx_calibration)
{
	union parameter_value parameter_value;
	enum parameter_type parameter_type;
	uint16_t read_length;
	uint8_t data_format;
	int8_t idac_self_rx;
	int8_t idac_self_tx;
	uint8_t rxdac;
	uint8_t ref_scale;
	int rx_atten_self_rx;
	int rx_atten_self_tx;
	int vref_low;
	int vref_mid;
	int vref_high;
	int v_swing;
	uint32_t tx_period_self;
	uint32_t scaling_factor_self;
	uint32_t balancing_target_self;
	uint32_t gidac_mult;
	int idac_lsb = IDAC_LSB_DEFAULT;
	int clk = CLK_DEFAULT; 
	uint8_t data[IDAC_AND_RX_ATTENUATOR_CALIBRATION_DATA_LENGTH];
	int ret;
	int i;

	/* Get CDC:REFGEN_CTL:RXDAC */
	ret = get_configuration_parameter(RXDAC, &parameter_type,
			&parameter_value);
	if (ret || parameter_type != INTEGER) {
		tp_log_err("%s:Unable to get rxdac!\n",__func__);
		goto exit;
	}

	rxdac = parameter_value.integer;
	tp_log_info("%s: rxdac: %d\n", __func__, rxdac);

	ret = get_configuration_parameter(REF_SCALE, &parameter_type,
			&parameter_value);
	if (ret || parameter_type != INTEGER) {
		tp_log_err("%s:Unable to get rxdac!\n",__func__);
		goto exit;
	}

	ref_scale = parameter_value.integer;
	tp_log_info("%s: ref_scale: %d\n", __func__, ref_scale);

	ret = selfcap_signal_swing_lookup(ref_scale, rxdac,
			&vref_low, &vref_mid, &vref_high);
	if (ret) {
		tp_log_err("%s:Invalid ref_scale or rxdac!\n",__func__);
		goto exit;
	}

	tp_log_info("%s: vref_low: %d\n", __func__, vref_low);
	tp_log_info("%s: vref_mid: %d\n", __func__, vref_mid);
	tp_log_info("%s: vref_high: %d\n", __func__, vref_high);

	v_swing = vref_high - vref_low;

	tp_log_info("%s: v_swing: %d\n", __func__, v_swing);

	/* Get CDC:INFRA_CTRL:GIDAC_MULT */
	ret = get_configuration_parameter(GIDAC_MULT, &parameter_type,
			&parameter_value);
	if (ret || parameter_type != INTEGER) {
		tp_log_err("%s:Unable to get infra_ctrl!\n",__func__);
		goto exit;
	}

	gidac_mult = parameter_value.integer;
	tp_log_info("%s: gidac_mult: %d\n", __func__, gidac_mult);
	/* Step 1: Perform calibration */
	ret = pip_calibrate_idacs(0);
	if (ret) {
		tp_log_err("%s:Unable to calibrate self IDACs!\n", __func__);
		goto exit;
	}
	if(button_num > 0)
	{
		ret = pip_calibrate_idacs(1);
		if (ret) {
			tp_log_err("%s:Unable to calibrate button IDACs!\n", __func__);
			goto exit;
		}
	}

	tp_log_info("%s: Get Self IDAC and RX Attenuator values\n", __func__);

	/* Step 2: Get Self IDAC and RX Attenuator Self RX values */
	ret = pip_retrieve_data_structure(0,
			IDAC_AND_RX_ATTENUATOR_CALIBRATION_DATA_LENGTH,
			IDAC_AND_RX_ATTENUATOR_CALIBRATION_DATA_ID,
			&read_length, &data_format, data);
	if (ret) {
		tp_log_err("%s:Unable to retrieve data structure!\n",__func__);
		goto exit;
	}

	/* Step 4a: Execute Panel Scan */
	ret = pip_execute_panel_scan();
	if (ret) {
		tp_log_err("%s:Unable to execute panel scan!\n",__func__);
		goto exit;
	}

	if (skip_cp_sensor)
		goto process_cp_button;

	/* Allocate sensor rx and tx buffers */
	*sensor_tx_raw_data = kzalloc(tx_num * sizeof(int32_t), GFP_KERNEL);
	*sensor_rx_raw_data = kzalloc(rx_num * sizeof(int32_t), GFP_KERNEL);
	*cp_sensor_tx_data = kzalloc(tx_num * sizeof(float), GFP_KERNEL);
	*cp_sensor_rx_data = kzalloc(rx_num * sizeof(float), GFP_KERNEL);
	if (!*sensor_tx_raw_data || !*sensor_rx_raw_data
			|| !*cp_sensor_tx_data || !*cp_sensor_rx_data) {
		ret = -ENOMEM;
		goto free_buffers;
	}

	/* Step 4b: Retrieve Panel Scan raw data */
	ret = retrieve_panel_raw_data(SELF_CAP_RAW_DATA_ID, 0, rx_num,
			&data_format, *sensor_rx_raw_data);
	if (ret) {
		tp_log_err("%s:Unable to retrieve panel raw data!\n",__func__);
		goto free_buffers;
	}

	ret = retrieve_panel_raw_data(SELF_CAP_RAW_DATA_ID, rx_num, tx_num,
			&data_format, *sensor_tx_raw_data);
	if (ret) {
		tp_log_err("%s:Unable to retrieve panel raw data!\n",__func__);
		goto free_buffers;
	}

	ret = rx_attenuator_lookup(data[RX_ATTENUATOR_SELF_RX_INDEX],
			&rx_atten_self_rx);
	if (ret) {
		tp_log_err("%s:Invalid RX Attenuator Index!\n",__func__);
		goto free_buffers;
	}

	idac_self_rx = (int8_t)data[IDAC_SELF_RX_INDEX];

	ret = rx_attenuator_lookup(data[RX_ATTENUATOR_SELF_TX_INDEX],
			&rx_atten_self_tx);
	if (ret) {
		tp_log_err("%s:Invalid RX Attenuator Index!\n",__func__);
		goto free_buffers;
	}

	idac_self_tx = (int8_t)data[IDAC_SELF_TX_INDEX];

	tp_log_info("%s: idac_self_rx: %d\n", __func__, idac_self_rx);
	tp_log_info("%s: rx_atten_self_rx: %d\n", __func__, rx_atten_self_rx);
	tp_log_info("%s: idac_self_tx: %d\n", __func__, idac_self_tx);
	tp_log_info("%s: rx_atten_self_tx: %d\n", __func__, rx_atten_self_tx);

	/* Get CDC:TX_PERIOD_SELF */
	ret = get_configuration_parameter(TX_PERIOD_SELF, &parameter_type,
				&parameter_value);
	if (ret || parameter_type != INTEGER) {
		tp_log_err("%s:Unable to get tx_period_self!\n",__func__);
		goto free_buffers;
	}

	tx_period_self = parameter_value.integer;
	tp_log_info("%s: tx_period_self: %d\n", __func__, tx_period_self);

	/* Get CDC:SCALING_FACTOR_SELF */
	ret = get_configuration_parameter(SCALING_FACTOR_SELF, &parameter_type,
				&parameter_value);
	if (ret || parameter_type != INTEGER) {
		tp_log_err("%s:Unable to get scaling_factor_self!\n",__func__);
		goto free_buffers;
	}

	scaling_factor_self = parameter_value.integer;
	tp_log_info("%s: scaling_factor_self: %d\n", __func__,
			scaling_factor_self);

	/* Get Calibration:BALANCING_TARGET_SELF */
	ret = get_configuration_parameter(BALANCING_TARGET_SELF,
				&parameter_type, &parameter_value);
	if (ret || parameter_type != INTEGER) {
		tp_log_err("%s:Unable to get balancing_target_self!\n",__func__);
		goto free_buffers;
	}

	balancing_target_self = parameter_value.integer;
	tp_log_info("%s: balancing_target_self: %d\n", __func__,
			balancing_target_self);

	*cp_sensor_rx_calibration = calculate_cp_calibration_gen6(tx_period_self,
			balancing_target_self, idac_lsb, idac_self_rx,
			rx_atten_self_rx, gidac_mult, clk, v_swing);

	tp_log_info("%s: cp_sensor_rx_calibration: %d\n", __func__,
			*cp_sensor_rx_calibration);

	*cp_sensor_tx_calibration = calculate_cp_calibration_gen6(tx_period_self,
			balancing_target_self, idac_lsb, idac_self_tx,
			rx_atten_self_tx, gidac_mult, clk, v_swing);

	tp_log_info("%s: cp_sensor_tx_calibration: %d\n", __func__,
			*cp_sensor_tx_calibration);

#ifdef LOG_IS_NOT_SHOW_RAW_DATA
	tp_log_info("%s: sensor_rx_raw_data[0..%d]:\n", __func__, rx_num - 1);
	for (i = 0; i < rx_num; i++){
		tp_log_info("%5d ", (*sensor_rx_raw_data)[i]);}
	tp_log_info("\n");
#endif
	*cp_sensor_rx_average = 0;
	for (i = 0; i < rx_num; i++) {
		(*cp_sensor_rx_data)[i] = calculate_cp_gen6((*sensor_rx_raw_data)[i],
				tx_period_self, balancing_target_self,
				scaling_factor_self, idac_lsb, idac_self_rx,
				rx_atten_self_rx, gidac_mult, clk, v_swing);
		*cp_sensor_rx_average += (*cp_sensor_rx_data)[i];
	}
	*cp_sensor_rx_average /= rx_num;
#ifdef LOG_IS_NOT_SHOW_RAW_DATA
	tp_log_info("%s: cp_sensor_rx_data[0..%d]:\n", __func__, rx_num - 1);
	for (i = 0; i < rx_num; i++){
		tp_log_info("%d ", (*cp_sensor_rx_data)[i]);}
	tp_log_info("\n");
#endif
	tp_log_info("%s: cp_sensor_rx_average: %d\n", __func__,
			*cp_sensor_rx_average);
#ifdef LOG_IS_NOT_SHOW_RAW_DATA
	tp_log_info("%s: sensor_tx_raw_data[0..%d]:\n", __func__, tx_num - 1);
	for (i = 0; i < tx_num; i++)
	{
		tp_log_info("%5d ", (*sensor_tx_raw_data)[i]);
	}

#endif
	*cp_sensor_tx_average = 0;
	for (i = 0; i < tx_num; i++) {
		(*cp_sensor_tx_data)[i] = calculate_cp_gen6((*sensor_tx_raw_data)[i],
				tx_period_self, balancing_target_self,
				scaling_factor_self, idac_lsb, idac_self_tx,
				rx_atten_self_tx, gidac_mult, clk, v_swing);
		*cp_sensor_tx_average += (*cp_sensor_tx_data)[i];
	}
	*cp_sensor_tx_average /= tx_num;
#ifdef LOG_IS_NOT_SHOW_RAW_DATA
	tp_log_info("%s: cp_sensor_tx_data[0..%d]:\n", __func__, tx_num - 1);
	for (i = 0; i < tx_num; i++)
	{
		tp_log_info("%d ", (*cp_sensor_tx_data)[i]);
	}
	tp_log_info("\n");
#endif
	tp_log_info("%s: cp_sensor_tx_average: %d\n", __func__,
			*cp_sensor_tx_average);

process_cp_button:
	if (skip_cp_button)
		goto exit;
free_buffers:
	if (ret) {
		if (!skip_cp_sensor) {
			kfree(*sensor_rx_raw_data);
			kfree(*sensor_tx_raw_data);
			kfree(*cp_sensor_rx_data);
			kfree(*cp_sensor_tx_data);
			*sensor_rx_raw_data = NULL;
			*sensor_tx_raw_data = NULL;
			*cp_sensor_rx_data = NULL;
			*cp_sensor_tx_data = NULL;
		}
	}

exit:
	return ret;
}

static int validate_cp_test_results_gen6(struct configuration *configuration,
		struct result *result, uint32_t tx_num, uint32_t rx_num,
		bool skip_cp_sensor,
		int *cp_sensor_rx_data, int *cp_sensor_tx_data,
		int cp_sensor_rx_calibration,
		int cp_sensor_tx_calibration,
		int cp_sensor_rx_average, int cp_sensor_tx_average,
		int cp_sensor_rx_delta,
		int cp_sensor_tx_delta,
		bool *pass)
{
	uint32_t i=0;
	uint32_t configuration_rx_num;
	uint32_t configuration_tx_num;
	result->cp_test_pass = true;
	configuration_rx_num=configuration->cp_min_max_table_rx_size/2;
	configuration_tx_num=configuration->cp_min_max_table_tx_size/2;

	/* Check Sensor Cp delta for range limit */
	result->cp_sensor_delta_pass = true;
	if ((cp_sensor_rx_delta > configuration->cp_max_delta_sensor_rx_percent)
			|| (cp_sensor_tx_delta >
				configuration->cp_max_delta_sensor_tx_percent)) {
		tp_log_err("%s: Cp_sensor_rx_delta:%d(%d) Cp_sensor_tx_delta:%d (%d)\n",
				"Cp sensor delta range limit test",
				cp_sensor_rx_delta,
				configuration->cp_max_delta_sensor_rx_percent,
				cp_sensor_tx_delta,
				configuration->cp_max_delta_sensor_tx_percent);
		result->cp_sensor_delta_pass = false;
	}

	/* Check sensor Cp rx for min/max values */
	result->cp_rx_validation_pass = true;
	for (i = 0; i < configuration_rx_num; i++) {
		int cp_rx_min =
			configuration->cp_min_max_table_rx[i * 2];
		int cp_rx_max =
			configuration->cp_min_max_table_rx[i * 2 + 1];
		if ((cp_sensor_rx_data[i] <= cp_rx_min)
				|| (cp_sensor_rx_data[i] >= cp_rx_max)) {
			tp_log_err("%s: Cp Rx[%d]:%d (%d,%d)\n",
					"Cp Rx min/max test",
					i,
					(int)cp_sensor_rx_data[i],
					cp_rx_min, cp_rx_max);
			result->cp_rx_validation_pass = false;
		}
	}
	/* Check sensor Cp tx for min/max values */
	result->cp_tx_validation_pass = true;
	for (i = 0; i < configuration_tx_num; i++) {
		int cp_tx_min =
			configuration->cp_min_max_table_tx[i * 2];
		int cp_tx_max =
			configuration->cp_min_max_table_tx[i * 2 + 1];
		if ((cp_sensor_tx_data[i] <= cp_tx_min)
				|| (cp_sensor_tx_data[i] >= cp_tx_max)) {
			tp_log_err("%s: Cp Tx[%d]:%d(%d,%d)\n",
					"Cp Tx min/max test",
					i,
					(int)cp_sensor_tx_data[i],
					cp_tx_min, cp_tx_max);
			result->cp_tx_validation_pass = false;
		}
	}
		
	result->cp_test_pass &= result->cp_sensor_delta_pass
			& result->cp_rx_validation_pass
			& result->cp_tx_validation_pass;

	if (pass)
		*pass = result->cp_test_pass;

	return 0;
}

static int get_extra_parameter(uint32_t* sensor_assignment,uint32_t * config_ver,uint32_t * revision_ctrl,uint32_t * device_id_high,uint32_t * device_id_low){
    union parameter_value parameter_value;
    enum parameter_type parameter_type;
    struct pip_sysinfo sysinfo;
    int i;
    int ret;
    *device_id_high=0;
    *device_id_low=0;
    /* Get Device Setup:SENSOR_ASSIGNMENT */
    ret = get_configuration_parameter(SENSOR_ASSIGNMENT, &parameter_type,
                &parameter_value);
    if (ret || parameter_type != STRING) {
        tp_log_err("%s:Unable to get sensor assignment!\n",__func__);
        goto exit;
    }
    *sensor_assignment = !strcmp(parameter_value.string,RX_IS_Y);
    tp_log_info("%s: sensor_assignment: %d\n", __func__, *sensor_assignment);
    
    /* Get config version and revision control version */
    ret=pip_get_system_information(&sysinfo);
    if(ret){
        tp_log_err("%s:Unable to get system information!\n",__func__);
        goto exit;
    }
    *revision_ctrl=sysinfo.cy_data.fw_revctrl;
    *config_ver =sysinfo.cy_data.fw_ver_conf;
    tp_log_info("%s: config_ver: %d\n", __func__, *config_ver);
    tp_log_info("%s: revision_ctrl: %d\n", __func__, *revision_ctrl);
    for(i=0;i<4;i++){
        *device_id_low=(*device_id_low<<8)+sysinfo.cy_data.mfg_id[i];
    }
    for(i=4;i<8;i++){
        *device_id_high=(*device_id_high<<8)+sysinfo.cy_data.mfg_id[i];
    }
    tp_log_info("%s: device_id_low: 0x%x\n", __func__, *device_id_low);
    tp_log_info("%s: device_id_high: 0x%x\n", __func__, *device_id_high);
    return 0;
exit:
    return ret;
}
static int check_tests(uint32_t *tx_num, uint32_t *rx_num, uint32_t *button_num,
        bool *skip_cm_button, bool *skip_cp_sensor,
        bool *skip_cp_button)
{
    union parameter_value parameter_value;
    enum parameter_type parameter_type;
    uint32_t act_lft_en_enabled;
    uint32_t bl_h2o_rjct_enabled;
    char *scanning_mode_button;
    int ret;

    /* Get CDC:TX_NUM */
    ret = get_configuration_parameter(TX_NUM, &parameter_type,
                &parameter_value);
    if (ret || parameter_type != INTEGER) {
        tp_log_err("%s:Unable to get tx_num!\n",__func__);
        goto exit;
    }

    *tx_num = parameter_value.integer;
    tp_log_info("%s: tx_num: %d\n", __func__, *tx_num);

    /* Get CDC:RX_NUM */
    ret = get_configuration_parameter(RX_NUM, &parameter_type,
                &parameter_value);
    if (ret || parameter_type != INTEGER) {
        tp_log_err("%s:Unable to get rx_num!\n",__func__);
        goto exit;
    }

    *rx_num = parameter_value.integer;
    tp_log_info("%s: rx_num: %d\n", __func__, *rx_num);

    /* Get CDC:BUTTON_NUM */
    ret = get_configuration_parameter(BUTTON_NUM, &parameter_type,
                &parameter_value);
    if (ret || parameter_type != INTEGER) {
        tp_log_err("%s:Unable to get button_num!\n",__func__);
        goto exit;
    }

    *button_num = parameter_value.integer;
    tp_log_info("%s: button_num: %d\n", __func__, *button_num);

    /* Get FingerTracking:ACT_LFT_EN */
    ret = get_configuration_parameter(ACT_LFT_EN, &parameter_type,
                &parameter_value);
    if (ret || parameter_type != STRING) {
        tp_log_err("%s:Unable to get act_lft_en!\n",__func__);
        goto exit;
    }

    act_lft_en_enabled = !strcmp(parameter_value.string,
                ACT_LFT_EN_ENABLED);
    tp_log_info("%s: act_lft_en: %d\n", __func__, act_lft_en_enabled);

    /* Get ScanFiltering:BL_H20_RJCT */
    ret = get_configuration_parameter(BL_H20_RJCT, &parameter_type,
                &parameter_value);
    if (ret || parameter_type != STRING) {
        tp_log_err("%s:Unable to get bl_h2o_rjct!\n",__func__);
        goto exit;
    }

    bl_h2o_rjct_enabled = !strcmp(parameter_value.string,
                BL_H20_RJCT_ENABLED);
    tp_log_info("%s: bl_h2o_rjct: %d\n", __func__, bl_h2o_rjct_enabled);

    /* Get CDC:SCANNING_MODE_BUTTON */
    ret = get_configuration_parameter(SCANNING_MODE_BUTTON,
                &parameter_type, &parameter_value);
    if (ret < 0|| parameter_type != STRING) {
        tp_log_err("%s:Unable to get scanning_mode_button! ret = %d\n",__func__, ret);
        goto exit;
    }
    scanning_mode_button = parameter_value.string;
    tp_log_info("%s: scanning_mode_button: %s\n", __func__,
            scanning_mode_button);

    *skip_cm_button = false;
    *skip_cp_button = false;
    *skip_cp_sensor = false;

    if (*button_num == 0) {
        *skip_cm_button = true;
        *skip_cp_button = true;
    } else if (!strcmp(scanning_mode_button,
                SCANNING_MODE_BUTTON_MUTUAL))
        *skip_cp_button = true;
    else if (!strcmp(scanning_mode_button, SCANNING_MODE_BUTTON_SELF))
        *skip_cm_button = true;

    if (!act_lft_en_enabled && !bl_h2o_rjct_enabled)
        *skip_cp_sensor = true;

exit:
    return ret;
}

int cm_cp_test_run(char *device_path, struct file *parameter_file,
        struct file *configuration_file, struct seq_file *result_file, int vdda,
        bool run_cm_test, bool run_cp_test, bool *cm_test_pass,
        bool *cp_test_pass)
{
    struct configuration *configuration;
    struct result *result;
    //char tmp_buf[5];
    uint32_t tx_num, rx_num, button_num;
    uint32_t sensor_assignment=0;//if tx is column, then sensor_assignment is 1;
    uint32_t config_ver,revision_ctrl,device_id_high,device_id_low;
    bool skip_cm_button, skip_cp_button, skip_cp_sensor;
    int32_t *cm_sensor_raw_data = NULL;
    int *cm_sensor_data = NULL;
    int *cm_sensor_column_delta = NULL, *cm_sensor_row_delta = NULL;
    struct gd_sensor *cm_gradient_col = NULL, *cm_gradient_row = NULL;
    int cm_sensor_average;
    int cm_sensor_calibration;
    int cm_sensor_delta;
    int32_t *cp_sensor_rx_raw_data = NULL, *cp_sensor_tx_raw_data = NULL;
    int *cp_sensor_rx_data = NULL, *cp_sensor_tx_data = NULL;
    int cp_sensor_rx_calibration, cp_sensor_tx_calibration;
    int cp_sensor_rx_average, cp_sensor_tx_average;
    int cp_sensor_rx_delta, cp_sensor_tx_delta;
    int ret = 0;
    tp_log_info("%s: begin\n" ,__func__ );
    
    configuration = kzalloc(sizeof(struct configuration), GFP_KERNEL);
    result = kzalloc(sizeof(struct result), GFP_KERNEL);
    if (!configuration || !result) {
        ret = -ENOMEM;
        tp_log_err("%s: malloc configuration result fail\n" ,__func__ );
        seq_printf(result_file, "2F -software_reason");
        goto exit;
    }

    memset(configuration, 0, sizeof(struct configuration));
    memset(result, 0, sizeof(struct result));

    ret = parameter_init(parameter_file);
    if (ret) {
        tp_log_err("%s: Fail initing parameters!\n" ,__func__ );
        seq_printf(result_file, "2F -software_reason");
        goto exit;
    }

    ret = configuration_get(configuration_file, configuration);
    if (ret) {
        tp_log_err("%s: Fail getting configuration\n" ,__func__ );
        seq_printf(result_file, "2F -software_reason");
        goto parameter_exit;
    }

    ret = pip_init(device_path, HID_DESCRIPTOR_REGISTER);
    if (ret) {
        tp_log_err("%s: Unable to init pip!\n" ,__func__ );
        seq_printf(result_file, "2F -software_reason");
        goto parameter_exit;
    }

    /* Suspend scanning */
    ret = pip_suspend_scanning();
    if (ret) {
        tp_log_err("%s: Unable to suspend scanning!\n" ,__func__ );
        seq_printf(result_file, "2F -software_reason");
        goto pip_exit;
    }

    ret = check_tests(&tx_num, &rx_num, &button_num,
            &skip_cm_button, &skip_cp_sensor, &skip_cp_button);
    if (ret) {
        tp_log_err("%s: Unable to check_tests !\n" ,__func__ );
        seq_printf(result_file, "2F -software_reason");
        goto resume_scanning;
    }
    /*add get_extra_parameter item*/
    ret=get_extra_parameter(&sensor_assignment,&config_ver,&revision_ctrl,&device_id_high,&device_id_low);
    if (ret) {
        tp_log_err("%s:Unable to get_extra_parameter\n",__func__);
        seq_printf(result_file, "2F -software_reason");
    }
    else{
        tp_log_info("%s: get_extra_parameter pass\n", __func__);
        result->sensor_assignment=sensor_assignment;
        result->config_ver=config_ver;
        result->revision_ctrl=revision_ctrl;
        result->device_id_high=device_id_high;
        result->device_id_low=device_id_low;
    }
    /*add short test item*/
    result->short_test_pass=true;
    ret = pip_short_test();
    if (ret) {
        result->short_test_pass=false;
        tp_log_err("%s:Unable to do short test\n",__func__);
        goto resume_scanning;
    }
    else{
        tp_log_info("%s: short tets pass\n", __func__);
    }
    seq_printf(result_file, "2P-");
    if (run_cp_test && skip_cp_sensor)
        tp_log_info("%s:Cp sensor test is skipped\n",__func__);
    if (run_cp_test && skip_cp_button)
        tp_log_info("%s:Cp button test is skipped\n",__func__);
    if (run_cm_test && skip_cm_button)
        tp_log_info("%s:Cm button test is skipped\n",__func__);

    if (run_cm_test) {
        ret = get_cm_uniformity_test_results_gen6(vdda, tx_num, rx_num,
                button_num, skip_cm_button,
                &cm_sensor_raw_data,
                &cm_sensor_data, 
                &cm_sensor_average);
        if (ret) {
            seq_printf(result_file, "3F -software_reason");
            tp_log_err("%s:Unable to run Cm uniformity test gen6!\n",__func__);
            goto err_get_cm_test_results;
        }

        ret = get_cm_calibration_check_test_results_gen6(vdda,
                skip_cm_button, &cm_sensor_calibration);
        if (ret) {
            seq_printf(result_file, "3F -software_reason");
            tp_log_err("%s:Unable to run Cm calibration check test gen6!\n",__func__);
            goto free_buffers;
        }
	cm_sensor_delta = ABS((cm_sensor_average -
				cm_sensor_calibration)*100/cm_sensor_average);
	tp_log_info("%s: cm_sensor_delta: %d\n", __func__,
			cm_sensor_delta);
	ret = validate_cm_test_results_gen6(configuration, result, tx_num,
			rx_num, button_num, skip_cm_button,
			cm_sensor_data,
			cm_sensor_calibration,
			cm_sensor_average,
			cm_sensor_delta,
			&cm_sensor_column_delta, &cm_sensor_row_delta,
            cm_test_pass);
        if (ret) {
            seq_printf(result_file, "3F -software_reason");
			tp_log_err("%s:Fail validating Cm test results!\n",__func__);
			goto free_buffers;
        }

        result->cm_sensor_data = cm_sensor_data;
        result->cm_sensor_raw_data = cm_sensor_raw_data;
		result->cm_sensor_column_delta = cm_sensor_column_delta;
		result->cm_sensor_row_delta = cm_sensor_row_delta;
		result->cm_sensor_calibration = cm_sensor_calibration;
		result->cm_sensor_average = cm_sensor_average;
		result->cm_sensor_delta = cm_sensor_delta;
    }
    
    if (run_cp_test) {
		ret = get_cp_calibration_check_test_results_gen6(tx_num, rx_num,
				button_num, skip_cp_sensor, skip_cp_button,
	       	    &cp_sensor_tx_raw_data, &cp_sensor_rx_raw_data,
				&cp_sensor_tx_data,
				&cp_sensor_rx_data, 
				&cp_sensor_tx_average, &cp_sensor_rx_average,
				&cp_sensor_rx_calibration,
				&cp_sensor_tx_calibration);
        if (ret) {
            seq_printf(result_file, "4F -software_reason");
            tp_log_err("%s:Unable to run Cp calibration check test!\n",__func__);
			goto free_buffers;
        }

        if (!skip_cp_sensor) {
			cp_sensor_rx_delta = ABS((cp_sensor_rx_calibration -
						cp_sensor_rx_average) *100/
						cp_sensor_rx_average);
			cp_sensor_tx_delta = ABS((cp_sensor_tx_calibration -
						cp_sensor_tx_average)*100 /
						cp_sensor_tx_average);
			tp_log_info("cp_sensor_rx_calibration: %d,cp_sensor_rx_average:%d\n", cp_sensor_rx_calibration,cp_sensor_rx_average);
			tp_log_info("cp_sensor_tx_calibration: %d,cp_sensor_tx_average:%d\n", cp_sensor_tx_calibration,cp_sensor_tx_average);
			tp_log_info("%s: cp_sensor_rx_delta: %d\n", __func__,
					cp_sensor_rx_delta);
			tp_log_info("%s: cp_sensor_tx_delta: %d\n", __func__,
					cp_sensor_tx_delta);
            result->cp_sensor_rx_data = cp_sensor_rx_data;
            result->cp_sensor_tx_data = cp_sensor_tx_data;
            result->cp_sensor_rx_raw_data = cp_sensor_rx_raw_data;
            result->cp_sensor_tx_raw_data = cp_sensor_tx_raw_data;
			result->cp_sensor_rx_delta = cp_sensor_rx_delta;
			result->cp_sensor_tx_delta = cp_sensor_tx_delta;
            result->cp_sensor_rx_average =
				cp_sensor_rx_average;
            result->cp_sensor_tx_average =
					cp_sensor_tx_average;
			result->cp_sensor_rx_calibration=
				cp_sensor_rx_calibration;
			result->cp_sensor_tx_calibration=
				cp_sensor_tx_calibration;
        }
    	ret = validate_cp_test_results_gen6(configuration, result, tx_num,
		    	rx_num, skip_cp_sensor,
		    	cp_sensor_rx_data,
                cp_sensor_tx_data,
		    	cp_sensor_rx_calibration,
		    	cp_sensor_tx_calibration,
			    cp_sensor_rx_average, cp_sensor_tx_average,
		    	cp_sensor_rx_delta,
		    	cp_sensor_tx_delta,
                cp_test_pass);
        if (ret) {
            seq_printf(result_file, "7F -software_reason");
            tp_log_err("%s:Fail validating Cp test results!\n",__func__);
            goto free_buffers;
        }


    result->tx_num = tx_num;
    result->rx_num = rx_num;
    result->button_num = button_num;

    result->cm_test_run = run_cm_test;
    result->cp_test_run = run_cp_test;
    result->test_summary= result->cm_test_pass && result->cp_test_pass;

    tp_log_info("%s,get over\n", __func__);
}
    ret = result_save(result_file, configuration, result);
    if (ret) {
        tp_log_err("%s:Fail saving result\n",__func__);
        goto free_buffers;
    }

free_buffers:

    if (run_cp_test) {
        if (!skip_cp_sensor) {
            kfree(cp_sensor_rx_raw_data);
            cp_sensor_rx_raw_data = NULL;

            kfree(cp_sensor_tx_raw_data);
            cp_sensor_tx_raw_data = NULL;

            kfree(cp_sensor_rx_data);
            cp_sensor_rx_data = NULL;

            kfree(cp_sensor_tx_data);
            cp_sensor_tx_data = NULL;
        }
    }
err_get_cm_test_results:    
    if (run_cm_test) {
        kfree(cm_sensor_raw_data);
        cm_sensor_raw_data = NULL;

        kfree(cm_sensor_data);
        cm_sensor_data = NULL;

        kfree(cm_gradient_col);
        cm_gradient_col = NULL;

        kfree(cm_gradient_row);
        cm_gradient_row = NULL;
        
    }

resume_scanning:
    if (pip_resume_scanning()) {
        tp_log_err("%s:Unable to resume scanning!\n",__func__);
        goto pip_exit;
    }

pip_exit:
    pip_exit();

parameter_exit:
    parameter_exit();

exit:
    kfree(result);
    result = NULL;

    kfree(configuration);
    configuration = NULL;

    return ret;
}
