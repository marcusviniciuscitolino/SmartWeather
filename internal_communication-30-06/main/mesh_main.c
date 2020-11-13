/* Mesh Internal Communication Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "mesh_light.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tcpip_adapter.h"
#include "protocol_examples_common.h"
#include "esp_tls.h"
#include "dht11.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"

#define CONFIG_MQTT_PROTOCOL_311
#define MAX_HTTP_RECV_BUFFER 512
#define EXAMPLE_BROKER_URI "mqtt://143.107.145.19"

extern const char howsmyssl_com_root_cert_pem_start[] asm("_binary_howsmyssl_com_root_cert_pem_start");
extern const char howsmyssl_com_root_cert_pem_end[] asm("_binary_howsmyssl_com_root_cert_pem_end");

/*******************************************************
 *                Macros
 *******************************************************/

/*******************************************************
 *                Constants                            *
 *******************************************************/
#define RX_SIZE (1500)
#define TX_SIZE (1460)

/*******************************************************
 *                Variable Definitions                 *
 *******************************************************/
static const char *TAG = "MQTT_EXAMPLE";
static const char *MESH_TAG = "mesh_main";
static const uint8_t MESH_ID[6] = {0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
static uint8_t tx_buf[TX_SIZE] = {
    0,
};
static uint8_t rx_buf[RX_SIZE] = {
    0,
};
static bool is_running = true;
static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;
unsigned long prevms = 15000;
unsigned long msprev = 15000;
unsigned long mssprev = 15000;
int temperatura = 0;
int umidade = 0;
static esp_mqtt_client_handle_t mqtt_client = NULL;

/*******************************************************
 *                Variable Definitions HTTP_CLIENT     *
 *******************************************************/

mesh_light_ctl_t light_on = {
    .cmd = MESH_CONTROL_CMD,
    .on = 1,
    .token_id = MESH_TOKEN_ID,
    .token_value = MESH_TOKEN_VALUE,
};

mesh_light_ctl_t light_off = {
    .cmd = MESH_CONTROL_CMD,
    .on = 0,
    .token_id = MESH_TOKEN_ID,
    .token_value = MESH_TOKEN_VALUE,
};

/*******************************************************
 *                Function Declarations
 *******************************************************/

/*******************************************************
 *                Function Definitions
 *******************************************************/

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    char aux_temp[50];
    char aux_c[50];

    // your_context_t *context = event->context;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        sprintf(aux_temp, "%d", temperatura);
        sprintf(aux_c, "c|");
        strcat(aux_c, aux_temp);

        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/iot/Motion001/attrs", aux_c, 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        //msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        //msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        //msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        //ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://143.107.145.19",
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
}

int Randoms(int lower, int upper)
{
    int num = (rand() %
               (upper - lower + 1)) +
              lower;
    return num;
}

