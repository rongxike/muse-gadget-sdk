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

/*
 * A chat backend that talks to Abacus.ai's OpenAI-compatible RouteLLM
 * (ChatLLM) API instead of Muse's servers (CONFIG_MUSE_CHAT_ROUTELLM). It
 * implements the same muse_hatch_* turn API the other backends do, so the
 * voice task drives it unchanged.
 *
 * A push-to-talk turn:
 *   1. muse_hatch_turn_begin/_audio record the press as 16 kHz mono PCM into a
 *      PSRAM buffer, capped at CONFIG_MUSE_ROUTELLM_MAX_SECONDS.
 *   2. muse_hatch_turn_end hands the recording to a worker task, which wraps it
 *      in a WAV, base64s it, and POSTs it to /chat/completions as an
 *      input_audio message for an audio-capable model.
 *   3. The worker stores the model's text answer; the voice task scrolls it
 *      through the caption at reading pace (no audio reply: turn_read is empty).
 *
 * The API key is set over the serial console (">chatllm_key=...") and kept in
 * NVS by muse_settings. The gadget does not use Muse's agent, the home-network
 * tunnel, or Muse-app pairing in this mode.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "muse_chat.h"
#include "muse_chat_priv.h"
#include "muse_routellm.h"
#include "muse_settings.h"
#include "muse_wifi.h"

static const char *TAG = "muse_routellm";

/* Google Trust Services WE1 + GlobalSign ECC Root CA R4 that routellm.abacus.ai
 * chains to; the ESP-IDF cert bundle doesn't match this ECC root. */
extern const uint8_t routellm_ca_pem_start[] asm("_binary_routellm_ca_pem_start");

#define MIC_RATE 16000
#define EV_TEXT 72                /* one caption line, as the other backends use */
#define REPLY_MAX 1024            /* reply text kept for the captions */
#define RESP_MAX (16 * 1024)      /* the JSON answer we read back */
#define READ_CHARS_PER_S 14       /* caption scroll, about speaking pace */
#define SETTLE_US (3 * 1000000LL) /* quiet after the reply before the turn ends */
#define REQUEST_TIMEOUT_US (60 * 1000000LL)
#define WORKER_STACK 10240

#define SYS_PROMPT "You are a helpful voice assistant on a small device. Reply in one or two short sentences."

typedef enum
{
    RL_IDLE,
    RL_RECORDING,
    RL_SENDING,
    RL_REPLY
} phase_t;

typedef struct
{
    muse_hatch_ev_t type;
    char text[EV_TEXT];
} ev_t;

/* Handed to the worker task; it owns pcm and frees it. */
typedef struct
{
    int16_t *pcm;
    size_t frames;
    uint32_t gen;
    char key[MUSE_ROUTELLM_KEY_MAX + 1];
} wk_t;

static QueueHandle_t s_events;
static SemaphoreHandle_t s_lock;

/* Voice task only. */
static phase_t s_phase;
static int16_t *s_pcm;
static size_t s_pcm_frames, s_pcm_cap; /* frames */
static int64_t s_send_us, s_reply_us, s_show_us;
static char s_reply[REPLY_MAX];
static char s_shown[EV_TEXT];
static char s_error[EV_TEXT];
static char s_key[MUSE_ROUTELLM_KEY_MAX + 1];
static bool s_sent_emitted;

/* Shared with the worker, under s_lock. */
static uint32_t s_gen;
static bool s_uploaded;
static bool s_result_ready, s_result_ok;
static char s_result_text[REPLY_MAX];

/* Connectivity test (Test connection / >chatllm_test), under s_lock. */
typedef enum
{
    TEST_NONE,
    TEST_RUNNING,
    TEST_DONE
} test_phase_t;
static test_phase_t s_test_phase;
static bool s_test_ok;
static char s_test_detail[96];

/* ---- Events to the voice task ---- */

