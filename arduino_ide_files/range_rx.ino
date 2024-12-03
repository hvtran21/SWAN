#include "dw3000.h"
#include "esp_efuse.h"
#include "SPI.h"

#define IS_ANCHOR

#define SPI_IRQ 4

#define SPI_CLK 5
#define SPI_MISO 6
#define SPI_MOSI 7
#define SPI_CSN 8

#define SPI_RST 9

#define RNG_DELAY_MS 1000
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
#define ALL_MSG_COMMON_LEN 5
#define ALL_DATA_COMMON_LEN 2      // common first two bytes to recognize data transmission

#define ALL_MSG_SN_IDX 2
#define RESP_MSG_POLL_RX_TS_IDX 6
#define RESP_MSG_RESP_TX_TS_IDX 10
#define RESP_MSG_DATA_START_IDX 2  // index where data starts

// #define POLL_TX_TO_RESP_RX_DLY_UUS 240
// #define RESP_RX_TIMEOUT_UUS 400
#define POLL_TX_TO_RESP_RX_DLY_UUS 400
#define RESP_RX_TIMEOUT_UUS 2000

#define SIZE_OF_RX_BUFFER 20
#define SIZE_OF_RX_DATA_BUFFER 40
#define VERIFIED_PIN 2


/* Default communication configuration. We use default non-STS DW mode. */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_128,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (129 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, /* STS disabled */
    DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       /* PDOA mode off */
};

static uint8_t tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 0xE0, 0, 0, 0, 0, 0, 0};
static uint8_t rx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static uint8_t tx_data_poll_msg[] = {0xAA, 0xDD, 0, 0, 0, 0, 0, 0};
static uint8_t rx_resp_data_msg[] = {0xAA, 0xCC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static uint8_t frame_seq_nb = 0;
static uint8_t rx_buffer[SIZE_OF_RX_BUFFER];
static uint8_t rx_data_buffer[SIZE_OF_RX_DATA_BUFFER];
static uint32_t status_reg = 0;
static double tof;
static double distance;
extern dwt_txconfig_t txconfig_options;
bool verified = false;

void setup() {
  UART_init();
  /*next two lines is the original code*/
  pinMode(SPI_IRQ, OUTPUT);
  pinMode(VERIFIED_PIN, OUTPUT);
  
  // spiBegin(SPI_IRQ, SPI_RST);
  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, SPI_CSN);
  spiSelect(SPI_CSN);

  delay(2); // Time needed for DW3000 to start up (transition from INIT_RC to IDLE_RC, or could wait for SPIRDY event)

  while (!dwt_checkidlerc()) // Need to make sure DW IC is in IDLE_RC before proceeding
  {
    UART_puts("IDLE FAILED\r\n");
    while (1)
      ;
  }

  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
  {
    UART_puts("INIT FAILED\r\n");
    while (1)
      ;
  }

  // Enabling LEDs here for debug so that for each TX the D1 LED will flash on DW3000 red eval-shield boards.
  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

  /* Configure DW IC. See NOTE 6 below. */
  if (dwt_configure(&config)) // if the dwt_configure returns DWT_ERROR either the PLL or RX calibration has failed the host should reset the device
  {
    UART_puts("CONFIG FAILED\r\n");
    while (1)
      ;
  }

  /* Configure the TX spectrum parameters (power, PG delay and PG count) */
  dwt_configuretxrf(&txconfig_options);

  /* Apply default antenna delay value. See NOTE 2 below. */
  dwt_setrxantennadelay(RX_ANT_DLY);
  dwt_settxantennadelay(TX_ANT_DLY);

  /* Set expected response's delay and timeout. See NOTE 1 and 5 below.
   * As this example only handles one incoming frame with always the same delay and timeout, those values can be set here once for all. */
  dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
  dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);

  /* Next can enable TX/RX states output on GPIOs 5 and 6 to help debug, and also TX/RX LEDs
   * Note, in real low power applications the LEDs should not be used. */
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

  for (int i = 0; i < SIZE_OF_RX_BUFFER; i++) {
    rx_buffer[i] = 0;
  }
  for (int i = 0; i < SIZE_OF_RX_DATA_BUFFER; i++) {
    rx_data_buffer[i] = 0;
  }

  Serial.println("Range RX");
  Serial.println("Setup over........");
}

