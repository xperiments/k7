/**
 * Who the install section is for (k7-site.md S13).
 *
 * Installing and running are different permissions. Safari and every phone can put an icon
 * on a home screen and none of them can run the App — it reaches the player over the local
 * network, and only Chrome has that permission. So the offer follows *runnability*
 * (`local-network` is answerable), not installability (`beforeinstallprompt`), and a browser
 * that fails it gets a sentence rather than an icon that opens a wall.
 */
import type { LocalNetworkState } from '../device/localNetwork';
import type { InstallState } from '../ui/install';

export type InstallOffer = 'checking' | 'unavailable' | 'open' | 'install' | 'waiting';

export function installOffer(
  install: InstallState,
  network: LocalNetworkState | null,
): InstallOffer {
  /* The permission query has not answered yet: nothing is known, so nothing is claimed. */
  if (network === null) return 'checking';
  if (network === 'unsupported') return 'unavailable';
  if (install === 'installed') return 'open';
  if (install === 'installable') return 'install';
  /* A Chromium that has not fired `beforeinstallprompt` yet: the install exists, the button
   * would not work, so the section says what to do instead of offering a dead control. */
  return 'waiting';
}