static void emit(muse_hatch_ev_t type, const char *text)
{
    ev_t ev;
    ev.type = type;
    strlcpy(ev.text, text ? text : "", sizeof(ev.text));
    xQueueSend(s_events, &ev, 0);
}

/* ---- The request, on the worker task ---- */

static void put_le(uint8_t *p, uint32_t v, int n)
{
    for (int i = 0; i < n; i++)
    {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

/* A complete WAV header with real sizes (muse_hatch_wav_header is the streaming,
 * unknown-length one the chunked note uses). */
static void wav_header(uint8_t h[MUSE_HATCH_WAV_HEADER], uint32_t rate, uint32_t data_len)
{
    memcpy(h, "RIFF", 4);
    put_le(h + 4, 36 + data_len, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le(h + 16, 16, 4);
    put_le(h + 20, 1, 2); /* PCM */
    put_le(h + 22, 1, 2); /* mono */
    put_le(h + 24, rate, 4);
    put_le(h + 28, rate * 2, 4);
    put_le(h + 32, 2, 2);
    put_le(h + 34, 16, 2);
    memcpy(h + 36, "data", 4);
    put_le(h + 40, data_len, 4);
}

static void mark_uploaded(uint32_t gen)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (gen == s_gen)
    {
        s_uploaded = true;
    }
    xSemaphoreGive(s_lock);
}

static bool write_all(esp_http_client_handle_t c, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        int w = esp_http_client_write(c, buf + off, len - off);
        if (w < 0)
        {
            return false;
        }
        off += (size_t)w;
    }
    return true;
}

/* Reads choices[0].message out of the answer; sets *ok and fills text (cap). */
static void parse_reply(const char *resp, int status, bool *ok, char *text, size_t cap)
{
    cJSON *root = cJSON_Parse(resp);
    if (!root)
    {
        if (status)
        {
            snprintf(text, cap, "CHATLLM HTTP %d", status);
        }
        else
        {
            strlcpy(text, "BAD REPLY FROM CHATLLM", cap);
        }
        *ok = false;
        return;
    }
    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsObject(err))
    {
        const char *m = cJSON_GetStringValue(cJSON_GetObjectItem(err, "message"));
        snprintf(text, cap, "CHATLLM: %.*s", (int)cap - 12, m ? m : "error");
        *ok = false;
        cJSON_Delete(root);
        return;
    }
    cJSON *msg = cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(root, "choices"), 0), "message");
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(msg, "content"));
    if ((!content || !content[0]) && cJSON_IsArray(cJSON_GetObjectItem(msg, "content")))
    {
        cJSON *part;
        cJSON_ArrayForEach(part, cJSON_GetObjectItem(msg, "content"))
        {
            const char *t = cJSON_GetStringValue(cJSON_GetObjectItem(part, "text"));
            if (t && t[0])
            {
                content = t;
                break;
            }
        }
    }
    if (!content || !content[0])
    {
        content = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(msg, "audio"), "transcript"));
    }
    if (content && content[0])
    {
        strlcpy(text, content, cap);
        *ok = true;
    }
    else if (status && (status < 200 || status >= 300))
    {
        snprintf(text, cap, "CHATLLM HTTP %d", status);
        *ok = false;
    }
    else
    {
        strlcpy(text, "CHATLLM SENT NO TEXT", cap);
        *ok = false;
    }
    cJSON_Delete(root);
}

/* Some networks (phone hotspots) fail to resolve third-party hosts. Point DNS
 * at public resolvers so ChatLLM's hostname resolves regardless. */
static void set_public_dns(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif)
    {
        netif = esp_netif_get_default_netif();
    }
    if (!netif)
    {
        ESP_LOGW(TAG, "set_public_dns: no netif");
        return;
    }
    esp_netif_dns_info_t d = {0};
    d.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_str_to_ip4("8.8.8.8", &d.ip.u_addr.ip4);
    esp_err_t e1 = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &d);
    esp_netif_str_to_ip4("1.1.1.1", &d.ip.u_addr.ip4);
    esp_err_t e2 = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &d);
    ESP_LOGI(TAG, "set_public_dns: main=%s backup=%s", esp_err_to_name(e1), esp_err_to_name(e2));
}

