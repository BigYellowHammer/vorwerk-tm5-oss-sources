/*
 * Freescale GPMI NFC NAND Flash Driver
 *
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "gpmi-nfc.h"

#include "gpmi-nfc-gpmi-regs-v1.h"
#include "gpmi-nfc-bch-regs-v1.h"

/**
 * init() - Initializes the NFC hardware.
 *
 * @this:  Per-device data.
 */
static int init(struct gpmi_nfc_data *this)
{
	struct resources  *resources = &this->resources;
	int               error;

	/* Initialize DMA. */

	error = gpmi_nfc_dma_init(this);

	if (error)
		return error;

	/* Enable the clock. */

	clk_enable(resources->clock);

	/* Reset the GPMI block. */

	mxs_reset_block(resources->gpmi_regs + HW_GPMI_CTRL0, true);

	/* Choose NAND mode. */
	__raw_writel(BM_GPMI_CTRL1_GPMI_MODE,
				resources->gpmi_regs + HW_GPMI_CTRL1_CLR);

	/* Set the IRQ polarity. */
	__raw_writel(BM_GPMI_CTRL1_ATA_IRQRDY_POLARITY,
				resources->gpmi_regs + HW_GPMI_CTRL1_SET);

	/* Disable write protection. */
	__raw_writel(BM_GPMI_CTRL1_DEV_RESET,
				resources->gpmi_regs + HW_GPMI_CTRL1_SET);

	/* Select BCH ECC. */
	__raw_writel(BM_GPMI_CTRL1_BCH_MODE,
				resources->gpmi_regs + HW_GPMI_CTRL1_SET);

	/* Disable the clock. */

	clk_disable(resources->clock);

	/* If control arrives here, all is well. */

	return 0;

}

/**
 * set_geometry() - Configures the NFC geometry.
 *
 * @this:  Per-device data.
 */
static int set_geometry(struct gpmi_nfc_data *this)
{
	struct resources     *resources = &this->resources;
	struct nfc_geometry  *nfc       = &this->nfc_geometry;
	unsigned int         block_count;
	unsigned int         block_size;
	unsigned int         metadata_size;
	unsigned int         ecc_strength;
	unsigned int         page_size;
	unsigned int 	     erase_threshold;

	/* We make the abstract choices in a common function. */

	if (gpmi_nfc_set_geometry(this))
		return !0;

	/* Translate the abstract choices into register fields. */

	block_count   = nfc->ecc_chunk_count - 1;
	block_size    = nfc->ecc_chunk_size_in_bytes;
	metadata_size = nfc->metadata_size_in_bytes;
	ecc_strength  = nfc->ecc_strength >> 1;
	page_size     = nfc->page_size_in_bytes;

	/* Enable the clock. */

	clk_enable(resources->clock);

	/* reset the BCH */
	mxs_reset_block(resources->bch_regs, true);

	/* Configure layout 0. */

	__raw_writel(
		BF_BCH_FLASH0LAYOUT0_NBLOCKS(block_count)     |
		BF_BCH_FLASH0LAYOUT0_META_SIZE(metadata_size) |
		BF_BCH_FLASH0LAYOUT0_ECC0(ecc_strength)       |
		BF_BCH_FLASH0LAYOUT0_DATA0_SIZE(block_size)   ,
		resources->bch_regs + HW_BCH_FLASH0LAYOUT0);

	__raw_writel(
		BF_BCH_FLASH0LAYOUT1_PAGE_SIZE(page_size)   |
		BF_BCH_FLASH0LAYOUT1_ECCN(ecc_strength)     |
		BF_BCH_FLASH0LAYOUT1_DATAN_SIZE(block_size) ,
		resources->bch_regs + HW_BCH_FLASH0LAYOUT1);

	/* Set the tolerance for bitflips when reading erased blocks. */
	erase_threshold = nfc->ecc_strength;
	__raw_writel(erase_threshold & BM_BCH_MODE_ERASE_THRESHOLD_MASK,
		resources->bch_regs + HW_BCH_MODE);

	/* Set *all* chip selects to use layout 0. */

	__raw_writel(0, resources->bch_regs + HW_BCH_LAYOUTSELECT);

	/* Enable interrupts. */

	__raw_writel(BM_BCH_CTRL_COMPLETE_IRQ_EN,
				resources->bch_regs + HW_BCH_CTRL_SET);

	/* Disable the clock. */

	clk_disable(resources->clock);

	/* Return success. */

	return 0;

}

