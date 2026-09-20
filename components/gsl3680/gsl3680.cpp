#include "gsl3680.h"
#include "esphome/core/log.h"

namespace esphome {
namespace gsl3680 {

#define STOP_ON_I2C_ERROR(code, value) code = value; if (code != esphome::i2c::ERROR_OK) { ESP_LOGE(TAG, "I2C Error: %d", code); return code; }

void GSL3680::setup() {

    ESP_LOGD(TAG, "Setup start");
    // Калибровка задаётся в YAML (calibration:) — матрица тача JC8012P4A1C
    // имеет СОБСТВЕННОЕ разрешение ~1664x896 (замерено по углам 23.07),
    // а не разрешение дисплея. Ничего не перетираем здесь.

    this->reset_pin_->pin_mode(esphome::gpio::FLAG_OUTPUT);
    this->reset_pin_->setup();

    this->reset_pin_->digital_write(false);
    this->reset_pin_->digital_write(true);

    auto err = this->init();
    if (err != esphome::i2c::ERROR_OK) {
        this->mark_failed(LOG_STR("I2C init error"));  // патч под ESPHome 2026.x API
        return;
    }

    this->interrupt_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
    this->interrupt_pin_->setup();
    this->attach_interrupt_(this->interrupt_pin_, gpio::INTERRUPT_FALLING_EDGE);

    // LITE: gsl_point_id.cpp (алгоритм Silead) ВЫПИЛЕН — его линковка роняла
    // ESP32-P4 до старта FreeRTOS (assert esp_startup_start_app). Используем
    // сырые координаты из регистров чипа; для 1 пальца этого достаточно
    ESP_LOGI(TAG, "Setup complete");
}

esphome::i2c::ErrorCode GSL3680::init() {
    auto err = esphome::i2c::ERROR_OK;

    STOP_ON_I2C_ERROR(err, this->read_configuration());
    STOP_ON_I2C_ERROR(err, this->clear_registers());
    STOP_ON_I2C_ERROR(err, this->reset());
    STOP_ON_I2C_ERROR(err, this->load_firmware());
    STOP_ON_I2C_ERROR(err, this->start());
    STOP_ON_I2C_ERROR(err, this->read_ram());

    return err;
}

esphome::i2c::ErrorCode GSL3680::reset() {
    this->reset_pin_->digital_write(false);
    esphome::delay(20);
    this->reset_pin_->digital_write(true);
    esphome::delay(20);

    auto err = esphome::i2c::ERROR_OK;
    uint8_t write_buf[4];

    write_buf[0] = 0x88;
    STOP_ON_I2C_ERROR(err, this->write_register(0xe0, (uint8_t *)&write_buf, 1));
    esphome::delay(10);
    write_buf[0] = 0x04;
    STOP_ON_I2C_ERROR(err, this->write_register(0xe4, (uint8_t *)&write_buf, 1));
    esphome::delay(10);

    write_buf[0] = 0x00;
    write_buf[1] = 0x00;
    write_buf[2] = 0x00;
    write_buf[3] = 0x00;
    STOP_ON_I2C_ERROR(err, this->write_register(0xbc, (uint8_t *)&write_buf, 4));
    esphome::delay(10);

    ESP_LOGD(TAG, "Reset complete");
    return err;

}

esphome::i2c::ErrorCode GSL3680::read_configuration() {
    auto err = esphome::i2c::ERROR_OK;

    uint8_t buf[4];
    uint8_t write[4] = {0x12, 0x34, 0x56, 0x00};

    esphome::delay(50);
    STOP_ON_I2C_ERROR(err, this->read_register(0xf0, (uint8_t *)&buf, 4));
    ESP_LOGD(TAG, "Read configuration #1: %x, %x, %x, %x", buf[0], buf[1], buf[2], buf[3]);
    esphome::delay(20);
    STOP_ON_I2C_ERROR(err, this->write_register(0xf0, (uint8_t *)&write, 4));
    esphome::delay(20);
    STOP_ON_I2C_ERROR(err, this->read_register(0xf0, (uint8_t *)&buf, 4));
    esphome::delay(20);
    ESP_LOGD(TAG, "Read configuration #2: %x, %x, %x, %x", buf[0], buf[1], buf[2], buf[3]);
    if (buf[0] != write[0]) {
        ESP_LOGE(TAG, "Invalid configuration byte returned, got 0x%x, expected 0x12", buf[0]);
        return esphome::i2c::ERROR_UNKNOWN;
    }
    return err;
}

esphome::i2c::ErrorCode GSL3680::clear_registers() {
    uint8_t clear_reg_regs[4] = {0xe0, 0x88, 0xe4, 0xe0};
    uint8_t clear_reg_data[4] = {0x88, 0x01, 0x04, 0x00};

    auto err = esphome::i2c::ERROR_OK;

    for (int i = 0; i < 4; i++) {
        STOP_ON_I2C_ERROR(err, this->write_register(clear_reg_regs[i], (uint8_t *)&clear_reg_data[i], 1));
        esphome::delay(20);
    }
    ESP_LOGD(TAG, "Clear registers complete");
    return err;

}

esphome::i2c::ErrorCode GSL3680::load_firmware() {
    ESP_LOGD(TAG,"Load firmware start");
    uint8_t addr;
    uint8_t wrbuf[4];
    uint16_t source_len = sizeof(GSLX680_FW) / sizeof(struct fw_data);
    auto err = esphome::i2c::ERROR_OK;

    for(int i = 0; i < source_len; i++) {
        addr = GSLX680_FW[i].offset;
        wrbuf[0] = (uint8_t)(GSLX680_FW[i].val & 0x000000ff);
        wrbuf[1] = (uint8_t)((GSLX680_FW[i].val & 0x0000ff00) >> 8);
        wrbuf[2] = (uint8_t)((GSLX680_FW[i].val & 0x00ff0000) >> 16);
        wrbuf[3] = (uint8_t)((GSLX680_FW[i].val & 0xff000000) >> 24);
        STOP_ON_I2C_ERROR(err, this->write_register(addr, (uint8_t *)&wrbuf, addr == 0xf0? 1: 4));
    }
    ESP_LOGD(TAG,"Load firmware complete");
    return err;
}

esphome::i2c::ErrorCode GSL3680::start() {
    uint8_t write_buf[1] = {0x00};
    uint8_t addr = 0xe0;

    auto err = esphome::i2c::ERROR_OK;
    STOP_ON_I2C_ERROR(err, this->write_register(addr, (uint8_t *)&write_buf, 1));
    esphome::delay(10);
    ESP_LOGD(TAG,"Start chipset complete");

    return err;

}

esphome::i2c::ErrorCode GSL3680::read_ram() {
    uint8_t buf[4];

    auto err = esphome::i2c::ERROR_OK;
    esphome::delay(30);
    STOP_ON_I2C_ERROR(err, this->read_register(0xb0, (uint8_t *)&buf, 4));
    ESP_LOGD(TAG, "Read RAM: %x, %x, %x, %x", buf[0], buf[1], buf[2], buf[3]);
    for (int i = 0; i < 4; i++) {
        if (buf[i] != 0x5a) {
            ESP_LOGE(TAG, "Unexpected byte in read_ram: got 0x%x, expected 0x5a", buf[i]);
            return esphome::i2c::ERROR_UNKNOWN;
        }
    }
    return err;
}


void GSL3680::update_touches() {
    uint16_t x[TOUCH_MAX_POINTS];
    uint16_t y[TOUCH_MAX_POINTS];
    uint16_t touch_strength[TOUCH_MAX_POINTS];
    uint8_t touch_cnt;

    uint8_t touch_data[24];

    auto err = this->read_register(0x80, (uint8_t *)&touch_data, 24);
    if (err != esphome::i2c::ERROR_OK) {
        ESP_LOGE(TAG, "I2C error in update_touches");
        return;
    }
    ESP_LOGV(TAG, "update_touches: %x %x %x %x %x %x %x %x", touch_data[0], touch_data[1], touch_data[2], touch_data[3], touch_data[4], touch_data[5], touch_data[6], touch_data[7]);

    uint8_t finger = touch_data[0];
    if (finger > 5) finger = 5;  // в 24 байтах помещается заголовок + 5 точек

    ESP_LOGV(TAG, "raw touch: fingers=%d", finger);

    // LITE-мультитач: сырые координаты до 5 пальцев, без алгоритма Silead.
    // Точность трекинга ниже (id могут меняться), но для жестов достаточно
    for (uint8_t i = 0; i < finger; i++) {
        uint8_t base = 4 + 4 * i;
        uint16_t yi = (touch_data[base + 1] << 8) | touch_data[base];
        uint16_t xi = ((touch_data[base + 3] & 0x0f) << 8) | touch_data[base + 2];
        // первый кадр после простоя приходит с мусором в старшем байте
        if (xi > 4095 || yi > 4095) {
            ESP_LOGV(TAG, "drop garbage point %d x=%d y=%d", i, xi, yi);
            continue;
        }
        this->add_raw_touch_position_(i, xi, yi);
    }
}

}
}