void get_distance() {
  tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0); /* Zero offset in TX buffer. */
  dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);          /* Zero offset in TX buffer, ranging. */

  /* Start transmission, indicating that a response is expected so that reception is enabled automatically after the frame is sent and the delay
   * set by dwt_setrxaftertxdelay() has elapsed. */
  dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

  /* We assume that the transmission is achieved correctly, poll for reception of a frame or error/timeout. See NOTE 8 below. */
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)))
  {
  };

  /* Increment frame sequence number after transmission of the poll message (modulo 256). */
  frame_seq_nb++;
  if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
  {
    uint32_t frame_len;

    /* Clear good RX frame event in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

    /* A frame has been received, see if it can be stored in local buffer */
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len <= sizeof(rx_buffer)) 
    {
      dwt_readrxdata(rx_buffer, frame_len, 0);

      /* Check that the frame is the expected response from the companion "SS TWR responder" example.
       * As the sequence number field of the frame is not relevant, it is cleared to simplify the validation of the frame. */
      rx_buffer[ALL_MSG_SN_IDX] = 0;
      if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) == 0)
      { 
        uint32_t poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts;
        int32_t rtd_init, rtd_resp;
        float clockOffsetRatio;

        /* Retrieve poll transmission and response reception timestamps. See NOTE 9 below. */
        poll_tx_ts = dwt_readtxtimestamplo32();
        resp_rx_ts = dwt_readrxtimestamplo32();

        /* Read carrier integrator value and calculate clock offset ratio. See NOTE 11 below. */
        clockOffsetRatio = ((float)dwt_readclockoffset()) / (uint32_t)(1 << 26);

        /* Get timestamps embedded in response message. */
        resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
        resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);

        /* Compute time of flight and distance, using clock offset ratio to correct for differing local and remote clock rates */
        rtd_init = resp_rx_ts - poll_tx_ts;
        rtd_resp = resp_tx_ts - poll_rx_ts;

        tof = ((rtd_init - rtd_resp * (1 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
        distance = tof * SPEED_OF_LIGHT;
        
        test_run_info((unsigned char *)dist_str);
      }
    }
  } 
  
  else
  {
    /* Clear RX error/timeout events in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
  }
}

void get_data() {
  /* 
    this function requests for a data frame, and waits to recieve it
  */
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  dwt_writetxdata(sizeof(tx_data_poll_msg), tx_data_poll_msg, 0); /* Zero offset in TX buffer. */
  dwt_writetxfctrl(sizeof(tx_data_poll_msg), 0, 1);          /* Zero offset in TX buffer, ranging. */

  /* Start transmission, indicating that a response is expected so that reception is enabled automatically after the frame is sent and the delay
   * set by dwt_setrxaftertxdelay() has elapsed. */
  dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

  /* We assume that the transmission is achieved correctly, poll for reception of a frame or error/timeout. See NOTE 8 below. */
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)))
  {
  };

  if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
  {
    uint32_t frame_len;

    /* Clear good RX frame event in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

    /* A frame has been received, read it into the local buffer. */
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len <= sizeof(rx_data_buffer))
    {
      memset(rx_data_buffer, 0, sizeof(rx_data_buffer));  // clear local buffer to read new data
    
      dwt_readrxdata(rx_data_buffer, frame_len+2, 0);
      /* Check that the frame is the expected response from the companion "SS TWR responder" example.
       * As the sequence number field of the frame is not relevant, it is cleared to simplify the validation of the frame. */
      if (memcmp(rx_data_buffer, rx_resp_data_msg, ALL_DATA_COMMON_LEN) == 0)
      { 
        Serial.println("Data received, and verified.");
        verified = true;
      }
    }
  } 
  
  else
  { 
    /* Clear RX error/timeout events in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
  }
}

void loop()
{
  get_distance();   // this function requests a tag for distance, recieves and calculates tof to obtain distance
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);   // clear recieve 
  Serial.println(distance);
  get_data();            // request data frame
  if (distance <= 1 and verified) {
    Serial.println("pin set high");
    digitalWrite(VERIFIED_PIN, HIGH);
  } else {
    digitalWrite(VERIFIED_PIN, LOW);
    Serial.println("pin set low");
  }
  Sleep(RNG_DELAY_MS);   // sleep after getting distance values

} 