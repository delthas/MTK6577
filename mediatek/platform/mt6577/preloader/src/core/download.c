/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein is
 * confidential and proprietary to MediaTek Inc. and/or its licensors. Without
 * the prior written permission of MediaTek inc. and/or its licensors, any
 * reproduction, modification, use or disclosure of MediaTek Software, and
 * information contained herein, in whole or in part, shall be strictly
 * prohibited.
 * 
 * MediaTek Inc. (C) 2010. All rights reserved.
 * 
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER
 * ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES
 * TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE
 * RELEASED HEREUNDER WILL BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE
 * MEDIATEK SOFTWARE AT ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE
 * CHARGE PAID BY RECEIVER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek
 * Software") have been modified by MediaTek Inc. All revisions are subject to
 * any receiver's applicable license agreements with MediaTek Inc.
 */

#define CFG_I2C_SUPPORT            (1)

#include "typedefs.h"
#include "platform.h"
#include "download.h"
#include "sec.h"
#if CFG_I2C_SUPPORT
#include "mt6577_i2c.h"
#endif
#include "cust_sec_ctrl.h"
#include "buffer.h"
#include "sec_region.h"

#define CMD_GET_HW_SW_VER          0xfc
#define CMD_GET_HW_CODE            0xfd
#define CMD_GET_BL_VER             0xfe

#define CMD_LEGACY_WRITE           0xa1
#define CMD_LEGACY_READ            0xa2

#define CMD_I2C_INIT               0xB0
#define CMD_I2C_DEINIT             0xB1
#define CMD_I2C_WRITE8             0xB2
#define CMD_I2C_READ8              0xB3
#define CMD_I2C_SET_SPEED          0xB4

#define CMD_READ16                 0xD0
#define CMD_READ32                 0xD1
#define CMD_WRITE16                0xD2
#define CMD_WRITE16_NO_ECHO        0xD3
#define CMD_WRITE32                0xD4
#define CMD_JUMP_DA                0xD5
#define CMD_JUMP_BL                0xD6
#define CMD_SEND_DA                0xD7
#define CMD_GET_TARGET_CONFIG      0xD8

#define TGT_CFG_SBC_EN             0x00000001
#define TGT_CFG_SLA_EN             0x00000002
#define TGT_CFG_DAA_EN             0x00000004

#define DA_ARG_MAGIC               0x58885168
#define DA_ARG_VER                 1

#define DA_FLAG_SKIP_PLL_INIT      0x00000001
#define DA_FLAG_SKIP_EMI_INIT      0x00000002

#if CFG_I2C_SUPPORT
#define BROM_I2C_CMD_LEN           32
#define BROM_I2C_DATA_LEN          32
#define B_OK                       0
#define E_I2C_CMD_BUF_OVERFLOW     0x1D1E
#define E_I2C_DATA_BUF_OVERFLOW    0x1D1F
#define E_I2C_SPEED_MODE_INVALID   0x1D20
static u8 g_I2C_CmdBuf[BROM_I2C_CMD_LEN];
static u8 g_I2C_DataBuf[BROM_I2C_DATA_LEN];
#endif

static struct bldr_comport *dlcomport = NULL;

typedef struct {
    u32 magic;
    u32 ver;
    u32 flags;
} DownloadArg;

#if CFG_USB_DOWNLOAD && !CFG_LEGACY_USB_DOWNLOAD

#define MOD     "[USBDL]"

//#define DEBUG
#ifdef DEBUG
#define DBGMSG(fmt, args...)             print(fmt, ##args)
#define DBG_SND_BUF(buf, size) \
do { \
    u32 i;\
    for (i = 0; i < size; i++) \
        print("%s TGT--BUF[%d]=(0x%x)->TOOL\n", MOD, i, buf[i]); \
}while(0)

#define DBG_RCV_BUF(buf, size) 

