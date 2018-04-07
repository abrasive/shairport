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

#ifndef HTTP_HEADER_H
#define HTTP_HEADER_H

#if defined(__cplusplus)
extern "C" {
#endif

enum http_header_status
{
    http_header_status_done,
    http_header_status_continue,
    http_header_status_version_character,
    http_header_status_code_character,
    http_header_status_status_character,
    http_header_status_key_character,
    http_header_status_value_character,
    http_header_status_store_keyvalue
};

/**
 * Parses a single character of an HTTP header stream. The state parameter is
 * used as internal state and should be initialized to zero for the first call.
 * Return value is a value from the http_header_status enuemeration specifying
 * the semantics of the character. If an error is encountered,
 * http_header_status_done will be returned with a non-zero state parameter. On
 * success http_header_status_done is returned with the state parameter set to
 * zero.
 */
int http_parse_header_char(int* state, char ch);

#if defined(__cplusplus)
}
#endif

#endif
