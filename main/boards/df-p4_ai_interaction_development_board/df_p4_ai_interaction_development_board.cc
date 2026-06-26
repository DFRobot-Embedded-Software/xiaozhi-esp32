#include "wifi_board.h"
#include "codecs/es8388_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "backlight.h"
#include "display/lcd_display.h"
#include "lcd_init_cmds.h"

#include "led/gpio_led.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "DfP4AiInteractionDevBoard"

class DfP4AiInteractionDevelopmentBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_ = nullptr;

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    static void EnableDsiPhyPower() {
#if MIPI_DSI_PHY_PWR_LDO_CHAN > 0
        static esp_ldo_channel_handle_t phy_pwr_chan = nullptr;
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
        ESP_LOGI(TAG, "MIPI DSI PHY powered on (LDO CH%d %dmV)",
                 MIPI_DSI_PHY_PWR_LDO_CHAN, MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV);
#endif
    }

    static void ResetLcdPanel() {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << PIN_NUM_LCD_RST,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));

        const int assert_level = LCD_RST_ACTIVE_HIGH ? 1 : 0;
        const int release_level = LCD_RST_ACTIVE_HIGH ? 0 : 1;
        gpio_set_level(PIN_NUM_LCD_RST, assert_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(PIN_NUM_LCD_RST, release_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    static void SendPanelInitCommands(esp_lcd_panel_io_handle_t io) {
        for (size_t i = 0; i < sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]); i++) {
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(
                io, lcd_init_cmds[i].cmd, lcd_init_cmds[i].data, lcd_init_cmds[i].data_bytes));
            if (lcd_init_cmds[i].delay_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(lcd_init_cmds[i].delay_ms));
            }
        }
    }

    void InitializeLcd() {
        EnableDsiPhyPower();

        esp_lcd_dsi_bus_handle_t mipi_dsi_bus = nullptr;
        esp_lcd_dsi_bus_config_t bus_config = {
            .bus_id = 0,
            .num_data_lanes = LCD_MIPI_DSI_LANE_NUM,
            .lane_bit_rate_mbps = LCD_MIPI_DSI_LANE_BITRATE_MBPS,
        };
        ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

        esp_lcd_panel_io_handle_t io = nullptr;
        esp_lcd_dbi_io_config_t dbi_config = {
            .virtual_channel = 0,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io));

        ResetLcdPanel();
        SendPanelInitCommands(io);

        esp_lcd_dpi_panel_config_t dpi_config = {
            .virtual_channel = 0,
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = LCD_MIPI_DPI_CLK_MHZ,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing = {
                .h_size = DISPLAY_WIDTH,
                .v_size = DISPLAY_HEIGHT,
                .hsync_pulse_width = 2,
                .hsync_back_porch = 40,
                .hsync_front_porch = 40,
                .vsync_pulse_width = 2,
                .vsync_back_porch = 10,
                .vsync_front_porch = 180,
            },
            .flags = {
                .use_dma2d = true,
            },
        };

        esp_lcd_panel_handle_t panel = nullptr;
        ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

        esp_err_t disp_err = esp_lcd_panel_disp_on_off(panel, true);
        if (disp_err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGI(TAG, "DPI panel: disp_on_off not supported (enabled via vendor init)");
        } else {
            ESP_ERROR_CHECK(disp_err);
        }

        display_ = new MipiLcdDisplay(io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                      DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                      DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        ESP_LOGI(TAG, "MIPI LCD initialized (%dx%d, TL043WVV02-B1900A)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

public:
    DfP4AiInteractionDevelopmentBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeLcd();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();
    }

    virtual Led* GetLed() override {
        static GpioLed led(BUILTIN_LED_GPIO, 0);
        return &led;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8388AudioCodec audio_codec(
            i2c_bus_,
            I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8388_ADDR);
        return &audio_codec;
    }
};

DECLARE_BOARD(DfP4AiInteractionDevelopmentBoard);