static void do_request(const wk_t *w, bool *ok, char *text, size_t cap)
{
    *ok = false;
    set_public_dns();
    size_t bytes = w->frames * 2;
    size_t wav_len = MUSE_HATCH_WAV_HEADER + bytes;
    uint8_t *wav = heap_caps_malloc(wav_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wav)
    {
        strlcpy(text, "OUT OF MEMORY", cap);
        return;
    }
    wav_header(wav, MIC_RATE, bytes);
    memcpy(wav + MUSE_HATCH_WAV_HEADER, w->pcm, bytes);

    size_t b64_cap = (wav_len + 2) / 3 * 4;
    char *b64 = heap_caps_malloc(b64_cap + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b64)
    {
        free(wav);
        strlcpy(text, "OUT OF MEMORY", cap);
        return;
    }
    size_t b64_len = muse_hatch_base64(wav, wav_len, b64);
    b64[b64_len] = '\0';
    free(wav);

    char head[512];
    int hn = snprintf(head, sizeof(head),
                      "{\"model\":\"%s\",\"modalities\":[\"text\"],\"messages\":["
                      "{\"role\":\"system\",\"content\":\"%s\"},"
                      "{\"role\":\"user\",\"content\":[{\"type\":\"input_audio\","
                      "\"input_audio\":{\"data\":\"",
                      CONFIG_MUSE_ROUTELLM_MODEL, SYS_PROMPT);
    static const char tail[] = "\",\"format\":\"wav\"}}]}]}";

    char url[256];
    snprintf(url, sizeof(url), "%s/chat/completions", CONFIG_MUSE_ROUTELLM_BASE_URL);
    char auth[MUSE_ROUTELLM_KEY_MAX + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", w->key);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .cert_pem = (const char *)routellm_ca_pem_start,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        free(b64);
        strlcpy(text, "CAN'T REACH CHATLLM", cap);
        return;
    }
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    int body_len = hn + (int)b64_len + (int)(sizeof(tail) - 1);
    esp_err_t oerr = esp_http_client_open(client, body_len);
    if (oerr != ESP_OK)
    {
        ESP_LOGW(TAG, "connect/handshake failed: %s", esp_err_to_name(oerr));
        free(b64);
        esp_http_client_cleanup(client);
        snprintf(text, cap, "CAN'T REACH CHATLLM (%s)", esp_err_to_name(oerr));
        return;
    }
    bool sent = write_all(client, head, hn) && write_all(client, b64, b64_len) && write_all(client, tail, sizeof(tail) - 1);
    free(b64);
    if (!sent)
    {
        ESP_LOGW(TAG, "body upload failed");
        esp_http_client_cleanup(client);
        strlcpy(text, "UPLOAD FAILED", cap);
        return;
    }
    mark_uploaded(w->gen);

    int clen = esp_http_client_fetch_headers(client);
    (void)clen;
    int status = esp_http_client_get_status_code(client);

    char *resp = heap_caps_malloc(RESP_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!resp)
    {
        esp_http_client_cleanup(client);
        strlcpy(text, "OUT OF MEMORY", cap);
        return;
    }
    int total = 0;
    for (;;)
    {
        int r = esp_http_client_read(client, resp + total, RESP_MAX - 1 - total);
        if (r <= 0)
        {
            break;
        }
        total += r;
        if (total >= RESP_MAX - 1)
        {
            break;
        }
    }
    resp[total] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "RouteLLM HTTP %d, %d bytes", status, total);
    parse_reply(resp, status, ok, text, cap);
    free(resp);
}

