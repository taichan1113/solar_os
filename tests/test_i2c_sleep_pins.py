from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class I2cSleepPinsTest(unittest.TestCase):
    def test_bus_ownership_sleep_isolation_and_failure_rollback(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('Host C compiler required')
        # Compile the actual bus initialization/teardown functions. Remaining
        # transfer code has no role in GPIO sleep ownership and is excluded.
        source = (ROOT / 'src/drivers/i2c_bus.c').read_text()
        source = source[:source.index('esp_err_t i2c_bus_set_speed(')]
        stubs = {
            'esp_err.h': '''#pragma once
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_NO_MEM, ESP_ERR_NOT_SUPPORTED };
''',
            'esp_check.h': '''#define ESP_RETURN_ON_ERROR(expr, tag, fmt, ...) do { int e=(expr); if(e) return e; } while(0)
''',
            'esp_log.h': '#define ESP_LOGI(...) ((void)0)\n',
            'freertos/FreeRTOS.h': '#define portMAX_DELAY 0\n',
            'freertos/semphr.h': '''typedef void *SemaphoreHandle_t;
static inline void *xSemaphoreCreateMutex(void) { return (void*)1; }
static inline int xSemaphoreTake(void *s, int t) { (void)s; (void)t; return 1; }
static inline int xSemaphoreGive(void *s) { (void)s; return 1; }
''',
            'driver/gpio.h': '''#pragma once
#include "esp_err.h"
typedef int gpio_num_t;
#define GPIO_IS_VALID_GPIO(p) ((p)>=0 && (p)<49)
esp_err_t gpio_sleep_sel_dis(int);
esp_err_t gpio_sleep_sel_en(int);
esp_err_t gpio_reset_pin(int);
''',
            'driver/i2c_master.h': '''#pragma once
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
enum { I2C_NUM_MAX=2, I2C_CLK_SRC_DEFAULT, I2C_ADDR_BIT_LEN_7 };
typedef struct { int i2c_port, sda_io_num, scl_io_num, clk_source, glitch_ignore_cnt;
 struct { int enable_internal_pullup; } flags; } i2c_master_bus_config_t;
typedef struct { int dev_addr_length, device_address; unsigned scl_speed_hz; } i2c_device_config_t;
int i2c_new_master_bus(const i2c_master_bus_config_t *, void **);
int i2c_master_get_bus_handle(int, void **);
int i2c_del_master_bus(void *);
int i2c_master_bus_add_device(void *, const i2c_device_config_t *, void **);
''',
            'solar_os_board.h': '''#define SOLAR_OS_BOARD_I2C_PORT 0
#define SOLAR_OS_BOARD_PIN_I2C_SDA 47
#define SOLAR_OS_BOARD_PIN_I2C_SCL 48
''',
        }
        harness = '''
#include <assert.h>
static int isolated[49], fail_pin=-1, new_result, deletes, resets;
int gpio_sleep_sel_dis(int pin) {
    if(pin==fail_pin) return ESP_ERR_INVALID_STATE;
    isolated[pin]=0; return ESP_OK;
}
int gpio_sleep_sel_en(int pin) { isolated[pin]=1; return ESP_OK; }
int gpio_reset_pin(int pin) { (void)pin; resets++; return ESP_OK; }
int i2c_new_master_bus(const i2c_master_bus_config_t *c, void **h) {
    (void)c; if(!new_result) *h=(void*)2; return new_result;
}
int i2c_master_get_bus_handle(int port, void **h) { (void)port; *h=(void*)3; return ESP_OK; }
int i2c_del_master_bus(void *h) { (void)h; deletes++; return ESP_OK; }
int i2c_master_bus_add_device(void *h, const i2c_device_config_t *c, void **d) {
    (void)h; (void)c; *d=(void*)4; return ESP_OK;
}
int main(void) {
    for(int i=0;i<49;i++) isolated[i]=1;
    i2c_bus_config_t c={0,47,48,100000}; void *h=NULL; bool owned=false;
    assert(i2c_bus_start_config(&c,true,&h,&owned)==ESP_OK && owned);
    assert(isolated[47]==!KEEP && isolated[48]==!KEEP);
    for(int i=0;i<47;i++) assert(isolated[i]); /* All other GPIOs unchanged. */
    assert(i2c_bus_stop_config(&c,h,owned)==ESP_OK);
    assert(deletes==1 && resets==2 && isolated[47] && isolated[48]);
    new_result=ESP_ERR_INVALID_STATE;
    assert(i2c_bus_start_config(&c,true,&h,&owned)==ESP_OK && !owned);
    assert(isolated[47]==!KEEP && isolated[48]==!KEEP);
    assert(i2c_bus_stop_config(&c,h,owned)==ESP_OK && deletes==1);
    assert(isolated[47]==!KEEP); /* Borrowed shutdown must not change owner pins. */
    gpio_sleep_sel_en(47); gpio_sleep_sel_en(48); new_result=0;
    c.sda_pin=45; c.scl_pin=46;
    assert(i2c_bus_start_config(&c,true,&h,&owned)==ESP_OK);
    assert(isolated[45] && isolated[46]); /* Alternate bus pins unaffected. */
    c.sda_pin=47; c.scl_pin=48; c.port=1;
    assert(i2c_bus_start_config(&c,true,&h,&owned)==ESP_OK);
    assert(isolated[47] && isolated[48]); /* Different port unaffected. */
    c.port=0;
    if(KEEP) {
        fail_pin=48; int before=deletes;
        assert(i2c_bus_start_config(&c,true,&h,&owned)==ESP_ERR_INVALID_STATE);
        assert(!h && !owned && deletes==before+1 && isolated[47] && isolated[48]);
        new_result=ESP_ERR_INVALID_STATE; before=deletes;
        assert(i2c_bus_start_config(&c,true,&h,&owned)==ESP_ERR_INVALID_STATE);
        assert(!h && !owned && deletes==before); /* Cannot delete a borrowed owner. */
        fail_pin=-1; new_result=0;
    }
    assert(i2c_bus_init_config(&c)==ESP_OK);
    assert(isolated[47]==!KEEP && isolated[48]==!KEEP);
    assert(i2c_bus_init_config(&c)==ESP_OK); /* Idempotent compatibility init. */
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            shutil.copyfile(ROOT / 'src/drivers/i2c_bus.h', folder / 'i2c_bus.h')
            for name, contents in stubs.items():
                target = folder / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(contents)
            cfile = folder / 'bus.c'
            cfile.write_text(source + harness)
            for keep in (0, 1):
                with self.subTest(board_policy=keep):
                    executable = folder / f'bus-{keep}'
                    result = subprocess.run([
                        compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-unused-variable',
                        f'-DKEEP={keep}', f'-DSOLAR_OS_BOARD_I2C_KEEP_PINS_IN_LIGHT_SLEEP={keep}',
                        '-I', str(folder), str(cfile), '-o', str(executable),
                    ], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    result = subprocess.run([str(executable)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
