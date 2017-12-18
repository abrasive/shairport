/*-
 * Copyright 2012 Matthew Endsley
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

static const unsigned char http_chunk_state[] = {
/*     *    LF    CR    HEX */
    0xC1, 0xC1, 0xC1,    1, /* s0: initial hex char */
    0xC1, 0xC1,    2, 0x81, /* s1: additional hex chars, followed by CR */
    0xC1, 0x83, 0xC1, 0xC1, /* s2: trailing LF */
    0xC1, 0xC1,    4, 0xC1, /* s3: CR after chunk block */
    0xC1, 0xC0, 0xC1, 0xC1, /* s4: LF after chunk block */
};

int http_parse_chunked(int* state, int *size, char ch)
{
    int newstate, code = 0;
    switch (ch) {
    case '\n': code = 1; break;
    case '\r': code = 2; break;
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7':
    case '8': case '9': case 'a': case 'b':
    case 'c': case 'd': case 'e': case 'f':
    case 'A': case 'B': case 'C': case 'D':
    case 'E': case 'F': code = 3; break;
    }

    newstate = http_chunk_state[*state * 4 + code];
    *state = (newstate & 0xF);

    switch (newstate) {
    case 0xC0:
        return *size != 0;

    case 0xC1: /* error */
        *size = -1;
        return 0;

    case 0x01: /* initial char */
        *size = 0;
        /* fallthrough */
    case 0x81: /* size char */
        if (ch >= 'a')
            *size = *size * 16 + (ch - 'a' + 10);
        else if (ch >= 'A')
            *size = *size * 16 + (ch - 'A' + 10);
        else
            *size = *size * 16 + (ch - '0');
        break;

    case 0x83:
        return *size == 0;
    }

    return 1;
}

