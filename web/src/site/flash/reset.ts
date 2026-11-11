/**
 * Put the board back into its application after a flash.
 *
 * esptool-js's `after('hard_reset')` only *releases* RTS. That is enough when the loader
 * happens to leave the line asserted, and it is a no-op when it does not — the board then
 * sits in the flasher stub until somebody pulls the cable, which is exactly what happened on
 * the first browser flash. A reset is an **edge**: assert EN (RTS high) to hold the chip in
 * reset, then release it, with DTR low so IO0 stays high and the application boots rather
 * than the ROM loader.
 */
export interface SignalPort {
  setSignals(dtr?: boolean, rts?: boolean): Promise<void>;
}

export type Sleep = (ms: number) => Promise<void>;

const wait: Sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export async function resetIntoApp(port: SignalPort, sleep: Sleep = wait): Promise<void> {
  await port.setSignals(false, true);
  await sleep(150);
  await port.setSignals(false, false);
  await sleep(150);
}
