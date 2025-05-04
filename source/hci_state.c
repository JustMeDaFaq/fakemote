#include <stdbool.h>
#include <string.h>

#include "fake_wiimote_mgr.h"
#include "hci.h"
#include "hci_state.h"
#include "syscalls.h"
#include "utils.h"

#define MAX_HCI_CONNECTIONS 32

/* Snooped HCI state (requested by SW BT stack) */
static u8 hci_unit_class[HCI_CLASS_SIZE];
static u8 hci_page_scan_enable;
static u8 hci_read_stored_link_key_read_all;
/* Other variables */
static u16 last_hci_virt_con_handle;

/* Simulated HCI state */
static struct {
    bool valid;
    u16 virt; /* The one we return to the BT SW stack */
    u16 phys; /* The one the BT dongle uses */
} hci_virt_con_handle_map_table[MAX_HCI_CONNECTIONS];

void hci_state_reset()
{
    for (int i = 0; i < ARRAY_SIZE(hci_virt_con_handle_map_table); i++)
        hci_virt_con_handle_map_table[i].valid = false;

    memset(hci_unit_class, 0, sizeof(hci_unit_class));
    hci_page_scan_enable = 0;
    hci_read_stored_link_key_read_all = 0;

    last_hci_virt_con_handle = 0;
}

u16 hci_con_handle_virt_alloc(void)
{
    u16 ret = last_hci_virt_con_handle;
    /* XXX: We can have collisions after it wraps around! */
    last_hci_virt_con_handle = (last_hci_virt_con_handle + 1) & 0x0EFF;
    return ret;
}

bool hci_can_request_connection(void)
{
    /* If page scan is disabled the controller will not see connection requests. */
    if (!(hci_page_scan_enable & HCI_PAGE_SCAN_ENABLE))
        return false;

    return true;
}

/* HCI connection handle virt<->phys mapping */

static bool hci_virt_con_handle_map(u16 phys, u16 virt)
{
    for (int i = 0; i < ARRAY_SIZE(hci_virt_con_handle_map_table); i++) {
        if (!hci_virt_con_handle_map_table[i].valid) {
            hci_virt_con_handle_map_table[i].virt = virt;
            hci_virt_con_handle_map_table[i].phys = phys;
            hci_virt_con_handle_map_table[i].valid = true;
            return true;
        }
    }
    return false;
}

static bool hci_virt_con_handle_unmap_virt(u16 virt)
{
    for (int i = 0; i < ARRAY_SIZE(hci_virt_con_handle_map_table); i++) {
        if (hci_virt_con_handle_map_table[i].valid &&
            hci_virt_con_handle_map_table[i].virt == virt) {
            hci_virt_con_handle_map_table[i].valid = false;
            return true;
        }
    }
    return false;
}

static bool hci_virt_con_handle_get_virt(u16 phys, u16 *virt)
{
    for (int i = 0; i < ARRAY_SIZE(hci_virt_con_handle_map_table); i++) {
        if (hci_virt_con_handle_map_table[i].valid &&
            hci_virt_con_handle_map_table[i].phys == phys) {
            *virt = hci_virt_con_handle_map_table[i].virt;
            return true;
        }
    }
    return false;
}

static bool hci_virt_con_handle_get_phys(u16 virt, u16 *phys)
{
    for (int i = 0; i < ARRAY_SIZE(hci_virt_con_handle_map_table); i++) {
        if (hci_virt_con_handle_map_table[i].valid &&
            hci_virt_con_handle_map_table[i].virt == virt) {
            *phys = hci_virt_con_handle_map_table[i].phys;
            return true;
        }
    }
    return false;
}

/* HCI handlers */

