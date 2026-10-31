import { describe, expect, it } from 'vitest';
import { initialView, selectView } from '../src/ui/view';

describe('the view the card chooses at load', () => {
  it('is the Player view when the card is in', () => {
    expect(initialView(true)).toBe('player');
  });

  it('is the Prepare view when the card is out', () => {
    expect(initialView(false)).toBe('prepare');
  });
});

describe('a change after load', () => {
  it('switches to the Player view when the queue is idle', () => {
    expect(selectView('prepare', true, true)).toEqual({
      view: 'player',
      switchOffered: false,
    });
  });

  it('stays on the Prepare view and offers the switch mid-analysis', () => {
    expect(selectView('prepare', true, false)).toEqual({
      view: 'prepare',
      switchOffered: true,
    });
  });

  it('switches back to the Prepare view when the card leaves and nothing is running', () => {
    expect(selectView('player', false, true)).toEqual({
      view: 'prepare',
      switchOffered: false,
    });
  });

  it('does not offer a switch when the desired view is already on screen', () => {
    expect(selectView('player', true, false)).toEqual({
      view: 'player',
      switchOffered: false,
    });
    expect(selectView('prepare', false, false)).toEqual({
      view: 'prepare',
      switchOffered: false,
    });
  });

  it('keeps the current view while the queue is busy, whichever way the card changed', () => {
    expect(selectView('player', false, false).view).toBe('player');
    expect(selectView('prepare', true, false).view).toBe('prepare');
  });
});
