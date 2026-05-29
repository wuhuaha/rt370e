#include "sitronix_ts.h"
#include <linux/export.h>

// #define SITRONIX_TEST_ST7123
//#define SITRONIX_TEST_ST7121P
//#define SITRONIX_TEST_ST7121PH
//#define SITRONIX_TEST_ST7123P
//#define SITRONIX_TEST_ST77921
#define SITRONIX_TEST_ST7102

#ifdef SITRONIX_TEST_ST7123
#include "sitronix_ts_test_st7123.h"
#elif defined(SITRONIX_TEST_ST7121P)
#include "sitronix_ts_test_st7121p.h"
#elif defined(SITRONIX_TEST_ST7121PH)
#include "sitronix_ts_test_st7121ph.h"
#elif defined(SITRONIX_TEST_ST7123P)
#include "sitronix_ts_test_st7123p.h"
#elif defined(SITRONIX_TEST_ST77921)
#include "sitronix_ts_test_st77921.h"
#elif defined(SITRONIX_TEST_ST7102)
#include "sitronix_ts_test_st7102.h"
#else
#error "Self-Test not defined!"
#endif

#ifdef ST_SELFTEST_EN_GKI
#include <linux/vmalloc.h>
#include <linux/string.h>
#endif

#define TEST_RETRY_MAX		3


#ifdef ST_SELFTEST_LOG_FILE

char *output_log_buff = NULL;
static int output_log_buff_offset = 0;

#ifdef ST_SELFTEST_EN_GKI
#define ST_OUTPUT_LOG_BUFF_SIZE	(1024 * 1024) //1MB

int st_self_test_malloc_output_log(void)
{
	if(output_log_buff == NULL){
		output_log_buff = vmalloc(ST_OUTPUT_LOG_BUFF_SIZE);	
		if(output_log_buff == NULL){
			sterr("vmalloc for self test log buffer failed\n");
			return -ENOMEM;
		}
	}
	memset(output_log_buff, 0, ST_OUTPUT_LOG_BUFF_SIZE);
	return 0;
}	

void st_self_test_free_output_log(void)
{
	if(output_log_buff != NULL){
		vfree(output_log_buff);
		output_log_buff = NULL;
	}
}

char *st_self_test_get_output_log(void)
{	
	if(output_log_buff == NULL){
		return "No self-test log available\n";
	}
	else{
		return output_log_buff;
	}
}

