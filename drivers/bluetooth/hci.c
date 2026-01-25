/*
 * Vib-OS - Bluetooth HCI Driver
 * 
 * Bluetooth Host Controller Interface for USB Bluetooth adapters.
 */

#include "types.h"
#include "printk.h"
#include "mm/kmalloc.h"

/* ===================================================================== */
/* HCI Constants */
/* ===================================================================== */

#define HCI_COMMAND_PKT         0x01
#define HCI_ACLDATA_PKT         0x02
#define HCI_SCODATA_PKT         0x03
#define HCI_EVENT_PKT           0x04

/* HCI Commands */
#define HCI_OP_RESET            0x0C03
#define HCI_OP_READ_BD_ADDR     0x1009
#define HCI_OP_READ_LOCAL_NAME  0x0C14
#define HCI_OP_WRITE_LOCAL_NAME 0x0C13
#define HCI_OP_INQUIRY          0x0401
#define HCI_OP_INQUIRY_CANCEL   0x0402
#define HCI_OP_CREATE_CONN      0x0405
#define HCI_OP_DISCONNECT       0x0406
#define HCI_OP_ACCEPT_CONN      0x0409
#define HCI_OP_REJECT_CONN      0x040A

/* HCI Events */
#define HCI_EV_INQUIRY_COMPLETE     0x01
#define HCI_EV_INQUIRY_RESULT       0x02
#define HCI_EV_CONN_COMPLETE        0x03
#define HCI_EV_CONN_REQUEST         0x04
#define HCI_EV_DISCONN_COMPLETE     0x05
#define HCI_EV_CMD_COMPLETE         0x0E
#define HCI_EV_CMD_STATUS           0x0F

/* ===================================================================== */
/* HCI Structures */
/* ===================================================================== */

struct hci_command_hdr {
    uint16_t opcode;
    uint8_t  plen;
} __attribute__((packed));

struct hci_event_hdr {
    uint8_t  evt;
    uint8_t  plen;
} __attribute__((packed));

struct hci_acl_hdr {
    uint16_t handle;
    uint16_t dlen;
} __attribute__((packed));

/* Bluetooth Device Address */
typedef struct {
    uint8_t b[6];
} bdaddr_t;

/* ===================================================================== */
/* Bluetooth Device State */
/* ===================================================================== */

#define MAX_BT_DEVICES  16

struct bt_device {
    bdaddr_t addr;
    char name[248];
    uint8_t class[3];
    int rssi;
    bool connected;
    uint16_t handle;
};

struct bt_adapter {
    bdaddr_t addr;
    char name[248];
    bool powered;
    bool discoverable;
    bool pairable;
    
    /* USB transport */
    void *usb_dev;
    int usb_ep_in;
    int usb_ep_out;
    
    /* Discovered devices */
    struct bt_device devices[MAX_BT_DEVICES];
    int device_count;
    
    /* Connection */
    bool connected;
    uint16_t conn_handle;
};

static struct bt_adapter adapter = {0};

/* ===================================================================== */
/* HCI Command Helpers */
/* ===================================================================== */

static int hci_send_cmd(uint16_t opcode, void *params, uint8_t plen)
{
    uint8_t buf[256];
    
    buf[0] = HCI_COMMAND_PKT;
    
    struct hci_command_hdr *hdr = (struct hci_command_hdr *)&buf[1];
    hdr->opcode = opcode;
    hdr->plen = plen;
    
    if (plen > 0 && params) {
        for (int i = 0; i < plen; i++) {
            buf[4 + i] = ((uint8_t *)params)[i];
        }
    }
    
    /* TODO: Send via USB bulk endpoint */
    printk(KERN_DEBUG "BT: Send cmd opcode=0x%04x len=%d\n", opcode, plen);
    
    return 0;
}

/* ===================================================================== */
/* HCI Commands */
/* ===================================================================== */

int bt_reset(void)
{
    printk(KERN_INFO "BT: Resetting adapter\n");
    return hci_send_cmd(HCI_OP_RESET, NULL, 0);
}

int bt_read_bd_addr(void)
{
    return hci_send_cmd(HCI_OP_READ_BD_ADDR, NULL, 0);
}

int bt_set_local_name(const char *name)
{
    uint8_t params[248] = {0};
    int len = 0;
    
    while (name[len] && len < 247) {
        params[len] = name[len];
        len++;
    }
    
    return hci_send_cmd(HCI_OP_WRITE_LOCAL_NAME, params, 248);
}

int bt_start_inquiry(uint8_t length, uint8_t num_responses)
{
    uint8_t params[5] = {
        0x33, 0x8B, 0x9E,  /* LAP (GIAC) */
        length,
        num_responses
    };
    
    printk(KERN_INFO "BT: Starting inquiry (length=%d)\n", length);
    return hci_send_cmd(HCI_OP_INQUIRY, params, 5);
}

int bt_cancel_inquiry(void)
{
    return hci_send_cmd(HCI_OP_INQUIRY_CANCEL, NULL, 0);
}