#define DBG_RCV_BUF1(buf, size) \
do { \
    u32 i;\
    for (i = 0; i < size; i++) \
        print("%s TGT<-BUF[%d]=(0x%x)--TOOL\n", MOD, i, buf[i]); \
}while(0)
#else
#define DBGMSG(fmt, args...)             do{}while(0)
#define DBG_SND_BUF(buf, size)           do{}while(0)
#define DBG_RCV_BUF(buf, size)           do{}while(0)
#endif

static int usbdl_put_data(u8 *buf, u32 size)
{
    DBG_SND_BUF(buf, size);
    return dlcomport->ops->send(buf, size);
}

static int usbdl_get_data(u8 *buf, u32 size)
{
    int ret = dlcomport->ops->recv(buf, size, 0);
    DBG_RCV_BUF(buf, size);
    return ret;
}

static int usbdl_put_byte(u8 byte)
{
    DBGMSG("%s TGT--(0x%x)->TOOL\n", MOD, byte);
    return dlcomport->ops->send(&byte, 1);
}

static int usbdl_put_word(u16 word)
{
    u8  buf[2];
    u8 *ptr = &buf[0];

    DBGMSG("%s TGT--(0x%x)->TOOL\n", MOD, word);

    /* high byte is first */
    *ptr++ = word >> 8;
    *ptr = word & 0xff;
    
    return dlcomport->ops->send(buf, 2);
}

static int usbdl_put_dword(u32 dword)
{
    u8  buf[4];
    u8 *ptr = &buf[0];

    DBGMSG("%s TGT--(0x%x)->TOOL\n", MOD, dword);

    /* high byte is first */
    *ptr++ = (u8)(dword >> 24) & 0xff;
    *ptr++ = (u8)(dword >> 16) & 0xff;
    *ptr++ = (u8)(dword >> 8) & 0xff;
    *ptr = (u8)(dword >> 0) & 0xff;

    return dlcomport->ops->send(buf, 4);
}

static int usbdl_get_byte(u8 *byte)
{
    int ret = dlcomport->ops->recv(byte, 1, 0);
    DBGMSG("%s TGT<-(0x%x)--TOOL\n", MOD, *byte);
    return ret;
}

static int usbdl_get_word(u16 *word)
{
    int ret;
    u8  buf[2];

    if (0 == (ret = dlcomport->ops->recv(buf, 2, 0))) {
        /* high byte is first */
        *word = (buf[0] << 8) | (buf[1]);
        DBGMSG("%s TGT<-(0x%x)--TOOL\n", MOD, *word);        
    }

    return ret;
}

static int usbdl_get_dword(u32 *dword)
{
    int ret, i;
    u8 buf[4];
    u8 *ptr = &buf[0];
    u32 result = 0;

    if (0 == (ret = dlcomport->ops->recv(buf, 4, 0))) {
        for (i = 3; i >= 0; i--) {
            result |= (*ptr++ << (8 * i));
        }
        *dword = result;
        DBGMSG("%s TGT<-(0x%x)--TOOL\n", MOD, result);
    }

    return ret;
}

static u32 usbdl_write16(bool echo)
{
    u32 index;
    u32 base_addr=0;
    u32 addr=0;
    u32 len16=0;
    u32 len8=0;
    u16 data=0;
    u32 status=0;

    usbdl_get_dword(&base_addr);
    usbdl_put_dword(base_addr);

    usbdl_get_dword(&len16);
    usbdl_put_dword(len16);

    /* check addr alignment */
    if (0 != (base_addr & (2-1))) {
        status = -1;
        goto end;
    }

    /* check len */
    if (0 == len16) {
        status = -2;
        goto end;
    }

    /* convert half-word(2B) length to byte length */
    len8 = (len16<<1);

    // overflow attack check 
    if (len16 >= len8) {
        status = -3;
        goto end;
    }

    /* check if addr range is valid */
    sec_region_check(base_addr,len8);

    /* return status */
    usbdl_put_word(status);

    for (index = 0; index < len16; index++) {
        usbdl_get_word(&data);
        if (echo)
            usbdl_put_word(data);

        addr = base_addr + (index<<1);
        *(u16*)(addr) = data;
    }

end:
    /* return status */
    usbdl_put_word(status);

    return status;
}