/**
 * set_timing() - Configures the NFC timing.
 *
 * @this:    Per-device data.
 * @timing:  The timing of interest.
 */
static int set_timing(struct gpmi_nfc_data *this,
					const struct gpmi_nfc_timing *timing)
{
	struct nfc_hal  *nfc = this->nfc;

	/* Accept the new timing. */

	nfc->timing = *timing;

	/* Return success. */

	return 0;

}

/**
 * get_timing() - Retrieves the NFC hardware timing.
 *
 * @this:                    Per-device data.
 * @clock_frequency_in_hz:   The clock frequency, in Hz, during the current
 *                           I/O transaction. If no I/O transaction is in
 *                           progress, this is the clock frequency during the
 *                           most recent I/O transaction.
 * @hardware_timing:         The hardware timing configuration in effect during
 *                           the current I/O transaction. If no I/O transaction
 *                           is in progress, this is the hardware timing
 *                           configuration during the most recent I/O
 *                           transaction.
 */
static void get_timing(struct gpmi_nfc_data *this,
			unsigned long *clock_frequency_in_hz,
			struct gpmi_nfc_hardware_timing *hardware_timing)
{
	struct resources                 *resources = &this->resources;
	struct nfc_hal                   *nfc       =  this->nfc;
	unsigned char                    *gpmi_regs = resources->gpmi_regs;
	uint32_t                         register_image;

	/* Return the clock frequency. */

	*clock_frequency_in_hz = nfc->clock_frequency_in_hz;

	/* We'll be reading the hardware, so let's enable the clock. */

	clk_enable(resources->clock);

	/* Retrieve the hardware timing. */

	register_image = __raw_readl(gpmi_regs + HW_GPMI_TIMING0);

	hardware_timing->data_setup_in_cycles =
		(register_image & BM_GPMI_TIMING0_DATA_SETUP) >>
						BP_GPMI_TIMING0_DATA_SETUP;

	hardware_timing->data_hold_in_cycles =
		(register_image & BM_GPMI_TIMING0_DATA_HOLD) >>
						BP_GPMI_TIMING0_DATA_HOLD;

	hardware_timing->address_setup_in_cycles =
		(register_image & BM_GPMI_TIMING0_ADDRESS_SETUP) >>
						BP_GPMI_TIMING0_ADDRESS_SETUP;

	register_image = __raw_readl(gpmi_regs + HW_GPMI_CTRL1);

	hardware_timing->use_half_periods =
		(register_image & BM_GPMI_CTRL1_HALF_PERIOD) >>
						BP_GPMI_CTRL1_HALF_PERIOD;

	hardware_timing->sample_delay_factor =
		(register_image & BM_GPMI_CTRL1_RDN_DELAY) >>
						BP_GPMI_CTRL1_RDN_DELAY;

	/* We're done reading the hardware, so disable the clock. */

	clk_disable(resources->clock);

}

/**
 * exit() - Shuts down the NFC hardware.
 *
 * @this:  Per-device data.
 */
static void exit(struct gpmi_nfc_data *this)
{
	gpmi_nfc_dma_exit(this);
}

/**
 * begin() - Begin NFC I/O.
 *
 * @this:  Per-device data.
 */
//static void begin(struct gpmi_nfc_data *this)
//{
//	struct resources                 *resources = &this->resources;
//	struct nfc_hal                   *nfc       =  this->nfc;
//	struct gpmi_nfc_hardware_timing  hw;
//
//	/* Enable the clock. */
//
//	clk_enable(resources->clock);
//
//	/* Get the timing information we need. */
//
//	nfc->clock_frequency_in_hz = clk_get_rate(resources->clock);
//	gpmi_nfc_compute_hardware_timing(this, &hw);
//
//	/* Apply the hardware timing. */
//
//	printk(KERN_INFO "MICHAL - Comming soon !\n");
//	/* Coming soon - the clock handling code isn't ready yet. */
//
//}