int bt_connect(bdaddr_t *addr)
{
    uint8_t params[13] = {0};
    
    /* Copy BD_ADDR */
    for (int i = 0; i < 6; i++) {
        params[i] = addr->b[i];
    }
    
    /* Packet type (DM1, DH1, DM3, DH3, DM5, DH5) */
    params[6] = 0x18;
    params[7] = 0xCC;
    
    /* Page scan mode */
    params[8] = 0x01;
    
    /* Reserved */
    params[9] = 0x00;
    params[10] = 0x00;
    
    /* Clock offset */
    params[11] = 0x00;
    params[12] = 0x00;
    
    printk(KERN_INFO "BT: Connecting to %02x:%02x:%02x:%02x:%02x:%02x\n",
           addr->b[5], addr->b[4], addr->b[3],
           addr->b[2], addr->b[1], addr->b[0]);
    
    return hci_send_cmd(HCI_OP_CREATE_CONN, params, 13);
}

int bt_disconnect(uint16_t handle, uint8_t reason)
{
    uint8_t params[3] = {
        handle & 0xFF,
        (handle >> 8) & 0xFF,
        reason
    };
    
    return hci_send_cmd(HCI_OP_DISCONNECT, params, 3);
}

/* ===================================================================== */
/* Event Processing */
/* ===================================================================== */

void bt_process_event(uint8_t *data, int len)
{
    if (len < 2) return;
    
    struct hci_event_hdr *hdr = (struct hci_event_hdr *)data;
    uint8_t *params = data + 2;
    
    switch (hdr->evt) {
        case HCI_EV_CMD_COMPLETE: {
            uint16_t opcode = params[1] | (params[2] << 8);
            uint8_t status = params[3];
            printk(KERN_DEBUG "BT: Command complete: opcode=0x%04x status=%d\n",
                   opcode, status);
            
            if (opcode == HCI_OP_READ_BD_ADDR && status == 0) {
                for (int i = 0; i < 6; i++) {
                    adapter.addr.b[i] = params[4 + i];
                }
                printk(KERN_INFO "BT: Local address: %02x:%02x:%02x:%02x:%02x:%02x\n",
                       adapter.addr.b[5], adapter.addr.b[4], adapter.addr.b[3],
                       adapter.addr.b[2], adapter.addr.b[1], adapter.addr.b[0]);
            }
            break;
        }
        
        case HCI_EV_INQUIRY_RESULT: {
            int num = params[0];
            printk(KERN_INFO "BT: Found %d device(s)\n", num);
            
            for (int i = 0; i < num && adapter.device_count < MAX_BT_DEVICES; i++) {
                struct bt_device *dev = &adapter.devices[adapter.device_count++];
                
                int offset = 1 + i * 14;
                for (int j = 0; j < 6; j++) {
                    dev->addr.b[j] = params[offset + j];
                }
                
                printk(KERN_INFO "BT:   Device: %02x:%02x:%02x:%02x:%02x:%02x\n",
                       dev->addr.b[5], dev->addr.b[4], dev->addr.b[3],
                       dev->addr.b[2], dev->addr.b[1], dev->addr.b[0]);
            }
            break;
        }
        
        case HCI_EV_CONN_COMPLETE: {
            uint8_t status = params[0];
            uint16_t handle = params[1] | (params[2] << 8);
            
            if (status == 0) {
                adapter.connected = true;
                adapter.conn_handle = handle;
                printk(KERN_INFO "BT: Connected (handle=%d)\n", handle);
            } else {
                printk(KERN_ERR "BT: Connection failed (status=%d)\n", status);
            }
            break;
        }
        
        case HCI_EV_DISCONN_COMPLETE: {
            uint16_t handle = params[1] | (params[2] << 8);
            printk(KERN_INFO "BT: Disconnected (handle=%d)\n", handle);
            adapter.connected = false;
            break;
        }
        
        case HCI_EV_INQUIRY_COMPLETE:
            printk(KERN_INFO "BT: Inquiry complete\n");
            break;
            
        default:
            printk(KERN_DEBUG "BT: Event 0x%02x\n", hdr->evt);
            break;
    }
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int bt_init(void *usb_device)
{
    printk(KERN_INFO "BT: Initializing Bluetooth adapter\n");
    
    adapter.usb_dev = usb_device;
    adapter.powered = false;
    adapter.device_count = 0;
    
    /* Reset adapter */
    bt_reset();
    
    /* Read local BD_ADDR */
    bt_read_bd_addr();
    
    /* Set local name */
    bt_set_local_name("Vib-OS");
    
    adapter.powered = true;
    
    printk(KERN_INFO "BT: Bluetooth adapter ready\n");
    
    return 0;
}

/* Get adapter info */
void bt_get_info(bdaddr_t *addr, char *name, bool *powered)
{
    if (addr) *addr = adapter.addr;
    if (name) {
        for (int i = 0; i < 248; i++) {
            name[i] = adapter.name[i];
        }
    }
    if (powered) *powered = adapter.powered;
}

/* Get discovered devices */
int bt_get_devices(struct bt_device *devices, int max_count)
{
    int count = (adapter.device_count < max_count) ? adapter.device_count : max_count;
    
    for (int i = 0; i < count; i++) {
        devices[i] = adapter.devices[i];
    }
    
    return count;
}