static u32 usbdl_write32(bool echo)
{
    u32 index;
    u32 base_addr=0;
    u32 addr=0;
    u32 len32=0;
    u32 len8=0;
    u32 data=0;
    u32 status=0;

    usbdl_get_dword(&base_addr);
    usbdl_put_dword(base_addr);

    usbdl_get_dword(&len32);
    usbdl_put_dword(len32);

    /* check addr alignment */
    if (0 != (base_addr & (4-1))) {
        status = -1;
        goto end;
    }

    /* check len */
    if (0 == len32) {
        status = -2;
        goto end;
    }

    /* convert dword(4B) length to byte length */
    len8 = (len32<<2);

    /* overflow attack check */
    if (len32 >= len8) {
        status = -3;
        goto end;
    }

    /* check if addr range is valid */
    sec_region_check(base_addr,len8);

    /* return status */
    usbdl_put_word(status);

    for (index = 0; index < len32; index++) {
        usbdl_get_dword(&data);
        if (echo)
            usbdl_put_dword(data);

        addr = base_addr + (index<<2);     
        *(u32*)(addr) = data;
    }

end:
    /* return status */
    usbdl_put_word(status);

    return status;
}

static u32 usbdl_read16(bool legacy)
{
    u32 index;
    u32 base_addr=0;
    u32 len16=0;
    u32 len8=0;
    u16 data=0;
    u32 status=0;

    usbdl_get_dword(&base_addr);
    usbdl_put_dword(base_addr);

    usbdl_get_dword(&len16);
    usbdl_put_dword(len16);

    /* check addr alignment */
    if (0 != (base_addr & (2-1))) {
        status = -1;
        goto end;
    }

    /* check len */
    if (0 == len16) {
        status = -2;
        goto end;
    }

    /* convert half-word(2B) length to byte length */
    len8 = (len16 << 1);

    /* overflow attack check */
    if (len16 >= len8) {
        status = -3;
        goto end;
    }

    /* check if addr range is valid */
    sec_region_check(base_addr,len8);

    if (!legacy) {
        /* return status */
        usbdl_put_word(status);
    }

    for (index = 0; index < len16; index++) {
        data = *(u16*)(base_addr + (index << 1));
        usbdl_put_word(data);
    }

end:
    if(!legacy) {
        /* return status */
        usbdl_put_word(status);
    }

    return status;
}

static u32 usbdl_read32(bool echo)
{
    u32 index;
    u32 base_addr=0;
    u32 len32=0;
    u32 len8=0;
    u32 data=0;
    u32 status=0;

    usbdl_get_dword(&base_addr);
    usbdl_put_dword(base_addr);

    usbdl_get_dword(&len32);
    usbdl_put_dword(len32);

    /* check addr alignment */
    if (0 != (base_addr&(4-1))) {
        status = -1;
        goto end;
    }

    /* check len */
    if (0 == len32) {
        status = -2;
        goto end;
    }

    /* convert word(4B) length to byte length */
    len8 = (len32<<2);

    /* overflow attack check */
    if (len32 >= len8) {
        status = -3;
        goto end;
    }

    /* check if addr range is valid */
    sec_region_check(base_addr,len8);

    /* return status */
    usbdl_put_word(status);

    for(index=0; index<len32; index++) {
        data = *(u32*)(base_addr + (index<<2));
        usbdl_put_dword(data);
    }

end:
    /* return status */
    usbdl_put_word(status);

    return status;
}

static void usbdl_get_hw_code(void)
{
    u16 status = 0;
    u16 hwcode = *(u16*)APHW_CODE;

    usbdl_put_word(hwcode);
    usbdl_put_word(status);
}

static void usbdl_get_hw_sw_ver(void)
{
    u16 status = 0;

    usbdl_put_word(*(u16*)APHW_SUBCODE);
    usbdl_put_word(*(u16*)APHW_VER);
    usbdl_put_word(*(u16*)APSW_VER);
    usbdl_put_word(status);
}