static void worker(void *arg)
{
    wk_t *w = arg;
    bool ok = false;
    char text[REPLY_MAX];
    text[0] = '\0';
    do_request(w, &ok, text, sizeof(text));
    free(w->pcm);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (w->gen == s_gen)
    {
        s_result_ok = ok;
        strlcpy(s_result_text, text, sizeof(s_result_text));
        s_result_ready = true;
    }
    xSemaphoreGive(s_lock);
    free(w);
    vTaskDelete(NULL);
}

/* ---- Caption scroll, on the voice task ---- */

static size_t read_at(int64_t now)
{
    return (size_t)((now - s_show_us) * READ_CHARS_PER_S / 1000000);
}

/* Scrolls the reply through the caption at reading pace; true once all shown. */
static bool scroll(int64_t now)
{
    size_t len = strlen(s_reply);
    if (!len)
    {
        return true;
    }
    size_t at = read_at(now);
    char line[EV_TEXT];
    if (muse_hatch_caption_at(s_reply, at < len ? at : len - 1, line, sizeof(line)) && strcmp(line, s_shown))
    {
        strlcpy(s_shown, line, sizeof(s_shown));
        emit(MUSE_HATCH_EV_REPLY, line);
    }
    return at >= len;
}

/* Moves the turn along; runs on the voice task each time it asks for events. */
static void pump(void)
{
    int64_t now = esp_timer_get_time();
    if (s_phase == RL_SENDING)
    {
        bool uploaded, ready, ok = false;
        char text[REPLY_MAX];
        text[0] = '\0';
        xSemaphoreTake(s_lock, portMAX_DELAY);
        uploaded = s_uploaded;
        ready = s_result_ready;
        if (ready)
        {
            ok = s_result_ok;
            strlcpy(text, s_result_text, sizeof(text));
        }
        xSemaphoreGive(s_lock);
        if (uploaded && !s_sent_emitted)
        {
            s_sent_emitted = true;
            emit(MUSE_HATCH_EV_SENT, NULL);
        }
        if (ready)
        {
            if (!ok)
            {
                emit(MUSE_HATCH_EV_ERROR, text[0] ? text : "CHATLLM ERROR");
                s_phase = RL_IDLE;
            }
            else
            {
                if (!s_sent_emitted)
                {
                    s_sent_emitted = true;
                    emit(MUSE_HATCH_EV_SENT, NULL);
                }
                strlcpy(s_reply, text, sizeof(s_reply));
                s_show_us = now;
                s_reply_us = now;
                s_shown[0] = '\0';
                s_phase = RL_REPLY;
                scroll(now);
            }
        }
        else if (now - s_send_us > REQUEST_TIMEOUT_US)
        {
            emit(MUSE_HATCH_EV_ERROR, "NO REPLY FROM CHATLLM");
            s_phase = RL_IDLE;
        }
    }
    if (s_phase == RL_REPLY)
    {
        if (scroll(now) && now - s_reply_us > SETTLE_US)
        {
            emit(MUSE_HATCH_EV_DONE, NULL);
            s_phase = RL_IDLE;
        }
    }
}

/* ---- Key ---- */

static void load_key(void)
{
    char key[MUSE_ROUTELLM_KEY_MAX + 1];
    muse_settings_routellm_key(key);
#ifdef CONFIG_MUSE_ROUTELLM_KEY
    if (key[0] == '\0')
    {
        strlcpy(key, CONFIG_MUSE_ROUTELLM_KEY, sizeof(key)); /* build-time fallback */
    }
#endif
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_key, key, sizeof(s_key));
    xSemaphoreGive(s_lock);
}

