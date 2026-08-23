#include "AC_DroneShowManager_Firmware.h"

#include <AP_Filesystem/AP_Filesystem.h>
#include <GCS_MAVLink/GCS.h>

#if CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS
#include <AP_HAL_ChibiOS/hwdef/common/stm32_util.h>
#include <hwdef.h>
#endif

#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>

extern const AP_HAL::HAL &hal;

extern "C" {
#include "esp_loader.h"
#include "esp_loader_io.h"
#include "esp_stubs.h"
}

namespace DroneShowFirmware {

#ifndef HAL_DRONESHOW_FIRMWARE_PROGRAMMER_ENABLED
#define HAL_DRONESHOW_FIRMWARE_PROGRAMMER_ENABLED (CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS)
#endif

static constexpr MAV_CMD PROGRAM_COMMAND = MAV_CMD_USER_1;
static constexpr uint32_t PROGRAM_BAUD = 460800;
static constexpr uint16_t CHUNK_SIZE = 1024;

static uint8_t file_chunk[CHUNK_SIZE];
static uint8_t flash_chunk[CHUNK_SIZE];

struct TargetConfig {
    Target target;
    const char *name;
    const char *path;
    const char *alternate_path;
    uint8_t serial_index;
#if CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS
    ioline_t en_line;
    ioline_t io_line;
#endif
    bool en_active_high;
    bool io_active_high;
};

#if HAL_DRONESHOW_FIRMWARE_PROGRAMMER_ENABLED && defined(HAL_GPIO_PIN_ESP32_EN) && defined(HAL_GPIO_PIN_ESP32_IO) && defined(HAL_GPIO_PIN_ELRS_EN) && defined(HAL_GPIO_PIN_ELRS_IO) && defined(HAL_GPIO_PIN_OLED_EN) && defined(HAL_GPIO_PIN_OLED_IO)
#define DRONESHOW_FIRMWARE_HAS_TARGET_PINS 1
#else
#define DRONESHOW_FIRMWARE_HAS_TARGET_PINS 0
#endif

#if DRONESHOW_FIRMWARE_HAS_TARGET_PINS
static const TargetConfig targets[] {
    { Target::WIFI,   "wifi",   "/APM/UPDATE/wifi.bin",   "/APM/UPDATE/WIFI.BIN",   4, HAL_GPIO_PIN_ESP32_EN, HAL_GPIO_PIN_ESP32_IO, true,  true  },
    { Target::ELRS,   "elrs",   "/APM/UPDATE/elrs.bin",   "/APM/UPDATE/ELRS.BIN",   6, HAL_GPIO_PIN_ELRS_EN,  HAL_GPIO_PIN_ELRS_IO,  true,  true  },
    { Target::SCREEN, "screen", "/APM/UPDATE/screen.bin", "/APM/UPDATE/SCREEN.BIN", 2, HAL_GPIO_PIN_OLED_EN,  HAL_GPIO_PIN_OLED_IO,  false, false },
};
#endif

class Programmer {
public:
    bool start(Target target, bool skip_verify, uint8_t requester_sysid, uint8_t requester_compid);
    bool busy() const { return _busy; }

    void thread();

    AP_HAL::UARTDriver *loader_uart() const { return _uart; }
    const TargetConfig *loader_target() const { return _cfg; }
    void enter_program_mode();
    void enter_run_mode();

private:
    const TargetConfig *find_target(Target target) const;
    const char *firmware_path(const TargetConfig &cfg, struct stat &st) const;
    bool run();
    bool flash_from_file(uint32_t file_size);
    bool verify_file(uint32_t file_size);
    bool finish_flash();
    uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len) const;
    void set_en_level(bool high);
    void set_io_level(bool high);
    void report(MAV_SEVERITY severity, const char *fmt, ...) FMT_PRINTF(3, 4);
    void send_ack(MAV_RESULT result, uint8_t progress);
    uint8_t status_mask() const;
    void progress(const char *stage, uint32_t done, uint32_t total, uint8_t &next_pct);

    volatile bool _busy;
    Target _target;
    bool _skip_verify;
    uint8_t _requester_sysid;
    uint8_t _requester_compid;
    const TargetConfig *_cfg;
    const char *_firmware_path;
    AP_HAL::UARTDriver *_uart;
};

static Programmer programmer;

bool start(Target target, bool skip_verify, uint8_t requester_sysid, uint8_t requester_compid)
{
    return programmer.start(target, skip_verify, requester_sysid, requester_compid);
}

bool busy()
{
    return programmer.busy();
}

