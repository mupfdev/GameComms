/* TCP output queue tests. SPDX-License-Identifier: MIT */
#include "tcp_output.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

int main(void)
{
    static tcp_output q;
    static uint8_t input[TCP_OUTPUT_CAPACITY];
    size_t i;
    for (i = 0; i < sizeof(input); ++i) {
        input[i] = (uint8_t)(i % 251);
    }

    assert(tcp_output_append(&q, input, TCP_OUTPUT_CAPACITY));
    assert(!tcp_output_append(&q, input, 1));
    assert(q.len == TCP_OUTPUT_CAPACITY);
    assert(memcmp(q.data, input, TCP_OUTPUT_CAPACITY) == 0);

    tcp_output_consume(&q, TCP_OUTPUT_CAPACITY - 5);
    assert(tcp_output_append(&q, input, 20));
    assert(q.len == 25);
    assert(tcp_output_chunk(&q) == 5);
    assert(memcmp(q.data + q.head, input + TCP_OUTPUT_CAPACITY - 5, 5) == 0);
    tcp_output_consume(&q, 5);
    assert(tcp_output_chunk(&q) == 20);
    assert(memcmp(q.data + q.head, input, 20) == 0);
    tcp_output_consume(&q, 7);
    assert(tcp_output_chunk(&q) == 13);
    assert(memcmp(q.data + q.head, input + 7, 13) == 0);
    tcp_output_clear(&q);
    assert(q.head == 0 && q.len == 0 && tcp_output_chunk(&q) == 0);
    puts("TCP output queue tests passed");
    return 0;
}