#define WRITE_LOG(filp, ppos, format, ...)		\
	do {										\
		if (output_log_buff) {					\
			int remain = ST_OUTPUT_LOG_BUFF_SIZE - output_log_buff_offset; \
			int len = snprintf(output_log_buff + output_log_buff_offset, (remain > 0 ? remain :0), format, ## __VA_ARGS__);	\
			output_log_buff_offset += (len > 0) ? len : 0; \
		}									\
	} while (0)

#else
static char log_data[150];
int st_self_test_malloc_output_log(void)
{
	return 0;
}	

void st_self_test_free_output_log(void)
{
	return;
}

char *st_self_test_get_output_log(void)
{
	return ST_SELFTEST_LOG_PATH;
}

#define WRITE_LOG(filp, ppos, format, ...)						\
	do {										\
		if (filp) {								\
			snprintf(log_data, sizeof(log_data), format, ## __VA_ARGS__);	\
			sitronix_vfswrite(filp, log_data, strlen(log_data), ppos);	\
		}									\
	} while (0)
#endif	//end of ST_SELFTEST_EN_GKI

#else	//#ifdef ST_SELFTEST_LOG_FILE
#define WRITE_LOG(filp, ppos, format, ...)

int st_self_test_malloc_output_log(void)
{
	return 0;
}	

void st_self_test_free_output_log(void)
{
	return;
}

char *st_self_test_get_output_log(void)
{
	return NULL;
}
#endif	//end of ST_SELFTEST_LOG_FILE

typedef struct sensing_setting
{
	uint8_t row_cnt;	   // stmsg("ROW_CNT : %d\n",tMode[2]);
	uint8_t aa_unit;	   // stmsg("AA_UNIT : %d\n",tMode[1]);
	uint8_t self_unit;	   // stmsg("SELF_UNIT : %d\n",tMode[5]);
	uint8_t noise_unit;	   // stmsg("NOISE_UNIT : %d\n",tMode[4]);
	uint8_t cmnc_ch_wr_en; // stmsg("CMNC_CH_WR_EN : %d\n",tMode[7]);
	uint8_t key;
} sensing_setting_t;

static sensing_setting_t	m_sensing_setting;

int st_get_afe_sensing_settings(sensing_setting_t *sensing_setting, unsigned char chip_id);

//#define __RAW_DATA_DEBUG__

// #define USE_ST_SQRT
#ifdef USE_ST_SQRT
int st_sqrt(int x);
#endif

static int get_cmd_data(AFE_CMD_T *code, uint8_t *data_buf, int data_buf_len)
{
	int len = 0;
	int i;

	// stmsg("%s: %u\n", __func__, max_len);

	if (!code)
	{
		return 0;
	}

	for (i = 0, len = 0; i < (data_buf_len >> 1); i++)
	{
		if (code[i].type == 0x02)
		{
			data_buf[(i * 2)] = (uint8_t)((code[i].value & 0xFF00) >> 8);
			data_buf[(i * 2) + 1] = (uint8_t)(code[i].value & 0x00FF);
			len += 2;
		}
		else
		{
			break;
		}
	}

	return len;
}

#if 0
int print_code(uint8_t *code, int max_len)
{
	int i, j;
	int len = 0;
	int data_offset = 0;

	for (i = 0, j = 1; i < max_len; i++, j++) {
		if (code[i] == 0x53 && code[i+1] == 0x54 && code[i+2] == 0x01) {
			data_offset = i + 6;
			len = get_data_len(&code[data_offset], max_len - data_offset);
		}

		if (i == data_offset) {
			printk("0x%02X,", (len & 0xFF00) >> 8);
			if (j%16 == 0) {
				printk("\\\n");
			}
			j++;
			printk("0x%02X,", (len & 0x00FF));
			if (j%16 == 0) {
				printk("\\\n");
			}
			j++;
			
			data_offset = -1;
		}

		printk("0x%02X,", code[i]);
		if (j%16 == 0)
			printk("\\\n");
	}

	printk("\n");

	return 0;
}
#endif // 0

uint8_t cmd_data_buf[1024];
int st_address_mode_hardcode_write_v2(AFE_CMD_T *code, int max_len) // This is for string macros.
{
	int ret = 0;
	int i;
	int nWriteCommCount = 0;
	uint16_t len = 0;
	uint16_t delay = 0;
	uint32_t addr = 0;

	// stmsg("%s: max_len = %u\n", __func__, max_len);

	if (!code)
	{
		return 0;
	}

	// print_code(code, max_len);

	for (i = 0; i < max_len; i++)
	{
		switch (code[i].type)
		{
		case 0x01:
			addr = code[i].value & 0xFFFFFF;
			len = get_cmd_data(&code[i + 1], cmd_data_buf, sizeof(cmd_data_buf));
			ret = sitronix_spi_pram_rw(false, addr, cmd_data_buf, NULL, len);
			nWriteCommCount++;
#if 0
			stmsg("write addr: 0x%06X , len :%x ", addr, len);		
			for (j = 0; j < 2; j++) {
					printk("0x%02X, ", cmd_data_buf[j]);
				}	
			printk("\r\n");
			
			stmsg("write addr: %06X , len :%x ", addr, len);			
			if (len) {
				int j;
				stmsg("write data: ");
				for (j = 0; j < len; j++) {
					printk("0x%02X, ", cmd_data_buf[j]);
				}
				printk("\n");
			}
#endif // 0
			break;
		case 0x02:
			break;
		case 0x03:
			delay = code[i].value;
// stmsg("delay 0x%x ms \n", delay);
#if 0
			stmsg("delay 0x%x ms \n", delay);
#endif // 0
			msleep(delay);
			break;
		default:
			i = max_len;
			addr = code[i].value & 0xFFFFFF;
			stmsg("unknown code: 0x%x, Addr:0x%06X\n", code[i].type, addr);
			break;
		}
	}
	stmsg("%s WriteComm Count: %d commands.\n", __FUNCTION__, nWriteCommCount);
	return ret;
}

int st_address_mode_hardcode_write(void *pcode, int max_len)
{
	int ret = 0;
	uint8_t *code = (uint8_t *)pcode;

	if (!code)
	{
		return 0;
	}

	ret = st_address_mode_hardcode_write_v2((AFE_CMD_T *)pcode, max_len);
	return ret;
}

bool st_get_test_enable(int col, int row)
{
	int index = col * 4 + row / 8;
	unsigned char mask = 0x80 >> (row % 8);

	if (index < sizeof(test_disable_sensor))
		return !(test_disable_sensor[index] & mask);
	else
		return false;
}

/* Reload FW */
int sitronix_ts_test_fw_init(void)
{
	int ret = 0;

#ifndef SITRONIX_TP_WITH_FLASH
	ret = sitronix_do_upgrade_hostdownload();
	if (ret >= 0)
		ret = 0;
#else //SITRONIX_TP_WITH_FLASH
	sitronix_ts_reset_device(gts);
#endif //SITRONIX_TP_WITH_FLASH

	msleep(100);

	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
bool sitonix_createlogfileok = true; // FIH self test open log file success or not

void sitronix_vfswrite(struct file *filp, char *buf, int str_len, loff_t *ppos)
{
	if (sitonix_createlogfileok)
	{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 14, 13)
		vfs_write(filp, buf, str_len, ppos);
#else
		kernel_write(filp, buf, str_len, ppos);
#endif
	}
}
#endif /* ST_SELFTEST_LOG_FILE */


#ifdef ST_SELFTEST_LOG_FILE
int st7102_STD(char func, int skipcol, struct file *filp, loff_t *ppos)
#else
int st7102_STD(char func, int skipcol)
#endif
{
		//unsigned char cmd[0x08];
	int tMode[8];
	int read_len;
	int ret = 0;
	unsigned char *raw_buf = NULL;
	signed short *rawI = NULL;
	signed short *rawP = NULL;
	int frameCounter;
	int retryCounter;
	int max_retry = 50; // 5;
	unsigned char raw_dat_rd_on[2] = {0x02, 0x00};
	unsigned char raw_dat_rd_off[2] = {0x00, 0x00};
	unsigned char raw_header[18],cmd[0xFF];
	unsigned char frame_counter = 0;
	int total_error = 0;
	int error_count = 0;
	int count_frame = 1;
	int buf_index, aa_index;
	int col, row ;
	int nRealRow = gts->ts_dev_info.y_chs;
	int nRealCol;
	int percentage = 0;
	int *rawS = NULL; // Raw data STD for STD test
	int raw_avg;
	unsigned long sqrt;
	int min = 0xFFF0, max = -0xFFF0;
	
	signed short rawdata;
#ifdef __RAW_DATA_DEBUG__
	// int i, j, k;
#endif //__RAW_DATA_DEBUG__

	st_get_afe_sensing_settings(&m_sensing_setting, gts->ts_dev_info.chip_id);
	/* ROW_CNT */
	sitronix_spi_pram_rw(true, 0xF04A, NULL, cmd, 2);
	tMode[2] = cmd[0] & 0x3F;
	nRealRow = gts->ts_dev_info.y_chs;
	tMode[2] = nRealRow;
	/* CMNC_CH_WR_EN */
	tMode[7] = cmd[1] & 0x01;
	tMode[7] = 0x00;
	/* AA_UNIT */
	sitronix_spi_pram_rw(true, 0xF024, NULL, cmd, 2);
	tMode[1] = (cmd[0] & 0xF0) >> 3;
	tMode[1] = gts->ts_dev_info.x_chs;
	nRealCol = tMode[1];
	// tMode[1] = 16;
	nRealCol = gts->ts_dev_info.x_chs; // tMode[1];
	tMode[1] = gts->ts_dev_info.x_chs;
	/* SELF_UNIT */
	tMode[5] = (cmd[0] & 0xC) >> 1;

	/* NOISE_UNIT */
	// tMode[4] = (cmd[0]&0x3)<<2;
	tMode[4] = 0x00; // for ST7102

	/* KEY */
	tMode[3] = 0;

	stmsg("ROW_CNT : %d, RealRow : %d\n", tMode[2], nRealRow);
	stmsg("AA_UNIT : %d, RealCol : %d\n", tMode[1], nRealCol);
	stmsg("SELF_UNIT : %d\n", tMode[5]);
	stmsg("NOISE_UNIT : %d\n", tMode[4]);
	stmsg("CMNC_CH_WR_EN : %d\n", tMode[7]);

	read_len = 18 * 32 * 2; //(tMode[2]+tMode[7]) * (tMode[1] + tMode[5] + tMode[4] + skipcol) *2;
	raw_buf = (unsigned char *)kmalloc(read_len, GFP_KERNEL);
	rawI = (signed short *)kmalloc((tMode[2]) * sizeof(short), GFP_KERNEL);

	// Ignore
	frameCounter = 0;
	retryCounter = 0;

	while (ST_SELFTEST_IGNORE_FRAME > 0 && retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);

#ifdef __RAW_DATA_DEBUG__
#if 0
		// Read Rawdata.
		memset(raw_buf, 0, read_len);
		sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
		for (i = 0, j = 0; j < (tMode[1] + tMode[5] + tMode[4] + skipcol); j++) {
			stmsg("AA_UNIT[%d]: ", j);
			for (k = 0; k < (tMode[2] + tMode[7]); i++, k++) {
				short raw = (short)(raw_buf[i*2] << 8 | raw_buf[i*2 + 1]);
				printk("%d ", raw);
			}
			printk(" (i=%d, k=%d)\n", i, k);
		}
#endif // 0
#endif //__RAW_DATA_DEBUG__

		stmsg("header %x \n", raw_header[1]);
		if (frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];
		}

		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

		if (frameCounter >= ST_SELFTEST_IGNORE_FRAME)
			break;
	}

	if (retryCounter >= max_retry)
	{
		if (func == 1 || func == 2)
		{
			sterr("st short test fail ,  can't wait IRQ \n");
			WRITE_LOG(filp, ppos, "st short test fail ,  can't wait IRQ \n");
		}
		else if (func == 4)
		{
			sterr("st STD test fail ,  can't wait IRQ \n");
			WRITE_LOG(filp, ppos, "st STD test fail ,  can't wait IRQ \n");
		}
		ret = -1;
		goto st_open_short_test_finish;
	}

	// STD test
	if (func == 4)
	{
		retryCounter = 0;
		count_frame = ST_SELFTEST_STD_FRAME_CNT;
		rawS = (int *)kmalloc((tMode[2] * tMode[1]) * sizeof(int), GFP_KERNEL);
		while (retryCounter++ < max_retry)
		{
			rawP = (signed short *)kmalloc((tMode[2] * tMode[1] * ST_SELFTEST_STD_FRAME_CNT) * sizeof(short), GFP_KERNEL);
			if (rawP != NULL)
				break;
		}

		if (retryCounter >= max_retry)
		{
			sterr("st STD test fail for can not kmalloc\n");
			WRITE_LOG(filp, ppos, "st STD test fail for can not kmalloc\n");
			ret = -1;
			goto st_open_short_test_finish;
		}
	}

	// raw
	frameCounter = 0;
	retryCounter = 0;

	while (count_frame > 0 && retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);
		if (frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];

			sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
			sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

			aa_index = (tMode[1] / 2) * (tMode[2] + tMode[7]) * 2;
			buf_index = (skipcol) * (tMode[2] + tMode[7]) * 2;

			if (skipcol != 0)
				for (col = 0; col < aa_index; col++)
					raw_buf[aa_index + col] = raw_buf[aa_index + col + buf_index];

			buf_index = 0;
			aa_index = 0;

			// AA A
			for (col = 0; col < tMode[1] / 2; col++)
			{
				error_count = 0;
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					// stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);
					if (func == 1) // short odd
					{
						if (row % 2 == 0)
						{
							rawdata = rawI[row];
							if (rawdata > gts->self_test_short_max)
								error_count++;

							if (rawdata < min)
								min = (int)rawdata;
							if (rawdata > max)
								max = (int)rawdata;
						}
					}
					else if (func == 2) // short even
					{
						if (row % 2 == 1)
						{
							rawdata = rawI[row];
							if (rawdata > gts->self_test_short_max)
								error_count++;

							if (rawdata < min)
								min = (int)rawdata;
							if (rawdata > max)
								max = (int)rawdata;
						}
					}
					else if (func == 4)
					{
						percentage = ((frameCounter - 1) * tMode[1] * tMode[2]) + (col * tMode[2]) + row;
						rawP[percentage] = rawI[row];
						// stmsg("sensor (%2d,%2d) , index %2d RAW (%4d) \n" , col, row, percentage, rawI[row]);
					}

					buf_index += 2;
				}

				if (error_count > ST_SELFTEST_ADJUST_COUNT)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						if (func == 1) // short odd
						{
							if (row % 2 == 0 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
						else if (func == 2) // short even
						{
							if (row % 2 == 1 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
					}
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			// SE A
			for (col = 0; col < tMode[5] / 2; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index += 2;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			// AA B
			for (col = tMode[1] / 2; col < tMode[1]; col++)
			{
				error_count = 0;
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					// stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);
					if (func == 1) // short odd
					{
						if (row % 2 == 0)
						{
							rawdata = rawI[row];
							if (rawdata > gts->self_test_short_max)
								error_count++;

							if (rawdata < min)
								min = (int)rawdata;
							if (rawdata > max)
								max = (int)rawdata;
						}
					}
					else if (func == 2) // short even
					{
						if (row % 2 == 1)
						{
							rawdata = rawI[row];
							if (rawdata > gts->self_test_short_max)
								error_count++;

							if (rawdata < min)
								min = (int)rawdata;
							if (rawdata > max)
								max = (int)rawdata;
						}
					}
					else if (func == 4)
					{
						percentage = ((frameCounter - 1) * tMode[1] * tMode[2]) + (col * tMode[2]) + row;
						rawP[percentage] = rawI[row];
					}

					buf_index += 2;
				}

				if (error_count > ST_SELFTEST_ADJUST_COUNT)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						if (func == 1) // short odd
						{
							if (row % 2 == 0 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
						else if (func == 2) // short even
						{
							if (row % 2 == 1 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
					}
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			// SE B
			for (col = tMode[5] / 2; col < tMode[5]; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index += 2;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			if (func == 2 || func == 1 || func == 0)
			{
				if (func == 0)
				{
					WRITE_LOG(filp, ppos, "[OPEN RAW Data start]\n");
				}
				else if (func == 1)
				{
					WRITE_LOG(filp, ppos, "[SHORT_ODD RAW Data start]        (Min/Max: %d / %d)\n", min, max);
				}
				else if (func == 2)
				{
					WRITE_LOG(filp, ppos, "[SHORT_EVEN RAW Data start]        (Min/Max: %d / %d)\n", min, max);
				}
				buf_index = 0;
				aa_index = 0;
				// AA A
				for (col = 0; col < tMode[1] / 2; col++)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
						if (row == tMode[2] - 1)
							WRITE_LOG(filp, ppos, "%6d \n", rawI[row]);
						else
							WRITE_LOG(filp, ppos, "%6d ", rawI[row]);
						buf_index += 2;
					}

					if (tMode[7] != 0)
						buf_index += 2;
				}
				// SE A
				for (col = 0; col < tMode[5] / 2; col++)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						buf_index += 2;
					}

					if (tMode[7] != 0)
						buf_index += 2;
				}
				// AA B
				for (col = tMode[1] / 2; col < tMode[1]; col++)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
						if (row == tMode[2] - 1)
							WRITE_LOG(filp, ppos, "%6d \n", rawI[row]);
						else
							WRITE_LOG(filp, ppos, "%6d ", rawI[row]);
						buf_index += 2;
					}
					if (tMode[7] != 0)
						buf_index += 2;
				}

				WRITE_LOG(filp, ppos, "[RAW Data end]\n");
			}
		}
		else
		{
			sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);
		}

		if (frameCounter >= count_frame)
			break;
	}

	if (func == 4)
	{
		WRITE_LOG(filp, ppos, "[STD start]\n         ");		
			for (row = 0; row < nRealRow; row++)
			{
				WRITE_LOG(filp, ppos, "X-%02d   ", row);
			}
			WRITE_LOG(filp, ppos, "\n");
		for (col = 0; col < tMode[1]; col++)
		{
			for (row = 0; row < nRealRow ; row++)
			{
				if(row == 0){
					WRITE_LOG(filp, ppos, "Y-%02d  ",col);
				}
				aa_index = col * tMode[2] + row;
				percentage = 0;
				for (frameCounter = 0; frameCounter < ST_SELFTEST_STD_FRAME_CNT; frameCounter++)
				{
					buf_index = frameCounter * tMode[1] * tMode[2];
					percentage += (int)rawP[buf_index + aa_index];
				}

				raw_avg = percentage * 10 / ST_SELFTEST_STD_FRAME_CNT;

				rawS[aa_index] = 0;
				for (frameCounter = 0; frameCounter < ST_SELFTEST_STD_FRAME_CNT; frameCounter++)
				{
					buf_index = frameCounter * tMode[1] * tMode[2];
					if (raw_avg >= (int)(rawP[buf_index + aa_index] * 10))
						percentage = raw_avg - (int)(rawP[buf_index + aa_index] * 10);
					else
						percentage = (int)(rawP[buf_index + aa_index] * 10) - raw_avg;
					if (percentage > ST_SELFTEST_STD_CALCULATE_LIMIT)
						percentage = ST_SELFTEST_STD_CALCULATE_LIMIT;
					rawS[aa_index] += percentage * percentage;
				}
				rawS[aa_index] = rawS[aa_index] / ST_SELFTEST_STD_FRAME_CNT;
#ifndef USE_ST_SQRT
				sqrt = int_sqrt((unsigned long)rawS[aa_index]);
#else
				sqrt = st_sqrt(rawS[aa_index]);
#endif // end of USE_ST_SQRT

				if (row == nRealRow - 1)
					WRITE_LOG(filp, ppos, "%4ld.%1ld \n", sqrt / 10, sqrt % 10); // WRITE_LOG(filp, ppos, "%6d \n", rawS[aa_index]);
				else
					WRITE_LOG(filp, ppos, "%4ld.%1ld ", sqrt / 10, sqrt % 10);

				if (sqrt < min)
					min = (int)sqrt;
				if ((int)sqrt > max)
					max = (int)sqrt;
			}
		}

		for (col = 0; col < nRealCol; col++)
		{
			for (row = 0; row < nRealRow; row++)
			{
				aa_index = col * nRealRow + row;
				if (st_get_test_enable(col, row) && (rawS[aa_index] > gts->self_test_std_square100_max))
				{
					total_error++;
#ifndef USE_ST_SQRT
					sqrt = int_sqrt((unsigned long)rawS[aa_index]);
#else
					sqrt = st_sqrt(rawS[aa_index]);
#endif // end of USE_ST_SQRT
					sterr("sensor (%2d,%2d) STD (%4ld.%1ld) > standard value (%4d.%1d) in STD test\n", col, row, sqrt / 10, sqrt % 10, gts->self_test_std_max / 10, gts->self_test_std_max % 10);
					WRITE_LOG(filp, ppos, "sensor (%2d,%2d) STD (%4ld.%1ld) > standard value (%4d.%1d) in STD test\n", col, row, sqrt / 10, sqrt % 10, gts->self_test_std_max / 10, gts->self_test_std_max % 10);
				}
			}
		}
	WRITE_LOG(filp, ppos, "[STD end]        (Def[Max]: %d.%d, Min/Max: %4ld.%1ld / %4ld.%1ld)\n",
				gts->self_test_std_max / 10, gts->self_test_std_max % 10,(unsigned long)min / 10, (unsigned long)min % 10, (unsigned long)max / 10, (unsigned long)max % 10);
	}

st_open_short_test_finish:

	if (rawI)
		kfree(rawI);

	if (raw_buf)
		kfree(raw_buf);

	if (rawP)
		kfree(rawP);

	if (rawS)
		kfree(rawS);

	if (ret < 0)
		return ret;
	else if (total_error == 0)
		return 0;
	else
		return total_error;
}