void esp_mesh_p2p_tx_main(void *arg)
{
    int i;
    esp_err_t err;
    int send_count = 0;
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    mesh_data_t data;
    data.data = tx_buf;
    data.size = sizeof(tx_buf);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;
    is_running = true;

    while (is_running)
    {
        if (!esp_mesh_is_root())
        {
            ESP_LOGI(MESH_TAG, "layer:%d, rtableSize:%d, %s", mesh_layer,
                     esp_mesh_get_routing_table_size(),
                     (is_mesh_connected && esp_mesh_is_root()) ? "ROOT" : is_mesh_connected ? "NODE" : "DISCONNECT");
            vTaskDelay(10 * 1000 / portTICK_RATE_MS);

            //inicializar o DHT
            DHT11_init(18);

            //vetor para transportar os dados para o Root
            uint8_t digits[6];

            //variaveis de controle
            int DHT = 0;

            //Vars Estação;
            int aux_temperatura = 0;
            int aux_umidade = 0;

            aux_temperatura = Randoms(20, 25);
            aux_umidade = Randoms(0, 99);

            if (DHT == 1)
            {
                aux_temperatura = DHT11_read().temperature;
                aux_umidade = DHT11_read().humidity;
            }

            digits[0] = aux_temperatura / 10;
            digits[1] = aux_temperatura % 10;

            digits[2] = aux_umidade / 10;
            digits[3] = aux_umidade % 10;

            //digits[4] e digits[5] indicam que o número da Estação
            digits[4] = 0;
            digits[5] = 1;

            data.data = digits;
            err = esp_mesh_send(NULL, &data, MESH_DATA_P2P, NULL, 0);
            printf("\n\n>-- Dados enviados ao Root --< \n");
            printf(">-- Temperatura ---> %d \n", aux_temperatura);
            printf(">-- Umidade ---> %d \n\n", aux_umidade);

            continue;
        }

        unsigned long tmm = esp_timer_get_time() / 1000;
        if (tmm - mssprev > 3000000)
        {

            esp_mqtt_client_stop(mqtt_client);
            esp_mqtt_client_start(mqtt_client);

            int aux_temperatura = 0;
            int aux_umidade = 0;

            aux_temperatura = Randoms(22, 23);
            aux_umidade = Randoms(55, 58);

            char url[100];
            sprintf(url, "/iot/teste001/attrs");
            char temperatura[100];
            char umidade[100];

            sprintf(umidade, "%d", aux_umidade);
            sprintf(temperatura, "%d", aux_temperatura);

            char aux_string_temperatura[100];
            sprintf(aux_string_temperatura, "t|");

            char aux_string_umidade[100];
            sprintf(aux_string_umidade, "h|");

            strcat(aux_string_temperatura, temperatura);
            strcat(aux_string_umidade, umidade);

            int msg_id2 = esp_mqtt_client_publish(mqtt_client, url, aux_string_temperatura, 0, 1, 0);
            ESP_LOGI(TAG, "sent publish, msg_id=%d", msg_id2);

            int msg_id3 = esp_mqtt_client_publish(mqtt_client, url, aux_string_umidade, 0, 1, 0);
            ESP_LOGI(TAG, "sent publish, msg_id=%d", msg_id3);

            mssprev = tmm;
        }

        esp_mesh_get_routing_table((mesh_addr_t *)&route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);
        if (send_count && !(send_count % 100))
        {
            ESP_LOGI(MESH_TAG, "size:%d/%d,send_count:%d", route_table_size,
                     esp_mesh_get_routing_table_size(), send_count);
        }
        send_count++;
        tx_buf[25] = (send_count >> 24) & 0xff;
        tx_buf[24] = (send_count >> 16) & 0xff;
        tx_buf[23] = (send_count >> 8) & 0xff;
        tx_buf[22] = (send_count >> 0) & 0xff;
        if (send_count % 2)
        {
            memcpy(tx_buf, (uint8_t *)&light_on, sizeof(light_on));
        }
        else
        {
            memcpy(tx_buf, (uint8_t *)&light_off, sizeof(light_off));
        }

        for (i = 0; i < route_table_size; i++)
        {
            err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
            if (err)
            {
                ESP_LOGE(MESH_TAG,
                         "[ROOT-2-UNICAST:%d][L:%d]parent:" MACSTR " to " MACSTR ", heap:%d[err:0x%x, proto:%d, tos:%d]",
                         send_count, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                         MAC2STR(route_table[i].addr), esp_get_free_heap_size(),
                         err, data.proto, data.tos);
            }
            else if (!(send_count % 100))
            {
                ESP_LOGW(MESH_TAG,
                         "[ROOT-2-UNICAST:%d][L:%d][rtableSize:%d]parent:" MACSTR " to " MACSTR ", heap:%d[err:0x%x, proto:%d, tos:%d]",
                         send_count, mesh_layer,
                         esp_mesh_get_routing_table_size(),
                         MAC2STR(mesh_parent_addr.addr),
                         MAC2STR(route_table[i].addr), esp_get_free_heap_size(),
                         err, data.proto, data.tos);
            }
        }

        //if route_table_size is less than 10, add delay to avoid watchdog in this task.
        if (route_table_size < 10)
        {
            vTaskDelay(1 * 1000 / portTICK_RATE_MS);
        }
        printf("Root Da Rede MESH\n");
    }
    vTaskDelete(NULL);
}

