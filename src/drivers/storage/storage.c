#include <stddef.h>
#include <stdint.h>
#include <meow/io.h>
#include <meow/storage.h>
#include <meow/string.h>
#include <meow/vfs.h>

#define IDE_PRIMARY_BASE 0x1F0
#define IDE_PRIMARY_CTRL 0x3F6
#define IDE_SECONDARY_BASE 0x170
#define IDE_SECONDARY_CTRL 0x376

#define ATA_REG_DATA 0x00
#define ATA_REG_FEATURES 0x01
#define ATA_REG_SECCOUNT0 0x02
#define ATA_REG_LBA0 0x03
#define ATA_REG_LBA1 0x04
#define ATA_REG_LBA2 0x05
#define ATA_REG_HDDEVSEL 0x06
#define ATA_REG_COMMAND 0x07
#define ATA_REG_STATUS 0x07

#define ATA_REG_ALTSTATUS 0x02
#define ATA_REG_CONTROL 0x02

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_PACKET 0xA0

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_DF 0x20
#define ATA_SR_BSY 0x80

#define IDE_MAX_DEVICES 4
#define AHCI_MAX_DEVICES 8

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define AHCI_DEV_NULL 0
#define AHCI_DEV_SATA 1

#define HBA_PORT_IPM_ACTIVE 1
#define HBA_PORT_DET_PRESENT 3

#define HBA_PxCMD_ST (1u << 0)
#define HBA_PxCMD_FRE (1u << 4)
#define HBA_PxCMD_FR (1u << 14)
#define HBA_PxCMD_CR (1u << 15)

#define HBA_PxIS_TFES (1u << 30)

#define ATA_CMD_IDENTIFY_DEVICE 0xEC
#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA_EX 0x35

typedef struct IDE_Channel
{
    uint16_t base;
    uint16_t ctrl;
} IDE_Channel;

typedef struct IDE_Device
{
    uint8_t present;
    uint8_t is_atapi;
    uint8_t channel;
    uint8_t drive;
    uint32_t sector_size;
    uint32_t sector_count;
    char name[16];
} IDE_Device;

typedef volatile struct HBA_PORT
{
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} HBA_PORT;