static void begin(struct gpmi_nfc_data *this)
{
	struct resources                 *resources = &this->resources;
	struct nfc_hal                   *nfc       =  this->nfc;
	static struct gpmi_nfc_hardware_timing  hw;
	unsigned char                    *gpmi_regs = resources->gpmi_regs;
	unsigned int                     clock_period_in_ns;
	uint32_t                         register_image;
	unsigned int                     dll_wait_time_in_us;

	// MICHAL - use precalculated values
	hw.address_setup_in_cycles = 4;
	hw.data_hold_in_cycles = 3;
	hw.data_setup_in_cycles = 3;
	hw.sample_delay_factor = 12;
	hw.use_half_periods = 0;
	clock_period_in_ns = 4;

	/* Enable the clock. */
	clk_enable(resources->clock);


// MICHAL - this line is ported from 3.4 vanilia kernel
// 0x24a = 586, for period 4.16ns (240Mhz), (10ms for max. delay for program/erase / 4.16 / 4096)
	__raw_writel(BF_GPMI_TIMING1_DEVICE_BUSY_TIMEOUT(587), gpmi_regs + HW_GPMI_TIMING1);


	/* Get the timing information we need. */

	nfc->clock_frequency_in_hz = clk_get_rate(resources->clock);
// MICHAL - use precalculated values instead
//	clock_period_in_ns = 1000000000 / nfc->clock_frequency_in_hz;
//	gpmi_nfc_compute_hardware_timing(this, &hw);

	/* Set up all the simple timing parameters. */

	register_image =
		BF_GPMI_TIMING0_ADDRESS_SETUP(hw.address_setup_in_cycles) |
		BF_GPMI_TIMING0_DATA_HOLD(hw.data_hold_in_cycles)         |
		BF_GPMI_TIMING0_DATA_SETUP(hw.data_setup_in_cycles)       ;

	__raw_writel(register_image, gpmi_regs + HW_GPMI_TIMING0);

	/*
	 * HEY - PAY ATTENTION
	 *
	 * DLL_ENABLE must be set to zero when setting RDN_DELAY or HALF_PERIOD.
	 */

	__raw_writel(BM_GPMI_CTRL1_DLL_ENABLE, gpmi_regs + HW_GPMI_CTRL1_CLR);

	/* Clear out the DLL control fields. */

	__raw_writel(BM_GPMI_CTRL1_RDN_DELAY,   gpmi_regs + HW_GPMI_CTRL1_CLR);
	__raw_writel(BM_GPMI_CTRL1_HALF_PERIOD, gpmi_regs + HW_GPMI_CTRL1_CLR);

	/* If no sample delay is called for, return immediately. */

	if (!hw.sample_delay_factor)
		return;

	/* Configure the HALF_PERIOD flag. */

	if (hw.use_half_periods)
		__raw_writel(BM_GPMI_CTRL1_HALF_PERIOD,
						gpmi_regs + HW_GPMI_CTRL1_SET);

	/* Set the delay factor. */

	__raw_writel(BF_GPMI_CTRL1_RDN_DELAY(hw.sample_delay_factor),
						gpmi_regs + HW_GPMI_CTRL1_SET);

	/* Enable the DLL. */

	__raw_writel(BM_GPMI_CTRL1_DLL_ENABLE, gpmi_regs + HW_GPMI_CTRL1_SET);

	/*
	 * After we enable the GPMI DLL, we have to wait 64 clock cycles before
	 * we can use the GPMI.
	 *
	 * Calculate the amount of time we need to wait, in microseconds.
	 */

	dll_wait_time_in_us = (clock_period_in_ns * 64) / 1000;

	if (!dll_wait_time_in_us)
		dll_wait_time_in_us = 1;

	/* Wait for the DLL to settle. */

	udelay(dll_wait_time_in_us);

}



/**
 * end() - End NFC I/O.
 *
 * @this:  Per-device data.
 */
static void end(struct gpmi_nfc_data *this)
{
	struct resources  *resources = &this->resources;

	/* Disable the clock. */

	clk_disable(resources->clock);

}

/**
 * clear_bch() - Clears a BCH interrupt.
 *
 * @this:  Per-device data.
 */
static void clear_bch(struct gpmi_nfc_data *this)
{
	struct resources  *resources = &this->resources;

	__raw_writel(BM_BCH_CTRL_COMPLETE_IRQ,
				resources->bch_regs + HW_BCH_CTRL_CLR);

}

/**
 * is_ready() - Returns the ready/busy status of the given chip.
 *
 * @this:  Per-device data.
 * @chip:  The chip of interest.
 */
static int is_ready(struct gpmi_nfc_data *this, unsigned chip)
{
	struct resources  *resources = &this->resources;
	uint32_t          mask;
	uint32_t          register_image;

	/* Extract and return the status. */

	mask = BF_GPMI_STAT_READY_BUSY(1 << chip);

	register_image = __raw_readl(resources->gpmi_regs + HW_GPMI_STAT);

	return !!(register_image & mask);

}

