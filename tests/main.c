#include "support/test.h"
#include "endian/test_endian.h"
#include "buffer/test_buffer.h"

#include <stdio.h>

int main(void)
{
    run_endian_tests();
    run_buffer_tests();

    printf("%d/%d checks passed\n", bt_test_count - bt_test_failures, bt_test_count);

    return bt_test_failures == 0 ? 0 : 1;
}