#ifdef ST_SELFTEST_LOG_FILE
int st7102_open_Delta_test(char func, int skipcol, struct file *filp, loff_t *ppos)
#else
int st7102_open_Delta_test(char func, int skipcol)
#endif
{
	unsigned char cmd[0x08];
	int tMode[8];
	int read_len;
	unsigned char *raw_buf;
	signed short *rawI;
	signed short *nMuxOnRaw;
	int frameCounter;
	int retryCounter;
	int max_retry = 3;
	unsigned char raw_dat_rd_on[2] = {0x02, 0x00};
	unsigned char raw_dat_rd_off[2] = {0x00, 0x00};
	unsigned char raw_header[18];
	unsigned char frame_counter = 0;
	int total_error = 0;
	int error_count = 0;
	// int count_frame = 1;
	int buf_index, aa_index;
	int nMax = -9999, nMin = 9999;
	int col, row, /*RawType,*/ nRealRow, nRealCol; //,nMuxOnRaw[16*32]={0},nMuxOffRaw[16*32]={0};

	/* ROW_CNT */
	sitronix_spi_pram_rw(true, 0xF04A, NULL, cmd, 2);
	tMode[2] = cmd[0] & 0x3F;
	nRealRow = gts->ts_dev_info.y_chs;
	tMode[2] = 32;
	/* CMNC_CH_WR_EN */
	tMode[7] = cmd[1] & 0x01;
	tMode[7] = 0x00;
	/* AA_UNIT */
	sitronix_spi_pram_rw(true, 0xF024, NULL, cmd, 2);
	tMode[1] = (cmd[0] & 0xF0) >> 3;
	nRealCol = tMode[1];
	// tMode[1] = 16;
	nRealCol = gts->ts_dev_info.x_chs; // tMode[1];
	tMode[1] = gts->ts_dev_info.x_chs;
	/* SELF_UNIT */
	tMode[5] = (cmd[0] & 0xC) >> 1;

	/* NOISE_UNIT */
	// tMode[4] = (cmd[0]&0x3)<<2;
	tMode[4] = 0x00; // for ST7102

	/* KEY */
	tMode[3] = 0;

	stmsg("ROW_CNT : %d, RealRow : %d\n", tMode[2], nRealRow);
	stmsg("AA_UNIT : %d, RealCol : %d\n", tMode[1], nRealCol);
	stmsg("SELF_UNIT : %d\n", tMode[5]);
	stmsg("NOISE_UNIT : %d\n", tMode[4]);
	stmsg("CMNC_CH_WR_EN : %d\n", tMode[7]);

	read_len = 16 * 32 * 2; //(tMode[2]+tMode[7]) * (tMode[1] + tMode[5] + tMode[4] + skipcol) *2;
	raw_buf = kmalloc(read_len * 2, GFP_KERNEL);

	rawI = (signed short *)kmalloc((16 * 32) * sizeof(short), GFP_KERNEL);
	nMuxOnRaw = (signed short *)kmalloc((16 * 32) * sizeof(short), GFP_KERNEL);

	// Ignore
	frameCounter = 0;
	retryCounter = 0;

	while (ST_SELFTEST_IGNORE_FRAME > 0 && retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);

		stmsg("header %x \n", raw_header[1]);
		if (frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];
		}

		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

		if (frameCounter >= ST_SELFTEST_IGNORE_FRAME)
			break;
	}
	// raw
	frameCounter = 0;
	retryCounter = 0;

	// Mux On Start
	stmsg("MuxOn Raw \n");
	msleep(10);
	sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
	sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);
	{
		frameCounter++;
		retryCounter = 0;
		frame_counter = raw_header[1];

		sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len * 2);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

		aa_index = (tMode[1] / 2) * (tMode[2] + tMode[7]) * 2;
		buf_index = (skipcol) * (tMode[2] + tMode[7]) * 2;
		stmsg("aa_index = %d, buf_index = %d\n", aa_index, buf_index);
		if (skipcol != 0)
			for (col = 0; col < aa_index; col++)
				raw_buf[aa_index + col] = raw_buf[aa_index + col + buf_index];

		buf_index = 0;
		aa_index = 0;

		// AA A
		for (col = 0; col < tMode[1]; col++)
		{
			error_count = 0;
			for (row = 0; row < tMode[2]; row++)
			{
				rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
				nMuxOnRaw[col * tMode[2] + row] = rawI[row];
				buf_index += 2;
				// WRITE_LOG(filp, &pos, "RawI[%2d,%2d]=[%4d],RawP[%4d], RawD[%4d]\n",col,row,rawI[row],rawP[row],rawD[row]);
			}
		}
	}	

		// AA A
		error_count = 0;
		for (col = 0; col < tMode[1]; col++)
		{			
			for (row = 0; row < tMode[2]; row++)
			{			
				if ((col < nRealCol) && (row < nRealRow))
				{	
					//stmsg("Raw = %d, Define = %d\n", nMuxOnRaw[col * tMode[2] + row], gts->self_test_open_mux_on_min);				
					if (st_get_test_enable(col, row) && (nMuxOnRaw[col * tMode[2] + row] < gts->self_test_open_mux_on_min)){					
					//if ( (nMuxOnRaw[col * tMode[2] + row] < gts->self_test_open_mux_on_min)){
						error_count++;
						//stmsg("Error = %d\n",error_count);
					}else{
					//	stmsg("\n");
					}
					if (nMin > (nMuxOnRaw[col * tMode[2] + row]))
						nMin = (nMuxOnRaw[col * tMode[2] + row]);
					if (nMax < (nMuxOnRaw[col * tMode[2] + row]))
						nMax = (nMuxOnRaw[col * tMode[2] + row]);
				}
				buf_index += 2;
			}

			if (error_count > ST_SELFTEST_ADJUST_COUNT)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					if ((col < nRealCol) && (row < nRealRow))
					{						
						if (st_get_test_enable(col, row) && (nMuxOnRaw[col * tMode[2] + row] < gts->self_test_open_mux_on_min))
						{
							total_error++;
							sterr("sensor (%2d,%2d) RAW (%4d) is less than min value(< %d) in open test\n", col, row, nMuxOnRaw[col * tMode[2] + row], gts->self_test_open_mux_on_min);
							WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) is less than min value(< %d) in open test\n", col, row, nMuxOnRaw[col * tMode[2] + row], gts->self_test_open_mux_on_min);
						}
					}
				}
			}

			if (tMode[7] != 0)
				buf_index += 2;
		} 

		// SE A
		for (col = 0; col < tMode[5] / 2; col++)
		{
			for (row = 0; row < tMode[2]; row++)
			{
				buf_index += 2;
			}

			if (tMode[7] != 0)
				buf_index += 2;
		}

		WRITE_LOG(filp, ppos, "[OPEN RAW Data start]\n");
		buf_index = 0;
		aa_index = 0;
		// AA A
		//for (RawType = 2; RawType < 3; RawType++)
		//{
			WRITE_LOG(filp, ppos, "\n\n        ");
			for (row = 0; row < nRealRow; row++)
			{
				WRITE_LOG(filp, ppos, "X-%02d   ", row);
			}
			WRITE_LOG(filp, ppos, "\n ");
			for (col = 0; col < tMode[1]; col++)
			{
				for (row = 0; row < nRealRow; row++)
				{
					aa_index = nMuxOnRaw[col * tMode[2] + row];
					if(row == 0){
						WRITE_LOG(filp, ppos, "Y-%02d ", col);
					}
					if (row == (nRealRow - 1))
						WRITE_LOG(filp, ppos, "%6d \n ", aa_index);
					else
						WRITE_LOG(filp, ppos, "%6d ", aa_index);
				}
			}
		//}

		WRITE_LOG(filp, ppos, "[RAW Data end]        (Def/Min/Max: %4d / %4d / %4d)\n", gts->self_test_open_mux_on_min,nMin, nMax);
	

	// st_open_short_test_finish:

	if (rawI)
		kfree(rawI);

	if (nMuxOnRaw)
		kfree(nMuxOnRaw);
	if(error_count>0){
		return -1;
	}else{
		return 0;
	}

}
#ifdef ST_SELFTEST_LOG_FILE
int st7102_test_open(struct file *filp, loff_t *ppos)
#else
int st7102_test_open(void)
#endif
{
	int ret = 0;

	ret = sitronix_ts_test_fw_init();
	if (ret) {
		sterr("FW init failed! (ret=%d)\n", ret);
		return ret;
	}

	stmsg("======ST7102 Open Test=====\r\n");
	// stmsg("======ST7102 Open Test Write Mux On Code Start \r\n=====");
	ret = st_address_mode_hardcode_write(test_cmd_open_mux_on, ARRAY_SIZE(test_cmd_open_mux_on));
	// stmsg("======ST7102 Open Test Write Mux On Code End \r\n=====");
	if (ret < 0)
		return ret;

#ifdef ST_SELFTEST_LOG_FILE
	ret = st7102_open_Delta_test(0, 0, filp, ppos); // for MuxOn Off use
#else
	ret = st7102_open_Delta_test(0, 0); // for MuxOn Off use
#endif
	if(ret != 0 ){
		ret = -1;
	}
	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st7102_Normal_test(char func, int skipcol, struct file *filp, loff_t *ppos)
#else
int st7102_Normal_test(char func, int skipcol)
#endif
{
	unsigned char cmd[0x08];
	int tMode[8];
	int read_len;
	int ret = 0;
	unsigned char *raw_buf;
	signed short *rawI;
	signed short *nMuxOnRaw;
	int frameCounter;
	int retryCounter;
	int max_retry = 5;
	unsigned char raw_dat_rd_on[2] = {0x02, 0x00};
	unsigned char raw_dat_rd_off[2] = {0x00, 0x00};
	unsigned char raw_header[18];
	unsigned char frame_counter = 0;
	int total_error = 0;
	int error_count = 0;
	// int count_frame = 1;
	int buf_index, aa_index;
	int col, row, RawType, nRealRow, nRealCol; //,nMuxOnRaw[16*32]={0},nMuxOffRaw[16*32]={0};


	/* ROW_CNT */
	sitronix_spi_pram_rw(true, 0xF04A, NULL, cmd, 2);
	tMode[2] = cmd[0] & 0x3F;
	nRealRow = gts->ts_dev_info.y_chs;
	tMode[2] = 32;
	/* CMNC_CH_WR_EN */
	tMode[7] = cmd[1] & 0x01;
	tMode[7] = 0x00;
	/* AA_UNIT */
	sitronix_spi_pram_rw(true, 0xF024, NULL, cmd, 2);
	tMode[1] = (cmd[0] & 0xF0) >> 3;
	nRealCol = gts->ts_dev_info.x_chs; // tMode[1];
	tMode[1] = gts->ts_dev_info.x_chs;
	/* SELF_UNIT */
	tMode[5] = (cmd[0] & 0xC) >> 1;

	/* NOISE_UNIT */
	// tMode[4] = (cmd[0]&0x3)<<2;
	tMode[4] = 0x00; // for ST7102

	/* KEY */
	tMode[3] = 0;

	stmsg("ROW_CNT : %d, RealRow : %d\n", tMode[2], nRealRow);
	stmsg("AA_UNIT : %d, RealCol : %d\n", tMode[1], nRealCol);
	stmsg("SELF_UNIT : %d\n", tMode[5]);
	stmsg("NOISE_UNIT : %d\n", tMode[4]);
	stmsg("CMNC_CH_WR_EN : %d\n", tMode[7]);

	read_len = 19 * 32 * 2; //(tMode[2]+tMode[7]) * (tMode[1] + tMode[5] + tMode[4] + skipcol) *2;
	raw_buf = kmalloc(read_len * 2, GFP_KERNEL);

	rawI = (signed short *)kmalloc((19 * 32) * sizeof(short), GFP_KERNEL);
	nMuxOnRaw = (signed short *)kmalloc((19 * 32) * sizeof(short), GFP_KERNEL);

	// Ignore
	frameCounter = 0;
	retryCounter = 0;
	// ST71XX_EnterAddressMode();

	while (ST_SELFTEST_IGNORE_FRAME > 0 && retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);

		stmsg("header %x \n", raw_header[1]);
		if (frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];
		}
		else
		{
			/*
			ST71XX_EnterAddressMode();
			ret = st_address_mode_hardcode_write(test_cmd_normal, sizeof(test_cmd_normal));
			sitronix_spi_pram_rw(true, 0xF000, NULL, raw_header, 18);
			if(raw_header[1]==0x00){
				for(i=0;i<10;i++){
					msleep(100);
					ret = st_address_mode_hardcode_write(test_cmd_normal, sizeof(test_cmd_normal));
					sitronix_spi_pram_rw(true, 0xF000, NULL, raw_header, 18);
					stmsg("0xF000 =  0x%02X\n",raw_header[1]);
					if(raw_header[1]==0x02){
						break;
					}
				}


			}
				*/
		}
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

		if (frameCounter >= ST_SELFTEST_IGNORE_FRAME)
			break;
	}

	// raw
	frameCounter = 0;
	retryCounter = 0;
	frame_counter = 0;

	stmsg("Normal Raw \n");
	msleep(10);
	sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
	sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);
	// if(frame_counter != raw_header[1])
	{
		frameCounter++;
		retryCounter = 0;
		frame_counter = raw_header[1];

		sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len * 2);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

		aa_index = (tMode[1] / 2) * (tMode[2] + tMode[7]) * 2;
		buf_index = (skipcol) * (tMode[2] + tMode[7]) * 2;
		stmsg("aa_index = %d, buf_index = %d\n", aa_index, buf_index);
		if (skipcol != 0)
			for (col = 0; col < aa_index; col++)
				raw_buf[aa_index + col] = raw_buf[aa_index + col + buf_index];

		buf_index = 0;
		aa_index = 0;

		// AA A
		for (col = 0; col < tMode[1]; col++)
		{
			error_count = 0;
			for (row = 0; row < tMode[2]; row++)
			{
				rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
				nMuxOnRaw[col * tMode[2] + row] = rawI[row];
				buf_index += 2;
				// WRITE_LOG(filp, ppos, "RawI[%2d,%2d]=[%4d],RawP[%4d], RawD[%4d]\n",col,row,rawI[row],rawP[row],rawD[row]);
			}
		}
	}
	// if(frame_counter != raw_header[1])
	{
		frameCounter++;
		retryCounter = 0;
		frame_counter = raw_header[1];
		aa_index = (tMode[1] / 2) * (tMode[2] + tMode[7]) * 2;
		buf_index = (skipcol) * (tMode[2] + tMode[7]) * 2;

		// AA A
		for (col = 0; col < tMode[1]; col++)
		{
			error_count = 0;
			for (row = 0; row < tMode[2]; row++)
			{
				if ((col < nRealCol) && (row < nRealRow))
					// if(skipNodeArray[col][row]!=0x01){
					if (st_get_test_enable(col, row) && ((nMuxOnRaw[col * tMode[2] + row] < gts->self_test_normal_min) || (nMuxOnRaw[col * tMode[2] + row] > gts->self_test_normal_max)))
						error_count++;
				//}

				buf_index += 2;
			}

			if (error_count > ST_SELFTEST_ADJUST_COUNT)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					if ((col < nRealCol) && (row < nRealRow))
					{
						// if(skipNodeArray[col][row]!=0x01){
						if (st_get_test_enable(col, row) && ((nMuxOnRaw[col * tMode[2] + row] < gts->self_test_normal_min) || (nMuxOnRaw[col * tMode[2] + row] > gts->self_test_normal_max)))
						{
							total_error++;
							WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) out of range (%d ~ %d) in normal test\n", col, row, nMuxOnRaw[col * tMode[2] + row], gts->self_test_normal_min, gts->self_test_normal_max);
						}
						//}
					}
				}
			}

			if (tMode[7] != 0)
				buf_index += 2;
		}

		// SE A
		for (col = 0; col < tMode[5] / 2; col++)
		{
			for (row = 0; row < tMode[2]; row++)
			{
				buf_index += 2;
			}

			if (tMode[7] != 0)
				buf_index += 2;
		}

		WRITE_LOG(filp, ppos, "[Normal RAW Data start]\n");
		buf_index = 0;
		aa_index = 0;
		// AA A		
		for (RawType = 0; RawType < 1; RawType++)
		{
			WRITE_LOG(filp, ppos, "\n\n         ");			
			for (row = 0; row < nRealRow; row++)
			{
				WRITE_LOG(filp, ppos, "X-%02d   ", row);
			}
			WRITE_LOG(filp, ppos, "\n ");
				
			for (col = 0; col < tMode[1]; col++)
			{
				for (row = 0; row < nRealRow; row++)
				{
					if(row == 0){
					WRITE_LOG(filp, ppos, "Y-%02d  ",col);
					}
					if (RawType == 0)
						aa_index = nMuxOnRaw[col * tMode[2] + row];
					else if (RawType == 1)
						aa_index = nMuxOnRaw[col * tMode[2] + row];
					else if (RawType == 2)
						aa_index = nMuxOnRaw[col * tMode[2] + row];
					if (row == (nRealRow - 1))
						WRITE_LOG(filp, ppos, "%6d \n ", aa_index);
					else
						WRITE_LOG(filp, ppos, "%6d ", aa_index);
				}
			}
		}

		WRITE_LOG(filp, ppos, "[Normal Raw Data end]\n");
	}

	// if (frameCounter >= count_frame)
	// break;
	//}

	// st_open_short_test_finish:

	kfree(rawI);
	kfree(nMuxOnRaw);

	if (ret < 0)
		return ret;
	else if (total_error == 0)
		return 0;
	else
		return total_error;
}