/**
 * send_command() - Sends a command and associated addresses.
 *
 * @this:    Per-device data.
 * @chip:    The chip of interest.
 * @buffer:  The physical address of a buffer that contains the command bytes.
 * @length:  The number of bytes in the buffer.
 */
static int send_command(struct gpmi_nfc_data *this, unsigned chip,
					dma_addr_t buffer, unsigned int length)
{
	struct device        *dev       =  this->dev;
	struct resources     *resources = &this->resources;
	struct nfc_hal       *nfc       =  this->nfc;
	struct mxs_dma_desc  **d        = nfc->dma_descriptors;
	int                  dma_channel;
	int                  error;
	uint32_t             command_mode;
	uint32_t             address;

	/* Compute the DMA channel. */

	dma_channel = resources->dma_low_channel + chip;

	/* A DMA descriptor that sends out the command. */

	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__WRITE;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_CLE;

	fill_dma_word1(&(*d)->cmd.cmd,
			DMA_READ, 1, 1, 0, 0, 1, 1, 0, 0, 3, length);
	(*d)->cmd.address = buffer;

	(*d)->cmd.pio_words[0] =
		BF_GPMI_CTRL0_COMMAND_MODE(command_mode) |
		BM_GPMI_CTRL0_WORD_LENGTH                |
		BF_GPMI_CTRL0_CS(chip)                   |
		BF_GPMI_CTRL0_ADDRESS(address)           |
		BM_GPMI_CTRL0_ADDRESS_INCREMENT          |
		BF_GPMI_CTRL0_XFER_COUNT(length)         ;

	(*d)->cmd.pio_words[1] = 0;
	(*d)->cmd.pio_words[2] = 0;

	mxs_dma_desc_append(dma_channel, (*d));
	d++;

	/* Go! */

	error = gpmi_nfc_dma_go(this, dma_channel);

	if (error)
		dev_err(dev, "[%s] DMA error\n", __func__);

	/* Return success. */

	return error;

}

/**
 * send_data() - Sends data to the given chip.
 *
 * @this:    Per-device data.
 * @chip:    The chip of interest.
 * @buffer:  The physical address of a buffer that contains the data.
 * @length:  The number of bytes in the buffer.
 */
static int send_data(struct gpmi_nfc_data *this, unsigned chip,
					dma_addr_t buffer, unsigned int length)
{
	struct device        *dev       =  this->dev;
	struct resources     *resources = &this->resources;
	struct nfc_hal       *nfc       =  this->nfc;
	struct mxs_dma_desc  **d        = nfc->dma_descriptors;
	int                  dma_channel;
	int                  error = 0;
	uint32_t             command_mode;
	uint32_t             address;

	/* Compute the DMA channel. */

	dma_channel = resources->dma_low_channel + chip;

	/* A DMA descriptor that writes a buffer out. */

	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__WRITE;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;

	fill_dma_word1(&(*d)->cmd.cmd,
			DMA_READ, 0, 1, 0, 0, 1, 1, 0, 0, 4, length);
	(*d)->cmd.address = buffer;

	(*d)->cmd.pio_words[0] =
		BF_GPMI_CTRL0_COMMAND_MODE(command_mode) |
		BM_GPMI_CTRL0_WORD_LENGTH                |
		BF_GPMI_CTRL0_CS(chip)                   |
		BF_GPMI_CTRL0_ADDRESS(address)           |
		BF_GPMI_CTRL0_XFER_COUNT(length)         ;
	(*d)->cmd.pio_words[1] = 0;
	(*d)->cmd.pio_words[2] = 0;
	(*d)->cmd.pio_words[3] = 0;

	mxs_dma_desc_append(dma_channel, (*d));
	d++;

	/* Go! */

	error = gpmi_nfc_dma_go(this, dma_channel);

	if (error)
		dev_err(dev, "[%s] DMA error\n", __func__);

	/* Return success. */

	return error;

}

/**
 * read_data() - Receives data from the given chip.
 *
 * @this:    Per-device data.
 * @chip:    The chip of interest.
 * @buffer:  The physical address of a buffer that will receive the data.
 * @length:  The number of bytes to read.
 */