bool Programmer::start(Target target, bool skip_verify, uint8_t requester_sysid, uint8_t requester_compid)
{
#if DRONESHOW_FIRMWARE_HAS_TARGET_PINS
    if (_busy) {
        return false;
    }

    if (find_target(target) == nullptr) {
        return false;
    }

    _target = target;
    _skip_verify = skip_verify;
    _requester_sysid = requester_sysid;
    _requester_compid = requester_compid;
    _busy = true;

    if (!hal.scheduler->thread_create(FUNCTOR_BIND_MEMBER(&Programmer::thread, void),
                                      "dsfw", 8192, AP_HAL::Scheduler::PRIORITY_IO, 0)) {
        _busy = false;
        return false;
    }
    return true;
#else
    return false;
#endif
}

void Programmer::thread()
{
    const bool ok = run();
    send_ack(ok ? MAV_RESULT_ACCEPTED : MAV_RESULT_FAILED, ok ? 100 : 0);
    report(ok ? MAV_SEVERITY_NOTICE : MAV_SEVERITY_ERROR,
           "FWPROG: %s %s", _cfg != nullptr ? _cfg->name : "target", ok ? "success" : "failed");
    if (_cfg != nullptr) {
        enter_run_mode();
    }
    _uart = nullptr;
    _cfg = nullptr;
    _firmware_path = nullptr;
    _busy = false;
}

const TargetConfig *Programmer::find_target(Target target) const
{
#if DRONESHOW_FIRMWARE_HAS_TARGET_PINS
    for (const auto &cfg : targets) {
        if (cfg.target == target) {
            return &cfg;
        }
    }
#endif
    return nullptr;
}

const char *Programmer::firmware_path(const TargetConfig &cfg, struct stat &st) const
{
    if (AP::FS().stat(cfg.path, &st) == 0 && st.st_size > 0) {
        return cfg.path;
    }
    if (AP::FS().stat(cfg.alternate_path, &st) == 0 && st.st_size > 0) {
        return cfg.alternate_path;
    }
    return nullptr;
}

bool Programmer::run()
{
    struct stat st {};

    _cfg = find_target(_target);
    if (_cfg == nullptr) {
        report(MAV_SEVERITY_ERROR, "FWPROG: invalid target");
        return false;
    }

    _firmware_path = firmware_path(*_cfg, st);
    if (_firmware_path == nullptr) {
        report(MAV_SEVERITY_ERROR, "FWPROG: missing %s", _cfg->path);
        return false;
    }

    _uart = hal.serial(_cfg->serial_index);
    if (_uart == nullptr) {
        report(MAV_SEVERITY_ERROR, "FWPROG: serial%u unavailable", unsigned(_cfg->serial_index));
        return false;
    }

    report(MAV_SEVERITY_NOTICE, "FWPROG: %s start %ld bytes", _cfg->name, long(st.st_size));
    send_ack(MAV_RESULT_IN_PROGRESS, 1);

    _uart->begin(115200, 2048, 2048);
    _uart->set_flow_control(AP_HAL::UARTDriver::FLOW_CONTROL_DISABLE);
    _uart->discard_input();
    esp_stub_set_running(false);

    if (!flash_from_file(uint32_t(st.st_size))) {
        return false;
    }

    if (!_skip_verify && !verify_file(uint32_t(st.st_size))) {
        return false;
    }

    return finish_flash();
}

bool Programmer::flash_from_file(uint32_t file_size)
{
    esp_loader_connect_args_t connect_args = ESP_LOADER_CONNECT_DEFAULT();
    connect_args.sync_timeout = 2000;
    connect_args.trials = 10;

    esp_loader_error_t err = esp_loader_connect(&connect_args);
    if (err != ESP_LOADER_SUCCESS) {
        report(MAV_SEVERITY_ERROR, "FWPROG: %s connect failed %d", _cfg->name, int(err));
        return false;
    }

    err = esp_loader_change_transmission_rate(PROGRAM_BAUD);
    if (err == ESP_LOADER_SUCCESS) {
        err = loader_port_change_transmission_rate(PROGRAM_BAUD);
    }
    if (err != ESP_LOADER_SUCCESS) {
        report(MAV_SEVERITY_ERROR, "FWPROG: %s baud failed %d", _cfg->name, int(err));
        return false;
    }

    const uint32_t aligned_size = (file_size + 3U) & ~3U;
    err = esp_loader_flash_start(0, aligned_size, CHUNK_SIZE);
    if (err != ESP_LOADER_SUCCESS) {
        report(MAV_SEVERITY_ERROR, "FWPROG: %s flash start failed %d", _cfg->name, int(err));
        return false;
    }

    int fd = AP::FS().open(_firmware_path, O_RDONLY);
    if (fd < 0) {
        report(MAV_SEVERITY_ERROR, "FWPROG: open failed %s", _firmware_path);
        return false;
    }

    uint32_t remaining = file_size;
    uint32_t written = 0;
    uint8_t next_pct = 10;

    while (remaining > 0) {
        const uint32_t to_read = MIN(remaining, uint32_t(sizeof(file_chunk)));
        uint32_t to_write = to_read;
        const int32_t got = AP::FS().read(fd, file_chunk, to_read);
        if (got != int32_t(to_read)) {
            AP::FS().close(fd);
            report(MAV_SEVERITY_ERROR, "FWPROG: read failed at %lu", (unsigned long)written);
            return false;
        }
        if (remaining == to_read && (to_write & 3U) != 0) {
            const uint32_t pad = 4U - (to_write & 3U);
            memset(&file_chunk[to_write], 0xFF, pad);
            to_write += pad;
        }
        err = esp_loader_flash_write(file_chunk, to_write);
        if (err != ESP_LOADER_SUCCESS) {
            AP::FS().close(fd);
            report(MAV_SEVERITY_ERROR, "FWPROG: write failed %d", int(err));
            return false;
        }

        written += to_read;
        remaining -= to_read;
        progress("write", written, file_size, next_pct);
    }

    AP::FS().close(fd);

    return true;
}