void muse_routellm_set_key_console(const char *value)
{
    while (*value == ' ')
    {
        value++;
    }
    size_t len = strlen(value);
    while (len && (value[len - 1] == ' ' || value[len - 1] == '\t'))
    {
        len--;
    }
    if (len == 0 || len > MUSE_ROUTELLM_KEY_MAX)
    {
        ESP_LOGW(TAG, "not a usable ChatLLM key");
        printf("@chatllm_key error invalid\n");
        fflush(stdout);
        return;
    }
    char key[MUSE_ROUTELLM_KEY_MAX + 1];
    memcpy(key, value, len);
    key[len] = '\0';
    muse_settings_set_routellm_key(key);
    load_key();
    ESP_LOGI(TAG, "stored ChatLLM key (%u chars)", (unsigned)len);
    printf("@chatllm_key ok\n");
    fflush(stdout);
}

/* Console ">dns=<host>": resolves host (via public DNS) and prints the IP. */
void muse_routellm_dns_console(const char *host)
{
    while (*host == ' ')
    {
        host++;
    }
    set_public_dns();
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    int err = getaddrinfo(host, "443", &hints, &res);
    if (err != 0 || !res)
    {
        printf("@dns %s fail (getaddrinfo=%d)\n", host, err);
        fflush(stdout);
        if (res)
        {
            freeaddrinfo(res);
        }
        return;
    }
    struct sockaddr_in *a = (struct sockaddr_in *)res->ai_addr;
    esp_ip4_addr_t ip4;
    ip4.addr = a->sin_addr.s_addr;
    char ip[16] = "?";
    esp_ip4addr_ntoa(&ip4, ip, sizeof(ip));
    printf("@dns %s ok %s\n", host, ip);
    fflush(stdout);
    freeaddrinfo(res);
}

/* Console ">net": prints the station's IP, gateway and DHCP-provided DNS. */
void muse_routellm_net_console(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif)
    {
        netif = esp_netif_get_default_netif();
    }
    if (!netif)
    {
        printf("@net no netif\n");
        fflush(stdout);
        return;
    }
    esp_netif_ip_info_t ip = {0};
    esp_netif_get_ip_info(netif, &ip);
    esp_netif_dns_info_t dmain = {0}, dback = {0};
    esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dmain);
    esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dback);
    char sip[16], sgw[16], sdns[16], sdns2[16];
    esp_ip4addr_ntoa(&ip.ip, sip, sizeof(sip));
    esp_ip4addr_ntoa(&ip.gw, sgw, sizeof(sgw));
    esp_ip4addr_ntoa(&dmain.ip.u_addr.ip4, sdns, sizeof(sdns));
    esp_ip4addr_ntoa(&dback.ip.u_addr.ip4, sdns2, sizeof(sdns2));
    printf("@net ip=%s gw=%s dns=%s dns2=%s\n", sip, sgw, sdns, sdns2);
    fflush(stdout);
}

/* Console ">tcp=<ip>": a raw TCP connect to <ip>:443, bypassing DNS. */
void muse_routellm_tcp_console(const char *ip)
{
    while (*ip == ' ')
    {
        ip++;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        printf("@tcp %s fail (socket)\n", ip);
        fflush(stdout);
        return;
    }
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(443);
    a.sin_addr.s_addr = inet_addr(ip);
    fcntl(s, F_SETFL, O_NONBLOCK);
    int r = connect(s, (struct sockaddr *)&a, sizeof(a));
    if (r < 0 && errno == EINPROGRESS)
    {
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(s, &wf);
        struct timeval tv = {.tv_sec = 6, .tv_usec = 0};
        int sel = select(s + 1, NULL, &wf, NULL, &tv);
        if (sel > 0)
        {
            int serr = 0;
            socklen_t len = sizeof(serr);
            getsockopt(s, SOL_SOCKET, SO_ERROR, &serr, &len);
            r = serr == 0 ? 0 : -1;
            errno = serr;
        }
        else
        {
            r = -1;
            if (sel == 0)
            {
                errno = ETIMEDOUT;
            }
        }
    }
    printf("@tcp %s %s (errno=%d)\n", ip, r == 0 ? "ok" : "fail", errno);
    fflush(stdout);
    close(s);
}

/* ---- Public muse_hatch_* API ---- */

