//verify by kochen use CTC4.96 20250513
//#define ST_REPLACE_TEST_CMD_BY_DISPLAY_ID
#define ST_ADDRESS_MODE_WRITE_COMMAND_V2
#define WriteComm(cmd)		{(0x01), (cmd)}
#define WriteData(data)		{(0x02), (data)}
#define Delay_ms(time)		{(0x03), (time)}
#ifdef ST_REPLACE_TEST_CMD_BY_DISPLAY_ID
unsigned char test_id_1[3] = {0x80,0xA0,0xFB};
#endif /* ST_REPLACE_TEST_CMD_BY_DISPLAY_ID */

#define ST_SELFTEST_LOG_FILE    1
#define ST_SELFTEST_LOG_PATH    "/sdcard/ST_SELFTEST_LOG.txt"
#define ST_SELFTEST_INI_PATH    "st_selftest_criteria.ini"

#ifdef	ST_SELFTEST_LOG_FILE
//#define ST_SELFTEST_EN_GKI 
#endif

#define ST_SELFTEST_IGNORE_FRAME		3
#define ST_SELFTEST_ADJUST_COUNT		0
#define ST_SELFTEST_SKIP_COLS			0  //Must set 0
//=============================================================================================================
// Self Test Criteria Section
//=============================================================================================================
#define ST_SELFTEST_SHORT_MAX			650		
#define ST_SELFTEST_NORMAL_MIN			10		//Suggest 200
#define ST_SELFTEST_NORMAL_MAX			30		//Suggest 2800
#define ST_SELFTEST_OPEN_MUX_ON_MIN 	9000	//Suggest 3000
#define ST_SELFTEST_STD_MAX				1		//Suggest 6.0
//=============================================================================================================
#define ST_SELFTEST_STD_FRAME_CNT		100
#define ST_SELFTEST_STD_CALCULATE_LIMIT 1000 //100 * 10


typedef struct sitronix_afe_cmd {
	uint8_t		type;
	uint32_t	value;
} sitronix_afe_cmd_t;

#define AFE_CMD_T	sitronix_afe_cmd_t
int sitronix_request_test_criteria(const char *name);

#ifdef ST_ADDRESS_MODE_WRITE_COMMAND_V2

unsigned char test_disable_sensor[]= {
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
	0x00,0x00,0x00,0x00,\
};
unsigned char golden_buf[] = {};
AFE_CMD_T test_flash_afe_df[]	= {};	//No default value for ST7121P.
AFE_CMD_T test_cmd_open		= {};	//Reserved for ST7123 open test.
AFE_CMD_T EnterSleepOut[]= {    
WriteComm (0x537123), //Addr
WriteData (0xa53c),
WriteComm (0x537123), //AFE Unlock 
WriteData (0x1455),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x7555),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x5555),
Delay_ms (100),
WriteComm (0x0306FC),
WriteData (0x5A9D),
WriteData (0x1100),
WriteComm (0x0306FE),
WriteData (0x00A5),
Delay_ms (100),
//MCU Reset Keep L
};
AFE_CMD_T EnterAFEMode[]= {
    WriteComm (0x537123), //Addr
WriteData (0xa53c),
WriteComm (0x537123), //AFE Unlock 
WriteData (0x1455),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x7555),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x5555),
Delay_ms (100),
};

AFE_CMD_T test_cmd_open_mux_on[]= {
WriteComm (0x537123), //Addr
WriteData (0xa53c),
WriteComm (0x537123), //AFE Unlock 
WriteData (0x1455),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x7555),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x5555),
Delay_ms (100),



/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////Reset AFE & Driver Start/////////////////////////
/////////////////////////////////////////////////////////////////////////////////
//MCU Reset Keep L

WriteComm (0x00F300),
WriteData (0x5AA5),

WriteComm (0x00F302),
WriteData (0x0001),



//------------Open_Test---------------
WriteComm (0x00F402),//OpenTest=1
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
AFE_CMD_T test_cmd_open_mux_off[]= {

}; 
AFE_CMD_T test_cmd_short_odd[] = {
WriteComm (0x537123), //Addr
WriteData (0xa53c),
WriteComm (0x537123), //AFE Unlock 
WriteData (0x1455),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x7555),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x5555),
Delay_ms (100),



//---Sleep Out--------------
WriteComm (0x0306FC),
WriteData (0x5A9D),
WriteData (0x1100),
WriteComm (0x0306FE),
WriteData (0x00A5),
Delay_ms (100),