void esp_mesh_p2p_rx_main(void *arg)
{
    int recv_count = 0;
    esp_err_t err;
    mesh_addr_t from;
    int send_count = 0;
    mesh_data_t data;
    int flag = 0;
    data.data = rx_buf;
    data.size = RX_SIZE;
    is_running = true;

    while (is_running)
    {
        data.size = RX_SIZE;
        err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        if (err != ESP_OK || !data.size)
        {
            ESP_LOGE(MESH_TAG, "err:0x%x, size:%d", err, data.size);
            continue;
        }

        if (esp_mesh_is_root())
        {

            uint8_t *temp = data.data;

            //-----------------------TRATAMENTO NUMERO DA ESTAÇÃO --------------------------------
            char aux_num_station_01[30];
            char aux_num_station_02[30];
            //criar chars para quebrar o numero recebido da folha;

            sprintf(aux_num_station_01, "%d", temp[4]);
            sprintf(aux_num_station_02, "%d", temp[5]);
            //inserindo as posições recebidas da folha nos chars criados acima;

            strcat(aux_num_station_01, aux_num_station_02);
            //concatenando ambos, criando um vetor de char com o numero;

            //-----------------------FIM TRATAMENTO NUMERO DA ESTAÇÃO --------------------------------

            //------------------------TRATAMENTO DE TEMPERATURA RECEBIDA-----------------------
            char aux_char_temperatura[30];
            char aux_char_digit_temperatura[30];
            //criar chars para quebrar o vetor recebido da folha;

            sprintf(aux_char_temperatura, "%d", temp[0]);
            sprintf(aux_char_digit_temperatura, "%d", temp[1]);
            //inserindo as posições recebidas da folha nos chars criados acima;

            strcat(aux_char_temperatura, aux_char_digit_temperatura);
            //concatenando ambos, criando um vetor de char com a temp;

            int aux_temperatura = 0;
            aux_temperatura = atoi(aux_char_temperatura);
            //------------------------FIM TRATAMENTO DE TEMPERATURA RECEBIDA--------------------

            //------------------------TRATAMENTO DE UMIDADE RECEBIDA-----------------------
            char aux_char_umidade[30];
            char aux_char_digit_umidade[30];
            //criar chars para quebrar o vetor recebido da folha;

            sprintf(aux_char_umidade, "%d", temp[2]);
            sprintf(aux_char_digit_umidade, "%d", temp[3]);
            //inserindo as posições recebidas da folha nos chars criados acima;

            strcat(aux_char_umidade, aux_char_digit_umidade);
            //concatenando ambos, criando um vetor de char com a temp;

            int aux_umidade = 0;
            aux_umidade = atoi(aux_char_umidade);
            //------------------------FIM TRATAMENTO DE UMIDADE RECEBIDA--------------------

            printf("Temperatura recebida ---> %d \n", aux_temperatura);
            printf("Umidade recebida ---> %d \n", aux_umidade);

            unsigned long mt = esp_timer_get_time() / 1000;

            if (mt - msprev > 30000)
            {

                esp_mqtt_client_stop(mqtt_client);
                esp_mqtt_client_start(mqtt_client);

                //---------------ENVIO DA TEMPERATURA---------------------------
                char aux_char_temp[100];
                char aux_string[100];

                sprintf(aux_char_temp, "%d", aux_temperatura);
                sprintf(aux_string, "t|");
                strcat(aux_string, aux_char_temp);

                char url[100];
                sprintf(url, "/iot/station");
                strcat(url, aux_num_station_01);
                strcat(url, "/attrs");

                int msg_id = esp_mqtt_client_publish(mqtt_client, url, aux_string, 0, 1, 0);
                ESP_LOGI(TAG, "sent publish, msg_id=%d", msg_id);
                //---------------FIM ENVIO DA TEMPERATURA------------------------------------------------------

                //--------------- ENVIO DA UMIDADE---------------------------
                char aux_char_umid[100];
                char aux_string2[100];

                sprintf(aux_char_umid, "%d", aux_umidade);
                sprintf(aux_string2, "h|");
                strcat(aux_string2, aux_char_umid);

                int msg_id2 = esp_mqtt_client_publish(mqtt_client, url, aux_string2, 0, 1, 0);
                ESP_LOGI(TAG, "sent publish, msg_id=%d", msg_id2);
                //--------------- FIM ENVIO DA UMIDADE---------------------------

                msprev = mt;
            }
        }

        /* extract send count */
        if (data.size >= sizeof(send_count))
        {
            send_count = (data.data[25] << 24) | (data.data[24] << 16) | (data.data[23] << 8) | data.data[22];
        }
        recv_count++;
    }
    vTaskDelete(NULL);
}

esp_err_t esp_mesh_comm_p2p_start(void)
{
    static bool is_comm_p2p_started = false;
    if (!is_comm_p2p_started)
    {
        is_comm_p2p_started = true;
        xTaskCreate(esp_mesh_p2p_tx_main, "MPTX", 3072, NULL, 5, NULL);
        xTaskCreate(esp_mesh_p2p_rx_main, "MPRX", 3072, NULL, 5, NULL);
    }
    return ESP_OK;
}

