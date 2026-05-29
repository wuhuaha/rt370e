//#define ST_REQUEST_SELF_TEST_INI

#define ST_SELFTEST_LOG_FILE
#define ST_SELFTEST_LOG_PATH    "/sdcard/ST_SELFTEST_LOG.txt"

#define ST_SELFTEST_INI_PATH    "st_selftest_criteria.ini"

#ifdef	ST_SELFTEST_LOG_FILE
//#define ST_SELFTEST_EN_GKI 
#endif

#define ST_SELFTEST_IGNORE_FRAME		3
#define ST_SELFTEST_ADJUST_COUNT		0
#define ST_SELFTEST_SKIP_COLS			3

//=============================================================================================================
// Self Test Criteria Section
//=============================================================================================================
#define ST_SELFTEST_SHORT_MAX			0	//650
#define ST_SELFTEST_OPEN_MUX_ON_MIN		6000	//500			//for ST7121P/ST7123P Mux On Open test.
#define ST_SELFTEST_NORMAL_MAX			0	//3000
#define ST_SELFTEST_NORMAL_MIN			0	//-2000
#define ST_SELFTEST_STD_MAX			0	//60	//60->6.0
//=============================================================================================================

#define ST_SELFTEST_STD_FRAME_CNT		100
#define ST_SELFTEST_STD_CALCULATE_LIMIT		1000 //100 * 10


#define WriteComm(cmd)		{(0x01), (cmd)}
#define WriteData(data)		{(0x02), (data)}
#define Delay_ms(time)		{(0x03), (time)}

typedef struct sitronix_afe_cmd {
	uint8_t		type;
	uint32_t	value;
} sitronix_afe_cmd_t;

#define AFE_CMD_T	sitronix_afe_cmd_t

int sitronix_request_test_criteria(const char *name);

unsigned char test_disable_sensor[]= {
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x01,\
	0x00,0x00,0x00,0x01,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
};