/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////Reset AFE & Driver Start/////////////////////////
/////////////////////////////////////////////////////////////////////////////////

WriteComm (0x00F300),
WriteData (0x5AA5),


//MCU Reset Keep L
WriteComm (0x00F302),
WriteData (0x0001),

Delay_ms (100),
WriteComm (0x0101DC), 
WriteData (0x0000),
Delay_ms (100),
WriteComm (0x0101DC), 
WriteData (0xF3F3),
Delay_ms (100),
/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////Reset AFE & Driver End/////////////////////////
/////////////////////////////////////////////////////////////////////////////////


//FRAM[0x0000], (NsUnit[00]: Touch, TxFreq=104.1K, Base=0) //
WriteComm (0x00E140),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0020], (NsUnit[01]: Touch, TxFreq=95.24K, Base=0) //
WriteComm (0x00E150),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0040], (NsUnit[02]: Touch, TxFreq=84.75K, Base=0) //
WriteComm (0x00E160),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0060], (NsUnit[03]: Touch, TxFreq=74.07K, Base=0) //
WriteComm (0x00E170),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0080], (NsUnit[04]: Touch, TxFreq=66.7K, Base=0) //
WriteComm (0x00E180),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x00A0], (NsUnit[05]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E190),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x00C0], (NsUnit[06]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1A0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x00E0], (NsUnit[07]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1B0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0100], (NsUnit[08]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1C0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0120], (NsUnit[09]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1D0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0140], (NsUnit[10]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1E0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0160], (NsUnit[11]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1F0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0180], (NsUnit[12]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E200),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x01A0], (NsUnit[13]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E210),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//-----FRAM Write(Coef Base0)---
WriteComm (0x00E000),
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
WriteData (0x0004),
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
//-----FRAM Write(RX_GAIN_TP)---
WriteComm (0x00E3AC),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),

//----AFE Reg
WriteComm (0x00F004), //IRQ_SW
WriteData (0x0000),
WriteComm (0x00F024), //Glob_Hopping_Unit=9
WriteData (0x8280),
WriteComm (0x00F042), //ANA_CTRL
WriteData (0x4160),
WriteComm (0x00F044), //ANA_CTRL
WriteData (0x8011),
WriteComm (0x00F046), //ANA_CTRL
WriteData (0x0046),
WriteComm (0x00F04A),//ROW_CNT
WriteData (0x2000),
WriteComm (0x00F156), //Active_LENGTH     
WriteData (0xFFFF),
WriteComm (0x00F158), //IDLE_SELF_LENGTH     
WriteData (0xFFFF),
//------TP_Pump_SEL----------
WriteComm (0x00F146), 
WriteData (0x0081),
WriteComm (0x00F148),                        
WriteData (0x031A),
WriteComm (0x00F160), 
WriteData (0x0060),
//--------SubSPI------------------
WriteComm (0x00F200),                        
WriteData (0x9190),
WriteComm (0x00F202),                        
WriteData (0x9000),
WriteComm (0x00F206),//Mulit-Noise
WriteData (0x0108),
WriteComm (0x00F20C), //VAG_HZ_MODE=1
WriteData (0x6100),
WriteComm (0x00F21C), //ADC_OFFSET
WriteData (0x1000),
WriteComm (0x00F230),//NOR_TRUNC_TP
WriteData (0x0000),
WriteComm (0x00F234),
WriteData (0x2800),
WriteComm (0x00F236),
WriteData (0x8000),
WriteComm (0x00F254), //MUX_DLY
WriteData (0x0190),
WriteComm (0x00F25C), 
WriteData (0x875C), 
WriteComm (0x00F402),
WriteData (0x0000),
//WriteComm (0x00F162), //TRGT Mode 
//WriteData (0x0004),
//WriteComm (0x00F020), //INT_HD_DLY
//WriteData (0x5410),
//--------ShortTest_Col_table-----------
WriteComm (0x00F074), //MuxAtable
WriteData (0x0001),
WriteData (0x0203),
WriteData (0x0405),
WriteData (0x0607),
WriteData (0x0809),
WriteData (0x0A16), 
WriteComm (0x00F080), //MuxBtable
WriteData (0x0B0C),
WriteData (0x0D0E),
WriteData (0x0F10),
WriteData (0x1112),
WriteData (0x1314),
WriteData (0x1516),
//------------Short_Test---------------
WriteComm (0x00F20E),//ShortTest=1
WriteData (0x0323),
WriteComm (0x00F25E),//STOG_E=0,_O=1
WriteData (0x0118),
//WriteComm (0x00F25E),//STOG_E=1,_O=0
//WriteData (0x0128),
//------------Short_TestEnd------------
//------Serial_Buff_WR------ 
WriteComm (0x00F51A), 
WriteData (0x0001),
WriteComm (0x00F51A), 
WriteData (0x0000),
Delay_ms (100),
WriteComm (0x00F000), 
WriteData (0x0002),
Delay_ms (100),

};
AFE_CMD_T test_cmd_short_even[] = {
WriteComm (0x537123), //Addr
WriteData (0xa53c),
WriteComm (0x537123), //AFE Unlock 
WriteData (0x1455),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x7555),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x5555),
Delay_ms (100),