static u16 usbdl_verify_da(u8* da_addr, u32 da_len, u32 sig_len)
{
    u16 status = 0;
    
#ifdef SEC_USBDL_WITHOUT_SLA_ENABLE 
    #ifdef DA_RAM_RELOCATE_ADDR
    u8 *sig_addr;
        
    if (FALSE == seclib_sec_usbdl_enabled(TRUE)) {
        print("%s usbdl_verify_da: DA validation disabled\n", MOD);
    } else {
        /* init download agent authentication key */
        if (da_auth_init() != 0) {
            print("%s usbdl_verify_da: DA init key fail\n", MOD);
            status = ERR_DA_INIT_KEY_FAIL;
        }
        
        /* validate download agent */
        sig_addr = (u8*)da_addr + (da_len - sig_len);
        
        if (sec_auth(da_addr, (da_len - sig_len), sig_addr, sig_len) != 0) {
            print("%s usbdl_verify_da: DA validation fail\n", MOD);
            status = ERR_DA_IMAGE_SIG_VERIFY_FAIL;
        }
    }
    #else
    /* validate fail */
    print("%s usbdl_verify_da: DA validation fail(mem)\n", MOD);
    status = ERR_DA_IMAGE_NO_MEM_FAIL;
    #endif      
#else
    print("%s usbdl_verify_da: DA validation bypass\n", MOD);
#endif

    return status;
}

static void usbdl_send_da(void)
{
    u32 da_addr;
    u32 da_len;
    u32 sig_len;
    u32 i;
    u32 count;
    u32 last_byte;
    u16 data16;    
    u16 chksum16; 
    u16 status = 0;

    /* get da addr */
    usbdl_get_dword(&da_addr);
    usbdl_put_dword(da_addr);

    /* get da len, which includes sig len */    
    usbdl_get_dword(&da_len);
    usbdl_put_dword(da_len);

    /* get DA sig len */
    usbdl_get_dword(&sig_len);
    usbdl_put_dword(sig_len);

    /* check if DA range is valid */
    
    /* check if DA overlap previous downloaded DA */

    /* return status 1 */    
    usbdl_put_word(status);

    /* receive DA data (no echo) */
#ifdef CFG_DA_RAM_ADDR    
    da_addr = CFG_DA_RAM_ADDR;
#endif
    usbdl_get_data((u8*)da_addr, da_len);

    /* calculate checksum */
    count     = (da_len >> 1);
    last_byte = (da_len & (2-1));
    chksum16  = 0;
    for (i = 0; i < count; i++) {
        chksum16 ^= *(u16*)(da_addr + (i<<1));
    }
    if (0 < last_byte) {
        data16 = *(u8*)(da_addr + (count<<1));
        chksum16 ^= data16;
    }
    usbdl_put_word(chksum16);

    status = usbdl_verify_da((u8*)da_addr, da_len, sig_len);

#ifdef DBG_PRELOADER
    print("%s usbdl_send_da: status2 is 0x%x\n", MOD, status);
#endif

    /* return status 2 */
    usbdl_put_word(status);

    /* check status. if verify failed, clean invalid da to prevent re-try attack */
    if (SEC_OK != status) {
        print("%s usbdl_send_da: clean invalid da\n", MOD);
        memset((void*)da_addr,0x0,da_len);
        ASSERT(0);    
    }

    return;
}

static void usbdl_jump_da(void)
{
    extern void bldr_jump(u32 addr, u32 arg1, u32 arg2);

    u32 da_addr;
    u16 status = 0;
    DownloadArg *da_arg;

    /* get DA jump addr */    
    usbdl_get_dword(&da_addr);
    usbdl_put_dword(da_addr);

    /* TODO: check da jump addr */

    /* return status */
    usbdl_put_word(status);
    
    /* jump to da */
#ifdef CFG_DA_RAM_ADDR
    da_addr = CFG_DA_RAM_ADDR;
#endif
    da_arg = (DownloadArg*)(CFG_DA_RAM_ADDR - sizeof(DownloadArg));

    BUG_ON((u32)da_arg > CFG_DA_RAM_ADDR);

    da_arg->magic = DA_ARG_MAGIC;
    da_arg->ver   = DA_ARG_VER;
    da_arg->flags = DA_FLAG_SKIP_PLL_INIT | DA_FLAG_SKIP_EMI_INIT;

    apmcu_dcache_clean_invalidate();
    apmcu_dsb();
    apmcu_dcache_invalidate();
    apmcu_disable_dcache();
    apmcu_disable_icache();    
    apmcu_icache_invalidate();    

    bldr_jump(da_addr, (u32)da_arg, (u32)sizeof(DownloadArg));
}

