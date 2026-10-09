"""Backport Espressif's RX-before-completion ordering to a build-local copy.

Upstream: 4118f4f72d7983063490cc1981aced0273df602d (ESP-IDF #19050).
The installed SDK is never modified. Unknown ISR layouts fail closed.
"""
from pathlib import Path
import argparse


NOTIFY = '''    if (i2c_master->event != I2C_EVENT_ALIVE) {
        xQueueSendFromISR(i2c_master->event_queue, (void *)&i2c_master->event, &HPTaskAwoken);
    }
'''
RECEIVE = '''    if (i2c_master->contains_read == true) {
        if (int_mask & I2C_LL_INTR_MST_COMPLETE || int_mask & I2C_LL_INTR_END_DETECT) {
            i2c_isr_receive_handler(i2c_master);
        }
    }
'''
COMMENT = '    /* Wait for the ISR to finish copying RX FIFO before notifying the waiter so the caller\'s buffer is complete */\n'


def patch_source(source: str) -> str:
    marker = 'static void i2c_master_isr_handler_default(void *arg)'
    if source.count(marker) != 1:
        raise ValueError('Expected one default I2C ISR')
    start = source.index(marker)
    end = source.index('    if (i2c_master->async_trans)', start)
    prefix = source[start:end]
    old = NOTIFY + RECEIVE
    fixed = RECEIVE + COMMENT + NOTIFY
    if prefix.count(old) == 1:
        prefix = prefix.replace(old, fixed)
    elif prefix.count(fixed) != 1:
        raise ValueError('Unknown I2C RX/completion ordering; review SDK source')
    return source[:start] + prefix + source[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    original = args.idf / 'components/esp_driver_i2c/i2c_master.c'
    source = original.read_text(encoding='utf-8')
    patched = patch_source(source)
    args.output.mkdir(parents=True, exist_ok=True)
    target = args.output / original.name
    if not target.exists() or target.read_text(encoding='utf-8') != patched:
        target.write_text(patched, encoding='utf-8', newline='\n')
    print('I2C RX completion overlay: ' + ('upstream already fixed' if source == patched else 'backport applied'))


if __name__ == '__main__':
    main()