void muse_hatch_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_events = xQueueCreate(8, sizeof(ev_t));
    load_key();
}

void muse_hatch_status(muse_hatch_status_t *out)
{
    out->vm_id[0] = '\0';
    if (!s_events || s_key[0] == '\0')
    {
        out->state = MUSE_HATCH_NOT_SET;
        strlcpy(out->detail, "Set a ChatLLM key", sizeof(out->detail));
        return;
    }
    if (!muse_wifi_connected())
    {
        out->state = MUSE_HATCH_OFFLINE;
        strlcpy(out->detail, "Waiting for Wi-Fi", sizeof(out->detail));
        return;
    }
    test_phase_t tp;
    bool tok, sending;
    char tdetail[96];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    tp = s_test_phase;
    tok = s_test_ok;
    sending = (s_phase == RL_SENDING);
    strlcpy(tdetail, s_test_detail, sizeof(tdetail));
    xSemaphoreGive(s_lock);
    if (tp == TEST_RUNNING)
    {
        out->state = MUSE_HATCH_TESTING;
        strlcpy(out->detail, "Testing ChatLLM...", sizeof(out->detail));
    }
    else if (tp == TEST_DONE && !tok)
    {
        out->state = MUSE_HATCH_UNREACHABLE;
        snprintf(out->detail, sizeof(out->detail), "%.*s", (int)sizeof(out->detail) - 1,
                 tdetail[0] ? tdetail : "ChatLLM test failed");
    }
    else if (sending)
    {
        out->state = MUSE_HATCH_TESTING;
        strlcpy(out->detail, "Thinking...", sizeof(out->detail));
    }
    else
    {
        out->state = MUSE_HATCH_REACHABLE;
        strlcpy(out->detail, (tp == TEST_DONE && tok) ? "ChatLLM OK" : "ChatLLM, text replies", sizeof(out->detail));
    }
}

const char *muse_hatch_state_name(muse_hatch_state_t state)
{
    switch (state)
    {
    case MUSE_HATCH_NOT_SET:
        return "Not set up";
    case MUSE_HATCH_OFFLINE:
        return "Offline";
    case MUSE_HATCH_UNTESTED:
        return "Saved";
    case MUSE_HATCH_TESTING:
        return "Connecting";
    case MUSE_HATCH_REACHABLE:
        return "Connected";
    case MUSE_HATCH_UNREACHABLE:
        return "Can't connect";
    }
    return "";
}

/* ---- Connectivity test (Test connection / >chatllm_test) ---- */

/* A small text round-trip that confirms the key actually reaches ChatLLM.
 * Fills detail with the reply on success or the failure reason, and logs it. */
