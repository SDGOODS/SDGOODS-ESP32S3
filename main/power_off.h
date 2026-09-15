#pragma once

/**
 * Cut device power and turn the badge off.
 *
 * Releases the self-holding battery latch (BOARD_BAT_CONTROL_GPIO) so the
 * power MOSFET opens and the whole board loses power. If power is not actually
 * cut (e.g. the physical power button is still held), it falls back to deep
 * sleep and wakes on the physical key press.
 *
 * Only call this when the user explicitly requests shutdown.
 */
void system_power_off(void);