AFE_CMD_T test_cmd_normal_rawdata[] = {
	WriteComm (0x537123),
	WriteData (0xA53C),
	WriteComm (0x537123),
	WriteData (0x1455),
	WriteComm (0x537123),
	WriteData (0x7555),
	WriteComm (0x537123),
	WriteData (0x5555),
	
	//-----------------------------Reset AFE & Driver Start------------------
	WriteComm (0x00F300),
	WriteData (0x5AA5),
	WriteComm (0x00F302),
	WriteData (0x0001),
	
	Delay_ms (50),


	//-----FRAM Write(OFTV_MUX0)---
	WriteComm (0x00E3B0),//Trim00000
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	//-----FRAM Write(OFTV_MUX1)---
	WriteComm (0x00E3E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	//-----FRAM Write(OFTV_MUX2)---
	WriteComm (0x00E410),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	//-----FRAM Write(OFTV_MUX3)---
	WriteComm (0x00E440),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	//-----FRAM Write(OFTV_MUX4)---
	WriteComm (0x00E470),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	//-----FRAM Write(OFTV_MUX5)---
	WriteComm (0x00E4A0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	//-----FRAM Write(OFTV_MUX6)---
	WriteComm (0x00E4D0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	//-----FRAM Write(OFTV_MUX7)---
	WriteComm (0x00E500),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	//-----FRAM Write(OFTV_MUX8)---
	WriteComm (0x00E530),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	
	
	WriteComm (0x00F236),//CFB
	WriteData (0x4700),
	WriteComm (0x00F506),//OFTV TRIM
	WriteData (0x0000),
	
	//------SUB_SPI------ 
	WriteComm (0x00F312),
	WriteData (0x0200),
	WriteComm (0x00F314),
	WriteData (0x0138),
	
	//------Serial_Buff_WR------ 
	WriteComm (0x00F51A),
	WriteData (0x0001),
	WriteComm (0x00F51A),
	WriteData (0x0000),
	Delay_ms (50),
	WriteComm (0x00F000),
	WriteData (0x0002),
	Delay_ms (100),
};

AFE_CMD_T test_cmd_open_mux_on[] = {
	WriteComm (0x537123),
	WriteData (0xA53C),
	WriteComm (0x537123),
	WriteData (0x1455),
	WriteComm (0x537123),
	WriteData (0x7555),
	WriteComm (0x537123),
	WriteData (0x5555),
	
	//-----------------------------Reset AFE & Driver Start------------------
	WriteComm (0x00F300),
	WriteData (0x5AA5),
	WriteComm (0x00F302),
	WriteData (0x0001),
	
	Delay_ms (50),
	
	
	WriteComm (0x00F236),//CFB
	WriteData (0x4700),
	WriteComm (0x00F506),//OFTV TRIM
	WriteData (0x0000),
	WriteComm (0x00F402),//OPEN Test bit
	WriteData (0x5100),

	////////Set Tx to 3V. Workaround of Tx=4V issue.
	WriteComm (0x00F042), 
	WriteData (0x4100),//TVH=3V, TVL=0V
	WriteComm (0x00F046),
	WriteData (0x0050), //VCMMVSEL=0
	////////Set Tx to 3V. Workaround of Tx=4V issue.
	
	
	//------SUB_SPI------ 
	WriteComm (0x00F312),
	WriteData (0x0200),
	WriteComm (0x00F314),
	WriteData (0x0138),
	
	//------Serial_Buff_WR------ 
	WriteComm (0x00F51A),
	WriteData (0x0001),
	WriteComm (0x00F51A),
	WriteData (0x0000),
	Delay_ms (50),
	WriteComm (0x00F000),
	WriteData (0x0002),
	Delay_ms (100),
};

AFE_CMD_T test_cmd_short_even[] = {
	WriteComm (0x537123),
	WriteData (0xA53C),
	WriteComm (0x537123),
	WriteData (0x1455),
	WriteComm (0x537123),
	WriteData (0x7555),
	WriteComm (0x537123),
	WriteData (0x5555),
	
	//-----------------------------Reset AFE & Driver Start------------------
	WriteComm (0x00F300),
	WriteData (0x5AA5),
	WriteComm (0x00F302),
	WriteData (0x0001),
	
	Delay_ms (50),
	
	WriteComm (0x00F000), 
	WriteData (0x0000),
	Delay_ms (20),
	/*FRAM[0x0000], (NsUnit[00]: Touch, TxFreq=104.1K, Base=0) */
	WriteComm (0x00E000),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0020], (NsUnit[01]: Touch, TxFreq=95.24K, Base=0) */
	WriteComm (0x00E020),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0040], (NsUnit[02]: Touch, TxFreq=84.75K, Base=0) */
	WriteComm (0x00E040),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0060], (NsUnit[03]: Touch, TxFreq=74.07K, Base=0) */
	WriteComm (0x00E060),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0080], (NsUnit[04]: Touch, TxFreq=66.7K, Base=0) */
	WriteComm (0x00E080),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x00A0], (NsUnit[05]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E0A0),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x00C0], (NsUnit[06]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E0C0),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x00E0], (NsUnit[07]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E0E0),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0100], (NsUnit[08]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E100),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0120], (NsUnit[09]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E120),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0140], (NsUnit[10]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E140),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0160], (NsUnit[11]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E160),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0180], (NsUnit[12]: Proximity, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E180),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x01A0], (NsUnit[13]: Proximity, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E1A0),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	//-----FRAM Write(Coef Base0)---
	WriteComm (0x00E1C0),
	WriteData (0x0000),//#1 TX_P
	WriteData (0x0000),//#1 TX_N
	WriteData (0x0000),//#2 TX_P
	WriteData (0x0000),//#2 TX_N
	WriteData (0x0000),//#3 TX_P
	WriteData (0x0000),//#3 TX_N
	WriteData (0x0000),//#4 TX_P
	WriteData (0x0000),//#4 TX_N
	WriteData (0x0000),//#5 TX_P
	WriteData (0x0000),//#5 TX_N
	WriteData (0x0000),//#6 TX_P
	WriteData (0x0000),//#6 TX_N
	WriteData (0x0004),//#7 TX_P
	WriteData (0x0000),//#7 TX_N
	WriteData (0x0000),//#8 TX_P
	WriteData (0x0000),//#8 TX_N
	WriteData (0x0000),//#9 TX_P
	WriteData (0x0000),//#9 TX_N
	WriteData (0x0000),//#10 TX_P
	WriteData (0x0000),//#10 TX_N
	WriteData (0x0000),//#11 TX_P
	WriteData (0x0000),//#11 TX_N
	WriteData (0x0000),//#12 TX_P
	WriteData (0x0000),//#12 TX_N
	WriteData (0x0000),//#13 TX_P
	WriteData (0x0000),//#13 TX_N
	WriteData (0x0000),//#14 TX_P
	WriteData (0x0000),//#14 TX_N
	WriteData (0x0000),//#15 TX_P
	WriteData (0x0000),//#15 TX_N
	WriteData (0x0000),//#16 TX_P
	WriteData (0x0000),//#16 TX_N
	//----AFE Reg
	WriteComm (0x00F024), //Glob_Hopping_Unit=9
	WriteData (0x9090),
	WriteComm (0x00F206), //Mulit-Noise
	WriteData (0x0108),
	WriteComm (0x00F20C),
	WriteData (0x6100),
	WriteComm (0x00F20E), //Short_EN
	WriteData (0x0323),
	WriteComm (0x00F21C), //ADC_OFFSET
	WriteData (0x1000),
	WriteComm (0x00F230), //NOR_TRUNC_TP
	WriteData (0x0000),
	WriteComm (0x00F23C), //MUXATable
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0808),
	WriteData (0x0808),
	WriteComm (0x00F248), //MUXBTable
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0808),
	WriteData (0x0808),
	WriteComm (0x00F25C),
	WriteData (0x875C),
	WriteComm (0x00F260), //ADC_SEN_FREQ_TP
	WriteData (0x0808),
	//======Default Sensor Mapping=====//
	WriteComm (0x00F074),
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0809),
	WriteData (0x0A0B),
	WriteData (0x0C0D),
	WriteData (0x0E0F),
	WriteData (0x1011),
	WriteData (0x1213),
	WriteData (0x1415),
	WriteData (0x1617),
	WriteComm (0x00F08C),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0809),
	WriteData (0x0A0B),
	WriteData (0x0C0D),
	WriteData (0x0E0F),
	WriteData (0x1011),
	WriteData (0x1213),
	WriteData (0x1415),
	WriteData (0x1617),
	WriteData (0x1819),
	WriteData (0x1A1B),
	WriteData (0x1C1D),
	WriteData (0x1E1F),
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0809),
	WriteData (0x0A0B),
	WriteData (0x0C0D),
	WriteData (0x0E0F),
	WriteData (0x1011),
	WriteData (0x1213),
	WriteData (0x1415),
	WriteData (0x1617),
	WriteData (0x1819),
	WriteData (0x1A1B),
	WriteData (0x1C1D),
	WriteData (0x1E1F),
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0809),
	WriteData (0x0A0B),
	WriteData (0x0C0D),
	WriteData (0x0E0F),
	WriteData (0x1011),
	WriteData (0x1213),
	WriteData (0x1415),
	WriteData (0x1617),
	WriteData (0x1819),
	WriteData (0x1A1B),
	WriteData (0x1C1D),
	WriteData (0x1E1F),
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0809),
	WriteData (0x0A0B),
	WriteData (0x0C0D),
	WriteData (0x0E0F),
	WriteData (0x1011),
	WriteData (0x1213),
	WriteData (0x1415),
	WriteData (0x1617),
	WriteData (0x1819),
	WriteData (0x1A1B),
	WriteData (0x1C1D),
	WriteData (0x1E1F),
	//------Side Region need to set SUB_SPI------ 
	WriteComm (0x00F312), //Write to Side Addr
	WriteData (0x0200),
	WriteComm (0x00F314), //Write Length 54word
	WriteData (0x0135),
	Delay_ms (10),
	WriteComm (0x00F506),//GLOB_EN
	WriteData (0x0100),
	WriteComm (0x00F50A), //Normal_Noise_unit=0
	WriteData (0x0000),
	WriteComm (0x00F50C), //Normal_unit=0
	WriteData (0x0000),
	WriteComm (0x00F522), //SINGLE_HOPPING
	WriteData (0x0000),
	
	WriteComm (0x00F25E),//STOG
	//WriteData (0x0118),//O=1,E=0
	WriteData (0x0128),//O=0,E=1
	WriteComm (0x00F402),
	WriteData (0x0000),
	//------Serial_Buff_WR------ 
	WriteComm (0x00F51A), 
	WriteData (0x0001),
	WriteComm (0x00F51A), 
	WriteData (0x0000),
	Delay_ms (10),
	WriteComm (0x00F000), 
	WriteData (0x0002),
	Delay_ms (100),
};
	