static void test_request(bool *ok, char *detail, size_t cap)
{
    *ok = false;
    char key[MUSE_ROUTELLM_KEY_MAX + 1];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(key, s_key, sizeof(key));
    xSemaphoreGive(s_lock);
    if (!key[0])
    {
        strlcpy(detail, "no key set", cap);
        return;
    }
    set_public_dns();

    char url[256];
    snprintf(url, sizeof(url), "%s/chat/completions", CONFIG_MUSE_ROUTELLM_BASE_URL);
    char auth[MUSE_ROUTELLM_KEY_MAX + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", key);
    static const char body[] =
        "{\"model\":\"route-llm\",\"messages\":[{\"role\":\"user\","
        "\"content\":\"Reply with the single word OK.\"}],\"max_tokens\":8}";

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .buffer_size = 1536,
        .buffer_size_tx = 1024,
        .cert_pem = (const char *)routellm_ca_pem_start,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        strlcpy(detail, "client init failed", cap);
        return;
    }
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t oerr = esp_http_client_open(client, (int)(sizeof(body) - 1));
    if (oerr != ESP_OK)
    {
        ESP_LOGW(TAG, "test: connect/handshake failed: %s", esp_err_to_name(oerr));
        snprintf(detail, cap, "connect failed (%s)", esp_err_to_name(oerr));
        esp_http_client_cleanup(client);
        return;
    }
    if (!write_all(client, body, sizeof(body) - 1))
    {
        ESP_LOGW(TAG, "test: body write failed");
        strlcpy(detail, "write failed", cap);
        esp_http_client_cleanup(client);
        return;
    }
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    char *resp = heap_caps_malloc(RESP_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int total = 0;
    if (resp)
    {
        for (;;)
        {
            int r = esp_http_client_read(client, resp + total, RESP_MAX - 1 - total);
            if (r <= 0)
                break;
            total += r;
            if (total >= RESP_MAX - 1)
                break;
        }
        resp[total] = '\0';
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "test: HTTP %d, %d bytes", status, total);

    if (status == 200 && resp)
    {
        bool pok;
        char ptext[REPLY_MAX];
        parse_reply(resp, status, &pok, ptext, sizeof(ptext));
        *ok = pok;
        snprintf(detail, cap, "%.*s", (int)cap - 1, ptext);
    }
    else
    {
        snprintf(detail, cap, "HTTP %d", status);
    }
    free(resp);
}

/* arg non-NULL: also print "@chatllm_test ..." for the serial console. */
static void test_worker(void *arg)
{
    bool print = (arg != NULL);
    bool ok;
    char detail[96];
    test_request(&ok, detail, sizeof(detail));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_test_ok = ok;
    strlcpy(s_test_detail, detail, sizeof(s_test_detail));
    s_test_phase = TEST_DONE;
    xSemaphoreGive(s_lock);
    if (print)
    {
        printf("@chatllm_test %s %s\n", ok ? "ok" : "fail", detail);
        fflush(stdout);
    }
    vTaskDelete(NULL);
}

static bool start_test(bool print)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool busy = (s_test_phase == TEST_RUNNING);
    if (!busy)
    {
        s_test_phase = TEST_RUNNING;
        s_test_detail[0] = '\0';
    }
    xSemaphoreGive(s_lock);
    if (busy)
    {
        return false;
    }
    if (xTaskCreate(test_worker, "rl_test", 10240, print ? (void *)1 : NULL, 5, NULL) != pdPASS)
    {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_test_phase = TEST_DONE;
        s_test_ok = false;
        strlcpy(s_test_detail, "task failed", sizeof(s_test_detail));
        xSemaphoreGive(s_lock);
        return false;
    }
    return true;
}

/* Console ">chatllm_test": runs the probe; the worker prints the result. */
void muse_routellm_test_console(void)
{
    if (start_test(true))
    {
        printf("@chatllm_test running\n");
        fflush(stdout);
    }
    else
    {
        printf("@chatllm_test busy\n");
        fflush(stdout);
    }
}

void muse_hatch_test(void)
{
    load_key();
    start_test(false);
}

void muse_hatch_config_changed(void)
{
    load_key();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_test_phase = TEST_NONE;
    xSemaphoreGive(s_lock);
}

void muse_hatch_set_resting(bool resting)
{
    (void)resting;
}

bool muse_hatch_ready(void)
{
    return s_events && s_key[0] != '\0' && muse_wifi_connected();
}

void muse_hatch_turn_begin(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_gen++;
    s_result_ready = false;
    s_uploaded = false;
    xSemaphoreGive(s_lock);
    s_sent_emitted = false;
    xQueueReset(s_events);
    free(s_pcm);
    s_pcm = NULL;
    s_pcm_frames = 0;
    s_reply[0] = '\0';
    s_shown[0] = '\0';
    s_error[0] = '\0';
    if (s_key[0] == '\0')
    {
        s_phase = RL_IDLE;
        strlcpy(s_error, "SET A CHATLLM KEY", sizeof(s_error));
        return;
    }
    s_pcm_cap = (size_t)CONFIG_MUSE_ROUTELLM_MAX_SECONDS * MIC_RATE;
    s_pcm = heap_caps_malloc(s_pcm_cap * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pcm)
    {
        s_pcm_cap = 0;
        s_phase = RL_IDLE;
        strlcpy(s_error, "OUT OF MEMORY", sizeof(s_error));
        return;
    }
    s_phase = RL_RECORDING;
}