#if 0
static int read_data(struct gpmi_nfc_data *this, unsigned chip,
					dma_addr_t buffer, unsigned int length)
{
	struct device        *dev       =  this->dev;
	struct resources     *resources = &this->resources;
	struct nfc_hal       *nfc       =  this->nfc;
	struct mxs_dma_desc  **d        = nfc->dma_descriptors;
	int                  dma_channel;
	int                  error = 0;
	uint32_t             command_mode;
	uint32_t             address;

	/* Compute the DMA channel. */

	dma_channel = resources->dma_low_channel + chip;

	/* A DMA descriptor that reads the data. */

	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__READ;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;

	fill_dma_word1(&(*d)->cmd.cmd,
			DMA_WRITE, 1, 0, 0, 0, 1, 1, 0, 0, 1, length);
	(*d)->cmd.address = buffer;

	(*d)->cmd.pio_words[0] =
		BF_GPMI_CTRL0_COMMAND_MODE(command_mode) |
		BM_GPMI_CTRL0_WORD_LENGTH                |
		BF_GPMI_CTRL0_CS(chip)                   |
		BF_GPMI_CTRL0_ADDRESS(address)           |
		BF_GPMI_CTRL0_XFER_COUNT(length)         ;

	mxs_dma_desc_append(dma_channel, (*d));
	d++;

	/*
	 * A DMA descriptor that waits for the command to end and the chip to
	 * become ready.
	 *
	 * I think we actually should *not* be waiting for the chip to become
	 * ready because, after all, we don't care. I think the original code
	 * did that and no one has re-thought it yet.
	 */

	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__WAIT_FOR_READY;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;

	fill_dma_word1(&(*d)->cmd.cmd,
			NO_DMA_XFER, 0, 1, 0, 1, 1, 1, 0, 0, 4, 0);
	(*d)->cmd.address = 0;

	(*d)->cmd.pio_words[0] =
		BF_GPMI_CTRL0_COMMAND_MODE(command_mode) |
		BM_GPMI_CTRL0_WORD_LENGTH                |
		BF_GPMI_CTRL0_CS(chip)                   |
		BF_GPMI_CTRL0_ADDRESS(address)           |
		BF_GPMI_CTRL0_XFER_COUNT(0)              ;
	(*d)->cmd.pio_words[1] = 0;
	(*d)->cmd.pio_words[2] = 0;
	(*d)->cmd.pio_words[3] = 0;

	mxs_dma_desc_append(dma_channel, (*d));
	d++;

	/* Go! */

	error = gpmi_nfc_dma_go(this, dma_channel);

	if (error)
		dev_err(dev, "[%s] DMA error\n", __func__);

	/* Return success. */

	return error;
}
#endif

static int read_data(struct gpmi_nfc_data *this, unsigned chip,
				dma_addr_t buffer, unsigned int length)
{
	struct device        *dev       =  this->dev;
	struct resources     *resources = &this->resources;
	struct nfc_hal       *nfc       =  this->nfc;
	struct mxs_dma_desc  **d        = nfc->dma_descriptors;
	int                  dma_channel;
	int                  error = 0;
	uint32_t             command_mode;
	uint32_t             address;

	/* Compute the DMA channel. */

	dma_channel = resources->dma_low_channel + chip;

	/* A DMA descriptor that reads the data. */

	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__READ;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;

	fill_dma_word1(&(*d)->cmd.cmd,
		DMA_WRITE, 0, 1, 0, 0, 1, 1, 1, 0, 1, length);
	(*d)->cmd.address = buffer;

	(*d)->cmd.pio_words[0] =
		BF_GPMI_CTRL0_COMMAND_MODE(command_mode) |
		BM_GPMI_CTRL0_WORD_LENGTH                |
		BF_GPMI_CTRL0_CS(chip)                   |
		BF_GPMI_CTRL0_ADDRESS(address)           |
		BF_GPMI_CTRL0_XFER_COUNT(length)         ;

	mxs_dma_desc_append(dma_channel, (*d));
	d++;

	/* Go! */

	error = gpmi_nfc_dma_go(this, dma_channel);

	if (error)
		dev_err(dev, "[%s] DMA error\n", __func__);
	/* Return success. */

	return error;
}

/**
 * send_page() - Sends a page, using ECC.
 *
 * @this:       Per-device data.
 * @chip:       The chip of interest.
 * @payload:    The physical address of the payload buffer.
 * @auxiliary:  The physical address of the auxiliary buffer.
 */
