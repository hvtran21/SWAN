#include "dw3000.h"
#include "SPI.h"
#include "esp_efuse.h"
#define IS_TAG

extern SPISettings _fastSPI;

/* below are pin definitions for esp32-c3 development board */
// #define PIN_RST 8 
// #define PIN_IRQ 1
// #define PIN_SS 7

/* below are pin definitions for esp32-c3 sdp pcb */
#define SPI_IRQ 4

#define SPI_CLK 5
#define SPI_MISO 6
#define SPI_MOSI 7
#define SPI_CSN 8

#define SPI_RST 9

#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
#define ALL_MSG_COMMON_LEN 5
#define ALL_DATA_COMMON_LEN 2      // common first two bytes to recognize data transmission


#define ALL_MSG_SN_IDX 2
#define RESP_MSG_POLL_RX_TS_IDX 6
#define RESP_MSG_RESP_TX_TS_IDX 10
#define RESP_MSG_DATA_START_IDX 2  // index where data starts

#define POLL_RX_TO_RESP_TX_DLY_UUS 450

#define SIZE_OF_RX_BUFFER 20

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
static uint64_t unique_ID = ESP.getEfuseMac();
static uint8_t rx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 0xE0, 0, 0, 0, 0, 0, 0};
static uint8_t tx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static uint8_t rx_data_poll_msg[] = {0xAA, 0xDD, 0, 0, 0, 0, 0, 0};
static uint8_t tx_data_resp_msg[] = {0xAA, 0xCC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static uint8_t distance_frame_counter = 0;
static uint8_t data_frame_counter = 0;
static uint8_t rx_buffer[SIZE_OF_RX_BUFFER];
static uint32_t status_reg = 0;
static uint64_t poll_rx_ts;
static uint64_t resp_tx_ts;

extern dwt_txconfig_t txconfig_options;

void setup() {
  UART_init();

  _fastSPI = SPISettings(16000000L, MSBFIRST, SPI_MODE0);

  pinMode(SPI_IRQ, OUTPUT);
  
  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, SPI_CSN);
  spiSelect(SPI_CSN);
  delay(100);  // Add a small delay to allow the reset to take effect.
  

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

  /* Next can enable TX/RX states output on GPIOs 5 and 6 to help debug, and also TX/RX LEDs
   * Note, in real low power applications the LEDs should not be used. */
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

  for (int i = 0; i < SIZE_OF_RX_BUFFER; i++) {
    rx_buffer[i] = 0;
  }

  Serial.println("Range TX");
  Serial.println("Setup over........");
}

void send_distance() {
    /* Activate reception immediately. */
  dwt_rxenable(DWT_START_RX_IMMEDIATE);

  /* Poll for reception of a frame or error/timeout. See NOTE 6 below. */
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR)))
  {
  };

  if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
  {
    uint32_t frame_len;
    /* Clear good RX frame event in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

    /* A frame has been received, read it into the local buffer. */
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len <= sizeof(rx_buffer))
    {
      dwt_readrxdata(rx_buffer, frame_len, 0);

      /* Check that the frame is a poll sent by "SS TWR initiator" example.
       * As the sequence number field of the frame is not relevant, it is cleared to simplify the validation of the frame. */
      rx_buffer[ALL_MSG_SN_IDX] = 0;
      if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) == 0)
      {
        uint32_t resp_tx_time;
        int ret;

        /* Retrieve poll reception timestamp. */
        poll_rx_ts = get_rx_timestamp_u64();

        /* Compute response message transmission time. See NOTE 7 below. */
        resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
        dwt_setdelayedtrxtime(resp_tx_time);

        /* Response TX timestamp is the transmission time we programmed plus the antenna delay. */
        resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

        /* Write all timestamps in the final message. See NOTE 8 below. */
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts); //resp_tx_ts is 64 bit, takes 8 slots in array

        /* Write and send the response message. See NOTE 9 below. */
        tx_resp_msg[ALL_MSG_SN_IDX] = distance_frame_counter;
        dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0); /* Zero offset in TX buffer. */
        dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 1);          /* Zero offset in TX buffer, ranging. */
        ret = dwt_starttx(DWT_START_TX_DELAYED);

        /* If dwt_starttx() returns an error, abandon this ranging exchange and proceed to the next one. See NOTE 10 below. */
        if (ret == DWT_SUCCESS)
        {
          /* Poll DW IC until TX frame sent event set. See NOTE 6 below. */
          while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK))
          {
          };

          /* Clear TXFRS event. */
          dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);

          /* Increment frame sequence number after transmission of the poll message (modulo 256). */
          distance_frame_counter++;
        }
      }
    }
  }
  else
  {
    /* Clear RX error events in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
  }
}

void send_data() {
  // this function is waiting to recieve a data request to transfer data
      /* Activate reception immediately. */
  dwt_rxenable(DWT_START_RX_IMMEDIATE);

  /* Poll for reception of a frame or error/timeout. See NOTE 6 below. */
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR)))
  {
  };

  if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
  {
    uint32_t frame_len;
    /* Clear good RX frame event in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

    /* A frame has been received, read it into the local buffer. */
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len <= sizeof(rx_buffer))
    {
      dwt_readrxdata(rx_buffer, frame_len, 0);
      /* Check that the frame is a poll sent by "SS TWR initiator" example.
       * As the sequence number field of the frame is not relevant, it is cleared to simplify the validation of the frame. */
      rx_buffer[ALL_MSG_SN_IDX] = 0;
      if (memcmp(rx_buffer, rx_data_poll_msg, ALL_DATA_COMMON_LEN) == 0)
      {
        int ret;
        uint64_t tmp_id = unique_ID;

        for (int i = sizeof(tx_data_resp_msg)-1; i >= RESP_MSG_DATA_START_IDX; i--) {
            if (tmp_id == 0) { 
                tx_data_resp_msg[i] = '0';  // Fill with zeros if tmp_id is exhausted
            } else {
                tx_data_resp_msg[i] = (tmp_id % 10) + '0';
                tmp_id = tmp_id / 10;
            }
        }

        dwt_writetxdata(sizeof(tx_data_resp_msg), tx_data_resp_msg, 0); /* Zero offset in TX buffer. */
        dwt_writetxfctrl(sizeof(tx_data_resp_msg), 0, 1);          /* Zero offset in TX buffer, ranging. */
        ret = dwt_starttx(DWT_START_TX_IMMEDIATE);

        /* If dwt_starttx() returns an error, abandon this ranging exchange and proceed to the next one. See NOTE 10 below. */
        if (ret == DWT_SUCCESS)
        {
          /* Poll DW IC until TX frame sent event set. See NOTE 6 below. */
          while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK))
          {
          };
          dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
        }
      }
    }
  }
  else
  {
    /* Clear RX error events in the DW IC status register. */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
  }
}

void loop() {
  send_distance();  // function waits for distance request, and sends distance back to anchor 
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);   // clear recieve 
  send_data(); // function waits for data request, and requests data

}