#ifdef ST_SELFTEST_LOG_FILE
int st7102_test_normal(struct file *filp, loff_t *ppos)
#else
int st7102_test_normal(void)
#endif
{
	int ret = 0, i = 0;

	ret = sitronix_ts_test_fw_init();
	if (ret) {
		sterr("FW init failed! (ret=%d)\n", ret);
		return ret;
	}

	msleep(10);
	ret = st_address_mode_hardcode_write(test_cmd_normal_rawdata, ARRAY_SIZE(test_cmd_normal_rawdata));
	if (ret < 0)
		return ret;
	for (i = 0; i < TEST_RETRY_MAX; i++)
	{
#ifdef ST_SELFTEST_LOG_FILE
		ret = st7102_Normal_test(0, 0, filp, ppos);
#else
		ret = st7102_Normal_test(0, 0);
#endif
		if (ret == 0)
			break;
	}
	if(ret != 0 ){
		ret = -1;
	}
	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st7102_short_test(char func, int skipcol, struct file *filp, loff_t *ppos)
#else
int st7102_short_test(char func, int skipcol)
#endif
{
	unsigned char cmd[0x08];
	int tMode[8];
	int read_len;
	int ret = 0, i = 0;
	unsigned char *raw_buf;
	signed short *rawI;
	// signed short *rawP;
	int frameCounter;
	int retryCounter;
	int max_retry = 5;
	unsigned char raw_dat_rd_on[2] = {0x02, 0x00};
	unsigned char raw_dat_rd_off[2] = {0x00, 0x00};
	unsigned char raw_header[18];
	unsigned char frame_counter = 0;
	int total_error = 0;
	int error_count = 0;
	// int count_frame = 1;
	int buf_index, aa_index;
	int col, row, nRealRow, nRealCol;
	// int percentage = 0;
	// int *rawS; //Raw data STD for STD test
	// int raw_avg;
	// unsigned long sqrt;

	/* ROW_CNT */
	sitronix_spi_pram_rw(true, 0xF04A, NULL, cmd, 2);

	/* ROW_CNT */
	sitronix_spi_pram_rw(true, 0xF04A, NULL, cmd, 2);
	tMode[2] = cmd[0] & 0x3F;
	nRealRow = gts->ts_dev_info.y_chs;
	tMode[2] = 32;
	/* CMNC_CH_WR_EN */
	tMode[7] = cmd[1] & 0x01;
	tMode[7] = 0x00;
	/* AA_UNIT */
	sitronix_spi_pram_rw(true, 0xF024, NULL, cmd, 2);
	tMode[1] = (cmd[0] & 0xF0) >> 3;
	tMode[1] = 16;
	nRealCol = tMode[1];
	/* SELF_UNIT */
	tMode[5] = (cmd[0] & 0xC) >> 1;

	/* NOISE_UNIT */
	// tMode[4] = (cmd[0]&0x3)<<2;
	tMode[4] = 0x00; // for ST7102

	/* KEY */
	tMode[3] = 0;
	/* CMNC_CH_WR_EN */
	tMode[7] = cmd[1] & 0x01;
	tMode[7] = 0x00;
	/* AA_UNIT */
	sitronix_spi_pram_rw(true, 0xF024, NULL, cmd, 2);
	// tMode[1] = (cmd[0]&0xF0)>>3;
	if ((func == 1) || (func == 2))
	{
		tMode[1] = tMode[1] + 3;
		// nRealCol = tMode[1];
	}

	/* SELF_UNIT */
	tMode[5] = (cmd[0] & 0xC) >> 1;

	/* NOISE_UNIT */
	// tMode[4] = (cmd[0]&0x3)<<2;
	tMode[4] = 0x00; // for ST7102

	/* KEY */
	tMode[3] = 0;
	// tMode[1] = 16;
	// tMode[2] = 24;
	stmsg("Short Test \n");
	stmsg("ROW_CNT : %d, RealRow : %d\n", tMode[2], nRealRow);
	stmsg("AA_UNIT : %d, RealCol : %d\n", tMode[1], nRealCol);
	stmsg("SELF_UNIT : %d\n", tMode[5]);
	stmsg("NOISE_UNIT : %d\n", tMode[4]);
	stmsg("CMNC_CH_WR_EN : %d\n", tMode[7]);

	read_len = 21 * 32 * 4;
	raw_buf = kmalloc(read_len, GFP_KERNEL);
	rawI = (signed short *)kmalloc((read_len) * sizeof(short), GFP_KERNEL);

	// Ignore
	frameCounter = 0;
	retryCounter = 0;
	sitronix_ts_reset_device(gts);
	if (func == 1)
	{ // odd
		ret = st_address_mode_hardcode_write(test_cmd_short_odd, ARRAY_SIZE(test_cmd_short_odd));
	}
	else if (func == 2)
	{ // even
		ret = st_address_mode_hardcode_write(test_cmd_short_even, ARRAY_SIZE(test_cmd_short_even));
		sitronix_spi_pram_rw(true, 0xF000, NULL, raw_header, 18);
		stmsg("0xF000 =  0x%02X\n", raw_header[1]);
		if (raw_header[1] == 0x00)
		{
			for (i = 0; i < 10; i++)
			{
				msleep(100);
				ret = st_address_mode_hardcode_write(test_cmd_short_even, ARRAY_SIZE(test_cmd_short_even));
				sitronix_spi_pram_rw(true, 0xF000, NULL, raw_header, 18);
				stmsg("0xF000 =  0x%02X\n", raw_header[1]);
				if (raw_header[1] == 0x02)
				{
					break;
				}
			}
		}
	}
	if (ret < 0)
		return ret;
	// msleep(100);

	while (ST_SELFTEST_IGNORE_FRAME > 0 && retryCounter++ < max_retry)
	{
		msleep(20);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);

		stmsg("header %x \n", raw_header[1]);
		if (frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];
		}
		else
		{
			// sitronix_ts_reset_device(gts);
		}
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

		if (frameCounter >= ST_SELFTEST_IGNORE_FRAME)
			break;
	}

	// raw
	frameCounter = 0;
	retryCounter = 0;

	// while (count_frame > 0 && retryCounter++ < max_retry)
	//{
	msleep(10);
	sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
	sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);
	// if(frame_counter != raw_header[1])
	{
		frameCounter++;
		retryCounter = 0;
		frame_counter = raw_header[1];

		sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

		aa_index = (tMode[1] / 2) * (tMode[2] + tMode[7]) * 2;
		buf_index = (skipcol) * (tMode[2] + tMode[7]) * 2;

		if (skipcol != 0)
			for (col = 0; col < aa_index; col++)
				raw_buf[aa_index + col] = raw_buf[aa_index + col + buf_index];

		buf_index = 0;
		aa_index = 0;

		// AA A
		for (col = 0; col < tMode[1] / 2; col++)
		{
			error_count = 0;
			for (row = 0; row < tMode[2]; row++)
			{
				rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
				// stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);

				if (func == 1) // short odd
				{
					if (col != 8 && col != 16 && col != 17)
					{
						if (row % 2 == 0 && rawI[row] > gts->self_test_short_max)
							error_count++;
					}
				}
				else if (func == 2) // short even
				{
					if (col != 8 && col != 16 && col != 17)
					{
						if (row % 2 == 1 && rawI[row] > gts->self_test_short_max)
							error_count++;
					}
				}
				buf_index += 2;
			}

			if (error_count > ST_SELFTEST_ADJUST_COUNT)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					if (func == 1)
					{
						if (col != 8 && col != 16 && col != 17)
						{
							if (row % 2 == 0 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								// sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n" , col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
					}
					else if (func == 2)
					{
						if (col != 8 && col != 16 && col != 17)
						{
							if (row % 2 == 1 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								// sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n" , col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
					}
				}
			}

			if (tMode[7] != 0)
				buf_index += 2;
		}

		// SE A
		for (col = 0; col < tMode[5] / 2; col++)
		{
			for (row = 0; row < tMode[2]; row++)
			{
				buf_index += 2;
			}

			if (tMode[7] != 0)
				buf_index += 2;
		}

		// AA B
		for (col = tMode[1] / 2; col < tMode[1]; col++)
		{
			error_count = 0;
			for (row = 0; row < tMode[2]; row++)
			{
				rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
				// stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);
				if (func == 1) // short odd
				{
					if (col != 8 && col != 16 && col != 17)
					{
						if (row % 2 == 0 && rawI[row] > gts->self_test_short_max)
							error_count++;
					}
				}
				else if (func == 2) // short even
				{
					if (col != 8 && col != 16 && col != 17)
					{
						if (row % 2 == 1 && rawI[row] > gts->self_test_short_max)
							error_count++;
					}
				}
				buf_index += 2;
			}

			if (error_count > ST_SELFTEST_ADJUST_COUNT)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					if (func == 1)
					{
						if (col != 8 && col != 16 && col != 17)
						{
							if (row % 2 == 0 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
					}
					else if (func == 2)
					{
						if (col != 8 && col != 16 && col != 17)
						{
							if (row % 2 == 1 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
					}
				}
			}

			if (tMode[7] != 0)
				buf_index += 2;
		}

		// SE B
		for (col = tMode[5] / 2; col < tMode[5]; col++)
		{
			for (row = 0; row < tMode[2]; row++)
			{
				buf_index += 2;
			}

			if (tMode[7] != 0)
				buf_index += 2;
		}

		if (func == 2 || func == 1)
		{
			if (func == 1)
				WRITE_LOG(filp, ppos, "[SHORT_ODD RAW Data start]\n");
			else if (func == 2)
				WRITE_LOG(filp, ppos, "[SHORT_EVEN RAW Data start]\n");
			buf_index = 0;
			aa_index = 0;
			// AA A
			for (col = 0; col < tMode[1] / 2; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					if (row == tMode[2] - 1)
						WRITE_LOG(filp, ppos, "%6d \n", rawI[row]);
					else
						WRITE_LOG(filp, ppos, "%6d ", rawI[row]);
					buf_index += 2;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}
			// SE A
			for (col = 0; col < tMode[5] / 2; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index += 2;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}
			// AA B
			for (col = tMode[1] / 2; col < tMode[1]; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					if (row == tMode[2] - 1)
						WRITE_LOG(filp, ppos, "%6d \n", rawI[row]);
					else
						WRITE_LOG(filp, ppos, "%6d ", rawI[row]);
					buf_index += 2;
				}
				if (tMode[7] != 0)
					buf_index += 2;
			}

			WRITE_LOG(filp, ppos, "[RAW Data end]\n");
		}
	}
	// else
	//{
	//		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);
	//		}

	// if (frameCounter >= count_frame)
	//	break;
	//	}

	// st_open_short_test_finish:

	kfree(rawI);
	kfree(raw_buf);

	if (ret < 0)
		return ret;
	else if (total_error == 0)
		return 0;
	else
		return total_error;
}

#ifdef ST_SELFTEST_LOG_FILE
int st7102_short_test_even(struct file *filp, loff_t *ppos)
#else
int st7102_short_test_even(void)
#endif
{
	int ret = 0;
	int i;

	for (i = 0; i < TEST_RETRY_MAX; i++) {
#ifdef ST_SELFTEST_LOG_FILE
		ret = st7102_short_test(2, ST_SELFTEST_SKIP_COLS, filp, ppos);
#else
		ret = st7102_short_test(2, ST_SELFTEST_SKIP_COLS);
#endif //ST_SELFTEST_LOG_FILE
		if (ret == 0)
			break;
	}
	if(ret != 0 ){
		ret = -1;
	}
	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st7102_short_test_odd(struct file *filp, loff_t *ppos)
#else
int st7102_short_test_odd(void)
#endif
{
	int ret = 0;
	int i;

	for (i = 0; i < TEST_RETRY_MAX; i++) {
#ifdef ST_SELFTEST_LOG_FILE
		ret = st7102_short_test(1, ST_SELFTEST_SKIP_COLS, filp, ppos);
#else
		ret = st7102_short_test(1, ST_SELFTEST_SKIP_COLS);
#endif //ST_SELFTEST_LOG_FILE
			if (ret == 0)
			break;
	}
	if(ret != 0 ){
		ret = -1;
	}
	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st_open_short_test(char func, int skipcol, struct file *filp, loff_t *ppos)
#else
int st_open_short_test(char func, int skipcol)
#endif
{
	//unsigned char cmd[0x08];
	int tMode[8];
	int read_len;
	int ret = 0;
	unsigned char *raw_buf = NULL;
	signed short *rawI = NULL;
	signed short *rawP = NULL;
	int frameCounter;
	int retryCounter;
	int max_retry = 50; // 5;
	unsigned char raw_dat_rd_on[2] = {0x02, 0x00};
	unsigned char raw_dat_rd_off[2] = {0x00, 0x00};
	unsigned char raw_header[18];
	unsigned char frame_counter = 0;
	int total_error = 0;
	int error_count = 0;
	int count_frame = 1;
	int buf_index, aa_index;
	int col, row;
	int nRealRow = gts->ts_dev_info.y_chs;
	int percentage = 0;
	int *rawS = NULL; // Raw data STD for STD test
	int raw_avg;
	unsigned long sqrt;
	int min = 0xFFF0, max = -0xFFF0;
	signed short rawdata;
#ifdef __RAW_DATA_DEBUG__
	// int i, j, k;
#endif //__RAW_DATA_DEBUG__

	st_get_afe_sensing_settings(&m_sensing_setting, gts->ts_dev_info.chip_id);
	/* ROW_CNT */
	tMode[2] = m_sensing_setting.row_cnt;
	
	/* CMNC_CH_WR_EN */
	tMode[7] = m_sensing_setting.cmnc_ch_wr_en;

	/* AA_UNIT */
	tMode[1] = m_sensing_setting.aa_unit;
	//if((func==1)||(func==2)){
	//	tMode[1]+=skipcol;
	//}
	/* SELF_UNIT */
	tMode[5] = m_sensing_setting.self_unit;

	/* NOISE_UNIT */
	tMode[4] = m_sensing_setting.noise_unit;
	
	/* KEY */
	tMode[3] = m_sensing_setting.key;	

	stmsg("ROW_CNT : %d\n", tMode[2]);
	stmsg("AA_UNIT : %d\n", tMode[1]);
	stmsg("SELF_UNIT : %d\n", tMode[5]);
	stmsg("NOISE_UNIT : %d\n", tMode[4]);
	stmsg("CMNC_CH_WR_EN : %d\n", tMode[7]);

	read_len = (tMode[2] + tMode[7]) * (tMode[1] + tMode[5] + tMode[4] + skipcol) * 2;
	raw_buf = (unsigned char *)kmalloc(read_len, GFP_KERNEL);
	rawI = (signed short *)kmalloc((tMode[2]) * sizeof(short), GFP_KERNEL);

	// Ignore
	frameCounter = 0;
	retryCounter = 0;

	while (ST_SELFTEST_IGNORE_FRAME > 0 && retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);

#ifdef __RAW_DATA_DEBUG__
#if 0
		// Read Rawdata.
		memset(raw_buf, 0, read_len);
		sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
		for (i = 0, j = 0; j < (tMode[1] + tMode[5] + tMode[4] + skipcol); j++) {
			stmsg("AA_UNIT[%d]: ", j);
			for (k = 0; k < (tMode[2] + tMode[7]); i++, k++) {
				short raw = (short)(raw_buf[i*2] << 8 | raw_buf[i*2 + 1]);
				printk("%d ", raw);
			}
			printk(" (i=%d, k=%d)\n", i, k);
		}
#endif // 0
#endif //__RAW_DATA_DEBUG__

		stmsg("header %x \n", raw_header[1]);
		if (frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];
		}

		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);
		
		if (frameCounter >= ST_SELFTEST_IGNORE_FRAME)
			break;
			
	}

	if (retryCounter >= max_retry)
	{
		if (func == 1 || func == 2)
		{
			sterr("st short test fail ,  can't wait IRQ \n");
			WRITE_LOG(filp, ppos, "st short test fail ,  can't wait IRQ \n");
		}
		else if (func == 4)
		{
			sterr("st STD test fail ,  can't wait IRQ \n");
			WRITE_LOG(filp, ppos, "st STD test fail ,  can't wait IRQ \n");
		}
		ret = -1;
		goto st_open_short_test_finish;
	}

	// STD test
	if (func == 4)
	{
		retryCounter = 0;
		count_frame = ST_SELFTEST_STD_FRAME_CNT;
		rawS = (int *)kmalloc((tMode[2] * tMode[1]) * sizeof(int), GFP_KERNEL);
		while (retryCounter++ < max_retry)
		{
			rawP = (signed short *)kmalloc((tMode[2] * tMode[1] * ST_SELFTEST_STD_FRAME_CNT) * sizeof(short), GFP_KERNEL);
			if (rawP != NULL)
				break;
		}

		if (retryCounter >= max_retry)
		{
			sterr("st STD test fail for can not kmalloc\n");
			WRITE_LOG(filp, ppos, "st STD test fail for can not kmalloc\n");
			ret = -1;
			goto st_open_short_test_finish;
		}
	}

	// raw
	frameCounter = 0;
	retryCounter = 0;

	while (count_frame > 0 && retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);
		if (frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];

			sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
			sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);
			
			aa_index = (tMode[1] / 2) * (tMode[2] + tMode[7]) * 2;			
			buf_index = (skipcol) * (tMode[2] + tMode[7]) * 2;
			if (skipcol != 0)
				for (col = 0; col < aa_index; col++){								
					raw_buf[aa_index + col] = raw_buf[aa_index + col + buf_index];					
				}

			buf_index = 0;
			aa_index = 0;

			// AA A
			for (col = 0; col < tMode[1] / 2; col++)
			{
				error_count = 0;
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					// stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);
					if (func == 1) // short odd
					{
						if (row % 2 == 0)
						{
							rawdata = rawI[row];
							if (rawdata > gts->self_test_short_max)
								error_count++;

							if (rawdata < min)
								min = (int)rawdata;
							if (rawdata > max)
								max = (int)rawdata;
						}
					}
					else if (func == 2) // short even
					{
						if (row % 2 == 1)
						{
							rawdata = rawI[row];
							if (rawdata > gts->self_test_short_max)
								error_count++;

							if (rawdata < min)
								min = (int)rawdata;
							if (rawdata > max)
								max = (int)rawdata;
						}
					}
					else if (func == 4)
					{
						percentage = ((frameCounter - 1) * tMode[1] * tMode[2]) + (col * tMode[2]) + row;
						rawP[percentage] = rawI[row];
						// stmsg("sensor (%2d,%2d) , index %2d RAW (%4d) \n" , col, row, percentage, rawI[row]);
					}

					buf_index += 2;
				}

				if (error_count > ST_SELFTEST_ADJUST_COUNT)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						if (func == 1) // short odd
						{
							if (row % 2 == 0 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
						else if (func == 2) // short even
						{
							if (row % 2 == 1 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
					}
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			// SE A
			for (col = 0; col < tMode[5] / 2; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index += 2;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			// AA B
			for (col = tMode[1] / 2; col < tMode[1]; col++)
			{
				error_count = 0;
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					// stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);
					if (func == 1) // short odd
					{
						if (row % 2 == 0)
						{
							rawdata = rawI[row];
							if (rawdata > gts->self_test_short_max)
								error_count++;

							if (rawdata < min)
								min = (int)rawdata;
							if (rawdata > max)
								max = (int)rawdata;
						}
					}
					else if (func == 2) // short even
					{
						if (row % 2 == 1)
						{
							rawdata = rawI[row];
							if (rawdata > gts->self_test_short_max)
								error_count++;

							if (rawdata < min)
								min = (int)rawdata;
							if (rawdata > max)
								max = (int)rawdata;
						}
					}
					else if (func == 4)
					{
						percentage = ((frameCounter - 1) * tMode[1] * tMode[2]) + (col * tMode[2]) + row;
						rawP[percentage] = rawI[row];
					}

					buf_index += 2;
				}

				if (error_count > ST_SELFTEST_ADJUST_COUNT)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						if (func == 1) // short odd
						{
							if (row % 2 == 0 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
						else if (func == 2) // short even
						{
							if (row % 2 == 1 && rawI[row] > gts->self_test_short_max)
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
								WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
							}
						}
					}
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			// SE B
			for (col = tMode[5] / 2; col < tMode[5]; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index += 2;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			if (func == 2 || func == 1 || func == 0)
			{
				if (func == 0)
				{
					WRITE_LOG(filp, ppos, "[OPEN RAW Data start]\n");
				}
				else if (func == 1)
				{
					WRITE_LOG(filp, ppos, "[SHORT_ODD RAW Data start]        (Min/Max: %d / %d)\n", min, max);
				}
				else if (func == 2)
				{
					WRITE_LOG(filp, ppos, "[SHORT_EVEN RAW Data start]        (Min/Max: %d / %d)\n", min, max);
				}
				buf_index = 0;
				aa_index = 0;
				// AA A
				for (col = 0; col < tMode[1] / 2; col++)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
						if (row == tMode[2] - 1)
							WRITE_LOG(filp, ppos, "%6d \n", rawI[row]);
						else
							WRITE_LOG(filp, ppos, "%6d ", rawI[row]);
						buf_index += 2;
					}

					if (tMode[7] != 0)
						buf_index += 2;
				}
				// SE A
				for (col = 0; col < tMode[5] / 2; col++)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						buf_index += 2;
					}

					if (tMode[7] != 0)
						buf_index += 2;
				}
				// AA B
				for (col = tMode[1] / 2; col < tMode[1]; col++)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
						if (row == tMode[2] - 1)
							WRITE_LOG(filp, ppos, "%6d \n", rawI[row]);
						else
							WRITE_LOG(filp, ppos, "%6d ", rawI[row]);
						buf_index += 2;
					}
					if (tMode[7] != 0)
						buf_index += 2;
				}

				WRITE_LOG(filp, ppos, "[RAW Data end]\n");
			}
		}
		else
		{
			sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);
		}

		if (frameCounter >= count_frame)
			break;
	}

	if (func == 4)
	{
		WRITE_LOG(filp, ppos, "[STD start]\n");
		for (col = 0; col < tMode[1]; col++)
		{
			for (row = 0; row < nRealRow ; row++)
			{
				aa_index = col * tMode[2] + row;
				percentage = 0;
				for (frameCounter = 0; frameCounter < ST_SELFTEST_STD_FRAME_CNT; frameCounter++)
				{
					buf_index = frameCounter * tMode[1] * tMode[2];
					percentage += (int)rawP[buf_index + aa_index];
				}

				raw_avg = percentage * 10 / ST_SELFTEST_STD_FRAME_CNT;

				rawS[aa_index] = 0;
				for (frameCounter = 0; frameCounter < ST_SELFTEST_STD_FRAME_CNT; frameCounter++)
				{
					buf_index = frameCounter * tMode[1] * tMode[2];
					if (raw_avg >= (int)(rawP[buf_index + aa_index] * 10))
						percentage = raw_avg - (int)(rawP[buf_index + aa_index] * 10);
					else
						percentage = (int)(rawP[buf_index + aa_index] * 10) - raw_avg;
					if (percentage > ST_SELFTEST_STD_CALCULATE_LIMIT)
						percentage = ST_SELFTEST_STD_CALCULATE_LIMIT;
					rawS[aa_index] += percentage * percentage;
				}
				rawS[aa_index] = rawS[aa_index] / ST_SELFTEST_STD_FRAME_CNT;
#ifndef USE_ST_SQRT
				sqrt = int_sqrt((unsigned long)rawS[aa_index]);
#else
				sqrt = st_sqrt(rawS[aa_index]);
#endif // end of USE_ST_SQRT

				if (row == nRealRow - 1)
					WRITE_LOG(filp, ppos, "%4ld.%1ld \n", sqrt / 10, sqrt % 10); // WRITE_LOG(filp, ppos, "%6d \n", rawS[aa_index]);
				else
					WRITE_LOG(filp, ppos, "%4ld.%1ld ", sqrt / 10, sqrt % 10);

				if (sqrt < min)
					min = (int)sqrt;
				if ((int)sqrt > max)
					max = (int)sqrt;
			}
		}

		for (col = 0; col < tMode[1]; col++)
		{
			for (row = 0; row < tMode[2]; row++)
			{
				aa_index = col * tMode[2] + row;
				if (st_get_test_enable(col, row) && (rawS[aa_index] > gts->self_test_std_square100_max))
				{
					total_error++;
#ifndef USE_ST_SQRT
					sqrt = int_sqrt((unsigned long)rawS[aa_index]);
#else
					sqrt = st_sqrt(rawS[aa_index]);
#endif // end of USE_ST_SQRT
					sterr("sensor (%2d,%2d) STD (%4ld.%1ld) > standard value (%4d.%1d) in STD test\n", col, row, sqrt / 10, sqrt % 10, gts->self_test_std_max / 10, gts->self_test_std_max % 10);
					WRITE_LOG(filp, ppos, "sensor (%2d,%2d) STD (%4ld.%1ld) > standard value (%4d.%1d) in STD test\n", col, row, sqrt / 10, sqrt % 10, gts->self_test_std_max / 10, gts->self_test_std_max % 10);
				}
			}
		}
	WRITE_LOG(filp, ppos, "[STD end]        (Def[Max]: %d.%d, Min/Max: %4ld.%1ld / %4ld.%1ld)\n",
				gts->self_test_std_max / 10, gts->self_test_std_max % 10,(unsigned long)min / 10, (unsigned long)min % 10, (unsigned long)max / 10, (unsigned long)max % 10);
	}