static int send_page(struct gpmi_nfc_data *this, unsigned chip,
				dma_addr_t payload, dma_addr_t auxiliary)
{
	struct device        *dev       =  this->dev;
	struct resources     *resources = &this->resources;
	struct nfc_hal       *nfc       =  this->nfc;
	struct nfc_geometry  *nfc_geo   = &this->nfc_geometry;
	struct mxs_dma_desc  **d        = nfc->dma_descriptors;
	int                  dma_channel;
	int                  error = 0;
	uint32_t             command_mode;
	uint32_t             address;
	uint32_t             ecc_command;
	uint32_t             buffer_mask;

	/* Compute the DMA channel. */

	dma_channel = resources->dma_low_channel + chip;

	/* A DMA descriptor that does an ECC page read. */

	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__WRITE;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;
	ecc_command  = BV_GPMI_ECCCTRL_ECC_CMD__ENCODE;
	buffer_mask  = BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_PAGE |
				BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_AUXONLY;

	fill_dma_word1(&(*d)->cmd.cmd,
			NO_DMA_XFER, 0, 1, 0, 0, 1, 1, 0, 0, 6, 0);
	(*d)->cmd.address = 0;

	(*d)->cmd.pio_words[0] =
		BF_GPMI_CTRL0_COMMAND_MODE(command_mode) |
		BM_GPMI_CTRL0_WORD_LENGTH                |
		BF_GPMI_CTRL0_CS(chip)                   |
		BF_GPMI_CTRL0_ADDRESS(address)           |
		BF_GPMI_CTRL0_XFER_COUNT(0)              ;

	(*d)->cmd.pio_words[1] = 0;

	(*d)->cmd.pio_words[2] =
		BM_GPMI_ECCCTRL_ENABLE_ECC               |
		BF_GPMI_ECCCTRL_ECC_CMD(ecc_command)     |
		BF_GPMI_ECCCTRL_BUFFER_MASK(buffer_mask) ;

	(*d)->cmd.pio_words[3] = nfc_geo->page_size_in_bytes;
	(*d)->cmd.pio_words[4] = payload;
	(*d)->cmd.pio_words[5] = auxiliary;

	mxs_dma_desc_append(dma_channel, (*d));
	d++;

	/* Prepare to receive an interrupt from the BCH block. */

	init_completion(&nfc->bch_done);

	/* Go! */

	error = gpmi_nfc_dma_go(this, dma_channel);

	if (error)
		dev_err(dev, "[%s] DMA error\n", __func__);

	/* Wait for the interrupt from the BCH block. */

	wait_for_completion(&nfc->bch_done);

	/* Return success. */

	return error;

}

/**
 * read_page() - Reads a page, using ECC.
 *
 * @this:       Per-device data.
 * @chip:       The chip of interest.
 * @payload:    The physical address of the payload buffer.
 * @auxiliary:  The physical address of the auxiliary buffer.
 */