void muse_hatch_turn_audio(const int16_t *pcm, size_t frames)
{
    if (s_phase != RL_RECORDING || !s_pcm)
    {
        return;
    }
    size_t room = s_pcm_cap - s_pcm_frames;
    size_t take = frames < room ? frames : room;
    if (take)
    {
        memcpy(s_pcm + s_pcm_frames, pcm, take * 2);
        s_pcm_frames += take;
    }
}

void muse_hatch_turn_end(void)
{
    if (s_phase != RL_RECORDING)
    {
        emit(MUSE_HATCH_EV_ERROR, s_error[0] ? s_error : "CAN'T REACH CHATLLM");
        return;
    }
    if (s_pcm_frames < MIC_RATE / 10)
    {
        free(s_pcm);
        s_pcm = NULL;
        s_pcm_frames = 0;
        s_phase = RL_IDLE;
        emit(MUSE_HATCH_EV_ERROR, "NOTHING HEARD");
        return;
    }
    wk_t *w = malloc(sizeof(*w));
    if (!w)
    {
        free(s_pcm);
        s_pcm = NULL;
        s_pcm_frames = 0;
        s_phase = RL_IDLE;
        emit(MUSE_HATCH_EV_ERROR, "OUT OF MEMORY");
        return;
    }
    w->pcm = s_pcm;
    w->frames = s_pcm_frames;
    strlcpy(w->key, s_key, sizeof(w->key));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    w->gen = s_gen;
    s_uploaded = false;
    s_result_ready = false;
    xSemaphoreGive(s_lock);
    s_sent_emitted = false;
    s_pcm = NULL;
    s_pcm_frames = 0;
    s_send_us = esp_timer_get_time();
    s_phase = RL_SENDING;
    if (xTaskCreate(worker, "routellm", WORKER_STACK, w, 5, NULL) != pdPASS)
    {
        free(w->pcm);
        free(w);
        s_phase = RL_IDLE;
        emit(MUSE_HATCH_EV_ERROR, "OUT OF MEMORY");
    }
}

void muse_hatch_turn_cancel(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_gen++;
    s_result_ready = false;
    xSemaphoreGive(s_lock);
    free(s_pcm);
    s_pcm = NULL;
    s_pcm_frames = 0;
    s_phase = RL_IDLE;
    xQueueReset(s_events);
}

muse_hatch_ev_t muse_hatch_turn_event(char *text, size_t cap)
{
    pump();
    ev_t ev;
    if (xQueueReceive(s_events, &ev, 0) != pdTRUE)
    {
        return MUSE_HATCH_EV_NONE;
    }
    strlcpy(text, ev.text, cap);
    return ev.type;
}

bool muse_hatch_turn_caption(size_t played, char *out, size_t cap)
{
    (void)played; /* no speech: the reply's page follows the reading pace */
    size_t len = strlen(s_reply);
    if (s_phase != RL_REPLY || !len)
    {
        return false;
    }
    size_t at = read_at(esp_timer_get_time());
    return muse_hatch_caption_at(s_reply, at < len ? at : len - 1, out, cap);
}

size_t muse_hatch_turn_read(int16_t *pcm, size_t frames, int wait_ms)
{
    (void)pcm;
    (void)frames;
    if (wait_ms > 0)
    {
        vTaskDelay(pdMS_TO_TICKS(wait_ms) ? pdMS_TO_TICKS(wait_ms) : 1);
    }
    return 0;
}

size_t muse_hatch_mp3_selftest(int16_t **pcm)
{
    *pcm = NULL;
    return 0;
}