st_open_short_test_finish:

	if (rawI)
		kfree(rawI);

	if (raw_buf)
		kfree(raw_buf);

	if (rawP)
		kfree(rawP);

	if (rawS)
		kfree(rawS);

	if (ret < 0)
		return ret;
	else if (total_error == 0)
		return 0;
	else if (total_error > 0)
		return -1;
	return 0;
}

int st_get_afe_sensing_settings(sensing_setting_t *sensing_setting, unsigned char chip_id)
{
	unsigned char cmd[0x08];

	if(chip_id == 0x84){ //ST77921
		/* ROW_CNT */
		sitronix_spi_pram_rw(true, 0xF044, NULL, cmd, 2);
		sensing_setting->row_cnt = cmd[0] & 0x0F;		//tMode[2] = cmd[0]&0x0F;

		/* CMNC_CH_WR_EN */
		sensing_setting->cmnc_ch_wr_en = 0;		//tMode[7] = 0;

		/* AA_UNIT */
		sitronix_spi_pram_rw(true, 0xF506, NULL, cmd, 2);
		sensing_setting->aa_unit = (cmd[0] & 0x0F);	//tMode[1] = (cmd[0]&0x0F);

		/* SELF_UNIT */		
		sensing_setting->self_unit = (cmd[1] & 0x0C);	//tMode[5] = (cmd[0]&0xC);

		/* NOISE_UNIT */
		sensing_setting->noise_unit = (cmd[1] & 0x03);	//tMode[4] = (cmd[0]&0x3);

		/* KEY */
		sensing_setting->key = 0;				//tMode[3] = 0;
	}
	else{	//ST71xx
		/* ROW_CNT */
		sitronix_spi_pram_rw(true, 0xF04A, NULL, cmd, 2);
		sensing_setting->row_cnt = cmd[0] & 0x3F;		//tMode[2] = cmd[0]&0x3F;

		/* CMNC_CH_WR_EN */
		sensing_setting->cmnc_ch_wr_en = cmd[1] & 0x01;		//tMode[7] = cmd[1]&0x01;

		/* AA_UNIT */
		sitronix_spi_pram_rw(true, 0xF024, NULL, cmd, 2);
		sensing_setting->aa_unit = (cmd[0] & 0xF0) >> 3;	//tMode[1] = (cmd[0]&0xF0) >> 3; 

		/* SELF_UNIT */		
		sensing_setting->self_unit = (cmd[0] & 0x0C) >> 1;	//tMode[5] = (cmd[0]&0xC) >> 1; 

		/* NOISE_UNIT */
		sensing_setting->noise_unit = (cmd[0] & 0x03) << 2;	//tMode[4] = (cmd[0]&0x3) << 2;

		/* KEY */
		sensing_setting->key = 0;				//tMode[3] = 0;
	}
#if 0
	stmsg("[%s] ROW_CNT : %d\n", __func__, sensing_setting->row_cnt);
	stmsg("[%s] AA_UNIT : %d\n", __func__, sensing_setting->aa_unit);
	stmsg("[%s] SELF_UNIT : %d\n", __func__, sensing_setting->self_unit);
	stmsg("[%s] NOISE_UNIT : %d\n", __func__, sensing_setting->noise_unit);
	stmsg("[%s] CMNC_CH_WR_EN : %d\n", __func__, sensing_setting->cmnc_ch_wr_en);
#endif
	return 0;
}