typedef volatile struct HBA_MEM
{
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;

    uint8_t rsv[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    HBA_PORT ports[32];
} HBA_MEM;

typedef struct HBA_CMD_HEADER
{
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} HBA_CMD_HEADER;

typedef struct HBA_PRDT_ENTRY
{
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc_i;
} HBA_PRDT_ENTRY;

typedef struct HBA_CMD_TBL
{
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    HBA_PRDT_ENTRY prdt[1];
} HBA_CMD_TBL;

typedef struct FIS_REG_H2D
{
    uint8_t fis_type;
    uint8_t pmport_c;
    uint8_t command;
    uint8_t featurel;

    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;

    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;

    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;

    uint8_t rsv1[4];
} __attribute__((packed)) FIS_REG_H2D;

typedef struct AHCI_Device
{
    uint8_t present;
    uint8_t index;
    uint8_t port_no;
    uint32_t sector_count;
    HBA_PORT* port;
    char name[16];
} AHCI_Device;

static IDE_Channel g_ide_channels[2] = {
    { IDE_PRIMARY_BASE, IDE_PRIMARY_CTRL },
    { IDE_SECONDARY_BASE, IDE_SECONDARY_CTRL }
};

static IDE_Device g_ide_devices[IDE_MAX_DEVICES];
static AHCI_Device g_ahci_devices[AHCI_MAX_DEVICES];
static uint8_t g_ahci_device_count;

static uint8_t g_ahci_clb[AHCI_MAX_DEVICES][1024] __attribute__((aligned(1024)));
static uint8_t g_ahci_fb[AHCI_MAX_DEVICES][256] __attribute__((aligned(256)));
static uint8_t g_ahci_ctba[AHCI_MAX_DEVICES][256] __attribute__((aligned(128)));
static uint16_t g_ahci_identify[AHCI_MAX_DEVICES][256];
static uint8_t g_storage_scratch[512] __attribute__((aligned(512)));

static uint32_t wr32le(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
    return v;
}

static uint16_t wr16le(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    return v;
}

static void append_decimal(char out[16], size_t* io_index, uint32_t value)
{
    char tmp[10];
    size_t n = 0;

    if (value == 0)
    {
        if (*io_index < 15)
        {
            out[*io_index] = '0';
            *io_index += 1;
        }
        return;
    }

    while (value > 0 && n < sizeof(tmp))
    {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (n > 0 && *io_index < 15)
    {
        out[*io_index] = tmp[n - 1];
        *io_index += 1;
        n--;
    }
}

static void make_name(const char* prefix, uint32_t index, char out[16])
{
    size_t i = 0;

    while (prefix[i] && i < 15)
    {
        out[i] = prefix[i];
        i++;
    }

    append_decimal(out, &i, index);
    out[i] = '\0';
}

static void ide_delay_400ns(const IDE_Channel* channel)
{
    (void)inb(channel->ctrl + ATA_REG_ALTSTATUS);
    (void)inb(channel->ctrl + ATA_REG_ALTSTATUS);
    (void)inb(channel->ctrl + ATA_REG_ALTSTATUS);
    (void)inb(channel->ctrl + ATA_REG_ALTSTATUS);
}

static int ide_wait(const IDE_Channel* channel, int require_drq)
{
    for (uint32_t t = 0; t < 1000000u; t++)
    {
        uint8_t status = inb(channel->base + ATA_REG_STATUS);

        if ((status & ATA_SR_BSY) != 0)
        {
            continue;
        }

        if ((status & ATA_SR_ERR) != 0)
        {
            return -1;
        }

        if (!require_drq)
        {
            return 0;
        }

        if ((status & ATA_SR_DRQ) != 0)
        {
            return 0;
        }
    }

    return -1;
}

static int ide_wait_bsy_clear(const IDE_Channel* channel)
{
    for (uint32_t t = 0; t < 1000000u; t++)
    {
        uint8_t status = inb(channel->base + ATA_REG_STATUS);
        if ((status & ATA_SR_BSY) == 0)
        {
            if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0)
            {
                return -1;
            }
            return 0;
        }
    }

    return -1;
}

static int atapi_send_packet(const IDE_Channel* channel, uint8_t drive, const uint8_t packet[12], void* out, uint32_t out_bytes)
{
    outb(channel->base + ATA_REG_HDDEVSEL, (uint8_t)(0xA0 | (drive << 4)));
    io_wait();

    outb(channel->base + ATA_REG_FEATURES, 0);
    outb(channel->base + ATA_REG_LBA1, (uint8_t)(out_bytes & 0xFFu));
    outb(channel->base + ATA_REG_LBA2, (uint8_t)((out_bytes >> 8) & 0xFFu));
    outb(channel->base + ATA_REG_COMMAND, ATA_CMD_PACKET);

    if (ide_wait(channel, 1) != 0)
    {
        return -1;
    }

    const uint16_t* words = (const uint16_t*)packet;
    for (size_t i = 0; i < 6; i++)
    {
        outw(channel->base + ATA_REG_DATA, words[i]);
    }

    if (ide_wait(channel, 1) != 0)
    {
        return -1;
    }

    uint16_t* outwbuf = (uint16_t*)out;
    for (uint32_t i = 0; i < (out_bytes / 2u); i++)
    {
        outwbuf[i] = inw(channel->base + ATA_REG_DATA);
    }

    ide_delay_400ns(channel);
    return ide_wait_bsy_clear(channel);
}

static uint32_t rd32be(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static int atapi_identify(uint8_t channel_index, uint8_t drive, uint32_t* out_sectors)
{
    IDE_Channel* channel = &g_ide_channels[channel_index];
    uint16_t identify[256];
    uint8_t cap_pkt[12];
    uint8_t cap[8];

    outb(channel->base + ATA_REG_HDDEVSEL, (uint8_t)(0xA0 | (drive << 4)));
    io_wait();
    outb(channel->base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);

    if (ide_wait(channel, 1) != 0)
    {
        return -1;
    }

    for (size_t i = 0; i < 256; i++)
    {
        identify[i] = inw(channel->base + ATA_REG_DATA);
    }

    (void)identify;

    memset(cap_pkt, 0, sizeof(cap_pkt));
    cap_pkt[0] = 0x25;
    memset(cap, 0, sizeof(cap));

    if (atapi_send_packet(channel, drive, cap_pkt, cap, sizeof(cap)) != 0)
    {
        return -1;
    }

    *out_sectors = rd32be(&cap[0]) + 1u;
    return 0;
}

static int atapi_read_sectors(IDE_Device* dev, uint32_t lba, uint32_t count, void* buffer)
{
    IDE_Channel* channel;
    uint8_t* out = (uint8_t*)buffer;

    if (!dev || !buffer || count == 0)
    {
        return -1;
    }

    channel = &g_ide_channels[dev->channel];

    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t pkt[12];
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 0xA8;
        pkt[2] = (uint8_t)(((lba + i) >> 24) & 0xFFu);
        pkt[3] = (uint8_t)(((lba + i) >> 16) & 0xFFu);
        pkt[4] = (uint8_t)(((lba + i) >> 8) & 0xFFu);
        pkt[5] = (uint8_t)((lba + i) & 0xFFu);
        pkt[9] = 1;

        if (atapi_send_packet(channel, dev->drive, pkt, out + ((size_t)i * 2048u), 2048u) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static int ide_identify(uint8_t channel_index, uint8_t drive, uint32_t* out_sectors)
{
    IDE_Channel* channel = &g_ide_channels[channel_index];
    uint16_t identify[256];

    outb(channel->ctrl + ATA_REG_CONTROL, 0x02);
    outb(channel->base + ATA_REG_HDDEVSEL, (uint8_t)(0xA0 | (drive << 4)));
    io_wait();

    outb(channel->base + ATA_REG_SECCOUNT0, 0);
    outb(channel->base + ATA_REG_LBA0, 0);
    outb(channel->base + ATA_REG_LBA1, 0);
    outb(channel->base + ATA_REG_LBA2, 0);
    outb(channel->base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(channel->base + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF)
    {
        return -1;
    }

    while ((status = inb(channel->base + ATA_REG_STATUS)) & ATA_SR_BSY)
    {
    }

    if (inb(channel->base + ATA_REG_LBA1) != 0 || inb(channel->base + ATA_REG_LBA2) != 0)
    {
        return -1;
    }

    if (ide_wait(channel, 1) != 0)
    {
        return -1;
    }

    for (size_t i = 0; i < 256; i++)
    {
        identify[i] = inw(channel->base + ATA_REG_DATA);
    }

    uint32_t sectors28 = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);
    uint64_t sectors48 = ((uint64_t)identify[100]) |
                         ((uint64_t)identify[101] << 16) |
                         ((uint64_t)identify[102] << 32) |
                         ((uint64_t)identify[103] << 48);

    if ((identify[83] & (1u << 10)) != 0 && sectors48 != 0)
    {
        *out_sectors = sectors48 > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)sectors48;
    }
    else
    {
        *out_sectors = sectors28;
    }

    return 0;
}

static int ide_read(void* user, uint32_t lba, uint32_t count, void* buffer)
{
    IDE_Device* dev = (IDE_Device*)user;
    IDE_Channel* channel;
    uint8_t* out = (uint8_t*)buffer;
    uint32_t done = 0;

    if (!dev || !dev->present || !buffer || count == 0)
    {
        return -1;
    }

    if (dev->is_atapi)
    {
        return atapi_read_sectors(dev, lba, count, buffer);
    }

    if (dev->sector_count != 0 && (lba + count > dev->sector_count))
    {
        return -1;
    }

    channel = &g_ide_channels[dev->channel];

    while (done < count)
    {
        uint32_t chunk = count - done;
        if (chunk > 255u)
        {
            chunk = 255u;
        }

        if ((lba + done) > 0x0FFFFFFFu)
        {
            return -1;
        }

        outb(channel->base + ATA_REG_HDDEVSEL,
             (uint8_t)(0xE0 | (dev->drive << 4) | (((lba + done) >> 24) & 0x0F)));
        outb(channel->base + ATA_REG_SECCOUNT0, (uint8_t)chunk);
        outb(channel->base + ATA_REG_LBA0, (uint8_t)((lba + done) & 0xFF));
        outb(channel->base + ATA_REG_LBA1, (uint8_t)(((lba + done) >> 8) & 0xFF));
        outb(channel->base + ATA_REG_LBA2, (uint8_t)(((lba + done) >> 16) & 0xFF));
        outb(channel->base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

        for (uint32_t s = 0; s < chunk; s++)
        {
            if (ide_wait(channel, 1) != 0)
            {
                return -1;
            }

            uint16_t* dst = (uint16_t*)(out + ((done + s) * 512u));
            for (size_t i = 0; i < 256; i++)
            {
                dst[i] = inw(channel->base + ATA_REG_DATA);
            }
        }

        ide_delay_400ns(channel);
        done += chunk;
    }

    return 0;
}

static int ide_write(void* user, uint32_t lba, uint32_t count, const void* buffer)
{
    IDE_Device* dev = (IDE_Device*)user;
    IDE_Channel* channel;
    const uint8_t* in = (const uint8_t*)buffer;
    uint32_t done = 0;

    if (!dev || !dev->present || !buffer || count == 0)
    {
        return -1;
    }

    if (dev->is_atapi)
    {
        return -1;
    }

    if (dev->sector_count != 0 && (lba + count > dev->sector_count))
    {
        return -1;
    }

    channel = &g_ide_channels[dev->channel];

    while (done < count)
    {
        uint32_t chunk = count - done;
        if (chunk > 255u)
        {
            chunk = 255u;
        }

        outb(channel->base + ATA_REG_HDDEVSEL,
             (uint8_t)(0xE0 | (dev->drive << 4) | (((lba + done) >> 24) & 0x0F)));
        outb(channel->base + ATA_REG_SECCOUNT0, (uint8_t)chunk);
        outb(channel->base + ATA_REG_LBA0, (uint8_t)((lba + done) & 0xFF));
        outb(channel->base + ATA_REG_LBA1, (uint8_t)(((lba + done) >> 8) & 0xFF));
        outb(channel->base + ATA_REG_LBA2, (uint8_t)(((lba + done) >> 16) & 0xFF));
        outb(channel->base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

        for (uint32_t s = 0; s < chunk; s++)
        {
            if (ide_wait(channel, 1) != 0)
            {
                return -1;
            }

            const uint16_t* src = (const uint16_t*)(in + ((done + s) * 512u));
            for (size_t i = 0; i < 256; i++)
            {
                outw(channel->base + ATA_REG_DATA, src[i]);
            }
        }

        done += chunk;
    }

    return 0;
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = (1u << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFCu);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t address = (1u << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFCu);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t v = pci_read32(bus, slot, func, (uint8_t)(offset & 0xFCu));
    uint8_t shift = (uint8_t)((offset & 2u) * 8u);
    return (uint16_t)((v >> shift) & 0xFFFFu);
}

static uint8_t ahci_check_type(HBA_PORT* port)
{
    uint32_t ssts = port->ssts;
    uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0F);
    uint8_t det = (uint8_t)(ssts & 0x0F);

    if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE)
    {
        return AHCI_DEV_NULL;
    }

    if (port->sig == 0x00000101)
    {
        return AHCI_DEV_SATA;
    }

    return AHCI_DEV_NULL;
}

static void ahci_stop_cmd(HBA_PORT* port)
{
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    for (uint32_t t = 0; t < 1000000u; t++)
    {
        if ((port->cmd & (HBA_PxCMD_FR | HBA_PxCMD_CR)) == 0)
        {
            break;
        }
    }
}

static void ahci_start_cmd(HBA_PORT* port)
{
    for (uint32_t t = 0; t < 1000000u; t++)
    {
        if ((port->cmd & HBA_PxCMD_CR) == 0)
        {
            break;
        }
    }

    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static int ahci_wait_ready(HBA_PORT* port)
{
    for (uint32_t t = 0; t < 1000000u; t++)
    {
        if ((port->tfd & (ATA_SR_BSY | ATA_SR_DRQ)) == 0)
        {
            return 0;
        }
    }
    return -1;
}

static int ahci_prepare_port(AHCI_Device* dev)
{
    HBA_PORT* port = dev->port;
    HBA_CMD_HEADER* cmd_header;

    ahci_stop_cmd(port);

    memset(g_ahci_clb[dev->index], 0, sizeof(g_ahci_clb[dev->index]));
    memset(g_ahci_fb[dev->index], 0, sizeof(g_ahci_fb[dev->index]));
    memset(g_ahci_ctba[dev->index], 0, sizeof(g_ahci_ctba[dev->index]));

    port->clb = (uint32_t)(uintptr_t)g_ahci_clb[dev->index];
    port->clbu = 0;
    port->fb = (uint32_t)(uintptr_t)g_ahci_fb[dev->index];
    port->fbu = 0;
    port->is = 0xFFFFFFFFu;
    port->ie = 0;

    cmd_header = (HBA_CMD_HEADER*)(uintptr_t)port->clb;
    cmd_header[0].flags = 0;
    cmd_header[0].prdtl = 1;
    cmd_header[0].prdbc = 0;
    cmd_header[0].ctba = (uint32_t)(uintptr_t)g_ahci_ctba[dev->index];
    cmd_header[0].ctbau = 0;

    port->serr = 0xFFFFFFFFu;
    ahci_start_cmd(port);
    return 0;
}

static int ahci_exec(AHCI_Device* dev, uint8_t command, uint64_t lba, uint16_t sector_count, void* buffer, int is_write)
{
    HBA_PORT* port = dev->port;
    HBA_CMD_HEADER* cmd_header = (HBA_CMD_HEADER*)(uintptr_t)port->clb;
    HBA_CMD_TBL* cmd_tbl = (HBA_CMD_TBL*)(uintptr_t)cmd_header[0].ctba;
    FIS_REG_H2D* fis;
    uint32_t bytes;

    if (sector_count == 0 || !buffer)
    {
        return -1;
    }

    if (sector_count > 8192u)
    {
        return -1;
    }

    if (ahci_wait_ready(port) != 0)
    {
        return -1;
    }

    bytes = (uint32_t)sector_count * 512u;

    memset(cmd_tbl, 0, sizeof(HBA_CMD_TBL));
    cmd_header[0].flags = (uint16_t)((sizeof(FIS_REG_H2D) / 4u) & 0x1Fu);
    if (is_write)
    {
        cmd_header[0].flags |= (1u << 6);
    }
    cmd_header[0].prdtl = 1;
    cmd_header[0].prdbc = 0;

    cmd_tbl->prdt[0].dba = (uint32_t)(uintptr_t)buffer;
    cmd_tbl->prdt[0].dbau = 0;
    cmd_tbl->prdt[0].rsv0 = 0;
    cmd_tbl->prdt[0].dbc_i = ((bytes - 1u) & 0x3FFFFFu) | (1u << 31);

    fis = (FIS_REG_H2D*)(&cmd_tbl->cfis[0]);
    fis->fis_type = 0x27;
    fis->pmport_c = 1u << 7;
    fis->command = command;
    fis->featurel = 0;
    fis->lba0 = (uint8_t)(lba & 0xFFu);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFFu);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFFu);
    fis->device = 1u << 6;
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFFu);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFFu);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFFu);
    fis->featureh = 0;
    fis->countl = (uint8_t)(sector_count & 0xFFu);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xFFu);
    fis->icc = 0;
    fis->control = 0;

    port->is = 0xFFFFFFFFu;
    port->ci = 1u;

    for (uint32_t t = 0; t < 1000000u; t++)
    {
        if ((port->ci & 1u) == 0)
        {
            break;
        }

        if ((port->is & HBA_PxIS_TFES) != 0)
        {
            return -1;
        }
    }

    if ((port->ci & 1u) != 0)
    {
        return -1;
    }

    if ((port->is & HBA_PxIS_TFES) != 0)
    {
        return -1;
    }

    return 0;
}