//---Sleep Out--------------
WriteComm (0x0306FC),
WriteData (0x5A9D),
WriteData (0x1100),
WriteComm (0x0306FE),
WriteData (0x00A5),
Delay_ms (100),


/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////Reset AFE & Driver Start/////////////////////////
/////////////////////////////////////////////////////////////////////////////////

WriteComm (0x00F300),
WriteData (0x5AA5),


//MCU Reset Keep L
WriteComm (0x00F302),
WriteData (0x0001),

Delay_ms (100),
WriteComm (0x0101DC), 
WriteData (0x0000),
Delay_ms (100),
WriteComm (0x0101DC), 
WriteData (0xF3F3),
Delay_ms (100),
/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////Reset AFE & Driver End/////////////////////////
/////////////////////////////////////////////////////////////////////////////////


//FRAM[0x0000], (NsUnit[00]: Touch, TxFreq=104.1K, Base=0) //
WriteComm (0x00E140),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0020], (NsUnit[01]: Touch, TxFreq=95.24K, Base=0) //
WriteComm (0x00E150),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0040], (NsUnit[02]: Touch, TxFreq=84.75K, Base=0) //
WriteComm (0x00E160),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0060], (NsUnit[03]: Touch, TxFreq=74.07K, Base=0) //
WriteComm (0x00E170),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0080], (NsUnit[04]: Touch, TxFreq=66.7K, Base=0) //
WriteComm (0x00E180),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x00A0], (NsUnit[05]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E190),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x00C0], (NsUnit[06]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1A0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x00E0], (NsUnit[07]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1B0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0100], (NsUnit[08]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1C0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0120], (NsUnit[09]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1D0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0140], (NsUnit[10]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1E0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0160], (NsUnit[11]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E1F0),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x0180], (NsUnit[12]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E200),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//FRAM[0x01A0], (NsUnit[13]: Touch, TxFreq=56.2K, Base=0) //
WriteComm (0x00E210),
WriteData (0x5408),
WriteData (0x0020),
WriteData (0x7007),
WriteData (0x0E00),
WriteData (0x4000),
WriteData (0x0000),
WriteData (0x4D89),
WriteData (0x430B),
//-----FRAM Write(Coef Base0)---
WriteComm (0x00E000),
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
WriteData (0x0004),
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
//-----FRAM Write(RX_GAIN_TP)---
WriteComm (0x00E3AC),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),
WriteData (0x8080),