#ifdef ST_SELFTEST_LOG_FILE
signed short *st_get_aa_rawdata(uint8_t *raw_cnt, uint8_t *aa_unit, int skipcol, struct file *filp, loff_t *ppos)
#else  // ST_SELFTEST_LOG_FILE
signed short *st_get_aa_rawdata(uint8_t *raw_cnt, uint8_t *aa_unit, int skipcol)
#endif // ST_SELFTEST_LOG_FILE
{
	//unsigned char cmd[0x08];
	int tMode[8];
	int read_len;
	unsigned char *raw_buf = NULL;
	signed short *rawI = NULL;
	int frameCounter;
	int retryCounter;
	int max_retry = 50; // 5;
	unsigned char raw_dat_rd_on[2] = {0x02, 0x00};
	unsigned char raw_dat_rd_off[2] = {0x00, 0x00};
	unsigned char raw_header[18];
	unsigned char frame_counter = 0;
	int buf_index, aa_index;
	int col, row;
#ifdef __RAW_DATA_DEBUG__
	int i, j, k;
#endif //__RAW_DATA_DEBUG__
	
	st_get_afe_sensing_settings(&m_sensing_setting, gts->ts_dev_info.chip_id);
	
	/* ROW_CNT */
	tMode[2] = m_sensing_setting.row_cnt;
	*raw_cnt = tMode[2];

	/* CMNC_CH_WR_EN */
	tMode[7] = m_sensing_setting.cmnc_ch_wr_en;

	/* AA_UNIT */
	tMode[1] = m_sensing_setting.aa_unit;
	*aa_unit = tMode[1];
	
	/* SELF_UNIT */
	tMode[5] = m_sensing_setting.self_unit;
	
	/* NOISE_UNIT */
	tMode[4] = m_sensing_setting.noise_unit;
	
	/* KEY */
	tMode[3] = m_sensing_setting.key;

	stmsg("ROW_CNT : %d\n", tMode[2]);
	stmsg("AA_UNIT : %d\n", tMode[1]);
	stmsg("SELF_UNIT : %d\n", tMode[5]);
	stmsg("NOISE_UNIT : %d\n", tMode[4]);
	stmsg("CMNC_CH_WR_EN : %d\n", tMode[7]);

	read_len = (tMode[2] + tMode[7]) * (tMode[1] + tMode[5] + tMode[4] + skipcol) * 2;
	raw_buf = (unsigned char *)kmalloc(read_len, GFP_KERNEL);
	rawI = (signed short *)kmalloc((tMode[1] * tMode[2] * sizeof(short)), GFP_KERNEL);

	// Ignore
	frameCounter = 0;
	retryCounter = 0;

	while (ST_SELFTEST_IGNORE_FRAME > 0 && retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);

#ifdef __RAW_DATA_DEBUG__
		// Read Rawdata.
		memset(raw_buf, 0, read_len);
		sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
		for (i = 0, j = 0; j < (tMode[1] + tMode[5] + tMode[4] + skipcol); j++)
		{
			stmsg("AA_UNIT[%d]: ", j);
			for (k = 0; k < (tMode[2] + tMode[7]); i++, k++)
			{
				short raw = (short)(raw_buf[i * 2] << 8 | raw_buf[i * 2 + 1]);
				printk("%d ", raw);
			}
			printk(" (i=%d, k=%d)\n", i, k);
		}
#endif //__RAW_DATA_DEBUG__

		stmsg("header %x \n", raw_header[1]);
		if (frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];
		}

		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

		if (frameCounter >= ST_SELFTEST_IGNORE_FRAME)
			break;
	}

	if (retryCounter >= max_retry)
	{
		sterr("Get rawdata failed. (Can't wait IRQ)\n");
		WRITE_LOG(filp, ppos, "Get rawdata failed. (Can't wait IRQ)\n");
		goto rawdata_retry_err;
	}

	// raw
	frameCounter = 0;
	retryCounter = 0;

	while (retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);
		if (frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];

			sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
			sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

			aa_index = (tMode[1] / 2) * (tMode[2] + tMode[7]) * 2;
			buf_index = (skipcol) * (tMode[2] + tMode[7]) * 2;

			if (skipcol != 0)
				for (col = 0; col < aa_index; col++)
					raw_buf[aa_index + col] = raw_buf[aa_index + col + buf_index];

			buf_index = 0;
			aa_index = 0;

			// AA A
			for (col = 0; col < tMode[1] / 2; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[aa_index++] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					// stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);

					buf_index += 2;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			// SE A
			for (col = 0; col < tMode[5] / 2; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index += 2;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			// AA B
			for (col = tMode[1] / 2; col < tMode[1]; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[aa_index++] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					// stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);

					buf_index += 2;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			// SE B
			for (col = tMode[5] / 2; col < tMode[5]; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index += 2;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}
			break;
		}
		else
		{
			sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);
		}
	}

rawdata_retry_err:

	if (raw_buf)
		kfree(raw_buf);

	if (retryCounter >= max_retry)
	{
		return NULL;
	}
	else
	{
		return rawI;
	}
}

#ifdef ST_SELFTEST_LOG_FILE
int normal_rawdata_check(uint8_t raw_cnt, uint8_t aa_unit, signed short *normal_rawdata, struct file *filp, loff_t *ppos)
#else
int normal_rawdata_check(uint8_t raw_cnt, uint8_t aa_unit, signed short *normal_rawdata)
#endif
{
	int col, row, raw_index;
	signed short rawdata;
	int total_error = 0;

	int i, j;
	int min = 0xFFF0, max = -0xFFF0;


#ifdef __RAW_DATA_DEBUG__
	stmsg("NORMAL_RAW:\n");
	for (raw_index = 0, i = 0; i < aa_unit; i++)
	{
		stmsg("AA_UNIT[%d]: ", i);
		for (j = 0; j < raw_cnt; j++)
		{
			printk("%d ", normal_rawdata[raw_index++]);
		}
		printk("\n");
	}
#endif //__RAW_DATA_DEBUG__

	WRITE_LOG(filp, ppos, "[NORMAL RAW Data start]\n");

	for (raw_index = 0, i = 0; i < aa_unit; i++)
	{
		for (j = 0; j < raw_cnt; j++)
		{
			rawdata = normal_rawdata[raw_index];
			WRITE_LOG(filp, ppos, "%6d ", rawdata);
			raw_index++;
			if (rawdata < min)
				min = (int)rawdata;
			if (rawdata > max)
				max = (int)rawdata;
		}
		WRITE_LOG(filp, ppos, "\n");
	}

	WRITE_LOG(filp, ppos, "[NORMAL RAW Data end]        (Def[Min/Max]: %d / %d, Min/Max: %d / %d)\n",gts->self_test_normal_min ,gts->self_test_normal_max,min, max);

	for (raw_index = 0, col = 0; col < aa_unit; col++)
	{
		for (row = 0; row < raw_cnt; row++)
		{
			rawdata = normal_rawdata[raw_index];
			if (st_get_test_enable(col, row) && (rawdata < gts->self_test_normal_min || rawdata > gts->self_test_normal_max))
			{
				total_error++;
				sterr("sensor (%2d,%2d) RAW (%4d) out of range (%d ~ %d) in normal rawdata test.\n",
					  col, row, rawdata, gts->self_test_normal_min, gts->self_test_normal_max);
				WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) out of range (%d ~ %d) in normal rawdata test.\n",
						 col, row, rawdata, gts->self_test_normal_min, gts->self_test_normal_max);
			}
			raw_index++;
		}
	}

	return total_error;
}