bool Programmer::verify_file(uint32_t file_size)
{
    int fd = AP::FS().open(_firmware_path, O_RDONLY);
    if (fd < 0) {
        report(MAV_SEVERITY_ERROR, "FWPROG: verify open failed");
        return false;
    }

    uint32_t file_crc = 0xFFFFFFFFU;
    uint32_t flash_crc = 0xFFFFFFFFU;
    uint32_t addr = 0;
    uint32_t remaining = file_size;
    uint32_t done = 0;
    uint8_t next_pct = 10;

    while (remaining > 0) {
        const uint32_t to_read = MIN(remaining, uint32_t(sizeof(file_chunk)));
        const int32_t got = AP::FS().read(fd, file_chunk, to_read);
        if (got != int32_t(to_read)) {
            AP::FS().close(fd);
            report(MAV_SEVERITY_ERROR, "FWPROG: verify file read failed");
            return false;
        }
        file_crc = crc32_update(file_crc, file_chunk, to_read);

        const esp_loader_error_t err = esp_loader_flash_read(flash_chunk, addr, to_read);
        if (err != ESP_LOADER_SUCCESS) {
            AP::FS().close(fd);
            report(MAV_SEVERITY_ERROR, "FWPROG: verify read failed %d", int(err));
            return false;
        }
        flash_crc = crc32_update(flash_crc, flash_chunk, to_read);

        addr += to_read;
        done += to_read;
        remaining -= to_read;
        progress("verify", done, file_size, next_pct);
    }

    AP::FS().close(fd);
    file_crc ^= 0xFFFFFFFFU;
    flash_crc ^= 0xFFFFFFFFU;
    if (file_crc != flash_crc) {
        report(MAV_SEVERITY_ERROR, "FWPROG: verify mismatch %08lx/%08lx",
               (unsigned long)file_crc, (unsigned long)flash_crc);
        return false;
    }
    report(MAV_SEVERITY_NOTICE, "FWPROG: verify ok %08lx", (unsigned long)file_crc);
    return true;
}

bool Programmer::finish_flash()
{
    esp_loader_error_t err = esp_loader_flash_finish(false);
    if (err == ESP_LOADER_ERROR_INVALID_RESPONSE) {
        report(MAV_SEVERITY_INFO, "FWPROG: finish ack missing, continuing");
        err = ESP_LOADER_SUCCESS;
    }
    if (err != ESP_LOADER_SUCCESS) {
        report(MAV_SEVERITY_ERROR, "FWPROG: finish failed %d", int(err));
        return false;
    }
    return true;
}