static void usbdl_get_tgt_config(void)
{
    u32 tgt_cfg = 0;
    u16 status = 0;

    if (seclib_sbc_enabled())
        tgt_cfg |= TGT_CFG_SBC_EN;
    if (seclib_sla_enabled())
        tgt_cfg |= TGT_CFG_SLA_EN;
    if (seclib_daa_enabled())
        tgt_cfg |= TGT_CFG_DAA_EN;

    usbdl_put_dword(tgt_cfg);
    usbdl_put_word(status);
}

static bool usbdl_check_start_command(struct bldr_comport *comport, u32 tmo)
{
    u8 startcmd[] = {0xa0, 0x0a, 0x50, 0x05};
    ulong start = get_timer(0);
    u32 i = 0;
    u8 cmd = 0, rsp;
    struct comport_ops *comm = comport->ops;

    do {
        if (get_timer(start) > tmo)
            return FALSE;

        /* timeout 1 ms */
        if (0 != comm->recv(&cmd, 1, 1)) {
            continue;
        }

        if (cmd == startcmd[i]) {
            rsp = ~cmd;
            i++;
        } else {
            rsp = cmd + 1;
            i = 0;
        }
        DBGMSG("%s TGT<-(0x%x)--TOOL\n", MOD, cmd);
        DBGMSG("%s TGT--(0x%x)->TOOL\n", MOD, rsp);
        comm->send(&rsp, 1);
    } while(i < sizeof(startcmd));

    return TRUE;
}

static void usbdl_i2c_write8(void)
{
    u32 addr, len, idx;
    U16 status = B_OK;

    usbdl_get_dword(&addr);
    usbdl_put_dword(addr);
    usbdl_get_dword(&len);
    usbdl_put_dword(len);
    if (BROM_I2C_CMD_LEN <= len)
    {
        status = E_I2C_CMD_BUF_OVERFLOW;
        goto __error;        
    }
    usbdl_put_word(status);
    for (idx = 0; idx < len; idx++)
        usbdl_get_byte(&g_I2C_CmdBuf[idx]);
    status = i2c_v1_write(addr, g_I2C_CmdBuf, len);    

__error:
    usbdl_put_word(status);    
}

static void usbdl_i2c_read8(void)
{
    u32 addr, len, idx;
    U16 status = B_OK;

    usbdl_get_dword(&addr);
    usbdl_put_dword(addr);
    usbdl_get_dword(&len);
    usbdl_put_dword(len);
    if (BROM_I2C_CMD_LEN <= len) 
    {
        status = E_I2C_DATA_BUF_OVERFLOW;
        goto __error;
    }
    usbdl_put_word(status);
    status = i2c_v1_read(addr, g_I2C_DataBuf, len);
    if (B_OK != status)
        goto __error;
    usbdl_put_word(status);
    for(idx = 0; idx < len; idx++)
        usbdl_put_byte(g_I2C_DataBuf[idx]);
    return;    
__error:
    usbdl_put_word(status);
}

static void usbdl_i2c_set_speed(void)
{
    U16 status = B_OK;
    U32 mode, khz;

    usbdl_get_dword(&mode);
    usbdl_put_dword(mode);
    usbdl_get_dword(&khz);
    usbdl_put_dword(khz);
    if ((ST_MODE != mode) && (FS_MODE != mode) && (HS_MODE != mode))
    {
        status = E_I2C_SPEED_MODE_INVALID;
        goto __error;
    }
    usbdl_put_word(status);
    status = i2c_v1_set_speed(I2C_CLK_RATE, mode, khz);
__error:
    usbdl_put_word(status);
}