AFE_CMD_T test_cmd_short_odd[] = {
	WriteComm (0x537123),
	WriteData (0xA53C),
	WriteComm (0x537123),
	WriteData (0x1455),
	WriteComm (0x537123),
	WriteData (0x7555),
	WriteComm (0x537123),
	WriteData (0x5555),
	
	//-----------------------------Reset AFE & Driver Start------------------
	WriteComm (0x00F300),
	WriteData (0x5AA5),
	WriteComm (0x00F302),
	WriteData (0x0001),
	
	Delay_ms (50),
	
	WriteComm (0x00F000), 
	WriteData (0x0000),
	Delay_ms (20),
	/*FRAM[0x0000], (NsUnit[00]: Touch, TxFreq=104.1K, Base=0) */
	WriteComm (0x00E000),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0020], (NsUnit[01]: Touch, TxFreq=95.24K, Base=0) */
	WriteComm (0x00E020),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0040], (NsUnit[02]: Touch, TxFreq=84.75K, Base=0) */
	WriteComm (0x00E040),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0060], (NsUnit[03]: Touch, TxFreq=74.07K, Base=0) */
	WriteComm (0x00E060),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0080], (NsUnit[04]: Touch, TxFreq=66.7K, Base=0) */
	WriteComm (0x00E080),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x00A0], (NsUnit[05]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E0A0),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x00C0], (NsUnit[06]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E0C0),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x00E0], (NsUnit[07]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E0E0),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0100], (NsUnit[08]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E100),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0120], (NsUnit[09]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E120),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0140], (NsUnit[10]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E140),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0160], (NsUnit[11]: Touch, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E160),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x0180], (NsUnit[12]: Proximity, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E180),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	/*FRAM[0x01A0], (NsUnit[13]: Proximity, TxFreq=56.2K, Base=0) */
	WriteComm (0x00E1A0),
	WriteData (0x5408),
	WriteData (0x0004),
	WriteData (0x7007),//RX_TGL_NUM
	WriteData (0x1FE0),//TBL_PRAM_TP
	WriteData (0x4000),//NOR_COEF_TP
	WriteData (0x00E0),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0088),
	WriteData (0x0000),
	WriteData (0x8000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	//-----FRAM Write(Coef Base0)---
	WriteComm (0x00E1C0),
	WriteData (0x0000),//#1 TX_P
	WriteData (0x0000),//#1 TX_N
	WriteData (0x0000),//#2 TX_P
	WriteData (0x0000),//#2 TX_N
	WriteData (0x0000),//#3 TX_P
	WriteData (0x0000),//#3 TX_N
	WriteData (0x0000),//#4 TX_P
	WriteData (0x0000),//#4 TX_N
	WriteData (0x0000),//#5 TX_P
	WriteData (0x0000),//#5 TX_N
	WriteData (0x0000),//#6 TX_P
	WriteData (0x0000),//#6 TX_N
	WriteData (0x0004),//#7 TX_P
	WriteData (0x0000),//#7 TX_N
	WriteData (0x0000),//#8 TX_P
	WriteData (0x0000),//#8 TX_N
	WriteData (0x0000),//#9 TX_P
	WriteData (0x0000),//#9 TX_N
	WriteData (0x0000),//#10 TX_P
	WriteData (0x0000),//#10 TX_N
	WriteData (0x0000),//#11 TX_P
	WriteData (0x0000),//#11 TX_N
	WriteData (0x0000),//#12 TX_P
	WriteData (0x0000),//#12 TX_N
	WriteData (0x0000),//#13 TX_P
	WriteData (0x0000),//#13 TX_N
	WriteData (0x0000),//#14 TX_P
	WriteData (0x0000),//#14 TX_N
	WriteData (0x0000),//#15 TX_P
	WriteData (0x0000),//#15 TX_N
	WriteData (0x0000),//#16 TX_P
	WriteData (0x0000),//#16 TX_N
	//----AFE Reg
	WriteComm (0x00F024), //Glob_Hopping_Unit=9
	WriteData (0x9090),
	WriteComm (0x00F206), //Mulit-Noise
	WriteData (0x0108),
	WriteComm (0x00F20C),
	WriteData (0x6100),
	WriteComm (0x00F20E), //Short_EN
	WriteData (0x0323),
	WriteComm (0x00F21C), //ADC_OFFSET
	WriteData (0x1000),
	WriteComm (0x00F230), //NOR_TRUNC_TP
	WriteData (0x0000),
	WriteComm (0x00F23C), //MUXATable
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0808),
	WriteData (0x0808),
	WriteComm (0x00F248), //MUXBTable
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0808),
	WriteData (0x0808),
	WriteComm (0x00F25C),
	WriteData (0x875C),
	WriteComm (0x00F260), //ADC_SEN_FREQ_TP
	WriteData (0x0808),
	//======Default Sensor Mapping=====//
	WriteComm (0x00F074),
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0809),
	WriteData (0x0A0B),
	WriteData (0x0C0D),
	WriteData (0x0E0F),
	WriteData (0x1011),
	WriteData (0x1213),
	WriteData (0x1415),
	WriteData (0x1617),
	WriteComm (0x00F08C),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0000),
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0809),
	WriteData (0x0A0B),
	WriteData (0x0C0D),
	WriteData (0x0E0F),
	WriteData (0x1011),
	WriteData (0x1213),
	WriteData (0x1415),
	WriteData (0x1617),
	WriteData (0x1819),
	WriteData (0x1A1B),
	WriteData (0x1C1D),
	WriteData (0x1E1F),
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0809),
	WriteData (0x0A0B),
	WriteData (0x0C0D),
	WriteData (0x0E0F),
	WriteData (0x1011),
	WriteData (0x1213),
	WriteData (0x1415),
	WriteData (0x1617),
	WriteData (0x1819),
	WriteData (0x1A1B),
	WriteData (0x1C1D),
	WriteData (0x1E1F),
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0809),
	WriteData (0x0A0B),
	WriteData (0x0C0D),
	WriteData (0x0E0F),
	WriteData (0x1011),
	WriteData (0x1213),
	WriteData (0x1415),
	WriteData (0x1617),
	WriteData (0x1819),
	WriteData (0x1A1B),
	WriteData (0x1C1D),
	WriteData (0x1E1F),
	WriteData (0x0001),
	WriteData (0x0203),
	WriteData (0x0405),
	WriteData (0x0607),
	WriteData (0x0809),
	WriteData (0x0A0B),
	WriteData (0x0C0D),
	WriteData (0x0E0F),
	WriteData (0x1011),
	WriteData (0x1213),
	WriteData (0x1415),
	WriteData (0x1617),
	WriteData (0x1819),
	WriteData (0x1A1B),
	WriteData (0x1C1D),
	WriteData (0x1E1F),
	//------Side Region need to set SUB_SPI------ 
	WriteComm (0x00F312), //Write to Side Addr
	WriteData (0x0200),
	WriteComm (0x00F314), //Write Length 54word
	WriteData (0x0135),
	Delay_ms (10),
	WriteComm (0x00F506),//GLOB_EN
	WriteData (0x0100),
	WriteComm (0x00F50A), //Normal_Noise_unit=0
	WriteData (0x0000),
	WriteComm (0x00F50C), //Normal_unit=0
	WriteData (0x0000),
	WriteComm (0x00F522), //SINGLE_HOPPING
	WriteData (0x0000),
	
	WriteComm (0x00F25E),//STOG
	WriteData (0x0118),//O=1,E=0
	//WriteData (0x0128),//O=0,E=1
	WriteComm (0x00F402),
	WriteData (0x0000),
	//------Serial_Buff_WR------ 
	WriteComm (0x00F51A), 
	WriteData (0x0001),
	WriteComm (0x00F51A), 
	WriteData (0x0000),
	Delay_ms (10),
	WriteComm (0x00F000), 
	WriteData (0x0002),
	Delay_ms (100),
};
	
AFE_CMD_T test_cmd_std[] = {
	WriteComm (0x537123),
	WriteData (0xA53C),
	WriteComm (0x537123),
	WriteData (0x1455),
	WriteComm (0x537123),
	WriteData (0x7555),
	WriteComm (0x537123),
	WriteData (0x5555),
	
	//-----------------------------Reset AFE & Driver Start------------------
	WriteComm (0x00F300),
	WriteData (0x5AA5),
	WriteComm (0x00F302),
	WriteData (0x0001),
	
	Delay_ms (50),


	WriteComm (0x00F000),
	WriteData (0x0002),
	Delay_ms (100),
};