static int ahci_identify(AHCI_Device* dev, uint32_t* out_sectors)
{
    uint16_t* identify = g_ahci_identify[dev->index];
    uint64_t sectors48;
    uint32_t sectors28;

    memset(identify, 0, 512);

    if (ahci_exec(dev, ATA_CMD_IDENTIFY_DEVICE, 0, 1, identify, 0) != 0)
    {
        return -1;
    }

    sectors28 = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);
    sectors48 = ((uint64_t)identify[100]) |
                ((uint64_t)identify[101] << 16) |
                ((uint64_t)identify[102] << 32) |
                ((uint64_t)identify[103] << 48);

    if (sectors48 != 0)
    {
        *out_sectors = sectors48 > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)sectors48;
    }
    else
    {
        *out_sectors = sectors28;
    }

    return 0;
}

static int ahci_read(void* user, uint32_t lba, uint32_t count, void* buffer)
{
    AHCI_Device* dev = (AHCI_Device*)user;
    uint32_t done = 0;

    if (!dev || !dev->present || !buffer || count == 0)
    {
        return -1;
    }

    if (dev->sector_count != 0 && (lba + count > dev->sector_count))
    {
        return -1;
    }

    while (done < count)
    {
        uint16_t chunk = (uint16_t)(count - done);
        if (chunk > 128u)
        {
            chunk = 128u;
        }

        if (ahci_exec(dev,
                      ATA_CMD_READ_DMA_EX,
                      (uint64_t)lba + done,
                      chunk,
                      (uint8_t*)buffer + ((size_t)done * 512u),
                      0) != 0)
        {
            return -1;
        }

        done += chunk;
    }

    return 0;
}