int usbdl_handler(struct bldr_comport *comport, u32 hshk_tmo_ms)
{
    u8 cmd;

    if (usbdl_check_start_command(comport, hshk_tmo_ms) == FALSE) {
        printf("%s start cmd handshake timeout (%dms)\n", MOD, hshk_tmo_ms);
        return -1;
    }
    else
    {
        sec_set_usb_handshake_status(TRUE);
    }

    dlcomport = comport;

    while (1) {
        platform_wdt_kick();

        usbdl_get_byte(&cmd);
        if (cmd != CMD_GET_BL_VER)
            usbdl_put_byte(cmd);    /* echo cmd */

        switch (cmd) {
        case CMD_GET_BL_VER:
            DBGMSG("%s CMD_GET_BL_VER\n", MOD);
            usbdl_put_byte(1);      /* echo bloader version */
            break;
        case CMD_GET_HW_SW_VER:
            DBGMSG("%s CMD_GET_HW_SW_VER\n", MOD);
            usbdl_get_hw_sw_ver();
            break;
        case CMD_GET_HW_CODE:
            DBGMSG("%s CMD_GET_HW_CODE\n", MOD);
            usbdl_get_hw_code();
            break;
        case CMD_SEND_DA:
            DBGMSG("%s CMD_SEND_DA\n", MOD);
            usbdl_send_da();
            break;
        case CMD_JUMP_DA:
            DBGMSG("%s CMD_JUMP_DA\n", MOD);
            usbdl_jump_da();
            break;
        case CMD_GET_TARGET_CONFIG:
            DBGMSG("%s CMD_GET_TARGET_CONFIG\n", MOD);
            usbdl_get_tgt_config();
            break;
        case CMD_LEGACY_READ:
            DBGMSG("%s CMD_LEGACY_READ\n", MOD);
            usbdl_read16(TRUE);
            break;
        case CMD_READ16:
            DBGMSG("%s CMD_READ16\n", MOD);
            usbdl_read16(FALSE);
            break;
        case CMD_LEGACY_WRITE:
            DBGMSG("%s CMD_LEGACY_WRITE\n", MOD);
            usbdl_write16(TRUE);
            break;
        case CMD_WRITE16:
            DBGMSG("%s CMD_WRITE16\n", MOD);
            usbdl_write16(TRUE);
            break;
        case CMD_WRITE16_NO_ECHO:
            DBGMSG("%s CMD_WRITE16_NO_ECHO\n", MOD);
            usbdl_write16(FALSE);
            break;
        case CMD_READ32:
            DBGMSG("%s CMD_READ32\n", MOD);
            usbdl_read32(FALSE);
            break;
        case CMD_WRITE32:
            DBGMSG("%s CMD_WRITE32\n", MOD);
            usbdl_write32(TRUE);
            break;
    #if CFG_I2C_SUPPORT
        case CMD_I2C_INIT:
            DBGMSG("%s CMD_I2C_INIT\n", MOD);
            usbdl_put_word(0);   //status = S_DONE
            break;
        case CMD_I2C_DEINIT:
            DBGMSG("%s CMD_I2C_DEINIT\n", MOD);
            usbdl_put_word(0);   //status = S_DONE
            break;
        case CMD_I2C_WRITE8:
            DBGMSG("%s CMD_I2C_WRITE8\n", MOD);
            usbdl_i2c_write8();
            break;
        case CMD_I2C_READ8:   
            DBGMSG("%s CMD_I2C_READ8\n", MOD);
            usbdl_i2c_read8();
            break;
        case CMD_I2C_SET_SPEED:
            DBGMSG("%s CMD_I2C_SET_SPEED\n", MOD);
            usbdl_i2c_set_speed();
            break;
    #endif            
        default:
            DBGMSG("%s Unhandled CMD 0x%x\n", MOD, cmd);
            break;
        }
        DBGMSG("\n", MOD);
    }
    return 0;
}

#endif /* CFG_USB_DOWNLOAD && !CFG_LEGACY_USB_DOWNLOAD */