//----AFE Reg
WriteComm (0x00F004), //IRQ_SW
WriteData (0x0000),
WriteComm (0x00F024), //Glob_Hopping_Unit=9
WriteData (0x8280),
WriteComm (0x00F042), //ANA_CTRL
WriteData (0x4160),
WriteComm (0x00F044), //ANA_CTRL
WriteData (0x8011),
WriteComm (0x00F046), //ANA_CTRL
WriteData (0x0046),
WriteComm (0x00F04A),//ROW_CNT
WriteData (0x2000),
WriteComm (0x00F156), //Active_LENGTH     
WriteData (0xFFFF),
WriteComm (0x00F158), //IDLE_SELF_LENGTH     
WriteData (0xFFFF),
//------TP_Pump_SEL----------
WriteComm (0x00F146), 
WriteData (0x0081),
WriteComm (0x00F148),                        
WriteData (0x031A),
WriteComm (0x00F160), 
WriteData (0x0060),
//--------SubSPI------------------
WriteComm (0x00F200),                        
WriteData (0x9190),
WriteComm (0x00F202),                        
WriteData (0x9000),
WriteComm (0x00F206),//Mulit-Noise
WriteData (0x0108),
WriteComm (0x00F20C), //VAG_HZ_MODE=1
WriteData (0x6100),
WriteComm (0x00F21C), //ADC_OFFSET
WriteData (0x1000),
WriteComm (0x00F230),//NOR_TRUNC_TP
WriteData (0x0000),
WriteComm (0x00F234),
WriteData (0x2800),
WriteComm (0x00F236),
WriteData (0x8000),
WriteComm (0x00F254), //MUX_DLY
WriteData (0x0190),
WriteComm (0x00F25C), 
WriteData (0x875C), 
WriteComm (0x00F402),
WriteData (0x0000),
//WriteComm (0x00F162), //TRGT Mode 
//WriteData (0x0004),
//WriteComm (0x00F020), //INT_HD_DLY
//WriteData (0x5410),
//--------ShortTest_Col_table-----------
WriteComm (0x00F074), //MuxAtable
WriteData (0x0001),
WriteData (0x0203),
WriteData (0x0405),
WriteData (0x0607),
WriteData (0x0809),
WriteData (0x0A16), 
WriteComm (0x00F080), //MuxBtable
WriteData (0x0B0C),
WriteData (0x0D0E),
WriteData (0x0F10),
WriteData (0x1112),
WriteData (0x1314),
WriteData (0x1516),
//------------Short_Test---------------
WriteComm (0x00F20E),//ShortTest=1
WriteData (0x0323),
//WriteComm (0x00F25E),//STOG_E=0,_O=1
//WriteData (0x0118),
WriteComm (0x00F25E),//STOG_E=1,_O=0
WriteData (0x0128),
//------------Short_TestEnd------------
//------Serial_Buff_WR------ 
WriteComm (0x00F51A), 
WriteData (0x0001),
WriteComm (0x00F51A), 
WriteData (0x0000),
Delay_ms (100),
WriteComm (0x00F000), 
WriteData (0x0002),
Delay_ms (100),

};    
AFE_CMD_T test_cmd_std[] = {
WriteComm (0x537123), //Addr
WriteData (0xa53c),
WriteComm (0x537123), //AFE Unlock 
WriteData (0x1455),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x7555),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x5555),
Delay_ms (100),

/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////Reset AFE & Driver Start/////////////////////////
/////////////////////////////////////////////////////////////////////////////////
//MCU Reset Keep L

WriteComm (0x00F300),
WriteData (0x5AA5),

WriteComm (0x00F302),
WriteData (0x0001),
Delay_ms (100),

WriteComm (0x00F506), //OFTV Trim on 5frame
WriteData (0x0001),
//------Serial_Buff_WR------ 
WriteComm (0x00F51A), 
WriteData (0x0001),
WriteComm (0x00F51A), 
WriteData (0x0000),
Delay_ms (100),
WriteComm (0x00F024), //7AA+0Self+2Noise
WriteData (0x72A0),
//--------END Trim---------------------------------
WriteComm (0x00F000), 
WriteData (0x0002),


};
AFE_CMD_T test_cmd_normal_rawdata[] = {
WriteComm (0x537123), //Addr
WriteData (0xa53c),
WriteComm (0x537123), //AFE Unlock 
WriteData (0x1455),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x7555),
WriteComm (0x537123), //HWRAM Unlock
WriteData (0x5555),
Delay_ms (100),



/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////Reset AFE & Driver Start/////////////////////////
/////////////////////////////////////////////////////////////////////////////////
//MCU Reset Keep L

WriteComm (0x00F300),
WriteData (0x5AA5),

WriteComm (0x00F302),
WriteData (0x0001),

//-----FRAM Write(OFTV_MER)---
WriteComm (0x00E220),//Trim11111
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
WriteData (0x7FFF),
//-----FRAM Write(OFTV_MUX0)---
WriteComm (0x00E24C),//Trim00000
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
WriteComm (0x00E278),
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
WriteComm (0x00E2A4),
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
WriteComm (0x00E2D0),
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
WriteComm (0x00E2FC),
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
WriteComm (0x00E328),
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
WriteComm (0x00E354),
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
WriteComm (0x00E380),
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

//-------Trim OFTV Flow-----------------
//WriteComm (0x00F024), //7AA+1Self+0Noise
//WriteData (0x74A0),
WriteComm (0x00F506), //OFTV Trim on 5frame
WriteData (0x0000),
//------Serial_Buff_WR------ 
WriteComm (0x00F51A), 
WriteData (0x0001),
WriteComm (0x00F51A), 
WriteData (0x0000),
Delay_ms (100),
//--------END Trim---------------------------------
WriteComm (0x00F000), 
WriteData (0x0002),
};

AFE_CMD_T test_cmd_channel_mapping[] = {

};
	

#else

#endif    
