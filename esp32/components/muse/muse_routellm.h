/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * The RouteLLM chat backend (CONFIG_MUSE_CHAT_ROUTELLM) implements the
     * muse_hatch_* turn API from muse_chat.h by posting each push-to-talk voice
     * note to Abacus.ai's OpenAI-compatible RouteLLM API and showing the text
     * reply. The key is set over the serial console and kept in NVS.
     */

    /* Handles ">chatllm_key=KEY" from the console: validates, stores it in NVS and
     * applies it live. Prints "@chatllm_key ok" or "@chatllm_key error ...".
     * `value` is the text after "chatllm_key=". */
    void muse_routellm_set_key_console(const char *value);

    /* Handles ">chatllm_test": runs a small Hello round-trip to ChatLLM and prints
     * "@chatllm_test ok|fail <detail>". */
    void muse_routellm_test_console(void);

    /* Handles ">dns=<host>": resolves host via public DNS and prints "@dns ...". */
    void muse_routellm_dns_console(const char *host);

    /* Handles ">net": prints IP/gateway/DNS. ">tcp=<ip>": raw connect to ip:443. */
    void muse_routellm_net_console(void);
    void muse_routellm_tcp_console(const char *ip);

#ifdef __cplusplus
}
#endif