static int ahci_write_dev(AHCI_Device* dev, uint32_t lba, uint32_t count, const void* buffer)
{
    uint32_t done = 0;

    if (!dev || !dev->present || !buffer || count == 0)
    {
        return -1;
    }

    if (dev->sector_count != 0 && (lba + count > dev->sector_count))
    {
        return -1;
    }

    while (done < count)
    {
        uint16_t chunk = (uint16_t)(count - done);
        if (chunk > 128u)
        {
            chunk = 128u;
        }

        if (ahci_exec(dev,
                      ATA_CMD_WRITE_DMA_EX,
                      (uint64_t)lba + done,
                      chunk,
                      (void*)((const uint8_t*)buffer + ((size_t)done * 512u)),
                      1) != 0)
        {
            return -1;
        }

        done += chunk;
    }

    return 0;
}

static int ahci_write(void* user, uint32_t lba, uint32_t count, const void* buffer)
{
    return ahci_write_dev((AHCI_Device*)user, lba, count, buffer);
}

static int find_ahci_device(const char* name, AHCI_Device** out_dev)
{
    if (!name || !out_dev)
    {
        return -1;
    }

    for (size_t i = 0; i < AHCI_MAX_DEVICES; i++)
    {
        if (!g_ahci_devices[i].present)
        {
            continue;
        }
        if (strcmp(g_ahci_devices[i].name, name) == 0)
        {
            *out_dev = &g_ahci_devices[i];
            return 0;
        }
    }

    return -1;
}

