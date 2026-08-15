#include "captive_dns.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "cdns";

#define DNS_PORT     53
#define DNS_MAX_LEN  512

/* Kopf einer DNS-Nachricht, alle Felder in Netzwerkbyte-Reihenfolge. */
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

/* Antwortsatz, angehaengt hinter der wiederholten Frage. */
typedef struct __attribute__((packed)) {
    uint16_t ptr;   /* Verweis auf den Namen in der Frage */
    uint16_t type;
    uint16_t klass;
    uint32_t ttl;
    uint16_t rd_len;
    uint32_t addr;
} dns_answer_t;

static TaskHandle_t s_task;
static volatile bool s_running;
static uint32_t s_answer_ip;

/* Ueberspringt einen Namen in Labelschreibweise und liefert die Laenge. */
static int skip_name(const uint8_t *p, int len)
{
    int i = 0;
    while (i < len && p[i] != 0) {
        if ((p[i] & 0xC0) == 0xC0) {
            return i + 2; /* Verweis, zwei Byte */
        }
        i += p[i] + 1;
    }
    return i + 1;
}

static void dns_task(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Kein Netzwerkanschluss verfuegbar");
        s_running = false;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Anschluss %d liess sich nicht belegen", DNS_PORT);
        close(sock);
        s_running = false;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Zeitbegrenzung, damit die Schleife das Ende mitbekommt. */
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "Namensdienst des Einrichtungsportals laeuft");

    uint8_t buf[DNS_MAX_LEN];
    while (s_running) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n < (int)sizeof(dns_header_t)) {
            continue;
        }

        dns_header_t *hdr = (dns_header_t *)buf;
        /* Nur gewoehnliche Anfragen mit genau einer Frage beantworten. */
        if ((ntohs(hdr->flags) & 0x8000) != 0 || ntohs(hdr->qd_count) != 1) {
            continue;
        }

        int qlen = skip_name(buf + sizeof(dns_header_t), n - (int)sizeof(dns_header_t));
        int question_end = (int)sizeof(dns_header_t) + qlen + 4; /* Typ und Klasse */
        if (question_end > n || question_end + (int)sizeof(dns_answer_t) > (int)sizeof(buf)) {
            continue;
        }

        hdr->flags = htons(0x8180); /* Antwort, Rekursion moeglich */
        hdr->an_count = htons(1);
        hdr->ns_count = 0;
        hdr->ar_count = 0;

        dns_answer_t *ans = (dns_answer_t *)(buf + question_end);
        ans->ptr = htons(0xC000 | (uint16_t)sizeof(dns_header_t));
        ans->type = htons(1);  /* A */
        ans->klass = htons(1); /* IN */
        ans->ttl = htonl(10);
        ans->rd_len = htons(4);
        ans->addr = s_answer_ip;

        sendto(sock, buf, question_end + sizeof(dns_answer_t), 0, (struct sockaddr *)&from,
               from_len);
    }

    close(sock);
    ESP_LOGI(TAG, "Namensdienst beendet");
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t captive_dns_start(uint32_t answer_ip)
{
    if (s_running) {
        return ESP_OK;
    }
    s_answer_ip = answer_ip;
    s_running = true;

    if (xTaskCreate(dns_task, "cdns", 3072, NULL, 4, &s_task) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void captive_dns_stop(void)
{
    s_running = false;
}

bool captive_dns_running(void)
{
    return s_running;
}