static int read_page(struct gpmi_nfc_data *this, unsigned chip,
				dma_addr_t payload, dma_addr_t auxiliary)
{
	struct device        *dev       =  this->dev;
	struct resources     *resources = &this->resources;
	struct nfc_hal       *nfc       =  this->nfc;
	struct nfc_geometry  *nfc_geo   = &this->nfc_geometry;
	struct mxs_dma_desc  **d        = nfc->dma_descriptors;
	int                  dma_channel;
	int                  error = 0;
	uint32_t             command_mode;
	uint32_t             address;
	uint32_t             ecc_command;
	uint32_t             buffer_mask;

	/* Compute the DMA channel. */

	dma_channel = resources->dma_low_channel + chip;

	/* Wait for the chip to report ready. */

	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__WAIT_FOR_READY;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;

	fill_dma_word1(&(*d)->cmd.cmd,
			NO_DMA_XFER, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0);
	(*d)->cmd.address = 0;

	(*d)->cmd.pio_words[0] =
		BF_GPMI_CTRL0_COMMAND_MODE(command_mode) |
		BM_GPMI_CTRL0_WORD_LENGTH                |
		BF_GPMI_CTRL0_CS(chip)                   |
		BF_GPMI_CTRL0_ADDRESS(address)           |
		BF_GPMI_CTRL0_XFER_COUNT(0)              ;

	mxs_dma_desc_append(dma_channel, (*d));
	d++;

	/* Enable the BCH block and read. */

	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__READ;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;
	ecc_command  = BV_GPMI_ECCCTRL_ECC_CMD__DECODE;
	buffer_mask  = BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_PAGE |
				BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_AUXONLY;

	fill_dma_word1(&(*d)->cmd.cmd,
			NO_DMA_XFER, 1, 1, 0, 0, 1, 1, 0, 0, 6, 0);
	(*d)->cmd.address = 0;

	(*d)->cmd.pio_words[0] =
		BF_GPMI_CTRL0_COMMAND_MODE(command_mode)              |
		BM_GPMI_CTRL0_WORD_LENGTH                             |
		BF_GPMI_CTRL0_CS(chip)                                |
		BF_GPMI_CTRL0_ADDRESS(address)                        |
		BF_GPMI_CTRL0_XFER_COUNT(nfc_geo->page_size_in_bytes) ;

	(*d)->cmd.pio_words[1] = 0;
	(*d)->cmd.pio_words[2] =
		BM_GPMI_ECCCTRL_ENABLE_ECC 	         |
		BF_GPMI_ECCCTRL_ECC_CMD(ecc_command)     |
		BF_GPMI_ECCCTRL_BUFFER_MASK(buffer_mask) ;
	(*d)->cmd.pio_words[3] = nfc_geo->page_size_in_bytes;
	(*d)->cmd.pio_words[4] = payload;
	(*d)->cmd.pio_words[5] = auxiliary;

	mxs_dma_desc_append(dma_channel, (*d));
	d++;

	/* Disable the BCH block */

	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__WAIT_FOR_READY;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;

	fill_dma_word1(&(*d)->cmd.cmd,
			NO_DMA_XFER, 1, 1, 0, 0, 1, 1, 0, 0, 3, 0);
	(*d)->cmd.address = 0;

	(*d)->cmd.pio_words[0] =
		BF_GPMI_CTRL0_COMMAND_MODE(command_mode)              |
		BM_GPMI_CTRL0_WORD_LENGTH                             |
		BF_GPMI_CTRL0_CS(chip)                                |
		BF_GPMI_CTRL0_ADDRESS(address)                        |
		BF_GPMI_CTRL0_XFER_COUNT(nfc_geo->page_size_in_bytes) ;

	(*d)->cmd.pio_words[1] = 0;
	(*d)->cmd.pio_words[2] = 0;

	mxs_dma_desc_append(dma_channel, (*d));
	d++;

	/* Deassert the NAND lock and interrupt. */
//	fill_dma_word1(&(*d)->cmd.cmd,
//			NO_DMA_XFER, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0);
//	(*d)->cmd.address = 0;
//
//	mxs_dma_desc_append(dma_channel, (*d));
//	d++;

	/* Prepare to receive an interrupt from the BCH block. */

	init_completion(&nfc->bch_done);

	/* Go! */

	error = gpmi_nfc_dma_go(this, dma_channel);

	if (error)
		dev_err(dev, "[%s] DMA error\n", __func__);

	/* Wait for the interrupt from the BCH block. */

//	wait_for_completion(&nfc->bch_done);
	error = wait_for_completion_timeout(&nfc->bch_done, msecs_to_jiffies(1000));
	error = (!error) ? -ETIMEDOUT : 0;
	if (error)
		dev_err(dev, "[%s] bch timeout!!! \n", __func__);

	/* Return success. */

	return error;

}

/* This structure represents the NFC HAL for this version of the hardware. */

struct nfc_hal  gpmi_nfc_hal_v1 = {
	.version                     = 1,
	.description                 = "8-chip GPMI and BCH",
	.max_chip_count              = 8,
	.max_data_setup_cycles       = (BM_GPMI_TIMING0_DATA_SETUP >>
						BP_GPMI_TIMING0_DATA_SETUP),
	.internal_data_setup_in_ns   = 0,
	.max_sample_delay_factor     = (BM_GPMI_CTRL1_RDN_DELAY >>
						BP_GPMI_CTRL1_RDN_DELAY),
	.max_dll_clock_period_in_ns  = 32,
	.max_dll_delay_in_ns         = 16,
	.init                        = init,
	.set_geometry                = set_geometry,
	.set_timing                  = set_timing,
	.get_timing                  = get_timing,
	.exit                        = exit,
	.begin                       = begin,
	.end                         = end,
	.clear_bch                   = clear_bch,
	.is_ready                    = is_ready,
	.send_command                = send_command,
	.send_data                   = send_data,
	.read_data                   = read_data,
	.send_page                   = send_page,
	.read_page                   = read_page,
};
