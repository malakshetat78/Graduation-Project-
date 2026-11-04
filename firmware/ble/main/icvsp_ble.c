#include <assert.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "icvsp_ble.h"
#include "transport.h"
static const char *TAG="ICVSP_BLE";
/* NimBLE UUID128 byte arrays are little endian. */
#define UUID_BYTES(kind) 0x00,0x63,0x9a,0x8e,0x04,0x2f,0x51,0x9b,0x82,0x4c,0x34,0x6a,kind,0x00,0x10,0x7b
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(UUID_BYTES(0x01));
static const ble_uuid128_t report_uuid=BLE_UUID128_INIT(UUID_BYTES(0x02));
static const ble_uuid128_t ack_uuid=BLE_UUID128_INIT(UUID_BYTES(0x03));
static transport_t transport;
static QueueHandle_t ingress;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE, report_handle;
static uint8_t address_type;
static struct ble_npl_callout tick;
static report_t incoming;
static int gap_event(struct ble_gap_event *event, void *arg);

static int access_report(uint16_t conn, uint16_t attr,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr; (void)arg;
    if (ctxt->op!=BLE_GATT_ACCESS_OP_WRITE_CHR || conn!=connection)
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint8_t ack[2];
    if (OS_MBUF_PKTLEN(ctxt->om)!=2 || os_mbuf_copydata(ctxt->om,0,2,ack)!=0)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint16_t id=((uint16_t)ack[0]<<8)|ack[1];
    if (!transport_ack(&transport,id)) return BLE_ATT_ERR_UNLIKELY;
    ESP_LOGI(TAG,"ACK report=%u remaining=%u",id,transport.count);
    return 0;
}
static int notify_access(uint16_t c,uint16_t a,struct ble_gatt_access_ctxt *x,void *v) {
    (void)c;(void)a;(void)x;(void)v; return BLE_ATT_ERR_READ_NOT_PERMITTED;
}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,
     .characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&report_uuid.u,.access_cb=notify_access,
         .flags=BLE_GATT_CHR_F_NOTIFY,.val_handle=&report_handle},
        {.uuid=&ack_uuid.u,.access_cb=access_report,.flags=BLE_GATT_CHR_F_WRITE},
        {0}}}, {0}
};
static void advertise(void) {
    struct ble_hs_adv_fields fields={0}, response={0};
    fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128=(ble_uuid128_t *)&service_uuid;
    fields.num_uuids128=1; fields.uuids128_is_complete=1;
    const char *name=ble_svc_gap_device_name();
    response.name=(uint8_t *)name; response.name_len=strlen(name);
    response.name_is_complete=1;
    int rc=ble_gap_adv_set_fields(&fields);
    if (!rc) rc=ble_gap_adv_rsp_set_fields(&response);
    struct ble_gap_adv_params params={0};
    params.conn_mode=BLE_GAP_CONN_MODE_UND; params.disc_mode=BLE_GAP_DISC_MODE_GEN;
    if (!rc) rc=ble_gap_adv_start(address_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
    if (rc) ESP_LOGE(TAG,"advertise error=%d",rc);
    else ESP_LOGI(TAG,"Advertising ICVSP-001");
}
static void tick_cb(struct ble_npl_event *ev) {
    (void)ev;
    while (xQueueReceive(ingress,&incoming,0)==pdTRUE) {
        uint32_t old=transport.overflow_count;
        if (!transport_enqueue(&transport,incoming.data,incoming.len))
            ESP_LOGE(TAG,"report rejected: size or transport ID space exhausted; reboot required for IDs");
        if (old!=transport.overflow_count) ESP_LOGW(TAG,"queue full: dropped oldest");
    }
    uint8_t frame[FRAME_MAX];
    bool was_stalled=transport.stalled;
    uint64_t now=(uint64_t)(esp_timer_get_time()/1000);
    size_t n=transport_frame(&transport,now,frame);
    if (n) {
        struct os_mbuf *om=ble_hs_mbuf_from_flat(frame,n);
        if (om) {
            /* NimBLE consumes om even on error. Advance only if accepted. */
            int rc=ble_gatts_notify_custom(connection,report_handle,om);
            if (!rc) transport_sent(&transport,now);
            else ESP_LOGW(TAG,"notify retry rc=%d",rc);
        }
    }
    if (!was_stalled && transport.stalled)
        ESP_LOGW(TAG,"ACK retries exhausted: head retained until resubscribe/reconnect or overflow");
    ble_npl_callout_reset(&tick,ble_npl_time_ms_to_ticks32(30));
}
static int gap_event(struct ble_gap_event *ev,void *arg) {
    (void)arg;
    switch(ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status==0) {
            connection=ev->connect.conn_handle;
            ESP_LOGI(TAG,"Connected; waiting for report subscription");
        } else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connection=BLE_HS_CONN_HANDLE_NONE;
        transport_link(&transport,false,23); advertise(); break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if(ev->subscribe.conn_handle==connection && ev->subscribe.attr_handle==report_handle) {
            transport_link(&transport,ev->subscribe.cur_notify,ble_att_mtu(connection));
            ESP_LOGI(TAG,"Notifications=%d payload/fragment=%u",ev->subscribe.cur_notify,transport.room);
        }
        break;
    case BLE_GAP_EVENT_MTU:
        /* Do not alter frame geometry mid-message. A later subscription or
         * reconnect picks up a newly negotiated MTU; small fragments remain legal. */
        ESP_LOGI(TAG,"Negotiated MTU=%u",ev->mtu.value); break;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    default: break;
    }
    return 0;
}
static void on_reset(int reason) {
    connection=BLE_HS_CONN_HANDLE_NONE; transport_link(&transport,false,23);
    ESP_LOGE(TAG,"host reset=%d",reason);
}
static void on_sync(void) {
    int rc=ble_hs_util_ensure_addr(0); assert(rc==0);
    rc=ble_hs_id_infer_auto(0,&address_type); assert(rc==0);
    advertise();
    ble_npl_callout_reset(&tick,ble_npl_time_ms_to_ticks32(30));
}
static void host_task(void *arg) {
    (void)arg; nimble_port_run(); nimble_port_freertos_deinit();
}
esp_err_t icvsp_ble_start(void) {
    if (ingress) return ESP_ERR_INVALID_STATE;
    ingress=xQueueCreate(4,sizeof(report_t));
    if (!ingress) return ESP_ERR_NO_MEM;
    transport_init(&transport);
    esp_err_t err=nimble_port_init();
    if(err!=ESP_OK) { vQueueDelete(ingress); ingress=NULL; return err; }
    ble_hs_cfg.sync_cb=on_sync; ble_hs_cfg.reset_cb=on_reset;
    ble_svc_gap_init(); ble_svc_gatt_init();
    int rc=ble_gatts_count_cfg(services); assert(rc==0);
    rc=ble_gatts_add_svcs(services); assert(rc==0);
    rc=ble_svc_gap_device_name_set("ICVSP-001"); assert(rc==0);
    rc=ble_att_set_preferred_mtu(247); assert(rc==0);
    ble_npl_callout_init(&tick,nimble_port_get_dflt_eventq(),tick_cb,NULL);
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
esp_err_t icvsp_ble_enqueue(const uint8_t *bytes,size_t length) {
    if (!ingress) return ESP_ERR_INVALID_STATE;
    if (!bytes || !length || length>REPORT_MAX) return ESP_ERR_INVALID_ARG;
    report_t item={.len=(uint16_t)length}; memcpy(item.data,bytes,length);
    return xQueueSend(ingress,&item,0)==pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