#ifdef ST_SELFTEST_LOG_FILE
int open_mux_on_check(uint8_t raw_cnt, uint8_t aa_unit, signed short *mux_on_raw, struct file *filp, loff_t *ppos)
#else
int open_mux_on_check(uint8_t raw_cnt, uint8_t aa_unit, signed short *mux_on_raw)
#endif
{
	int col, row, raw_index;
	signed short data;
	int total_error = 0;

	int i, j;
	int min = 0xFFF0, max = -0xFFF0;


#ifdef __RAW_DATA_DEBUG__
	stmsg("MUX_ON_RAW:\n");
	for (raw_index = 0, i = 0; i < aa_unit; i++)
	{
		stmsg("AA_UNIT[%d]: ", i);
		for (j = 0; j < raw_cnt; j++)
		{
			printk("%d ", mux_on_raw[raw_index++]);
		}
		printk("\n");
	}
#endif //__RAW_DATA_DEBUG__

	WRITE_LOG(filp, ppos, "[OPEN RAW Data start]\n");
	min = 0xFFF0, max = -0xFFF0;
	for (raw_index = 0, i = 0; i < aa_unit; i++)
	{
		for (j = 0; j < raw_cnt; j++)
		{
			data = mux_on_raw[raw_index];
			WRITE_LOG(filp, ppos, "%6d ", data );
			raw_index++;
			if (data < min)
				min = (int)data;
			if (data > max)
				max = (int)data;
		}
		WRITE_LOG(filp, ppos, "\n");
	}
	WRITE_LOG(filp, ppos, "\n");
	WRITE_LOG(filp, ppos, "[OPEN RAW Data end]        (Def[Min]: %d , Min/Max: %d / %d)\n",gts->self_test_open_mux_on_min, min, max);	

	for (raw_index = 0, col = 0; col < aa_unit; col++)
	{
		for (row = 0; row < raw_cnt; row++)
		{
			data = mux_on_raw[raw_index];
			if (st_get_test_enable(col, row) && (data < gts->self_test_open_mux_on_min))
			{
				total_error++;
				sterr("sensor (%2d,%2d) RAW (%4d) is less than min value(< %d) in open mux on test.\n",
					  col, row, data, gts->self_test_open_mux_on_min);
				WRITE_LOG(filp, ppos, "sensor (%2d,%2d) RAW (%4d) is less than min value(< %d) in open mux on test.\n",
						 col, row, data, gts->self_test_open_mux_on_min);
			}
			raw_index++;
		}
	}

	return total_error;
}