uint32_t Programmer::crc32_update(uint32_t crc, const uint8_t *data, uint32_t len) const
{
    while (len-- > 0) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++) {
            if ((crc & 1U) != 0) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void Programmer::set_en_level(bool high)
{
#if DRONESHOW_FIRMWARE_HAS_TARGET_PINS
    palWriteLine(_cfg->en_line, high == _cfg->en_active_high ? PAL_HIGH : PAL_LOW);
#endif
}

void Programmer::set_io_level(bool high)
{
#if DRONESHOW_FIRMWARE_HAS_TARGET_PINS
    palWriteLine(_cfg->io_line, high == _cfg->io_active_high ? PAL_HIGH : PAL_LOW);
#endif
}

void Programmer::enter_program_mode()
{
    const uint32_t en_hold_ms = (_cfg != nullptr && _cfg->target == Target::SCREEN) ? 3000 : 1000;
    set_io_level(false);
    hal.scheduler->delay(10);
    set_en_level(false);
    hal.scheduler->delay(en_hold_ms);
    set_en_level(true);
    hal.scheduler->delay(100);
    if (_uart != nullptr) {
        _uart->discard_input();
    }
}

void Programmer::enter_run_mode()
{
    set_io_level(true);
    hal.scheduler->delay(10);
    set_en_level(false);
    hal.scheduler->delay(1000);
    set_en_level(true);
    hal.scheduler->delay(100);
}

uint8_t Programmer::status_mask() const
{
    uint8_t mask = gcs().statustext_send_channel_mask();
    if (_uart == nullptr) {
        return mask;
    }
    for (uint8_t i = 0; i < gcs().num_gcs(); i++) {
        GCS_MAVLINK *link = gcs().chan(i);
        if (link != nullptr && link->get_uart() == _uart) {
            mask &= ~(1U << i);
        }
    }
    return mask;
}

void Programmer::report(MAV_SEVERITY severity, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    gcs().send_textv(severity, fmt, args, status_mask());
    va_end(args);
}

void Programmer::send_ack(MAV_RESULT result, uint8_t progress_pct)
{
    const uint8_t mask = status_mask();
    for (uint8_t i = 0; i < gcs().num_gcs(); i++) {
        if ((mask & (1U << i)) == 0) {
            continue;
        }
        GCS_MAVLINK *link = gcs().chan(i);
        if (link == nullptr) {
            continue;
        }
        const mavlink_channel_t chan = link->get_chan();
        if (HAVE_PAYLOAD_SPACE(chan, COMMAND_ACK)) {
            mavlink_msg_command_ack_send(chan, PROGRAM_COMMAND, result, progress_pct, 0,
                                         _requester_sysid, _requester_compid);
        }
    }
}

void Programmer::progress(const char *stage, uint32_t done, uint32_t total, uint8_t &next_pct)
{
    if (total == 0) {
        return;
    }
    const uint8_t pct = MIN((done * 100U) / total, 100U);
    while (next_pct <= 100 && pct >= next_pct) {
        report(MAV_SEVERITY_INFO, "FWPROG: %s %s %u%%", _cfg->name, stage, unsigned(next_pct));
        send_ack(MAV_RESULT_IN_PROGRESS, next_pct);
        next_pct += 10;
    }
}

} // namespace DroneShowFirmware

extern "C" {

esp_loader_error_t loader_port_write(const uint8_t *data, uint16_t size, uint32_t timeout)
{
    AP_HAL::UARTDriver *uart = DroneShowFirmware::programmer.loader_uart();
    if (uart == nullptr || data == nullptr || size == 0) {
        return ESP_LOADER_ERROR_FAIL;
    }
    const uint32_t start = AP_HAL::millis();
    uint16_t sent = 0;
    while (sent < size) {
        const size_t n = uart->write(&data[sent], size - sent);
        sent += n;
        if (sent >= size) {
            return ESP_LOADER_SUCCESS;
        }
        if (AP_HAL::millis() - start >= timeout) {
            return ESP_LOADER_ERROR_TIMEOUT;
        }
        hal.scheduler->delay(1);
    }
    return ESP_LOADER_SUCCESS;
}

esp_loader_error_t loader_port_read(uint8_t *data, uint16_t size, uint32_t timeout)
{
    AP_HAL::UARTDriver *uart = DroneShowFirmware::programmer.loader_uart();
    if (uart == nullptr || data == nullptr || size == 0) {
        return ESP_LOADER_ERROR_FAIL;
    }
    const uint32_t start = AP_HAL::millis();
    uint16_t got = 0;
    while (got < size) {
        uint8_t b;
        if (uart->read(b)) {
            data[got++] = b;
            continue;
        }
        if (AP_HAL::millis() - start >= timeout) {
            return ESP_LOADER_ERROR_TIMEOUT;
        }
        hal.scheduler->delay(1);
    }
    return ESP_LOADER_SUCCESS;
}

esp_loader_error_t loader_port_change_transmission_rate(uint32_t transmission_rate)
{
    AP_HAL::UARTDriver *uart = DroneShowFirmware::programmer.loader_uart();
    if (uart == nullptr) {
        return ESP_LOADER_ERROR_FAIL;
    }
    uart->begin(transmission_rate, 2048, 2048);
    uart->discard_input();
    return ESP_LOADER_SUCCESS;
}

void loader_port_enter_bootloader(void)
{
    DroneShowFirmware::programmer.enter_program_mode();
}

void loader_port_reset_target(void)
{
    DroneShowFirmware::programmer.enter_run_mode();
}

void loader_port_delay_ms(uint32_t ms)
{
    hal.scheduler->delay(ms);
}

static uint32_t timer_end_ms;

void loader_port_start_timer(uint32_t ms)
{
    timer_end_ms = AP_HAL::millis() + ms;
}

uint32_t loader_port_remaining_time(void)
{
    const uint32_t now = AP_HAL::millis();
    return now >= timer_end_ms ? 0 : timer_end_ms - now;
}

void loader_port_debug_print(const char *str)
{
    (void)str;
}

}