void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    mesh_addr_t id = {
        0,
    };
    static uint8_t last_layer = 0;

    switch (event_id)
    {
    case MESH_EVENT_STARTED:
    {
        esp_mesh_get_id(&id);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:" MACSTR "", MAC2STR(id.addr));
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_STOPPED:
    {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_CHILD_CONNECTED:
    {
        mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, " MACSTR "",
                 child_connected->aid,
                 MAC2STR(child_connected->mac));
    }
    break;
    case MESH_EVENT_CHILD_DISCONNECTED:
    {
        mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, " MACSTR "",
                 child_disconnected->aid,
                 MAC2STR(child_disconnected->mac));
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_ADD:
    {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new);
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE:
    {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new);
    }
    break;
    case MESH_EVENT_NO_PARENT_FOUND:
    {
        mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                 no_parent->scan_times);
    }
    /* TODO handler for the failure */
    break;
    case MESH_EVENT_PARENT_CONNECTED:
    {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        esp_mesh_get_id(&id);
        mesh_layer = connected->self_layer;
        memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:" MACSTR "%s, ID:" MACSTR "",
                 last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 esp_mesh_is_root() ? "<ROOT>" : (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr));
        last_layer = mesh_layer;
        mesh_connected_indicator(mesh_layer);
        is_mesh_connected = true;
        if (esp_mesh_is_root())
        {
            tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
        }
        esp_mesh_comm_p2p_start();
    }
    break;
    case MESH_EVENT_PARENT_DISCONNECTED:
    {
        mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 disconnected->reason);
        is_mesh_connected = false;
        mesh_disconnected_indicator();
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_LAYER_CHANGE:
    {
        mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *)event_data;
        mesh_layer = layer_change->new_layer;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                 last_layer, mesh_layer,
                 esp_mesh_is_root() ? "<ROOT>" : (mesh_layer == 2) ? "<layer2>" : "");
        last_layer = mesh_layer;
        mesh_connected_indicator(mesh_layer);
    }
    break;
    case MESH_EVENT_ROOT_ADDRESS:
    {
        mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:" MACSTR "",
                 MAC2STR(root_addr->addr));
    }
    break;
    case MESH_EVENT_VOTE_STARTED:
    {
        mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:" MACSTR "",
                 vote_started->attempts,
                 vote_started->reason,
                 MAC2STR(vote_started->rc_addr.addr));
    }
    break;
    case MESH_EVENT_VOTE_STOPPED:
    {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
        break;
    }
    case MESH_EVENT_ROOT_SWITCH_REQ:
    {
        mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:" MACSTR "",
                 switch_req->reason,
                 MAC2STR(switch_req->rc_addr.addr));
    }
    break;
    case MESH_EVENT_ROOT_SWITCH_ACK:
    {
        /* new root */
        mesh_layer = esp_mesh_get_layer();
        esp_mesh_get_parent_bssid(&mesh_parent_addr);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:" MACSTR "", mesh_layer, MAC2STR(mesh_parent_addr.addr));
    }
    break;
    case MESH_EVENT_TODS_STATE:
    {
        mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
    }
    break;
    case MESH_EVENT_ROOT_FIXED:
    {
        mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                 root_fixed->is_fixed ? "fixed" : "not fixed");
    }
    break;
    case MESH_EVENT_ROOT_ASKED_YIELD:
    {
        mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_ASKED_YIELD>" MACSTR ", rssi:%d, capacity:%d",
                 MAC2STR(root_conflict->addr),
                 root_conflict->rssi,
                 root_conflict->capacity);
    }
    break;
    case MESH_EVENT_CHANNEL_SWITCH:
    {
        mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
    }
    break;
    case MESH_EVENT_SCAN_DONE:
    {
        mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                 scan_done->number);
    }
    break;
    case MESH_EVENT_NETWORK_STATE:
    {
        mesh_event_network_state_t *network_state = (mesh_event_network_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                 network_state->is_rootless);
    }
    break;
    case MESH_EVENT_STOP_RECONNECTION:
    {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
    }
    break;
    case MESH_EVENT_FIND_NETWORK:
    {
        mesh_event_find_network_t *find_network = (mesh_event_find_network_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:" MACSTR "",
                 find_network->channel, MAC2STR(find_network->router_bssid));
    }
    break;
    case MESH_EVENT_ROUTER_SWITCH:
    {
        mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, " MACSTR "",
                 router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
    }
    break;
    default:
        ESP_LOGI(MESH_TAG, "unknown id:%d", event_id);
        break;
    }
}

void ip_event_handler(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:%s", ip4addr_ntoa(&event->ip_info.ip));
}

void app_main(void)
{
    ESP_ERROR_CHECK(mesh_light_init());
    ESP_ERROR_CHECK(nvs_flash_init());
    /*  tcpip initialization */
    tcpip_adapter_init();
    /* for mesh
     * stop DHCP server on softAP interface by default
     * stop DHCP client on station interface by default
     * */
    ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
    ESP_ERROR_CHECK(tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA));
    /*  event initialization */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /*  wifi initialization */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *)&cfg.mesh_id, MESH_ID, 6);
    /* router */
    /*cfg.channel = CONFIG_MESH_CHANNEL;*/
    cfg.channel = 11;
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *)&cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *)&cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
           strlen(CONFIG_MESH_ROUTER_PASSWD));

    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *)&cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
           strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));

    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());
    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%d, %s\n", esp_get_free_heap_size(),
             esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed");

    //ESP_ERROR_CHECK(example_connect());
    //if (esp_mesh_is_root())
    mqtt_app_start();
}