void hci_state_handle_hci_cmd_from_host(void *data, u32 length, bool *fwd_to_usb)
{
    hci_cmd_hdr_t *hdr = data;
    void *payload = (void *)((u8 *)hdr + sizeof(hci_cmd_hdr_t));
    u16 opcode = le16toh(hdr->opcode);
    u16 virt, phys = 0;
    bool success;

    LOG_DEBUG("H > C HCI CMD: opcode: 0x%04x\n", opcode);

    /* If the request targets a "fake wiimote", we don't have to hand it down to OH1.
     * Otherwise, we just have to patch the HCI connection handle from virtual to physical.
     */
    if (fake_wiimote_mgr_handle_hci_cmd_from_host(hdr)) {
        *fwd_to_usb = false;
        return;
    }

#define TRANSLATE_CON_HANDLE(event, type)                                                          \
    case event: {                                                                                  \
        type *cp = (type *)payload;                                                                \
        virt = le16toh(cp->con_handle);                                                            \
        success = hci_virt_con_handle_get_phys(virt, &phys);                                       \
        assert(success);                                                                           \
        cp->con_handle = htole16(phys);                                                            \
        os_sync_after_write(&cp->con_handle, sizeof(cp->con_handle));                              \
        break;                                                                                     \
    }

    switch (opcode) {
    case HCI_CMD_CREATE_CON:
        /* TODO */
        assert(0);
        break;
        TRANSLATE_CON_HANDLE(HCI_CMD_DISCONNECT, hci_discon_cp)
    case HCI_CMD_WRITE_SCAN_ENABLE: {
        hci_write_scan_enable_cp *cp = payload;
        hci_page_scan_enable = cp->scan_enable;
        break;
    }
    case HCI_CMD_WRITE_UNIT_CLASS: {
        hci_write_unit_class_cp *cp = payload;
        hci_unit_class[0] = cp->uclass[0];
        hci_unit_class[1] = cp->uclass[1];
        hci_unit_class[2] = cp->uclass[2];
        break;
    }
        TRANSLATE_CON_HANDLE(HCI_CMD_ADD_SCO_CON, hci_add_sco_con_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_CHANGE_CON_PACKET_TYPE, hci_change_con_pkt_type_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_AUTH_REQ, hci_auth_req_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_SET_CON_ENCRYPTION, hci_set_con_encryption_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_CHANGE_CON_LINK_KEY, hci_change_con_link_key_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_REMOTE_FEATURES, hci_read_remote_features_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_REMOTE_EXTENDED_FEATURES,
                             hci_read_remote_extended_features_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_REMOTE_VER_INFO, hci_read_remote_ver_info_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_CLOCK_OFFSET, hci_read_clock_offset_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_LMP_HANDLE, hci_read_lmp_handle_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_SETUP_SCO_CON, hci_setup_sco_con_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_HOLD_MODE, hci_hold_mode_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_SNIFF_MODE, hci_sniff_mode_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_EXIT_SNIFF_MODE, hci_exit_sniff_mode_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_PARK_MODE, hci_park_mode_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_EXIT_PARK_MODE, hci_exit_park_mode_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_QOS_SETUP, hci_qos_setup_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_ROLE_DISCOVERY, hci_role_discovery_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_LINK_POLICY_SETTINGS, hci_read_link_policy_settings_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_WRITE_LINK_POLICY_SETTINGS, hci_write_link_policy_settings_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_FLOW_SPECIFICATION, hci_flow_specification_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_SNIFF_SUBRATING, hci_sniff_subrating_cp)
    case HCI_CMD_RESET:
        LOG_DEBUG("HCI_CMD_RESET\n");
        hci_state_reset();
        break;
        TRANSLATE_CON_HANDLE(HCI_CMD_FLUSH, hci_flush_cp)
    case HCI_CMD_READ_STORED_LINK_KEY: {
        hci_read_stored_link_key_cp *cp = payload;
        /* Save requested info to patch the corresponding Command Complete Event */
        hci_read_stored_link_key_read_all = cp->read_all;
        break;
    }
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_AUTO_FLUSH_TIMEOUT, hci_read_auto_flush_timeout_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_WRITE_AUTO_FLUSH_TIMEOUT, hci_write_auto_flush_timeout_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_XMIT_LEVEL, hci_read_xmit_level_cp)
    case HCI_CMD_HOST_NUM_COMPL_PKTS:
        /* TODO: Is this command ever sent actually? */
        assert(0);
        break;
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_LINK_SUPERVISION_TIMEOUT,
                             hci_read_link_supervision_timeout_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_WRITE_LINK_SUPERVISION_TIMEOUT,
                             hci_write_link_supervision_timeout_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_REFRESH_ENCRYPTION_KEY, hci_refresh_encryption_key_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_ENHANCED_FLUSH, hci_enhanced_flush_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_FAILED_CONTACT_CNTR, hci_read_failed_contact_cntr_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_RESET_FAILED_CONTACT_CNTR, hci_reset_failed_contact_cntr_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_LINK_QUALITY, hci_read_link_quality_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_RSSI, hci_read_rssi_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_AFH_CHANNEL_MAP, hci_read_afh_channel_map_cp)
        TRANSLATE_CON_HANDLE(HCI_CMD_READ_CLOCK, hci_read_clock_cp)
    }
