/*
 * mtest - Perform a memory test
 *
 * (C) Copyright 2000
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include "configuration.h"

/*
 * Perform a memory test. A more complete alternative test can be
 * configured using CONFIG_CMD_MTEST_ALTERNATIVE. The complete test
 * loops until interrupted by ctrl-c or by a failure of one of the
 * sub-tests.
 */
#ifdef CONFIG_CMD_MTEST_ALTERNATIVE
static int mem_test(uint32_t _start, uint32_t _end, uint32_t pattern_unused)
{
    volatile uint32_t *start = (volatile uint32_t *)_start;
    volatile uint32_t *end = (volatile uint32_t *)_end;
    volatile uint32_t *addr;
    uint32_t val;
    uint32_t readback;
    vu_long addr_mask;
    vu_long offset;
    vu_long test_offset;
    vu_long pattern;
    vu_long temp;
    vu_long anti_pattern;
    vu_long num_words;
#ifdef CFG_MEMTEST_SCRATCH
    volatile uint32_t *dummy = (vu_long *)CFG_MEMTEST_SCRATCH;
#else
    volatile uint32_t *dummy = start;
#endif
    int j;
    int iterations = 1;
    static const uint32_t bitpattern[] = {
        0x00000001, /* single bit */
        0x00000003, /* two adjacent bits */
        0x00000007, /* three adjacent bits */
        0x0000000F, /* four adjacent bits */
        0x00000005, /* two non-adjacent bits */
        0x00000015, /* three non-adjacent bits */
        0x00000055, /* four non-adjacent bits */
        0xaaaaaaaa, /* alternating 1/0 */
    };
    /* XXX: enforce alignment of start and end? */
    for (;;) {
        if (ctrlc()) {
            putchar('\n');
            return 1;
        }
        printf("Iteration: %6d\r", iterations);
        iterations++;
        /*
         * Data line test: write a pattern to the first
         * location, write the 1's complement to a 'parking'
         * address (changes the state of the data bus so a
         * floating bus doen't give a false OK), and then
         * read the value back. Note that we read it back
         * into a variable because the next time we read it,
         * it might be right (been there, tough to explain to
         * the quality guys why it prints a failure when the
         * "is" and "should be" are obviously the same in the
         * error message).
         *
         * Rather than exhaustively testing, we test some
         * patterns by shifting '1' bits through a field of
         * '0's and '0' bits through a field of '1's (i.e.
         * pattern and ~pattern).
         */
        addr = start;
        /* XXX */
        if (addr == dummy)
            ++addr;
        for (j = 0; j < sizeof(bitpattern) / sizeof(bitpattern[0]); j++) {
            val = bitpattern[j];
            for (; val != 0; val <<= 1) {
                *addr = val;
                *dummy = ~val; /* clear the test data off of the bus */
                readback = *addr;
                if (readback != val) {
                    printf("FAILURE (data line): "
                           "expected 0x%08lx, actual 0x%08lx at address 0x%p\n",
                           val, readback, addr);
                }
                *addr = ~val;
                *dummy = val;
                readback = *addr;
                if (readback != ~val) {
                    printf("FAILURE (data line): "
                           "Is 0x%08lx, should be 0x%08lx at address 0x%p\n",
                           readback, ~val, addr);
                }
            }
        }
        /*
         * Based on code whose Original Author and Copyright
         * information follows: Copyright (c) 1998 by Michael
         * Barr. This software is placed into the public
         * domain and may be used for any purpose. However,
         * this notice must not be changed or removed and no
         * warranty is either expressed or implied by its
         * publication or distribution.
         */
        /*
         * Address line test
         *
         * Description: Test the address bus wiring in a
         *              memory region by performing a walking
         *              1's test on the relevant bits of the
         *              address and checking for aliasing.
         *              This test will find single-bit
         *              address failures such as stuck -high,
         *              stuck-low, and shorted pins. The base
         *              address and size of the region are
         *              selected by the caller.
         *
         * Notes:	For best results, the selected base
         *              address should have enough LSB 0's to
         *              guarantee single address bit changes.
         *              For example, to test a 64-Kbyte
         *              region, select a base address on a
         *              64-Kbyte boundary. Also, select the
         *              region size as a power-of-two if at
         *              all possible.
         *
         * Returns:     0 if the test succeeds, 1 if the test fails.
         *
         * ## NOTE ##	Be sure to specify start and end
         *              addresses such that addr_mask has
         *              lots of bits set. For example an
         *              address range of 01000000 02000000 is
         *              bad while a range of 01000000
         *              01ffffff is perfect.
         */
        addr_mask = ((uint32_t)end - (uint32_t)start) / sizeof(vu_long);
        pattern = (vu_long)0xaaaaaaaa;
        anti_pattern = (vu_long)0x55555555;
        debug("%s:%d: addr mask = 0x%.8lx\n", __FUNCTION__, __LINE__, addr_mask);
        /*
         * Write the default pattern at each of the
         * power-of-two offsets.
         */
        for (offset = 1; (offset & addr_mask) != 0; offset <<= 1)
            start[offset] = pattern;
        /*
         * Check for address bits stuck high.
         */
        test_offset = 0;
        start[test_offset] = anti_pattern;
        for (offset = 1; (offset & addr_mask) != 0; offset <<= 1) {
            temp = start[offset];
            if (temp != pattern) {
                printf("\nFAILURE: Address bit stuck high @ 0x%.8lx:"
                       " expected 0x%.8lx, actual 0x%.8lx\n",
                       (uint32_t)&start[offset], pattern, temp);
                return 1;
            }
        }
        start[test_offset] = pattern;
        /*
         * Check for addr bits stuck low or shorted.
         */
        for (test_offset = 1; (test_offset & addr_mask) != 0; test_offset <<= 1) {
            start[test_offset] = anti_pattern;
            for (offset = 1; (offset & addr_mask) != 0; offset <<= 1) {
                temp = start[offset];
                if ((temp != pattern) && (offset != test_offset)) {
                    printf("\nFAILURE: Address bit stuck low or shorted @"
                           " 0x%.8lx: expected 0x%.8lx, actual 0x%.8lx\n",
                           (uint32_t)&start[offset], pattern, temp);
                    return 1;
                }
            }
            start[test_offset] = pattern;
        }
        /*
         * Description: Test the integrity of a physical
         *		memory device by performing an
         *		increment/decrement test over the
         *		entire region. In the process every
         *		storage bit in the device is tested
         *		as a zero and a one. The base address
         *		and the size of the region are
         *		selected by the caller.
         *
         * Returns:     0 if the test succeeds, 1 if the test fails.
         */
        num_words = ((uint32_t)end - (uint32_t)start) / sizeof(vu_long) + 1;
        /*
         * Fill memory with a known pattern.
         */
        for (pattern = 1, offset = 0; offset < num_words; pattern++, offset++) {
            start[offset] = pattern;
        }
        /*
         * Check each location and invert it for the second pass.
         */
        for (pattern = 1, offset = 0; offset < num_words; pattern++, offset++) {
            temp = start[offset];
            if (temp != pattern) {
                printf("\nFAILURE (read/write) @ 0x%.8lx:"
                       " expected 0x%.8lx, actual 0x%.8lx)\n",
                       (uint32_t)&start[offset], pattern, temp);
                return 1;
            }
            anti_pattern = ~pattern;
            start[offset] = anti_pattern;
        }
        /*
         * Check each location for the inverted pattern and zero it.
         */
        for (pattern = 1, offset = 0; offset < num_words; pattern++, offset++) {
            anti_pattern = ~pattern;
            temp = start[offset];
            if (temp != anti_pattern) {
                printf("\nFAILURE (read/write): @ 0x%.8lx:"
                       " expected 0x%.8lx, actual 0x%.8lx)\n",
                       (uint32_t)&start[offset], anti_pattern, temp);
                return 1;
            }
            start[offset] = 0;
        }
    }
}
#else
static int mem_test(uint32_t *_start, size_t len, bool doRead = true, bool doWrite = true)
{
    volatile uint32_t *addr;
    volatile uint32_t *start = (volatile uint32_t *)_start;
    const volatile uint32_t *end = start + len / sizeof(uint32_t);
    uint32_t pattern = 0;
    uint32_t val;
    uint32_t readback;
    uint32_t incr;
    int rcode = 0;
    incr = 1;

    //DEBUG_MSG("memtest read=%d, write=%d\n", doRead, doWrite);

    if (doWrite) {
        //DEBUG_MSG("writing\n");
        for (addr = start, val = pattern; addr < end; addr++) {
            *addr = val;
            val += incr;
        }
    }

    if (doRead) {
        //DEBUG_MSG("reading\n");
        for (addr = start, val = pattern; addr < end; addr++) {
            readback = *addr;
            if (readback != val) {
                DEBUG_MSG("Mem error @ 0x%08X: "
                       "found 0x%08lX, expected 0x%08lX\n",
                       addr, readback, val);
                rcode++;
            }
            val += incr;
        }
    }

#if 0            
		/*
		 * Flip the pattern each time to make lots of zeros and
		 * then, the next time, lots of ones.  We decrement
		 * the "negative" patterns and increment the "positive"
		 * patterns to preserve this feature.
		 */
		if(pattern & 0x80000000) {
			pattern = -pattern;	/* complement & increment */
		}
		else {
			pattern = ~pattern;
		}
#endif

    return rcode;
}
#endif

#define TESTBUF_LEN 16384

#include <assert.h>

void doMemTest()
{
    static uint32_t *testBuf;
    static int iter;

    if (!testBuf)
        testBuf = (uint32_t *)malloc(TESTBUF_LEN);

    assert(testBuf);
    if (mem_test(testBuf, TESTBUF_LEN, iter % 2 == 1, iter % 2 == 0) > 0)
        assert(0); // FIXME report error better

    iter++;
}
