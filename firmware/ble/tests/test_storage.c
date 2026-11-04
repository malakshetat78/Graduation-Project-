/* Host test: compile real report_storage.c with a minimal transactional NVS mock.
 * No ESP32 flash, BLE radio, or NVS partition layout is exercised.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "report_storage.h"
#include "nvs.h"

#define SLOTS 8
#define MAX_RECORD 2052
typedef struct { int used; size_t n; uint8_t b[MAX_RECORD]; } slot_t;
static slot_t committed[SLOTS], staged[SLOTS];
static int dirty, fail_commit;
static int slot_number(const char *k) {
    if (k[0]!='r' || k[1]<'0' || k[1]>'9' ||
        k[2]<'0' || k[2]>'9' || k[3]) return -1;
    int i=(k[1]-'0')*10 + k[2]-'0';
    return i<SLOTS ? i : -1;
}
esp_err_t nvs_flash_init_partition(const char *part) {
    return strcmp(part,"report_q")==0 ? ESP_OK : ESP_ERR_INVALID_ARG;
}
esp_err_t nvs_open_from_partition(const char *p,const char *ns,int rw,nvs_handle_t *h) {
    if (strcmp(p,"report_q") || strcmp(ns,"pending") || rw!=NVS_READWRITE) return ESP_FAIL;
    *h=1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n) {
    assert(h==1);
    int i=slot_number(key);
    if (i<0 || !committed[i].used) return ESP_ERR_NVS_NOT_FOUND;
    if (out) {
        if (*n<committed[i].n) return ESP_ERR_INVALID_SIZE;
        memcpy(out,committed[i].b,committed[i].n);
    }
    *n=committed[i].n;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *p,size_t n) {
    assert(h==1);
    int i=slot_number(key);
    if (i<0 || n>MAX_RECORD) return ESP_ERR_INVALID_ARG;
    if (!dirty) memcpy(staged,committed,sizeof(committed));
    staged[i].used=1;
    staged[i].n=n;
    memcpy(staged[i].b,p,n);
    dirty=1;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) {
    assert(h==1);
    int i=slot_number(key);
    if (i<0 || !committed[i].used) return ESP_ERR_NVS_NOT_FOUND;
    if (!dirty) memcpy(staged,committed,sizeof(committed));
    staged[i].used=0;
    dirty=1;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h==1);
    if (fail_commit) return ESP_FAIL;
    if (dirty) memcpy(committed,staged,sizeof(committed));
    dirty=0;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h==1); dirty=0; }

int main(void) {
    transport_t queue;
    transport_init(&queue);
    assert(report_storage_open(&queue)==ESP_OK);
    assert(queue.count==0);
    uint8_t a[]={0x01,0x02,0x03}, b[]={0x04,0x05};
    assert(report_storage_append(1,a,sizeof(a))==ESP_OK);
    assert(report_storage_append(2,b,sizeof(b))==ESP_OK);
    report_storage_close();

    transport_init(&queue);
    assert(report_storage_open(&queue)==ESP_OK);
    assert(queue.count==2 && queue.items[0].id==1 && queue.items[1].id==2);
    assert(queue.next_id==3);
    assert(queue.items[0].len==sizeof(a) && !memcmp(queue.items[0].data,a,sizeof(a)));

    fail_commit=1;
    assert(report_storage_remove(1)==ESP_FAIL);
    /* Crash before ACK commit: still present after power cycle. */
    report_storage_close();
    fail_commit=0;
    transport_init(&queue);
    assert(report_storage_open(&queue)==ESP_OK);
    assert(queue.count==2);
    assert(report_storage_remove(1)==ESP_OK);
    report_storage_close();

    transport_init(&queue);
    assert(report_storage_open(&queue)==ESP_OK);
    assert(queue.count==1 && queue.items[0].id==2);
    assert(report_storage_remove(2)==ESP_OK);
    report_storage_close();
    transport_init(&queue);
    assert(report_storage_open(&queue)==ESP_OK);
    assert(queue.count==0 && queue.next_id==1);
    report_storage_close();
    puts("PASS: NVS durable append, reboot restore, failed ACK commit, persistent removal");
    return 0;
}