static uint8_t pick_sectors_per_cluster(uint32_t total_sectors)
{
    if (total_sectors < 66600u)
    {
        return 1;
    }
    if (total_sectors < 532480u)
    {
        return 8;
    }
    if (total_sectors < 16777216u)
    {
        return 16;
    }
    return 32;
}

int storage_format_fat32(const char* device_name)
{
    AHCI_Device* dev;
    uint32_t total;
    uint32_t reserved = 32;
    uint32_t fats = 2;
    uint8_t spc;
    uint32_t spf = 1;
    uint32_t root_cluster = 2;
    uint32_t data_start;

    if (find_ahci_device(device_name, &dev) != 0)
    {
        return -1;
    }

    total = dev->sector_count;
    if (total < 65536u)
    {
        return -1;
    }

    spc = pick_sectors_per_cluster(total);

    for (size_t i = 0; i < 16; i++)
    {
        uint32_t data = total - reserved - (fats * spf);
        uint32_t clusters = data / spc;
        uint32_t next_spf = ((clusters + 2u) * 4u + 511u) / 512u;
        if (next_spf == spf)
        {
            break;
        }
        spf = next_spf;
    }

    data_start = reserved + fats * spf;
    if (data_start + spc >= total)
    {
        return -1;
    }

    memset(g_storage_scratch, 0, sizeof(g_storage_scratch));

    for (uint32_t s = 0; s < reserved; s++)
    {
        if (ahci_write_dev(dev, s, 1, g_storage_scratch) != 0)
        {
            return -1;
        }
    }

    memset(g_storage_scratch, 0, sizeof(g_storage_scratch));
    g_storage_scratch[0] = 0xEB;
    g_storage_scratch[1] = 0x58;
    g_storage_scratch[2] = 0x90;
    memcpy(&g_storage_scratch[3], "MEOWOS  ", 8);
    wr16le(&g_storage_scratch[11], 512);
    g_storage_scratch[13] = spc;
    wr16le(&g_storage_scratch[14], (uint16_t)reserved);
    g_storage_scratch[16] = (uint8_t)fats;
    wr16le(&g_storage_scratch[17], 0);
    wr16le(&g_storage_scratch[19], 0);
    g_storage_scratch[21] = 0xF8;
    wr16le(&g_storage_scratch[22], 0);
    wr16le(&g_storage_scratch[24], 63);
    wr16le(&g_storage_scratch[26], 255);
    wr32le(&g_storage_scratch[28], 0);
    wr32le(&g_storage_scratch[32], total);
    wr32le(&g_storage_scratch[36], spf);
    wr16le(&g_storage_scratch[40], 0);
    wr16le(&g_storage_scratch[42], 0);
    wr32le(&g_storage_scratch[44], root_cluster);
    wr16le(&g_storage_scratch[48], 1);
    wr16le(&g_storage_scratch[50], 6);
    g_storage_scratch[64] = 0x80;
    g_storage_scratch[66] = 0x29;
    wr32le(&g_storage_scratch[67], 0x4D45574Fu);
    memcpy(&g_storage_scratch[71], "MEOWOSDISK ", 11);
    memcpy(&g_storage_scratch[82], "FAT32   ", 8);
    g_storage_scratch[510] = 0x55;
    g_storage_scratch[511] = 0xAA;

    if (ahci_write_dev(dev, 0, 1, g_storage_scratch) != 0)
    {
        return -1;
    }
    if (ahci_write_dev(dev, 6, 1, g_storage_scratch) != 0)
    {
        return -1;
    }

    memset(g_storage_scratch, 0, sizeof(g_storage_scratch));
    wr32le(&g_storage_scratch[0], 0x41615252u);
    wr32le(&g_storage_scratch[484], 0x61417272u);
    wr32le(&g_storage_scratch[488], 0xFFFFFFFFu);
    wr32le(&g_storage_scratch[492], 3u);
    g_storage_scratch[510] = 0x55;
    g_storage_scratch[511] = 0xAA;

    if (ahci_write_dev(dev, 1, 1, g_storage_scratch) != 0)
    {
        return -1;
    }
    if (ahci_write_dev(dev, 7, 1, g_storage_scratch) != 0)
    {
        return -1;
    }

    memset(g_storage_scratch, 0, sizeof(g_storage_scratch));
    wr32le(&g_storage_scratch[0], 0x0FFFFFF8u);
    wr32le(&g_storage_scratch[4], 0xFFFFFFFFu);
    wr32le(&g_storage_scratch[8], 0x0FFFFFFFu);

    for (uint32_t f = 0; f < fats; f++)
    {
        uint32_t fat_lba = reserved + (f * spf);
        if (ahci_write_dev(dev, fat_lba, 1, g_storage_scratch) != 0)
        {
            return -1;
        }

        memset(g_storage_scratch, 0, sizeof(g_storage_scratch));
        for (uint32_t s = 1; s < spf; s++)
        {
            if (ahci_write_dev(dev, fat_lba + s, 1, g_storage_scratch) != 0)
            {
                return -1;
            }
        }
    }

    memset(g_storage_scratch, 0, sizeof(g_storage_scratch));
    for (uint32_t s = 0; s < spc; s++)
    {
        if (ahci_write_dev(dev, data_start + s, 1, g_storage_scratch) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static void ide_init_devices(void)
{
    uint32_t ide_index = 0;
    uint32_t cd_index = 0;

    memset(g_ide_devices, 0, sizeof(g_ide_devices));

    for (uint8_t ch = 0; ch < 2; ch++)
    {
        for (uint8_t drv = 0; drv < 2; drv++)
        {
            uint32_t sectors = 0;
            IDE_Device* dev;
            uint8_t is_atapi = 0;

            if (ide_identify(ch, drv, &sectors) != 0)
            {
                if (atapi_identify(ch, drv, &sectors) != 0)
                {
                    continue;
                }
                is_atapi = 1;
            }

            dev = &g_ide_devices[(ch * 2u) + drv];
            dev->present = 1;
            dev->is_atapi = is_atapi;
            dev->channel = ch;
            dev->drive = drv;
            dev->sector_size = is_atapi ? 2048u : 512u;
            dev->sector_count = sectors;

            if (is_atapi)
            {
                make_name("cd", cd_index, dev->name);
            }
            else
            {
                make_name("ide", ide_index, dev->name);
            }

            if (vfs_register_block_device(dev->name,
                                          dev->sector_size,
                                          dev->sector_count,
                                          ide_read,
                                          is_atapi ? 0 : ide_write,
                                          dev) == 0)
            {
                if (is_atapi)
                {
                    printf("[storage] ATAPI %s: channel=%u drive=%u sectors=%u\n",
                           dev->name,
                           ch,
                           drv,
                           dev->sector_count);
                }
                else
                {
                    printf("[storage] IDE %s: channel=%u drive=%u sectors=%u\n",
                           dev->name,
                           ch,
                           drv,
                           dev->sector_count);
                }
            }

            if (is_atapi)
            {
                cd_index++;
            }
            else
            {
                ide_index++;
            }
        }
    }
}

static void ahci_probe_controller(uint32_t abar_phys)
{
    HBA_MEM* hba;
    uint32_t pi;

    if (abar_phys == 0 || g_ahci_device_count >= AHCI_MAX_DEVICES)
    {
        return;
    }

    hba = (HBA_MEM*)(uintptr_t)(abar_phys & ~0x0Fu);
    hba->ghc |= (1u << 31);

    pi = hba->pi;
    for (uint8_t port = 0; port < 32 && g_ahci_device_count < AHCI_MAX_DEVICES; port++)
    {
        AHCI_Device* dev;
        uint32_t sectors = 0;

        if ((pi & (1u << port)) == 0)
        {
            continue;
        }

        if (ahci_check_type(&hba->ports[port]) != AHCI_DEV_SATA)
        {
            continue;
        }

        dev = &g_ahci_devices[g_ahci_device_count];
        memset(dev, 0, sizeof(*dev));
        dev->present = 1;
        dev->index = g_ahci_device_count;
        dev->port_no = port;
        dev->port = &hba->ports[port];
        make_name("ahci", g_ahci_device_count, dev->name);

        if (ahci_prepare_port(dev) != 0)
        {
            dev->present = 0;
            continue;
        }

        if (ahci_identify(dev, &sectors) != 0)
        {
            dev->present = 0;
            continue;
        }

        dev->sector_count = sectors;

        if (vfs_register_block_device(dev->name, 512, dev->sector_count, ahci_read, ahci_write, dev) == 0)
        {
            printf("[storage] AHCI %s: port=%u sectors=%u\n",
                   dev->name,
                   port,
                   dev->sector_count);
            g_ahci_device_count++;
        }
        else
        {
            dev->present = 0;
        }
    }
}

static void ahci_init_devices(void)
{
    memset(g_ahci_devices, 0, sizeof(g_ahci_devices));
    g_ahci_device_count = 0;

    for (uint16_t bus = 0; bus < 256; bus++)
    {
        for (uint8_t slot = 0; slot < 32; slot++)
        {
            for (uint8_t func = 0; func < 8; func++)
            {
                uint32_t id = pci_read32((uint8_t)bus, slot, func, 0x00);
                uint16_t vendor = (uint16_t)(id & 0xFFFFu);
                uint32_t class_reg;
                uint8_t class_code;
                uint8_t subclass;
                uint8_t prog_if;

                if (vendor == 0xFFFFu)
                {
                    if (func == 0)
                    {
                        break;
                    }
                    continue;
                }

                class_reg = pci_read32((uint8_t)bus, slot, func, 0x08);
                class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
                subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
                prog_if = (uint8_t)((class_reg >> 8) & 0xFFu);

                if (class_code == 0x01 && subclass == 0x06 && prog_if == 0x01)
                {
                    uint16_t cmd = pci_read16((uint8_t)bus, slot, func, 0x04);
                    uint32_t bar5;

                    cmd |= (1u << 1);
                    cmd |= (1u << 2);
                    pci_write32((uint8_t)bus, slot, func, 0x04,
                                ((uint32_t)pci_read16((uint8_t)bus, slot, func, 0x06) << 16) | cmd);

                    bar5 = pci_read32((uint8_t)bus, slot, func, 0x24);
                    printf("[storage] AHCI controller at %u:%u.%u BAR5=0x%x\n",
                           bus,
                           slot,
                           func,
                           bar5 & ~0x0Fu);
                    ahci_probe_controller(bar5);
                }
            }
        }
    }
}

void storage_init(void)
{
    printf("[storage] probing IDE/AHCI...\n");
    ide_init_devices();
    ahci_init_devices();
}
