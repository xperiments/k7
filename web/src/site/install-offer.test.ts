import { describe, expect, it } from 'vitest';

import { installOffer } from './install-offer';

describe('the install section', () => {
  it('claims nothing before the permission has answered', () => {
    expect(installOffer('installable', null)).toBe('checking');
  });

  it('offers no install where the App cannot run', () => {
    expect(installOffer('installable', 'unsupported')).toBe('unavailable');
    expect(installOffer('installed', 'unsupported')).toBe('unavailable');
    expect(installOffer('unknown', 'unsupported')).toBe('unavailable');
  });

  it('opens the App once it is installed', () => {
    expect(installOffer('installed', 'prompt')).toBe('open');
    expect(installOffer('installed', 'granted')).toBe('open');
  });

  it('offers the install when Chrome has a prompt to replay', () => {
    expect(installOffer('installable', 'granted')).toBe('install');
    expect(installOffer('installable', 'denied')).toBe('install');
  });

  it('waits rather than offering a control that cannot work', () => {
    expect(installOffer('unknown', 'prompt')).toBe('waiting');
    expect(installOffer('unsupported', 'prompt')).toBe('waiting');
  });
});