#ifdef ST_SELFTEST_LOG_FILE
int st_test_normal_rawdata_func(struct file *filp, loff_t *ppos)
#else
int st_test_normal_rawdata_func(void)
#endif
{ 
	int ret = 0;
	signed short *normal_rawdata = NULL;
	uint8_t raw_cnt, aa_unit;

	ret = sitronix_ts_test_fw_init();
	if (ret) {
		sterr("FW init failed! (ret=%d)\n", ret);
		goto test_normal_rawdata_func_err;
	}

	ret = st_address_mode_hardcode_write(test_cmd_normal_rawdata, ARRAY_SIZE(test_cmd_normal_rawdata));
	if (ret < 0)
		goto test_normal_rawdata_func_err;

	stmsg("Normal Raw\n");
#ifdef ST_SELFTEST_LOG_FILE
	normal_rawdata = st_get_aa_rawdata(&raw_cnt, &aa_unit, 0, filp, ppos);
#else  // ST_SELFTEST_LOG_FILE
	normal_rawdata = st_get_aa_rawdata(&raw_cnt, &aa_unit, 0);
#endif // ST_SELFTEST_LOG_FILE

	if (!normal_rawdata) {
		sterr("Normal rawdata test fail! (normal_rawdata==NULL)\n");
		goto test_normal_rawdata_func_err;
	}

#ifdef ST_SELFTEST_LOG_FILE
	ret = normal_rawdata_check(raw_cnt, aa_unit, normal_rawdata, filp, ppos);
#else
	ret = normal_rawdata_check(raw_cnt, aa_unit, normal_rawdata);
#endif

test_normal_rawdata_func_err:

	if (normal_rawdata)
		kfree(normal_rawdata);

	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st_test_normal_rawdata(struct file *filp, loff_t *ppos)
#else
int st_test_normal_rawdata(void)
#endif
{
	int i;
	int ret = 0;

	for (i = 0; i < TEST_RETRY_MAX; i++) {
		#ifdef ST_SELFTEST_LOG_FILE
		ret = st_test_normal_rawdata_func(filp, ppos);
		#else
		ret = st_test_normal_rawdata_func();
		#endif
		if (ret == 0)
			break;
	}
	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st_test_open_mux_on_func(struct file *filp, loff_t *ppos)
#else
int st_test_open_mux_on_func(void)
#endif
{
	int ret = 0;
	signed short *mux_on_raw = NULL;
	uint8_t raw_cnt, aa_unit;

	ret = sitronix_ts_test_fw_init();
	if (ret) {
		sterr("FW init failed! (ret=%d)\n", ret);
		goto test_open_mux_on_func_err;
	}

	ret = st_address_mode_hardcode_write(test_cmd_open_mux_on, ARRAY_SIZE(test_cmd_open_mux_on));
	if (ret < 0)
		goto test_open_mux_on_func_err;

	stmsg("MuxOn Raw\n");
#ifdef ST_SELFTEST_LOG_FILE
	mux_on_raw = st_get_aa_rawdata(&raw_cnt, &aa_unit, 0, filp, ppos);
#else  // ST_SELFTEST_LOG_FILE
	mux_on_raw = st_get_aa_rawdata(&raw_cnt, &aa_unit, 0);
#endif // ST_SELFTEST_LOG_FILE

	if (!mux_on_raw) {
		sterr("Open test fail! (mux_on_raw==NULL)\n");
		goto test_open_mux_on_func_err;
	}

#ifdef ST_SELFTEST_LOG_FILE
	ret = open_mux_on_check(raw_cnt, aa_unit, mux_on_raw, filp, ppos);
#else
	ret = open_mux_on_check(raw_cnt, aa_unit, mux_on_raw);
#endif

test_open_mux_on_func_err:

	if (mux_on_raw)
		kfree(mux_on_raw);

	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st_test_open_mux_on(struct file *filp, loff_t *ppos)
#else
int st_test_open_mux_on(void)
#endif
{
	int i;
	int ret = 0;

	for (i = 0; i < TEST_RETRY_MAX; i++) {
		#ifdef ST_SELFTEST_LOG_FILE
		ret = st_test_open_mux_on_func(filp, ppos);
		#else
		ret = st_test_open_mux_on_func();
		#endif
		if (ret == 0)
			break;
	}

	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st_test_short_even_func(struct file *filp, loff_t *ppos)
#else
int st_test_short_even_func(void)
#endif
{
	int ret = 0;

	ret = sitronix_ts_test_fw_init();
	if (ret) {
		sterr("FW init failed! (ret=%d)\n", ret);
		goto test_short_even_func_err;
	}

	ret = st_address_mode_hardcode_write(test_cmd_short_even, ARRAY_SIZE(test_cmd_short_even));
	if (ret < 0)
		goto test_short_even_func_err;

#ifdef ST_SELFTEST_LOG_FILE
	ret = st_open_short_test(2, ST_SELFTEST_SKIP_COLS, filp, ppos);
#else
	ret = st_open_short_test(2, ST_SELFTEST_SKIP_COLS);
#endif

test_short_even_func_err:

	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st_test_short_even(struct file *filp, loff_t *ppos)
#else
int st_test_short_even(void)
#endif
{
	int i;
	int ret = 0;

	for (i = 0; i < TEST_RETRY_MAX; i++) {
		#ifdef ST_SELFTEST_LOG_FILE
		ret = st_test_short_even_func(filp, ppos);
		#else
		ret = st_test_short_even_func();
		#endif
		if (ret == 0)
			break;
	}
	
	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st_test_short_odd_func(struct file *filp, loff_t *ppos)
#else
int st_test_short_odd_func(void)
#endif
{
	int ret = 0;

	ret = sitronix_ts_test_fw_init();
	if (ret) {
		sterr("FW init failed! (ret=%d)\n", ret);
		goto test_short_odd_func_err;
	}

	ret = st_address_mode_hardcode_write(test_cmd_short_odd, ARRAY_SIZE(test_cmd_short_odd));
	if (ret < 0)
		goto test_short_odd_func_err;

#ifdef ST_SELFTEST_LOG_FILE
	ret = st_open_short_test(1, ST_SELFTEST_SKIP_COLS, filp, ppos);
#else
	ret = st_open_short_test(1, ST_SELFTEST_SKIP_COLS);
#endif

test_short_odd_func_err:

	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st_test_short_odd(struct file *filp, loff_t *ppos)
#else
int st_test_short_odd(void)
#endif
{
	int i;
	int ret = 0;

	for (i = 0; i < TEST_RETRY_MAX; i++) {
		#ifdef ST_SELFTEST_LOG_FILE
		ret = st_test_short_odd_func(filp, ppos);
		#else
		ret = st_test_short_odd_func();
		#endif
		if (ret == 0)
			break;
	}

	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st_test_std_func(struct file *filp, loff_t *ppos)
#else
int st_test_std_func(void)
#endif
{
	int ret = 0;

	ret = sitronix_ts_test_fw_init();
	if (ret) {
		sterr("FW init failed! (ret=%d)\n", ret);
		goto test_std_func_err;
	}

	ret = st_address_mode_hardcode_write(test_cmd_std, ARRAY_SIZE(test_cmd_std));
	if (ret < 0)
		goto test_std_func_err;

#ifdef ST_SELFTEST_LOG_FILE
	ret = st_open_short_test(4, 0, filp, ppos);
#else
	ret = st_open_short_test(4, 0);
#endif

test_std_func_err:

	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
int st7102_test_std_func(struct file *filp, loff_t *ppos)
#else
int st7102_test_std_func(void)
#endif
{
	int ret = 0;

	ret = sitronix_ts_test_fw_init();
	if (ret) {
		sterr("FW init failed! (ret=%d)\n", ret);
		goto test_std_func_err;
	}

	ret = st_address_mode_hardcode_write(test_cmd_std, ARRAY_SIZE(test_cmd_std));
	if (ret < 0)
		goto test_std_func_err;

#ifdef ST_SELFTEST_LOG_FILE
	ret = st_open_short_test(4, 0, filp, ppos);
#else
	ret = st_open_short_test(4, 0);
#endif

test_std_func_err:

	return ret;
}
#ifdef ST_SELFTEST_LOG_FILE
int st_test_std(struct file *filp, loff_t *ppos)
#else
int st_test_std(void)
#endif
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < TEST_RETRY_MAX; i++) {
#ifdef SITRONIX_TEST_ST7102
	#ifdef ST_SELFTEST_LOG_FILE
		ret = st7102_STD(4, 0, filp, ppos);
	#else
		ret = st7102_STD();
	#endif
#else
	#ifdef ST_SELFTEST_LOG_FILE
		ret = st_test_std_func(filp, ppos);
	#else
		ret = st_test_std_func();
	#endif
#endif
		if (ret == 0)
			break;
	}
	return ret;
}

#ifdef ST_SELFTEST_LOG_FILE
void st_record_ic_info(struct file *filp, loff_t *ppos)
{
	int ret = 0;

	ret = sitronix_ts_get_device_info(gts);
	if (ret < 0)
	{
		WRITE_LOG(filp, ppos, "sitronix_ts_get_device_info failed!\n");
	}
	WRITE_LOG(filp, ppos, "%s\n", SITRONIX_TP_DRIVER_VERSION);
	WRITE_LOG(filp, ppos, "Chip ID = %02X\n", gts->ts_dev_info.chip_id);
	WRITE_LOG(filp, ppos, "FW Verison = %02X\n", gts->ts_dev_info.fw_version);
	WRITE_LOG(filp, ppos, "FW Revision = %02X %02X %02X %02X\n", gts->ts_dev_info.fw_revision[0], gts->ts_dev_info.fw_revision[1], gts->ts_dev_info.fw_revision[2], gts->ts_dev_info.fw_revision[3]);
	WRITE_LOG(filp, ppos, "Resolution = %d x %d\n", gts->ts_dev_info.x_res, gts->ts_dev_info.y_res);
	WRITE_LOG(filp, ppos, "Channels = %d x %d\n", gts->ts_dev_info.x_chs, gts->ts_dev_info.y_chs);
	WRITE_LOG(filp, ppos, "Max touches = %d\n", gts->ts_dev_info.max_touches);
	WRITE_LOG(filp, ppos, "Misc. Info = 0x%X\n", gts->ts_dev_info.misc_info);

	ret = sitronix_get_ic_sfrver();
	if (ret < 0)
	{
		WRITE_LOG(filp, ppos, "sitronix_get_ic_sfrver failed!\n");
	}
	else
	{
		WRITE_LOG(filp, ppos, "IC SFR VER = 0x%X\n", ret);
	}
}
#endif

/*
return of st_self_test
/ ret = 0 : test success
/ ret < 0 : test failed with error
/ ret > 0 : test failed with N sensors :
*/
int st_self_test(void)
{
	int ret = 0;
	int result_normal_rawdata = 0;
	int result_open = 0;
	int result_short_odd = 0;
	int result_short_even = 0;
	int result_std = 0;
	int result_fw = 0;
	char ic_position[2];
	
#ifdef ST_SELFTEST_LOG_FILE
#ifdef ST_SELFTEST_EN_GKI 
	
	struct file *filp = NULL;
	loff_t pos = 0;
	int i = 0;
	
#else
	struct file *filp = NULL;
	int i = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
	mm_segment_t fs;
#endif
	loff_t pos = 0;

	filp = filp_open(ST_SELFTEST_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	sitonix_createlogfileok = true;
	if (IS_ERR(filp))
	{
		sitonix_createlogfileok = false;
		sterr("ST open %s error...\n", ST_SELFTEST_LOG_PATH);
		// return -1;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	fs = get_fs();
	set_fs(KERNEL_DS);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
	fs = force_uaccess_begin();
#else
	// do nothing
#endif

	pos = 0;

#endif	//end of #ifdef ST_SELFTEST_EN_GKI
	output_log_buff_offset = 0;
	st_record_ic_info(filp, &pos);
#endif

	// get IC position
	sitronix_get_ic_position(ic_position);
	stmsg("IC position X,Y = (%03d,%03d)\n", (((ic_position[0] & 0x1) << 4) | ic_position[1]), (ic_position[0] >> 4));
#ifdef ST_SELFTEST_LOG_FILE
	WRITE_LOG(filp, &pos, "IC position X,Y = (%03d,%03d)\n", (((ic_position[0] & 0x1) << 4) | ic_position[1]), (ic_position[0] >> 4));

	memset(&m_sensing_setting, 0, sizeof(sensing_setting_t));

	// normal rawdata test
#ifdef SITRONIX_TEST_ST7102
	result_normal_rawdata = st7102_test_normal(filp, &pos);
#else
	result_normal_rawdata = st_test_normal_rawdata(filp, &pos);
#endif
	if (result_normal_rawdata == 0)
	{
		stmsg("Test normal rawdata successed!\n");
		WRITE_LOG(filp, &pos, "Test normal rawdata successed!\n");
	}
	else
	{
		stmsg("Test normal rawdata failed!\n");
		WRITE_LOG(filp, &pos, "Test normal rawdata failed!\n");
	}

	// open test
	for (i = 0; i < TEST_RETRY_MAX; i++)
	{
#ifdef SITRONIX_TEST_ST7102
		result_open = st7102_test_open(filp, &pos);
#else
		// open test(mux on)
		result_open = st_test_open_mux_on(filp, &pos);
#endif

		if (result_open == 0)
			break;
	}
	if (result_open == 0)
	{
		stmsg("Test open successed!\n");
		WRITE_LOG(filp, &pos, "Test open successed!\n");
	}
	else
	{
		stmsg("Test open failed!\n");
		WRITE_LOG(filp, &pos, "Test open failed!\n");
	}

	// short test even
#ifdef SITRONIX_TEST_ST7102
	result_short_even = st7102_short_test_even(filp, &pos);
#else
	result_short_even = st_test_short_even(filp, &pos);
#endif
	if (result_short_even == 0)
	{
		stmsg("Test short_even successed!\n");
		WRITE_LOG(filp, &pos, "Test short_even successed!\n");
	}
	else
	{
		stmsg("Test short_even failed!\n");
		WRITE_LOG(filp, &pos, "Test short_even failed!\n");
	}

	// short test odd
#ifdef SITRONIX_TEST_ST7102
	result_short_odd = st7102_short_test_odd(filp, &pos);
#else
	result_short_odd = st_test_short_odd(filp, &pos);
#endif
	if (result_short_odd == 0)
	{
		stmsg("Test short_odd successed!\n");
		WRITE_LOG(filp, &pos, "Test short_odd successed!\n");
	}
	else
	{
		stmsg("Test short_odd failed! %d\n", result_short_odd);
		WRITE_LOG(filp, &pos, "Test short_odd failed!\n");
	}

	// STD test
	result_std = st_test_std(filp, &pos);
	if (result_std == 0)
	{
		stmsg("Test STD successed!\n");
		WRITE_LOG(filp, &pos, "Test STD successed!\n");
	}
	else
	{
		stmsg("Test STD failed!\n");
		WRITE_LOG(filp, &pos, "Test STD failed!\n");
	}

#else

	// normal rawdata test
#ifdef SITRONIX_TEST_ST7102
	result_normal_rawdata = st7102_test_normal();
#else
	result_normal_rawdata = st_test_normal_rawdata();
#endif
	if (result_normal_rawdata == 0)
		stmsg("Test normal rawdata successed!\n");
	else
		stmsg("Test normal rawdata failed!\n");

	// open test
#ifdef SITRONIX_TEST_ST7102
	result_open = st7102_test_open();
#else
	// open test(mux on)
	result_open = st_test_open_mux_on();
#endif
	if (result_open == 0)
		stmsg("Test open successed!\n");
	else
		stmsg("Test open failed!\n");

	// short test even
#ifdef SITRONIX_TEST_ST7102
	result_short_even = st7102_short_test_even(filp, &pos);
#else
	result_short_even = st_test_short_even();
#endif
	if (result_short_even == 0)
		stmsg("Test short_even successed!\n");
	else
		stmsg("Test short_even failed!\n");

	// short test odd
#ifdef SITRONIX_TEST_ST7102
	result_short_odd = st7102_short_test_odd(filp, &pos);
#else
	result_short_odd = st_test_short_odd();
#endif
	if (result_short_odd == 0)
		stmsg("Test short_odd successed!\n");
	else
		stmsg("Test short_odd failed!\n");

	// STD test
	result_std = st_test_std();
	if (result_std == 0)
		stmsg("Test STD successed!\n");
	else
		stmsg("Test STD failed!\n");

#endif
#ifdef SITRONIX_TEST_ST7102
	//ret = st_address_mode_hardcode_write(EnterSleepOut, ARRAY_SIZE(EnterSleepOut));
#endif
	if (result_normal_rawdata != 0 || result_open != 0 || result_short_odd != 0 ||
		result_short_even != 0 || result_std != 0 || result_fw != 0) {
		ret = -1;
	} else {
		ret = result_normal_rawdata + result_open + result_short_odd + result_short_even + result_std + result_fw;
	}

#ifdef ST_SELFTEST_LOG_FILE

#ifdef ST_SELFTEST_EN_GKI 
	//test_seq_file = NULL;
#else

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	set_fs(fs);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
	force_uaccess_end(fs);
#else
	// do nothing
#endif

	if (sitonix_createlogfileok)
	{
		filp_close(filp, NULL);
		stmsg("Test log file : %s\n", ST_SELFTEST_LOG_PATH);
	}
#endif

#endif
	return ret;
}

void sitronix_set_default_test_criteria(void)
{
	gts->self_test_normal_min = ST_SELFTEST_NORMAL_MIN;
	gts->self_test_normal_max = ST_SELFTEST_NORMAL_MAX;
	gts->self_test_short_max = ST_SELFTEST_SHORT_MAX;
	gts->self_test_open_mux_on_min = ST_SELFTEST_OPEN_MUX_ON_MIN;
	gts->self_test_std_max = ST_SELFTEST_STD_MAX;

	sitronix_request_test_criteria(ST_SELFTEST_INI_PATH);

	gts->self_test_std_square100_max = gts->self_test_std_max * gts->self_test_std_max;
}

int sitronix_request_test_criteria(const char *name)
{
#ifdef ST_REQUEST_SELF_TEST_INI
	u8 *tmp = NULL;
	u8 *buff = NULL;
	int ret = 0;
	const struct firmware *fw = NULL;

	ret = request_firmware(&fw, name, &gts->pdev->dev);
	if (ret == 0)
	{
		stmsg("selftesst INI request(%s) success\n", name);
		buff = kzalloc(fw->size, GFP_KERNEL);
		// stmsg("INI size is:%d\n", fw->size);
		if (!buff)
		{
			sterr("selftest INI buffer kzalloc fail\n");
			return -EPERM;
		}
		memcpy(buff, fw->data, fw->size);
		release_firmware(fw);

		/* self_test_normal_min */
		tmp = strstr(buff, "self_test_normal_min=");
		if (tmp != NULL)
		{
			ret = sscanf(tmp, "self_test_normal_min=%d", &gts->self_test_normal_min);
			if (ret < 0)
				sterr("%s read self_test_normal_min error.\n", __func__);
			// stmsg("self_test_normal_min = %d\n", gts->self_test_normal_min);
		}
		/* self_test_normal_max */
		tmp = strstr(buff, "self_test_normal_max=");
		if (tmp != NULL)
		{
			ret = sscanf(tmp, "self_test_normal_max=%d", &gts->self_test_normal_max);
			if (ret < 0)
				sterr("%s read self_test_normal_max error.\n", __func__);
			// stmsg("self_test_normal_max = %d\n", gts->self_test_normal_max);
		}

		/* self_test_short_max */
		tmp = strstr(buff, "self_test_short_max=");
		if (tmp != NULL)
		{
			ret = sscanf(tmp, "self_test_short_max=%d", &gts->self_test_short_max);
			if (ret < 0)
				sterr("%s read self_test_short_max error.\n", __func__);
			// stmsg("self_test_short_max = %d\n", gts->self_test_short_max);
		}
		/* self_test_open_mux_on_min */
		tmp = strstr(buff, "self_test_open_mux_on_min=");
		if (tmp != NULL)
		{
			ret = sscanf(tmp, "self_test_open_mux_on_min=%d", &gts->self_test_open_mux_on_min);
			if (ret < 0)
				sterr("%s read self_test_open_mux_on_min error.\n", __func__);
			// stmsg("self_test_open_mux_on_min = %d\n", gts->self_test_open_mux_on_min);
		}
		/* self_test_std_max */
		tmp = strstr(buff, "self_test_std_max=");
		if (tmp != NULL)
		{
			ret = sscanf(tmp, "self_test_std_max=%d", &gts->self_test_std_max);
			if (ret < 0)
				sterr("%s read self_test_std_max error.\n", __func__);
			// stmsg("self_test_std_max = %d\n", gts->self_test_std_max);
		}
		kfree(buff);
	}
	else
	{
		sterr("firmware request(%s) fail,ret=%d", name, ret);
	}
#endif // ST_REQUEST_SELF_TEST_INI
	return 0;
}

void st_self_test_recovery(void)
{
#ifdef SITRONIX_HDL_IN_MT
	mutex_lock(&gts->mutex);
	sitronix_do_upgrade();
	mutex_unlock(&gts->mutex);
#else
	sitronix_ts_reset_device(gts);
#endif /* SITRONIX_HDL_IN_MT */

	gts->coord_chk_err_cnt = 0;

	/* restore status*/
	if( gts->in_suspend ) {
		mutex_lock(&gts->mutex);
		if (gts->swu_flag) {
			sitronix_ts_set_smart_wake_up(gts, true);			
		} else {
			sitronix_ts_set_smart_wake_up(gts, false);			
		}

		msleep(20);
		sitronix_ts_powerdown(gts, true);
		mutex_unlock(&gts->mutex);
	}
	
	mutex_lock(&gts->mutex);
	sitronix_mode_restore();
	mutex_unlock(&gts->mutex);
}

//[CC]FIH factory functions
bool sitonix_resultselftest = false; // FIH self test result

int sitronix_selftest_result_read(void)
{
	int num_read_chars = 0;
	if (sitonix_resultselftest)
		num_read_chars = 0;
	else
		num_read_chars = 1;
	return num_read_chars;
}

#ifdef USE_ST_SQRT
int st_sqrt(int x)
{
	int l, h, mid, sqrt;
	if (x <= 1)
	{
		return x;
	}
	l = 1;
	h = x;
	while (l <= h)
	{
		mid = l + (h - l) / 2;
		sqrt = x / mid;
		if (sqrt == mid)
		{
			return mid;
		}
		else if (mid > sqrt)
		{
			h = mid - 1;
		}
		else
		{
			l = mid + 1;
		}
	}
	return h;
}
#endif
