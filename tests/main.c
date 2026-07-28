#include "support/test.h"
#include "endian/test_endian.h"
#include "buffer/test_buffer.h"
#include "hci/test_hci.h"
#include "virtual_transport/test_virtual_transport.h"
#include "queue/test_queue.h"
#include "timer/test_timer.h"
#include "controller/test_command_queue.h"
#include "controller/test_controller.h"
#include "addr/test_addr.h"
#include "device/test_device_registry.h"
#include "discovery/test_discovery.h"

#include <stdio.h>

int main(void)
{
    run_endian_tests();
    run_buffer_tests();
    run_hci_tests();
    run_virtual_transport_tests();
    run_queue_tests();
    run_timer_tests();
    run_command_queue_tests();
    run_controller_tests();
    run_addr_tests();
    run_device_registry_tests();
    run_discovery_tests();

    printf("%d/%d checks passed\n", bt_test_count - bt_test_failures, bt_test_count);

    return bt_test_failures == 0 ? 0 : 1;
}