#undef TRANSLATE_CON_HANDLE
}

void hci_state_handle_hci_event_from_controller(void *data, u32 length)
{
    bool ret;
    bool success;
    u16 phys, virt = 0;
    hci_event_hdr_t *hdr = data;
    void *payload = (void *)((u8 *)hdr + sizeof(hci_event_hdr_t));

    /* Here we just have to patch the HCI connection handles from physical to virtual,
     * and check for connection/disconnection events to create/remove the mappings.  */

    LOG_DEBUG("C > H HCI EVT: event: 0x%02x, len: 0x%x\n", hdr->event, hdr->length);

#define TRANSLATE_CON_HANDLE(event, type)                                                          \
    case event: {                                                                                  \
        type *ep = (type *)payload;                                                                \
        phys = le16toh(ep->con_handle);                                                            \
        success = hci_virt_con_handle_get_virt(phys, &virt);                                       \
        assert(success);                                                                           \
        ep->con_handle = htole16(virt);                                                            \
        os_sync_after_write(&ep->con_handle, sizeof(ep->con_handle));                              \
        break;                                                                                     \
    }

    switch (hdr->event) {
    case HCI_EVENT_CON_COMPL: {
        hci_con_compl_ep *ep = payload;
        /* The BT controller sent us the *physical* HCI handle for the new connection.
         * Allocate a new virtual HCI handle and map it. */
        LOG_DEBUG("HCI_EVENT_CON_COMPL: status: 0x%x, handle: 0x%x\n", ep->status,
                  le16toh(ep->con_handle));
        if (ep->status == 0) {
            /* Allocate a new virtual connection handle */
            virt = hci_con_handle_virt_alloc();
            /* Create the new connection handle mapping */
            ret = hci_virt_con_handle_map(le16toh(ep->con_handle), virt);
            assert(ret);
            LOG_DEBUG("New HCI connection. Mapping: p 0x%x -> v 0x%x\n", le16toh(ep->con_handle),
                      virt);
            ep->con_handle = htole16(virt);
            os_sync_after_write(&ep->con_handle, sizeof(ep->con_handle));
        }
        break;
    }
    case HCI_EVENT_DISCON_COMPL: {
        hci_discon_compl_ep *ep = payload;
        /* The BT controller sent us the *physical* HCI handle for the disconnection.
         * Unmap the virtual HCI handle associated to it. */
        LOG_DEBUG("HCI_EVENT_DISCON_COMPL: status: 0x%x, handle: 0x%x, reason: 0x%x\n", ep->status,
                  le16toh(ep->con_handle), ep->reason);
        if (ep->status == 0) {
            ret = hci_virt_con_handle_get_virt(le16toh(ep->con_handle), &virt);
            assert(ret);
            /* Remove the connection handle mapping */
            ret = hci_virt_con_handle_unmap_virt(virt);
            assert(ret);
            ep->con_handle = htole16(virt);
            os_sync_after_write(&ep->con_handle, sizeof(ep->con_handle));
        }
        break;
    }
        TRANSLATE_CON_HANDLE(HCI_EVENT_AUTH_COMPL, hci_auth_compl_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_ENCRYPTION_CHANGE, hci_encryption_change_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_CHANGE_CON_LINK_KEY_COMPL, hci_change_con_link_key_compl_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_MASTER_LINK_KEY_COMPL, hci_master_link_key_compl_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_READ_REMOTE_FEATURES_COMPL,
                             hci_read_remote_features_compl_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_READ_REMOTE_VER_INFO_COMPL,
                             hci_read_remote_ver_info_compl_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_QOS_SETUP_COMPL, hci_qos_setup_compl_ep)
    case HCI_EVENT_COMMAND_COMPL: {
        hci_command_compl_ep *ep = payload;
        u16 opcode = le16toh(ep->opcode);
        if (opcode == HCI_CMD_READ_STORED_LINK_KEY) {
            hci_read_stored_link_key_rp *rp = (void *)((u8 *)ep + sizeof(*ep));
            u16 max_num_keys = le16toh(rp->max_num_keys);
            u16 num_keys_read = le16toh(rp->num_keys_read);
            /* Patch the event to include the link keys for the Fake Wiimotes */
            if (hci_read_stored_link_key_read_all) {
                rp->max_num_keys = htole16(max_num_keys + MAX_FAKE_WIIMOTES);
                rp->num_keys_read = htole16(num_keys_read + MAX_FAKE_WIIMOTES);
                os_sync_after_write(rp, sizeof(*rp));
            }
        }
        break;
    }
        TRANSLATE_CON_HANDLE(HCI_EVENT_FLUSH_OCCUR, hci_flush_occur_ep)
    case HCI_EVENT_NUM_COMPL_PKTS: {
        hci_num_compl_pkts_ep *ep = payload;
        hci_num_compl_pkts_info *info = (void *)((u8 *)ep + sizeof(*ep));
        /* Translate all HCI Connection Handles */
        for (int i = 0; i < ep->num_con_handles; i++) {
            phys = le16toh(info[i].con_handle);
            success = hci_virt_con_handle_get_virt(phys, &virt);
            assert(success);
            info[i].con_handle = htole16(virt);
        }
        os_sync_after_write(info, ep->num_con_handles * sizeof(hci_num_compl_pkts_info));
        break;
    }
        TRANSLATE_CON_HANDLE(HCI_EVENT_MODE_CHANGE, hci_mode_change_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_MAX_SLOT_CHANGE, hci_max_slot_change_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_READ_CLOCK_OFFSET_COMPL, hci_read_clock_offset_compl_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_CON_PKT_TYPE_CHANGED, hci_con_pkt_type_changed_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_QOS_VIOLATION, hci_qos_violation_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_FLOW_SPECIFICATION_COMPL, hci_flow_specification_compl_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_READ_REMOTE_EXTENDED_FEATURES,
                             hci_read_remote_extended_features_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_SCO_CON_COMPL, hci_sco_con_compl_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_SCO_CON_CHANGED, hci_sco_con_changed_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_SNIFF_SUBRATING, hci_sniff_subrating_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_ENCRYPTION_KEY_REFRESH, hci_encryption_key_refresh_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_LINK_SUPERVISION_TO_CHANGED,
                             hci_link_supervision_to_changed_ep)
        TRANSLATE_CON_HANDLE(HCI_EVENT_ENHANCED_FLUSH_COMPL, hci_enhanced_flush_compl_ep)
    }
#undef TRANSLATE_CON_HANDLE
}

