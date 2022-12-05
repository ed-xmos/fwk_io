// Copyright 2022 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

#include <stdio.h>
#include <stdint.h>
#define PORT_TIMEAFTER(NOW, EVENT_TIME) ((int16_t)((EVENT_TIME) - (NOW)) < 0)

void test(uint16_t now, uint16_t et, int expected){
    printf("%d %d\n", (int16_t)(now-et),  (int16_t)(et-now));
    int16_t diff = PORT_TIMEAFTER(et,now) ? -(int16_t)(now-et) : (int16_t)(et-now);
    printf("now: %u et: %u %d %d %d\n", now, et, PORT_TIMEAFTER(now,et), diff, expected);
}

int main()
{
    test(1, 65535, -2);
    test(65535, 1, 2);
    test(32769, 0, 32767);
    test(0, 32767, 32767);
    test(0, 32768, -32768);
    


    return 0;
}
