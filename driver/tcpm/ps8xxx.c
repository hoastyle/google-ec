/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Type-C port manager for Parade PS8XXX with integrated superspeed muxes.
 *
 * Supported TCPCs:
 * - PS8751
 * - PS8805
 */

#include "common.h"
#include "ps8xxx.h"
#include "tcpci.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_pd.h"

#if !defined(CONFIG_USB_PD_TCPM_PS8751) && \
	!defined(CONFIG_USB_PD_TCPM_PS8805)
#error "Unsupported PS8xxx TCPC."
#endif

#if !defined(CONFIG_USB_PD_TCPM_TCPCI) || \
	!defined(CONFIG_USB_PD_TCPM_MUX) || \
	!defined(CONFIG_USBC_SS_MUX)

#error "PS8XXX is using a standard TCPCI interface with integrated mux control"
#error "Please upgrade your board configuration"

#endif

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.
 */
static uint64_t hpd_deadline[CONFIG_USB_PD_PORT_COUNT];

static int dp_set_hpd(int port, int enable)
{
	int reg;
	int rv;

	rv = tcpc_read(port, MUX_IN_HPD_ASSERTION_REG, &reg);
	if (rv)
		return rv;
	if (enable)
		reg |= IN_HPD;
	else
		reg &= ~IN_HPD;
	return tcpc_write(port, MUX_IN_HPD_ASSERTION_REG, reg);
}

static int dp_set_irq(int port, int enable)
{

	int reg;
	int rv;

	rv = tcpc_read(port, MUX_IN_HPD_ASSERTION_REG, &reg);
	if (rv)
		return rv;
	if (enable)
		reg |= HPD_IRQ;
	else
		reg &= ~HPD_IRQ;
	return tcpc_write(port, MUX_IN_HPD_ASSERTION_REG, reg);
}

void ps8xxx_tcpc_update_hpd_status(int port, int hpd_lvl, int hpd_irq)
{
	dp_set_hpd(port, hpd_lvl);

	if (hpd_irq) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < hpd_deadline[port])
			usleep(hpd_deadline[port] - now);

		dp_set_irq(port, 0);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		dp_set_irq(port, hpd_irq);
	}
	/* enforce 2-ms delay between HPD pulses */
	hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
}

int ps8xxx_tcpc_get_fw_version(int port, int *version)
{
	return tcpc_read(port, FW_VER_REG, version);
}

static int ps8xxx_tcpm_release(int port)
{
	int version;
	int status;

	status = tcpc_read(port, FW_VER_REG, &version);
	if (status != 0) {
		/* wait for chip to wake up */
		msleep(10);
	}

	return tcpci_tcpm_release(port);
}

const struct tcpm_drv ps8xxx_tcpm_drv = {
	.init			= &tcpci_tcpm_init,
	.release		= &ps8xxx_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level		= &tcpci_tcpm_get_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message		= &tcpci_tcpm_get_message,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= &tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info		= &tcpci_get_chip_info,
};

#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
struct i2c_stress_test_dev ps8xxx_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = PS8XXX_REG_VENDOR_ID_L,
		.read_val = PS8XXX_VENDOR_ID & 0xFF,
		.write_reg = MUX_IN_HPD_ASSERTION_REG,
	},
	.i2c_read = &tcpc_i2c_read,
	.i2c_write = &tcpc_i2c_write,
};
#endif /* CONFIG_CMD_I2C_STRESS_TEST_TCPC */