void hci_state_handle_acl_data_in_response_from_controller(void *data, u32 length)
{
    bool ret;
    u16 virt = 0;
    hci_acldata_hdr_t *hdr = data;
    u16 handle_pb_bc = le16toh(hdr->con_handle);
    u16 payload_len = le16toh(hdr->length);
    u16 phys = HCI_CON_HANDLE(handle_pb_bc);
    u16 pb = HCI_PB_FLAG(handle_pb_bc);
    u16 pc = HCI_BC_FLAG(handle_pb_bc);
    UNUSED(payload_len);

    LOG_DEBUG("H < C ACL  IN: pcon_handle: 0x%x, len: 0x%x\n", phys, payload_len);

    ret = hci_virt_con_handle_get_virt(phys, &virt);
    assert(ret);
    hdr->con_handle = htole16(HCI_MK_CON_HANDLE(virt, pb, pc));

    LOG_DEBUG("    p 0x%x -> v 0x%x\n", phys, virt);

    /* Flush modified data */
    os_sync_after_write(&hdr->con_handle, sizeof(hdr->con_handle));
}

void hci_state_handle_acl_data_out_request_from_host(void *data, u32 length, bool *fwd_to_usb)
{
    bool ret;
    u16 phys = 0;
    hci_acldata_hdr_t *hdr = data;
    u16 handle_pb_bc = le16toh(hdr->con_handle);
    u16 payload_len = le16toh(hdr->length);
    u16 virt = HCI_CON_HANDLE(handle_pb_bc);
    u16 pb = HCI_PB_FLAG(handle_pb_bc);
    u16 pc = HCI_BC_FLAG(handle_pb_bc);
    UNUSED(payload_len);

    LOG_DEBUG("H > C ACL OUT: vcon_handle: 0x%x, len: 0x%x\n", virt, payload_len);

    /* First check if the virtual connection handle corresponds to a fake wiimote */
    if (fake_wiimote_mgr_handle_acl_data_out_request_from_host(virt, hdr)) {
        *fwd_to_usb = false;
        return;
    }

    ret = hci_virt_con_handle_get_phys(virt, &phys);
    assert(ret);
    hdr->con_handle = htole16(HCI_MK_CON_HANDLE(phys, pb, pc));

    LOG_DEBUG("    v 0x%x -> p 0x%x\n", virt, phys);

    /* Flush modified data */
    os_sync_after_write(&hdr->con_handle, sizeof(hdr->con_handle));
}
