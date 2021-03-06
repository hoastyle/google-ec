/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * EC detect state machine.
 */
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "uart_bitbang.h"
#include "uartn.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

static enum device_state state = DEVICE_STATE_INIT;

void print_ec_state(void)
{
	ccprintf("EC:      %s\n", device_state_name(state));
}

int ec_is_on(void)
{
	/* Debouncing and on are both still on */
	return (state == DEVICE_STATE_DEBOUNCING || state == DEVICE_STATE_ON);
}

/**
 * Set the EC state.
 *
 * Done as a function to make it easier to debug state transitions.  Note that
 * this ONLY sets the state (and possibly prints debug info), and doesn't do
 * all the additional transition work that set_ec_on(), etc. do.
 *
 * @param new_state	State to set.
 */
static void set_state(enum device_state new_state)
{
#ifdef CR50_DEBUG_EC_STATE
	/* Print all state transitions.  May spam the console. */
	if (state != new_state)
		CPRINTS("EC %s -> %s",
			device_state_name(state), device_state_name(new_state));
#endif
	state = new_state;
}

/**
 * Move the EC to the ON state.
 *
 * This can be deferred from the interrupt handler, or called from the state
 * machine which also runs in HOOK task, so it needs to check the current state
 * to determine whether we're already on.
 */
static void set_ec_on(void)
{
	if (state == DEVICE_STATE_INIT ||
	    state == DEVICE_STATE_INIT_DEBOUNCING) {
		/*
		 * Enable the UART peripheral so we start receiving on EC RX,
		 * but do not call uartn_tx_connect() to connect EC TX yet.  We
		 * need to be able to use EC TX to detect servo, so if we drive
		 * it right away that blocks us from detecting servo.
		 */
		CPRINTS("EC RX only");
		if (!uart_bitbang_is_enabled(UART_EC))
			uartn_enable(UART_EC);
		set_state(DEVICE_STATE_INIT_RX_ONLY);
		return;
	}

	/* If we were debouncing ON->OFF, cancel it because we're still on */
	if (state == DEVICE_STATE_DEBOUNCING)
		set_state(DEVICE_STATE_ON);

	/* If we're already on, done */
	if (state == DEVICE_STATE_ON)
		return;

	/* We were previously off */
	CPRINTS("EC on");
	set_state(DEVICE_STATE_ON);

	/* Enable UART RX if we're not bit-banging */
	if (!uart_bitbang_is_enabled(UART_EC))
		enable_ccd_uart(UART_EC);
}
DECLARE_DEFERRED(set_ec_on);

/**
 * Interrupt handler for EC detect asserted.
 */
void ec_detect_asserted(enum gpio_signal signal)
{
	gpio_disable_interrupt(GPIO_DETECT_EC);
	hook_call_deferred(&set_ec_on_data, 0);
}

/**
 * Detect state machine
 */
static void ec_detect(void)
{
	/* Disable interrupts if we had them on for debouncing */
	gpio_disable_interrupt(GPIO_DETECT_EC);

	/* If we detect the EC, make sure it's on */
	if (gpio_get_level(GPIO_DETECT_EC)) {
		set_ec_on();
		return;
	}

	/* EC wasn't detected.  If we're already off, done. */
	if (state == DEVICE_STATE_OFF)
		return;

	/* If we were debouncing, we're now sure we're off */
	if (state == DEVICE_STATE_DEBOUNCING ||
	    state == DEVICE_STATE_INIT_DEBOUNCING) {
		CPRINTS("EC off");
		set_state(DEVICE_STATE_OFF);
		disable_ccd_uart(UART_EC);
		return;
	}

	/*
	 * Otherwise, we were on or initializing, and we're not sure if the EC
	 * is actually off or just sending a 0-bit.  So start debouncing.
	 */
	if (state == DEVICE_STATE_INIT)
		set_state(DEVICE_STATE_INIT_DEBOUNCING);
	else
		set_state(DEVICE_STATE_DEBOUNCING);
	gpio_enable_interrupt(GPIO_DETECT_EC);
}
DECLARE_HOOK(HOOK_SECOND, ec_detect, HOOK_PRIO_DEFAULT);
